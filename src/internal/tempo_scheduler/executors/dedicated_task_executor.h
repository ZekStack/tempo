#pragma once

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
