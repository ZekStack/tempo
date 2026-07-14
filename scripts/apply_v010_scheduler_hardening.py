#!/usr/bin/env python3
from pathlib import Path


def write(path: str, content: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}\n--- old ---\n{old}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


write(
    "src/internal/tempo_scheduler/service/scheduler_commands.h",
    r'''#pragma once

#include <atomic>
#include <cstdint>

#include "../core/scheduler_core.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
}

enum class SchedulerCommandState : uint8_t {
	Pending = 0,
	Executing,
	Completed,
	Canceled,
};

class SchedulerServiceCommand {
  public:
	SchedulerServiceCommand();
	virtual ~SchedulerServiceCommand();

	void retain();
	void release();
	bool wait(uint32_t timeoutMs);
	bool waitForever();
	bool cancelPending();
	bool tryBeginExecution();
	void complete();
	void cancelAndSignal();
	SchedulerCommandState state() const;

	virtual void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) = 0;

  private:
	void signal();

	SemaphoreHandle_t completion_ = nullptr;
	StaticSemaphore_t completionBuffer_{};
	std::atomic<uint32_t> referenceCount_{1};
	std::atomic<SchedulerCommandState> state_{SchedulerCommandState::Pending};
};

class AddJobCommand : public SchedulerServiceCommand {
  public:
	TempoSchedule schedule{};
	SchedulerJobOptions options{};
	DedicatedTaskOptions dedicatedTaskCopy{};
	CallbackRef callback{};
	SchedulerResult<uint32_t> result =
	    SchedulerResult<uint32_t>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class CancelJobCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class PauseJobCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class ResumeJobCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class CancelAllCommand : public SchedulerServiceCommand {
  public:
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class RefreshAllSchedulesCommand : public SchedulerServiceCommand {
  public:
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class JobCountCommand : public SchedulerServiceCommand {
  public:
	SchedulerResult<size_t> result =
	    SchedulerResult<size_t>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class GetJobInfoCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<JobInfo> result =
	    SchedulerResult<JobInfo>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};

class SetMinValidCommand : public SchedulerServiceCommand {
  public:
	int64_t minEpochSeconds = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) override;
};
''',
)

write(
    "src/internal/tempo_scheduler/service/scheduler_commands.cpp",
    r'''#include "scheduler_commands.h"

SchedulerServiceCommand::SchedulerServiceCommand() {
	completion_ = xSemaphoreCreateBinaryStatic(&completionBuffer_);
}

SchedulerServiceCommand::~SchedulerServiceCommand() {
	if (completion_) {
		vSemaphoreDelete(completion_);
		completion_ = nullptr;
	}
}

void SchedulerServiceCommand::retain() {
	referenceCount_.fetch_add(1, std::memory_order_relaxed);
}

void SchedulerServiceCommand::release() {
	if (referenceCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete this;
	}
}

bool SchedulerServiceCommand::wait(uint32_t timeoutMs) {
	if (!completion_) {
		return false;
	}
	return xSemaphoreTake(completion_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool SchedulerServiceCommand::waitForever() {
	if (!completion_) {
		return false;
	}
	return xSemaphoreTake(completion_, portMAX_DELAY) == pdTRUE;
}

bool SchedulerServiceCommand::cancelPending() {
	SchedulerCommandState expected = SchedulerCommandState::Pending;
	return state_.compare_exchange_strong(
	    expected,
	    SchedulerCommandState::Canceled,
	    std::memory_order_acq_rel,
	    std::memory_order_acquire
	);
}

bool SchedulerServiceCommand::tryBeginExecution() {
	SchedulerCommandState expected = SchedulerCommandState::Pending;
	return state_.compare_exchange_strong(
	    expected,
	    SchedulerCommandState::Executing,
	    std::memory_order_acq_rel,
	    std::memory_order_acquire
	);
}

void SchedulerServiceCommand::complete() {
	state_.store(SchedulerCommandState::Completed, std::memory_order_release);
	signal();
}

void SchedulerServiceCommand::cancelAndSignal() {
	SchedulerCommandState expected = SchedulerCommandState::Pending;
	if (state_.compare_exchange_strong(
	        expected,
	        SchedulerCommandState::Canceled,
	        std::memory_order_acq_rel,
	        std::memory_order_acquire
	    )) {
		signal();
	}
}

SchedulerCommandState SchedulerServiceCommand::state() const {
	return state_.load(std::memory_order_acquire);
}

void SchedulerServiceCommand::signal() {
	if (completion_) {
		xSemaphoreGive(completion_);
	}
}

void AddJobCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)executors;
	result = core.addJob(schedule, options, callback, date.now());
}

void CancelJobCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.cancelJob(jobId);
}

void PauseJobCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.pauseJob(jobId);
}

void ResumeJobCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)executors;
	result = core.resumeJob(jobId, date.now());
}

void CancelAllCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.cancelAll();
}

void RefreshAllSchedulesCommand::execute(
    SchedulerCore &core, Tempo &date, IExecutorResolver &executors
) {
	(void)executors;
	result = core.refreshAllSchedules(date.now());
}

void JobCountCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.jobCount();
}

void GetJobInfoCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	JobInfo info{};
	const SchedulerResult<void> infoResult = core.getJobInfo(jobId, info);
	if (!infoResult) {
		result = SchedulerResult<JobInfo>::failure(infoResult.error);
		return;
	}
	result = SchedulerResult<JobInfo>::success(info);
}

void SetMinValidCommand::execute(SchedulerCore &core, Tempo &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	core.setMinValidUnixSeconds(minEpochSeconds);
	result = SchedulerResult<void>::success();
}
''',
)

