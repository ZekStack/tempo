#include "scheduler_service.h"

namespace {
constexpr uint32_t kIdlePollMs = 1000;

[[noreturn]] void suspendForever() {
	vTaskSuspend(nullptr);
	for (;;) {
		vTaskDelay(portMAX_DELAY);
	}
}
} // namespace

TickType_t scheduler_service_detail::nextWakeTicks(
    Tempo &date,
    const DateTime &nowUtc,
    bool hasNextDue,
    int64_t nextDueEpochSeconds,
    TickType_t idlePollTicks
) {
	int64_t nextWakeEpoch = 0;
	bool hasWake = false;

	if (hasNextDue) {
		nextWakeEpoch = nextDueEpochSeconds;
		hasWake = true;
	}

	const DateTime nextLocalMidnightUtc = date.startOfDayLocal(date.addDays(nowUtc, 1));
	if (!hasWake || nextLocalMidnightUtc.epochSeconds < nextWakeEpoch) {
		nextWakeEpoch = nextLocalMidnightUtc.epochSeconds;
		hasWake = true;
	}

	if (!hasWake) {
		return idlePollTicks;
	}
	if (nextWakeEpoch <= nowUtc.epochSeconds) {
		return 0;
	}
	const int64_t waitSeconds = nextWakeEpoch - nowUtc.epochSeconds;
	return pdMS_TO_TICKS(static_cast<uint32_t>(waitSeconds * 1000));
}

SchedulerService::SchedulerService(
    Tempo &date,
    const SchedulerServiceConfig &config,
    const Strata::MemoryPolicy &memory,
    int64_t minValidEpochSeconds,
    std::atomic<bool> &timeContextRefreshRequested,
    IExecutorResolver &executors
) noexcept
    : date_(date), config_(config), memory_(memory),
      core_(date, minValidEpochSeconds, memory.allocation),
      timeContextRefreshRequested_(timeContextRefreshRequested), executors_(executors) {
}

SchedulerService::~SchedulerService() {
	stop();
}

bool SchedulerService::begin() {
	if (started_.load()) {
		return true;
	}

	stopRequested_.store(false);
	taskReadyForDelete_.store(false);
	activeInvocationCount_.store(0);

	commandQueue_ = Strata::FreeRTOS::Queue<SchedulerServiceCommand *>::create({
	    .length = config_.commandQueueDepth,
	    .storagePlacement = memory_.allocation,
	    .usage = Strata::FreeRTOS::QueueUsage::TaskOnly,
	});
	eventQueue_ = Strata::FreeRTOS::Queue<SchedulerEvent>::create({
	    .length = config_.eventQueueDepth,
	    .storagePlacement = memory_.allocation,
	    .usage = Strata::FreeRTOS::QueueUsage::TaskOnly,
	});
	wake_ = Strata::FreeRTOS::BinarySemaphore::create();
	if (!commandQueue_ || !eventQueue_ || !wake_) {
		stop();
		return false;
	}

	const Strata::Placement stackPlacement = config_.stackPlacement.value_or(memory_.taskStack);
	auto task = Strata::FreeRTOS::Task::create(
	    &SchedulerService::taskEntry,
	    this,
	    Strata::FreeRTOS::TaskConfig{
	        .name = "sched-svc",
	        .stackBytes = config_.taskStackSize,
	        .stackPlacement = stackPlacement,
	        .priority = config_.taskPriority,
	        .affinity = config_.coreId,
	    }
	);
	if (!task) {
		stop();
		return false;
	}
	task_ = std::move(task);
	started_.store(true);
	return true;
}

