#pragma once

#include <atomic>
#include <memory>

#include <strata/freertos/Task.h>

#include "../core/runtime_containers.h"
#include "scheduler_executor.h"

class DedicatedTaskExecutor : public ISchedulerExecutor {
  public:
	DedicatedTaskExecutor(
	    Strata::Placement allocationPlacement,
	    Strata::Placement defaultStackPlacement
	);
	~DedicatedTaskExecutor() override;

	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override;
	void end(bool drainRunningJobs) override;
	bool submit(const JobInvocation &invocation) override;
	void reapCompleted() override;
	const char *name() const override;

  private:
	struct TaskRecord;

	static void taskEntry(void *arg);

	Strata::Placement allocationPlacement_ = Strata::Placement::PreferExternal;
	Strata::Placement defaultStackPlacement_ = Strata::Placement::PreferExternal;
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
	SchedulerArray<Strata::UniquePtr<TaskRecord>> tasks_{};
};