replace_once(
    "src/internal/tempo_scheduler/service/scheduler_service.h",
    "\tbool send(SchedulerServiceCommand *command);\n\n\tQueueHandle_t eventQueue() const {",
    "\tbool send(SchedulerServiceCommand *command);\n\tbool isCurrentTask() const;\n\n\tQueueHandle_t eventQueue() const {",
)

replace_once(
    "src/internal/tempo_scheduler/service/scheduler_service.cpp",
    "\tif (started_.load()) {\n\t\treturn true;\n\t}\n\n\tcommandQueue_ = xQueueCreate(config_.commandQueueDepth, sizeof(SchedulerServiceCommand *));",
    "\tif (started_.load()) {\n\t\treturn true;\n\t}\n\n\tstopRequested_.store(false);\n\ttaskExited_.store(false);\n\tcommandQueue_ = xQueueCreate(config_.commandQueueDepth, sizeof(SchedulerServiceCommand *));",
)

replace_once(
    "src/internal/tempo_scheduler/service/scheduler_service.cpp",
    '''\t\t\t// Producer owns the command until send() succeeds, then may free it only after wait()
\t\t\t// completes. The service may free only abandoned commands, and must decide before
\t\t\t// signal().
\t\t\tconst bool shouldDelete = pending->abandoned();
\t\t\tpending->signal();
\t\t\tif (shouldDelete) {
\t\t\t\tdelete pending;
\t\t\t}
''',
    '''\t\t\tpending->cancelAndSignal();
\t\t\tpending->release();
''',
)

replace_once(
    "src/internal/tempo_scheduler/service/scheduler_service.cpp",
    '''bool SchedulerService::send(SchedulerServiceCommand *command) {
\tif (!commandQueue_) {
\t\treturn false;
\t}
\treturn xQueueSend(commandQueue_, &command, 0) == pdTRUE;
}
''',
    '''bool SchedulerService::send(SchedulerServiceCommand *command) {
\tif (!commandQueue_) {
\t\treturn false;
\t}
\treturn xQueueSend(commandQueue_, &command, 0) == pdTRUE;
}

bool SchedulerService::isCurrentTask() const {
\treturn task_ != nullptr && xTaskGetCurrentTaskHandle() == task_;
}
''',
)

replace_once(
    "src/internal/tempo_scheduler/service/scheduler_service.cpp",
    '''\t\tcommand->execute(core_, date_, executors_);
\t\t// Producer owns the command until send() succeeds, then may free it only after wait()
\t\t// completes. The service may free only abandoned commands, and must decide before signal().
\t\tconst bool shouldDelete = command->abandoned();
\t\tcommand->signal();
\t\tif (shouldDelete) {
\t\t\tdelete command;
\t\t}
''',
    '''\t\tif (command->tryBeginExecution()) {
\t\t\tcommand->execute(core_, date_, executors_);
\t\t\tcommand->complete();
\t\t}
\t\tcommand->release();
''',
)

replace_once(
    "src/internal/tempo_scheduler/scheduler.h",
    "struct JobInfo {\n\tuint32_t id = 0;\n\tconst char *name = nullptr;",
    "static constexpr size_t kSchedulerJobNameCapacity = 48;\n\nstruct JobInfo {\n\tuint32_t id = 0;\n\tchar name[kSchedulerJobNameCapacity]{};\n\tbool nameTruncated = false;",
)

