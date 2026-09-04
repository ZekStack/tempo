#include "worker_pool_executor.h"

#include "../service/scheduler_events.h"

namespace {
[[noreturn]] void suspendForever() {
	vTaskSuspend(nullptr);
	for (;;) {
		vTaskDelay(portMAX_DELAY);
	}
}

bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
	if (!runtime) {
		return false;
	}
	return runtime->publish(SchedulerEvent{
	    .kind = SchedulerEventKind::JobFinished,
	    .jobId = jobId,
	    .generation = generation,
	    .slotIndex = slotIndex,
	});
}
} // namespace

struct WorkerPoolExecutor::TaskItem {
	JobInvocation invocation{};
};

struct WorkerPoolExecutor::WorkerRecord {
	WorkerPoolExecutor *owner = nullptr;
	Strata::FreeRTOS::Task task{};
	std::atomic<bool> readyForDelete{false};
};

WorkerPoolExecutor::WorkerPoolExecutor(
    const WorkerPoolConfig &config,
    Strata::Placement allocationPlacement,
    Strata::Placement defaultStackPlacement
)
    : config_(config), allocationPlacement_(allocationPlacement),
      defaultStackPlacement_(defaultStackPlacement), workers_(allocationPlacement) {
}

WorkerPoolExecutor::~WorkerPoolExecutor() {
	end(true);
}

bool WorkerPoolExecutor::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	if (started_.load()) {
		return true;
	}
	if (!runtime) {
		return false;
	}

	runtime_ = runtime;
	queue_ = Strata::FreeRTOS::Queue<TaskItem *>::create({
	    .length = config_.queueDepth,
	    .storagePlacement = allocationPlacement_,
	    .usage = Strata::FreeRTOS::QueueUsage::TaskOnly,
	});
	if (!queue_) {
		runtime_.reset();
		return false;
	}

	workers_.clear();
	const Strata::Placement stackPlacement =
	    config_.stackPlacement.value_or(defaultStackPlacement_);
	for (uint8_t index = 0; index < config_.workerCount; ++index) {
		auto worker = Strata::makeUnique<WorkerRecord>(allocationPlacement_);
		if (!worker) {
			end(false);
			return false;
		}
		worker->owner = this;
		WorkerRecord *record = worker.get();
		if (!workers_.pushBack(std::move(worker))) {
			end(false);
			return false;
		}

		auto task = Strata::FreeRTOS::Task::create(
		    &WorkerPoolExecutor::workerTaskEntry,
		    record,
		    Strata::FreeRTOS::TaskConfig{
		        .name = "sched-pool",
		        .stackBytes = config_.stackSize,
		        .stackPlacement = stackPlacement,
		        .priority = config_.priority,
		        .affinity = config_.coreId,
		    }
		);
		if (!task) {
			workers_.popBack();
			end(false);
			return false;
		}
		record->task = std::move(task);
	}

	started_.store(true);
	return true;
}

void WorkerPoolExecutor::destroyPendingItems() {
	if (!queue_) {
		return;
	}
	TaskItem *pending = nullptr;
	while (queue_.receive(pending, 0)) {
		if (pending) {
			Strata::destroy(pending);
		}
	}
}

void WorkerPoolExecutor::end(bool drainRunningJobs) {
	started_.store(false);
	if (!queue_) {
		workers_.clear();
		runtime_.reset();
		return;
	}

	if (!drainRunningJobs) {
		destroyPendingItems();
	}

	for (size_t index = 0; index < workers_.size(); ++index) {
		TaskItem *sentinel = nullptr;
		while (!queue_.send(sentinel, pdMS_TO_TICKS(50))) {
			if (!drainRunningJobs) {
				destroyPendingItems();
			}
		}
	}

	const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
	for (;;) {
		bool allReady = true;
		for (size_t index = 0; index < workers_.size(); ++index) {
			if (workers_[index] && !workers_[index]->readyForDelete.load(std::memory_order_acquire)) {
				allReady = false;
				break;
			}
		}
		if (allReady || xTaskGetTickCount() >= deadline) {
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	for (size_t index = 0; index < workers_.size(); ++index) {
		if (workers_[index] && workers_[index]->task) {
			workers_[index]->task.reset();
		}
	}
	workers_.clear();
	destroyPendingItems();
	queue_.reset();
	runtime_.reset();
}

bool WorkerPoolExecutor::submit(const JobInvocation &invocation) {
	if (!queue_ || !started_.load()) {
		return false;
	}
	auto item = Strata::makeUnique<TaskItem>(allocationPlacement_);
	if (!item) {
		return false;
	}
	item->invocation = invocation;
	item->invocation.runtime = runtime_;
	TaskItem *rawItem = item.release();
	if (!queue_.send(rawItem, 0)) {
		Strata::destroy(rawItem);
		return false;
	}
	return true;
}

const char *WorkerPoolExecutor::name() const {
	return "worker-pool";
}

void WorkerPoolExecutor::workerTaskEntry(void *arg) {
	auto *record = static_cast<WorkerRecord *>(arg);
	if (!record || !record->owner) {
		suspendForever();
	}

	WorkerPoolExecutor *owner = record->owner;
	for (;;) {
		TaskItem *item = nullptr;
		if (!owner->queue_.receive(item, portMAX_DELAY)) {
			continue;
		}
		if (!item) {
			break;
		}

		item->invocation.callback.invoke();
		postCompletion(
		    item->invocation.runtime,
		    item->invocation.jobId,
		    item->invocation.generation,
		    item->invocation.slotIndex
		);
		Strata::destroy(item);
	}

	record->readyForDelete.store(true, std::memory_order_release);
	suspendForever();
}
