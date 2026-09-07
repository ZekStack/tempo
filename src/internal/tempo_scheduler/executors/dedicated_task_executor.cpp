#include "dedicated_task_executor.h"

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

struct DedicatedTaskExecutor::TaskRecord {
	JobInvocation invocation{};
	Strata::FreeRTOS::Task task{};
	std::atomic<bool> readyForDelete{false};
};

DedicatedTaskExecutor::DedicatedTaskExecutor(
    Strata::Placement allocationPlacement,
    Strata::Placement defaultStackPlacement
) noexcept
    : allocationPlacement_(allocationPlacement), defaultStackPlacement_(defaultStackPlacement),
      tasks_(allocationPlacement) {
}

DedicatedTaskExecutor::~DedicatedTaskExecutor() {
	end(true);
}

bool DedicatedTaskExecutor::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	if (runtime_ && runtime_->accepting.load(std::memory_order_acquire)) {
		return true;
	}
	if (!runtime) {
		return false;
	}
	runtime_ = runtime;
	return true;
}

void DedicatedTaskExecutor::reapCompleted() {
	for (size_t index = tasks_.size(); index > 0; --index) {
		const size_t slot = index - 1;
		if (!tasks_[slot] ||
		    !tasks_[slot]->readyForDelete.load(std::memory_order_acquire)) {
			continue;
		}
		if (tasks_[slot]->task) {
			tasks_[slot]->task.reset();
		}
		tasks_.erase(slot);
	}
}

void DedicatedTaskExecutor::end(bool drainRunningJobs) {
	if (!runtime_ && tasks_.empty()) {
		return;
	}

	if (drainRunningJobs) {
		const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
		while (!tasks_.empty() && xTaskGetTickCount() < deadline) {
			reapCompleted();
			if (!tasks_.empty()) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		}
	}

	for (size_t index = 0; index < tasks_.size(); ++index) {
		if (tasks_[index] && tasks_[index]->task) {
			tasks_[index]->task.reset();
		}
	}
	tasks_.clear();
	runtime_.reset();
}

bool DedicatedTaskExecutor::submit(const JobInvocation &invocation) {
	if (!runtime_ || !runtime_->accepting.load(std::memory_order_acquire)) {
		return false;
	}

	reapCompleted();
	auto record = Strata::makeUnique<TaskRecord>(allocationPlacement_);
	if (!record) {
		return false;
	}
	record->invocation = invocation;
	record->invocation.runtime = runtime_;
	TaskRecord *rawRecord = record.get();
	if (!tasks_.pushBack(std::move(record))) {
		return false;
	}

	const DedicatedTaskOptions &task = rawRecord->invocation.dedicatedTask;
	const Strata::Placement stackPlacement =
	    task.stackPlacement.value_or(defaultStackPlacement_);
	auto owner = Strata::FreeRTOS::Task::create(
	    &DedicatedTaskExecutor::taskEntry,
	    rawRecord,
	    Strata::FreeRTOS::TaskConfig{
	        .name = task.name ? task.name : "sched-task",
	        .stackBytes = task.stackSize,
	        .stackPlacement = stackPlacement,
	        .priority = task.priority,
	        .affinity = task.coreId,
	    }
	);
	if (!owner) {
		tasks_.popBack();
		return false;
	}
	rawRecord->task = std::move(owner);
	return true;
}

const char *DedicatedTaskExecutor::name() const {
	return "dedicated-task";
}

void DedicatedTaskExecutor::taskEntry(void *arg) {
	auto *record = static_cast<TaskRecord *>(arg);
	if (!record) {
		suspendForever();
	}

	record->invocation.callback.invoke();
	postCompletion(
	    record->invocation.runtime,
	    record->invocation.jobId,
	    record->invocation.generation,
	    record->invocation.slotIndex
	);
	record->readyForDelete.store(true, std::memory_order_release);
	suspendForever();
}
