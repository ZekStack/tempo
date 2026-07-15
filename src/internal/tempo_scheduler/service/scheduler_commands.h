#pragma once

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