replace_once(
    "src/internal/tempo_scheduler/core/job_record.h",
    "\texplicit JobRecord(bool usePSRAMMetadata = false) : name(usePSRAMMetadata) {",
    "\texplicit JobRecord(bool usePSRAMMetadata = false)\n\t    : name(usePSRAMMetadata), dedicatedTaskName(usePSRAMMetadata) {",
)

replace_once(
    "src/internal/tempo_scheduler/core/job_record.h",
    "\tSchedulerOwnedString name{};\n\tDedicatedTaskOptions dedicatedTask{};",
    "\tSchedulerOwnedString name{};\n\tSchedulerOwnedString dedicatedTaskName{};\n\tDedicatedTaskOptions dedicatedTask{};",
)

replace_once(
    "src/internal/tempo_scheduler/core/scheduler_core.cpp",
    "\trecord.name.clear();\n\trecord.id = 0;",
    "\trecord.name.clear();\n\trecord.dedicatedTaskName.clear();\n\trecord.dedicatedTask = DedicatedTaskOptions{};\n\trecord.hasDedicatedTaskOptions = false;\n\trecord.id = 0;",
)

replace_once(
    "src/internal/tempo_scheduler/core/scheduler_core.cpp",
    '''\tif (options.dedicatedTask) {
\t\trecord.dedicatedTask = *options.dedicatedTask;
\t}
''',
    '''\tif (options.dedicatedTask) {
\t\trecord.dedicatedTask = *options.dedicatedTask;
\t\tif (!record.dedicatedTaskName.assign(options.dedicatedTask->name)) {
\t\t\treturn SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
\t\t}
\t\trecord.dedicatedTask.name = record.dedicatedTaskName.empty()
\t\t                                ? nullptr
\t\t                                : record.dedicatedTaskName.c_str();
\t}
''',
)

replace_once(
    "src/internal/tempo_scheduler/core/scheduler_core.cpp",
    '''\tout = JobInfo{};
\tout.id = record.id;
\tout.name = record.name.empty() ? nullptr : record.name.c_str();
\tout.paused = record.paused;
''',
    '''\tout = JobInfo{};
\tout.id = record.id;
\tconst char *sourceName = record.name.empty() ? nullptr : record.name.c_str();
\tif (sourceName) {
\t\tsize_t nameIndex = 0;
\t\twhile (sourceName[nameIndex] != '\\0' && nameIndex + 1 < kSchedulerJobNameCapacity) {
\t\t\tout.name[nameIndex] = sourceName[nameIndex];
\t\t\t++nameIndex;
\t\t}
\t\tout.name[nameIndex] = '\\0';
\t\tout.nameTruncated = sourceName[nameIndex] != '\\0';
\t}
\tout.paused = record.paused;
''',
)

replace_once(
    "src/internal/tempo_scheduler/executors/scheduler_executor.h",
    "\tstd::atomic<bool> accepting{true};\n\tQueueHandle_t eventQueue = nullptr;",
    "\tstd::atomic<bool> accepting{true};\n\tstd::atomic<QueueHandle_t> eventQueue{nullptr};",
)

post_completion_old = '''bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
\tif (!runtime || !runtime->accepting.load() || runtime->eventQueue == nullptr) {
\t\treturn false;
\t}
\tSchedulerEvent event{};
\tevent.kind = SchedulerEventKind::JobFinished;
\tevent.jobId = jobId;
\tevent.generation = generation;
\tevent.slotIndex = slotIndex;
\treturn xQueueSend(runtime->eventQueue, &event, 0) == pdTRUE;
}
'''
post_completion_new = '''bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
\tif (!runtime) {
\t\treturn false;
\t}
\tSchedulerEvent event{};
\tevent.kind = SchedulerEventKind::JobFinished;
\tevent.jobId = jobId;
\tevent.generation = generation;
\tevent.slotIndex = slotIndex;
\twhile (runtime->accepting.load(std::memory_order_acquire)) {
\t\tQueueHandle_t queue = runtime->eventQueue.load(std::memory_order_acquire);
\t\tif (!queue) {
\t\t\treturn false;
\t\t}
\t\tif (xQueueSend(queue, &event, pdMS_TO_TICKS(50)) == pdTRUE) {
\t\t\treturn true;
\t\t}
\t}
\treturn false;
}
'''
replace_once(
    "src/internal/tempo_scheduler/executors/worker_pool_executor.cpp",
    post_completion_old,
    post_completion_new,
)
replace_once(
    "src/internal/tempo_scheduler/executors/dedicated_task_executor.cpp",
    post_completion_old,
    post_completion_new,
)

