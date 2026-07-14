#include "dedicated_task_executor.h"

#include <new>

#include "../service/scheduler_events.h"
#include "task_support.h"

namespace {
bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
	if (!runtime) {
		return false;
	}
	SchedulerEvent event{};
	event.kind = SchedulerEventKind::JobFinished;
	event.jobId = jobId;
	event.generation = generation;
	event.slotIndex = slotIndex;
	while (runtime->accepting.load(std::memory_order_acquire)) {
		QueueHandle_t queue = runtime->eventQueue.load(std::memory_order_acquire);
		if (!queue) {
			return false;
		}
		if (xQueueSend(queue, &event, pdMS_TO_TICKS(50)) == pdTRUE) {
			return true;
		}
	}
	return false;
}
} // namespace

struct DedicatedTaskExecutor::RuntimeState {
	std::atomic<bool> accepting{false};
	std::atomic<size_t> activeTasks{0};
};

struct DedicatedTaskExecutor::TaskContext {
	JobInvocation invocation{};
	std::shared_ptr<RuntimeState> state{};
	bool createdWithCaps = false;
};

DedicatedTaskExecutor::~DedicatedTaskExecutor() {
	end(true);
}

bool DedicatedTaskExecutor::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	if (state_ && state_->accepting.load()) {
		return true;
	}
	RuntimeState *rawState = new (std::nothrow) RuntimeState{};
	if (!rawState) {
		return false;
	}
	state_.reset(rawState);
	state_->accepting.store(true);
	runtime_ = runtime;
	return true;
}

void DedicatedTaskExecutor::end(bool drainRunningJobs) {
	std::shared_ptr<RuntimeState> state = state_;
	if (state) {
		state->accepting.store(false);
		if (drainRunningJobs) {
			const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
			while (state->activeTasks.load() > 0 && xTaskGetTickCount() < deadline) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		}
	}
	runtime_.reset();
	state_.reset();
}

bool DedicatedTaskExecutor::submit(const JobInvocation &invocation) {
	std::shared_ptr<RuntimeState> state = state_;
	if (!state || !state->accepting.load()) {
		return false;
	}
	TaskContext *context = new (std::nothrow) TaskContext{};
	if (!context) {
		return false;
	}
	context->invocation = invocation;
	context->invocation.runtime = runtime_;
	context->state = state;
	state->activeTasks.fetch_add(1);

	TaskHandle_t handle = nullptr;
	const DedicatedTaskOptions &task = invocation.dedicatedTask;
	const BaseType_t created = scheduler_task_support::createTaskPinned(
	    &DedicatedTaskExecutor::taskEntry,
	    task.name ? task.name : "sched-task",
	    task.stackSize,
	    context,
	    task.priority,
	    &handle,
	    task.coreId,
	    task.usePsramStack,
	    context->createdWithCaps
	);
	if (created != pdPASS || handle == nullptr) {
		state->activeTasks.fetch_sub(1);
		delete context;
		return false;
	}
	return true;
}

const char *DedicatedTaskExecutor::name() const {
	return "dedicated-task";
}

void DedicatedTaskExecutor::taskEntry(void *arg) {
	TaskContext *context = static_cast<TaskContext *>(arg);
	if (!context) {
		vTaskDelete(nullptr);
		return;
	}

	context->invocation.callback.invoke();
	postCompletion(
	    context->invocation.runtime,
	    context->invocation.jobId,
	    context->invocation.generation,
	    context->invocation.slotIndex
	);
	std::shared_ptr<RuntimeState> state = context->state;
	const bool createdWithCaps = context->createdWithCaps;
	delete context;
	if (state) {
		state->activeTasks.fetch_sub(1);
	}
	scheduler_task_support::deleteCurrentTask(createdWithCaps);
}
