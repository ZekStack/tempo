#include "scheduler_commands.h"

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
