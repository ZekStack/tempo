#pragma once

#include <atomic>

#include <strata/freertos/Queue.h>
#include <strata/freertos/Task.h>

#include "../core/runtime_containers.h"
#include "scheduler_executor.h"

class WorkerPoolExecutor : public ISchedulerExecutor {
  public:
	WorkerPoolExecutor(
	    const WorkerPoolConfig &config,
	    Strata::Placement allocationPlacement,
	    Strata::Placement defaultStackPlacement
	);
	~WorkerPoolExecutor() override;

	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override;
	void end(bool drainRunningJobs) override;
	bool submit(const JobInvocation &invocation) override;
	const char *name() const override;

  private:
	struct TaskItem;
	struct WorkerRecord;

	static void workerTaskEntry(void *arg);
	void destroyPendingItems();

	WorkerPoolConfig config_{};
	Strata::Placement allocationPlacement_ = Strata::Placement::PreferExternal;
	Strata::Placement defaultStackPlacement_ = Strata::Placement::PreferExternal;
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
	Strata::FreeRTOS::Queue<TaskItem *> queue_{};
	SchedulerArray<Strata::UniquePtr<WorkerRecord>> workers_{};
	std::atomic<bool> started_{false};
};