replace_once(
    "src/internal/tempo_scheduler/executors/worker_pool_executor.cpp",
    '''void WorkerPoolExecutor::end(bool drainRunningJobs) {
\tif (!queue_) {
\t\tstarted_.store(false);
\t\truntime_.reset();
\t\treturn;
\t}

\tif (!drainRunningJobs) {
\t\tTaskItem *pending = nullptr;
\t\twhile (xQueueReceive(queue_, &pending, 0) == pdTRUE) {
\t\t\tdelete pending;
\t\t}
\t}

\tfor (size_t index = 0; index < workers_.size(); ++index) {
\t\tTaskItem *sentinel = nullptr;
\t\txQueueSend(queue_, &sentinel, 0);
\t}

\tconst TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
\twhile (workersRunning_.load() > 0 && xTaskGetTickCount() < deadline) {
\t\tvTaskDelay(pdMS_TO_TICKS(10));
\t}

\tif (workersRunning_.load() > 0) {
\t\tfor (size_t index = 0; index < workers_.size(); ++index) {
\t\t\tscheduler_task_support::deleteTask(
\t\t\t    workers_[index].task,
\t\t\t    workers_[index].createdWithCaps
\t\t\t);
\t\t}
\t}

\tif (queue_) {
\t\tvQueueDelete(queue_);
\t\tqueue_ = nullptr;
\t}
\tworkers_.clear();
\tstarted_.store(false);
\truntime_.reset();
}
''',
    '''void WorkerPoolExecutor::end(bool drainRunningJobs) {
\tstarted_.store(false);
\tif (!queue_) {
\t\truntime_.reset();
\t\treturn;
\t}

\tif (!drainRunningJobs) {
\t\tTaskItem *pending = nullptr;
\t\twhile (xQueueReceive(queue_, &pending, 0) == pdTRUE) {
\t\t\tdelete pending;
\t\t}
\t}

\tfor (size_t index = 0; index < workers_.size(); ++index) {
\t\tTaskItem *sentinel = nullptr;
\t\twhile (workersRunning_.load() > 0 &&
\t\t       xQueueSend(queue_, &sentinel, pdMS_TO_TICKS(50)) != pdTRUE) {
\t\t}
\t}

\tconst TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
\twhile (workersRunning_.load() > 0 && xTaskGetTickCount() < deadline) {
\t\tvTaskDelay(pdMS_TO_TICKS(10));
\t}

\tif (workersRunning_.load() > 0) {
\t\tfor (size_t index = 0; index < workers_.size(); ++index) {
\t\t\tscheduler_task_support::deleteTask(
\t\t\t    workers_[index].task,
\t\t\t    workers_[index].createdWithCaps
\t\t\t);
\t\t}
\t\tworkersRunning_.store(0);
\t}

\tTaskItem *pending = nullptr;
\twhile (xQueueReceive(queue_, &pending, 0) == pdTRUE) {
\t\tdelete pending;
\t}
\tvQueueDelete(queue_);
\tqueue_ = nullptr;
\tworkers_.clear();
\truntime_.reset();
}
''',
)

replace_once(
    "src/internal/tempo_scheduler/executors/worker_pool_executor.cpp",
    '''\t\tpostCompletion(
\t\t    owner->runtime_,
\t\t    item->invocation.jobId,
''',
    '''\t\tpostCompletion(
\t\t    item->invocation.runtime,
\t\t    item->invocation.jobId,
''',
)

write(
    "src/internal/tempo_scheduler/executors/dedicated_task_executor.h",
    r'''#pragma once

#include <memory>

#include "scheduler_executor.h"

class DedicatedTaskExecutor : public ISchedulerExecutor {
  public:
	DedicatedTaskExecutor() = default;
	~DedicatedTaskExecutor() override;

	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override;
	void end(bool drainRunningJobs) override;
	bool submit(const JobInvocation &invocation) override;
	const char *name() const override;

  private:
	struct RuntimeState;
	struct TaskContext;

	static void taskEntry(void *arg);

	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
	std::shared_ptr<RuntimeState> state_{};
};
''',
)

write(
    "src/internal/tempo_scheduler/executors/dedicated_task_executor.cpp",
    post_completion_new.replace('bool postCompletion(', '#include "dedicated_task_executor.h"\n\n#include <new>\n\n#include "../service/scheduler_events.h"\n#include "task_support.h"\n\nnamespace {\nbool postCompletion(', 1)
    + r'''} // namespace

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
''',
)

replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''TResult executeBackgroundCommand(
    SchedulerService &service,
    uint32_t timeoutMs,
    SchedulerError queueError,
    SchedulerError timeoutError,
    FBuild &&build
) {
\tTCommand *command = new (std::nothrow) TCommand();
\tif (!command) {
\t\treturn TResult::failure(SchedulerError::NoMemory);
\t}
\tbuild(*command);
\tif (!service.send(command)) {
\t\tdelete command;
\t\treturn TResult::failure(queueError);
\t}
\tif (!command->wait(timeoutMs)) {
\t\tcommand->abandon();
\t\treturn TResult::failure(timeoutError);
\t}
\tTResult result = command->result;
\tdelete command;
\treturn result;
}
''',
    '''TResult executeBackgroundCommand(
    SchedulerService &service,
    uint32_t timeoutMs,
    SchedulerError queueError,
    SchedulerError timeoutError,
    FBuild &&build
) {
\tif (service.isCurrentTask()) {
\t\treturn TResult::failure(SchedulerError::Busy);
\t}
\tTCommand *command = new (std::nothrow) TCommand();
\tif (!command) {
\t\treturn TResult::failure(SchedulerError::NoMemory);
\t}
\tbuild(*command);
\tcommand->retain();
\tif (!service.send(command)) {
\t\tcommand->release();
\t\tcommand->release();
\t\treturn TResult::failure(queueError);
\t}
\tif (!command->wait(timeoutMs)) {
\t\tif (command->cancelPending()) {
\t\t\tcommand->release();
\t\t\treturn TResult::failure(timeoutError);
\t\t}
\t\tif (!command->waitForever()) {
\t\t\tcommand->release();
\t\t\treturn TResult::failure(SchedulerError::InternalError);
\t\t}
\t}
\tTResult result = command->result;
\tcommand->release();
\treturn result;
}
''',
)

for old, new in [
    ("impl_->runtime->eventQueue = impl_->service->eventQueue();", "impl_->runtime->eventQueue.store(impl_->service->eventQueue());"),
    ("impl_->runtime->eventQueue = impl_->eventQueue;", "impl_->runtime->eventQueue.store(impl_->eventQueue);"),
    ("impl_->runtime->eventQueue = nullptr;", "impl_->runtime->eventQueue.store(nullptr);"),
]:
    text = Path("src/internal/tempo_scheduler/scheduler.cpp").read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"scheduler.cpp missing expected runtime queue assignment: {old}")
    Path("src/internal/tempo_scheduler/scheduler.cpp").write_text(text.replace(old, new), encoding="utf-8")

replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\tif (!impl_ || !impl_->started) {
\t\treturn;
\t}

\timpl_->draining = true;
''',
    '''\tif (!impl_ || !impl_->started) {
\t\treturn;
\t}
\tif (impl_->config.mode == TempoSchedulerMode::Background && impl_->service &&
\t    impl_->service->isCurrentTask()) {
\t\treturn;
\t}

\timpl_->draining = true;
''',
)

replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\tif (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
\t\treturn executeBackgroundCommand<GetJobInfoCommand, SchedulerResult<void>>(
\t\t    *impl_->service,
\t\t    impl_->config.service.controlTimeoutMs,
\t\t    SchedulerError::QueueFull,
\t\t    SchedulerError::Timeout,
\t\t    [&](GetJobInfoCommand &command) {
\t\t\t    command.jobId = jobId;
\t\t\t    command.info = &out;
\t\t    }
\t\t);
\t}
''',
    '''\tif (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
\t\tconst SchedulerResult<JobInfo> result =
\t\t    executeBackgroundCommand<GetJobInfoCommand, SchedulerResult<JobInfo>>(
\t\t        *impl_->service,
\t\t        impl_->config.service.controlTimeoutMs,
\t\t        SchedulerError::QueueFull,
\t\t        SchedulerError::Timeout,
\t\t        [&](GetJobInfoCommand &command) { command.jobId = jobId; }
\t\t    );
\t\tif (!result) {
\t\t\tout = JobInfo{};
\t\t\treturn SchedulerResult<void>::failure(result.error);
\t\t}
\t\tout = result.value;
\t\treturn SchedulerResult<void>::success();
\t}
''',
)

# Keep the temporary workflow out of the resulting branch commit.
Path("scripts/apply_v010_scheduler_hardening.py").unlink(missing_ok=True)
Path(".github/workflows/apply-v010-scheduler-hardening.yml").unlink(missing_ok=True)