void SchedulerService::stop() {
	if (!commandQueue_ && !eventQueue_ && !wake_ && !task_) {
		started_.store(false);
		return;
	}

	stopRequested_.store(true);
	if (wake_) {
		(void)wake_.give();
	}

	const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
	while (task_ && !taskReadyForDelete_.load(std::memory_order_acquire) &&
	       xTaskGetTickCount() < deadline) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	if (task_) {
		task_.reset();
	}

	if (commandQueue_) {
		SchedulerServiceCommand *pending = nullptr;
		while (commandQueue_.receive(pending, 0)) {
			if (!pending) {
				continue;
			}
			pending->cancelAndSignal();
			pending->release();
		}
	}

	commandQueue_.reset();
	eventQueue_.reset();
	wake_.reset();
	taskReadyForDelete_.store(false);
	stopRequested_.store(false);
	activeInvocationCount_.store(0);
	started_.store(false);
}

bool SchedulerService::send(SchedulerServiceCommand *command) {
	if (!commandQueue_ || !wake_) {
		return false;
	}
	if (command != nullptr && !commandQueue_.send(command, 0)) {
		return false;
	}
	(void)wake_.give();
	return true;
}

bool SchedulerService::postEvent(const SchedulerEvent &event) {
	if (!eventQueue_ || !wake_) {
		return false;
	}
	if (!eventQueue_.send(event, 0)) {
		return false;
	}
	(void)wake_.give();
	return true;
}

bool SchedulerService::isCurrentTask() const {
	return task_ && xTaskGetCurrentTaskHandle() == task_.handle();
}

void SchedulerService::taskEntry(void *arg) {
	auto *service = static_cast<SchedulerService *>(arg);
	if (!service) {
		suspendForever();
	}
	service->run();
	service->taskReadyForDelete_.store(true, std::memory_order_release);
	suspendForever();
}

void SchedulerService::drainCommands() {
	if (!commandQueue_) {
		return;
	}
	for (;;) {
		SchedulerServiceCommand *command = nullptr;
		if (!commandQueue_.receive(command, 0)) {
			break;
		}
		if (!command) {
			continue;
		}
		if (command->tryBeginExecution()) {
			command->execute(core_, date_, executors_);
			command->complete();
		}
		command->release();
	}
}

void SchedulerService::drainEvents() {
	if (!eventQueue_) {
		return;
	}
	for (;;) {
		SchedulerEvent event{};
		if (!eventQueue_.receive(event, 0)) {
			break;
		}
		core_.handleEvent(event, date_.now(), executors_);
	}
}

void SchedulerService::refreshTimeContextIfNeeded(const DateTime &nowUtc) {
	bool refreshRequested = timeContextRefreshRequested_.exchange(false);
	const DateTime currentLocalDayStartUtc = date_.startOfDayLocal(nowUtc);
	if (!hasLastObservedLocalDayStartUtc_) {
		lastObservedLocalDayStartUtc_ = currentLocalDayStartUtc;
		hasLastObservedLocalDayStartUtc_ = true;
	} else if (!date_.isEqual(currentLocalDayStartUtc, lastObservedLocalDayStartUtc_)) {
		lastObservedLocalDayStartUtc_ = currentLocalDayStartUtc;
		refreshRequested = true;
	}

	if (refreshRequested) {
		(void)core_.refreshAllSchedules(nowUtc);
	}
}

void SchedulerService::run() {
	while (!stopRequested_.load(std::memory_order_acquire)) {
		drainCommands();
		drainEvents();
		executors_.reapCompletedExecutors();

		const DateTime nowUtc = date_.now();
		refreshTimeContextIfNeeded(nowUtc);
		core_.dispatchDue(nowUtc, executors_);
		activeInvocationCount_.store(core_.activeInvocationCount());

		int64_t nextEpochSeconds = 0;
		TickType_t waitTicks = pdMS_TO_TICKS(kIdlePollMs);
		if (core_.clockValid(nowUtc)) {
			const bool hasNextDue = core_.nextDueEpoch(nextEpochSeconds);
			waitTicks = scheduler_service_detail::nextWakeTicks(
			    date_, nowUtc, hasNextDue, nextEpochSeconds, waitTicks
			);
		}

		if (wake_) {
			(void)wake_.take(waitTicks);
		} else {
			vTaskDelay(waitTicks);
		}
	}

	drainCommands();
	drainEvents();
	executors_.reapCompletedExecutors();
}
