#pragma once

#include <atomic>
#include <memory>

#include <strata/freertos/BinarySemaphore.h>
#include <strata/freertos/Queue.h>
#include <strata/freertos/Task.h>

#include "../core/scheduler_core.h"
#include "../executors/scheduler_executor.h"
#include "scheduler_commands.h"

namespace scheduler_service_detail {
TickType_t nextWakeTicks(
    Tempo &date,
    const DateTime &nowUtc,
    bool hasNextDue,
    int64_t nextDueEpochSeconds,
    TickType_t idlePollTicks
);
}

class SchedulerService {
  public:
	SchedulerService(
	    Tempo &date,
	    const SchedulerServiceConfig &config,
	    const Strata::MemoryPolicy &memory,
	    int64_t minValidEpochSeconds,
	    std::atomic<bool> &timeContextRefreshRequested,
	    IExecutorResolver &executors
	);
	~SchedulerService();

	bool begin();
	void stop();

	bool send(SchedulerServiceCommand *command);
	bool postEvent(const SchedulerEvent &event);
	bool isCurrentTask() const;

	size_t activeInvocationCount() const {
		return activeInvocationCount_.load();
	}

  private:
	static void taskEntry(void *arg);
	static bool postEventThunk(void *context, const SchedulerEvent &event);

	void run();
	void drainCommands();
	void drainEvents();
	void refreshTimeContextIfNeeded(const DateTime &nowUtc);

	Tempo &date_;
	SchedulerServiceConfig config_{};
	Strata::MemoryPolicy memory_{};
	SchedulerCore core_;
	std::atomic<bool> &timeContextRefreshRequested_;
	IExecutorResolver &executors_;
	DateTime lastObservedLocalDayStartUtc_{};
	bool hasLastObservedLocalDayStartUtc_ = false;

	Strata::FreeRTOS::Queue<SchedulerServiceCommand *> commandQueue_{};
	Strata::FreeRTOS::Queue<SchedulerEvent> eventQueue_{};
	Strata::FreeRTOS::BinarySemaphore wake_{};
	Strata::FreeRTOS::Task task_{};

	std::atomic<bool> started_{false};
	std::atomic<bool> stopRequested_{false};
	std::atomic<bool> taskReadyForDelete_{false};
	std::atomic<size_t> activeInvocationCount_{0};
};
