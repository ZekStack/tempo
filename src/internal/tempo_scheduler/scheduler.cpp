#include "scheduler.h"

#include <utility>

#include <strata/freertos/Queue.h>

#include "core/runtime_containers.h"
#include "core/scheduler_core.h"
#include "executors/dedicated_task_executor.h"
#include "executors/inline_executor.h"
#include "executors/worker_pool_executor.h"
#include "service/scheduler_commands.h"
#include "service/scheduler_service.h"

namespace {
constexpr size_t kMinTaskStackBytes = 1024;

bool validStackSize(size_t stackBytes) {
	return stackBytes >= kMinTaskStackBytes && (stackBytes % sizeof(StackType_t)) == 0;
}

bool validOptionalPlacement(const std::optional<Strata::Placement> &placement) {
	return !placement || Strata::validPlacement(*placement);
}

bool validSchedulerConfig(const SchedulerConfig &config) {
	return Strata::validMemoryPolicy(config.memory) &&
	       validOptionalPlacement(config.service.stackPlacement) &&
	       validOptionalPlacement(config.defaultWorkerPool.stackPlacement) &&
	       validOptionalPlacement(config.defaultDedicatedTask.stackPlacement) &&
	       config.service.commandQueueDepth > 0 && config.service.eventQueueDepth > 0 &&
	       validStackSize(config.service.taskStackSize) && config.service.controlTimeoutMs > 0 &&
	       config.defaultWorkerPool.workerCount > 0 && config.defaultWorkerPool.queueDepth > 0 &&
	       validStackSize(config.defaultWorkerPool.stackSize) &&
	       validStackSize(config.defaultDedicatedTask.stackSize);
}

template <typename TCommand, typename TResult, typename FBuild>
TResult executeBackgroundCommand(
    SchedulerService &service,
    Strata::Placement allocationPlacement,
    uint32_t timeoutMs,
    SchedulerError queueError,
    SchedulerError timeoutError,
    FBuild &&build
) {
	if (service.isCurrentTask()) {
		return TResult::failure(SchedulerError::Busy);
	}
	TCommand *command = SchedulerServiceCommand::create<TCommand>(allocationPlacement);
	if (!command) {
		return TResult::failure(SchedulerError::NoMemory);
	}
	build(*command);
	command->retain();
	if (!service.send(command)) {
		command->release();
		command->release();
		return TResult::failure(queueError);
	}
	if (!command->wait(timeoutMs)) {
		if (command->cancelPending()) {
			command->release();
			return TResult::failure(timeoutError);
		}
		if (!command->waitForever()) {
			command->release();
			return TResult::failure(SchedulerError::InternalError);
		}
	}
	TResult result = command->result;
	command->release();
	return result;
}
} // namespace

struct TempoScheduler::Impl : public IExecutorResolver {
	explicit Impl(Tempo &date, const SchedulerConfig &config) noexcept
	    : date(date), config(config),
	      manualCore(date, config.minValidEpochSeconds, config.memory.allocation),
	      externalExecutors(config.memory.allocation), executors(config.memory.allocation) {
	}

	~Impl() override {
		eventQueue.reset();
	}

	ISchedulerExecutor *inlineExecutor() override {
		return inlineDispatch.get();
	}

	ISchedulerExecutor *executorFor(uint8_t executorId) override {
		if (executorId >= executors.size()) {
			return nullptr;
		}
		return executors[executorId];
	}

	void reapCompletedExecutors() override {
		for (size_t index = 0; index < executors.size(); ++index) {
			if (executors[index]) {
				executors[index]->reapCompleted();
			}
		}
	}

	static bool postServiceEvent(void *context, const SchedulerEvent &event) {
		auto *schedulerService = static_cast<SchedulerService *>(context);
		return schedulerService != nullptr && schedulerService->postEvent(event);
	}

	static bool postManualEvent(void *context, const SchedulerEvent &event) {
		auto *self = static_cast<Impl *>(context);
		return self != nullptr && self->eventQueue && self->eventQueue.send(event, 0);
	}

	bool startExecutors() {
		inlineDispatch = Strata::makeUnique<InlineExecutor>(config.memory.allocation);
		dedicatedTask = Strata::makeUnique<DedicatedTaskExecutor>(
		    config.memory.allocation,
		    config.memory.allocation,
		    config.memory.taskStack
		);
		workerPool = Strata::makeUnique<WorkerPoolExecutor>(
		    config.memory.allocation,
		    config.defaultWorkerPool,
		    config.memory.allocation,
		    config.memory.taskStack
		);
		if (!inlineDispatch || !dedicatedTask || !workerPool) {
			return false;
		}
		if (!inlineDispatch->begin(runtime)) {
			return false;
		}

		executors.clear();
		if (!executors.pushBack(workerPool.get())) {
			return false;
		}
		if (!executors.pushBack(dedicatedTask.get())) {
			return false;
		}
		for (size_t index = 0; index < externalExecutors.size(); ++index) {
			if (!executors.pushBack(externalExecutors[index])) {
				return false;
			}
		}

		for (size_t index = 0; index < executors.size(); ++index) {
			ISchedulerExecutor *executor = executors[index];
			if (!executor || !executor->begin(runtime)) {
				return false;
			}
		}
		return true;
	}

	void stopExecutors(bool drainRunningJobs) {
		for (size_t index = 0; index < executors.size(); ++index) {
			ISchedulerExecutor *executor = executors[index];
			if (executor) {
				executor->end(drainRunningJobs);
			}
		}
		executors.clear();
		if (inlineDispatch) {
			inlineDispatch->end(drainRunningJobs);
		}
		inlineDispatch.reset();
		dedicatedTask.reset();
		workerPool.reset();
	}

	void drainManualEvents(const DateTime &nowUtc) {
		if (!eventQueue) {
			return;
		}
		for (;;) {
			SchedulerEvent event{};
			if (!eventQueue.receive(event, 0)) {
				break;
			}
			manualCore.handleEvent(event, nowUtc, *this);
		}
	}

	void resetTimeContextTracking() {
		timeContextRefreshRequested.store(false);
		lastObservedLocalDayStartUtc = DateTime{};
		hasLastObservedLocalDayStartUtc = false;
	}

	void requestTimeContextRefresh() {
		timeContextRefreshRequested.store(true);
	}

	bool refreshTimeContextIfNeeded(const DateTime &nowUtc) {
		bool refreshRequested = timeContextRefreshRequested.exchange(false);
		const DateTime currentLocalDayStartUtc = date.startOfDayLocal(nowUtc);
		if (!hasLastObservedLocalDayStartUtc) {
			lastObservedLocalDayStartUtc = currentLocalDayStartUtc;
			hasLastObservedLocalDayStartUtc = true;
		} else if (!date.isEqual(currentLocalDayStartUtc, lastObservedLocalDayStartUtc)) {
			lastObservedLocalDayStartUtc = currentLocalDayStartUtc;
			refreshRequested = true;
		}
		return refreshRequested;
	}

	bool registerTimeSyncListener() {
		ntpSyncListenerId = date.addNtpSyncListener([this](const DateTime &) {
			requestTimeContextRefresh();
			if (config.mode == TempoSchedulerMode::Background && service) {
				(void)service->send(nullptr);
			}
		});
		return ntpSyncListenerId != 0;
	}

	void unregisterTimeSyncListener() {
		if (ntpSyncListenerId != 0) {
			(void)date.removeNtpSyncListener(ntpSyncListenerId);
			ntpSyncListenerId = 0;
		}
		resetTimeContextTracking();
	}

	void clearRuntimePublisher() {
		if (!runtime) {
			return;
		}
		runtime->accepting.store(false, std::memory_order_release);
		runtime->postEvent.store(nullptr, std::memory_order_release);
		runtime->postEventContext.store(nullptr, std::memory_order_release);
	}

	Tempo &date;
	SchedulerConfig config{};
	SchedulerCore manualCore;
	Strata::UniquePtr<SchedulerService> service{};
	Strata::UniquePtr<InlineExecutor> inlineDispatch{};
	Strata::UniquePtr<WorkerPoolExecutor> workerPool{};
	Strata::UniquePtr<DedicatedTaskExecutor> dedicatedTask{};
	SchedulerArray<ISchedulerExecutor *> externalExecutors{};
	SchedulerArray<ISchedulerExecutor *> executors{};
	std::shared_ptr<SchedulerExecutorRuntime> runtime{};
	Strata::FreeRTOS::Queue<SchedulerEvent> eventQueue{};
	std::atomic<bool> timeContextRefreshRequested{false};
	DateTime lastObservedLocalDayStartUtc{};
	bool hasLastObservedLocalDayStartUtc = false;
	Tempo::NtpSyncListenerId ntpSyncListenerId = 0;
	bool started = false;
	bool draining = false;
};

TempoScheduler::TempoScheduler() = default;

TempoScheduler::TempoScheduler(Tempo &date, const SchedulerConfig &config)
    : impl_(Strata::makeUnique<Impl>(config.memory.allocation, date, config)) {
}

TempoScheduler::~TempoScheduler() {
	end(true);
}

SchedulerResult<void> TempoScheduler::init(Tempo &date, const SchedulerConfig &config) {
	if (!validSchedulerConfig(config)) {
		return SchedulerResult<void>::failure(SchedulerError::InvalidConfiguration);
	}
	if (impl_ && impl_->started) {
		return SchedulerResult<void>::failure(SchedulerError::AlreadyInitialized);
	}
	impl_ = Strata::makeUnique<Impl>(config.memory.allocation, date, config);
	if (!impl_) {
		return SchedulerResult<void>::failure(SchedulerError::NoMemory);
	}
	return init() ? SchedulerResult<void>::success()
	              : SchedulerResult<void>::failure(SchedulerError::InternalError);
}

bool TempoScheduler::init() {
	if (!impl_ || !validSchedulerConfig(impl_->config)) {
		return false;
	}
	if (impl_->started) {
		return true;
	}

	impl_->runtime = Strata::makeShared<SchedulerExecutorRuntime>(impl_->config.memory.allocation);
	if (!impl_->runtime) {
		return false;
	}

	if (impl_->config.mode == TempoSchedulerMode::Background) {
		impl_->service = Strata::makeUnique<SchedulerService>(
		    impl_->config.memory.allocation,
		    impl_->date,
		    impl_->config.service,
		    impl_->config.memory,
		    impl_->config.minValidEpochSeconds,
		    impl_->timeContextRefreshRequested,
		    *impl_
		);
		if (!impl_->service || !impl_->service->begin()) {
			impl_->service.reset();
			impl_->runtime.reset();
			return false;
		}
		impl_->runtime->postEventContext.store(impl_->service.get(), std::memory_order_release);
		impl_->runtime->postEvent.store(&Impl::postServiceEvent, std::memory_order_release);
	} else {
		impl_->eventQueue = Strata::FreeRTOS::Queue<SchedulerEvent>::create({
		    .length = impl_->config.service.eventQueueDepth,
		    .storagePlacement = impl_->config.memory.allocation,
		    .usage = Strata::FreeRTOS::QueueUsage::TaskOnly,
		});
		if (!impl_->eventQueue) {
			impl_->runtime.reset();
			return false;
		}
		impl_->runtime->postEventContext.store(impl_.get(), std::memory_order_release);
		impl_->runtime->postEvent.store(&Impl::postManualEvent, std::memory_order_release);
	}

	impl_->runtime->accepting.store(true, std::memory_order_release);
	impl_->started = true;
	if (!impl_->startExecutors()) {
		end(false);
		return false;
	}
	if (!impl_->registerTimeSyncListener()) {
		end(false);
		return false;
	}

	impl_->draining = false;
	return true;
}

void TempoScheduler::end(bool waitForRunningJobs, uint32_t timeoutMs) {
	if (!impl_ || !impl_->started) {
		return;
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service &&
	    impl_->service->isCurrentTask()) {
		return;
	}

	impl_->draining = true;
	impl_->unregisterTimeSyncListener();

	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		(void)executeBackgroundCommand<CancelAllCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [](CancelAllCommand &) {}
		);

		if (waitForRunningJobs) {
			const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
			while (impl_->service->activeInvocationCount() > 0 && xTaskGetTickCount() < deadline) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		} else if (impl_->runtime) {
			impl_->runtime->accepting.store(false, std::memory_order_release);
		}

		impl_->stopExecutors(waitForRunningJobs);
		impl_->clearRuntimePublisher();
		impl_->service->stop();
		impl_->service.reset();
	} else {
		(void)impl_->manualCore.cancelAll();
		const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
		while (waitForRunningJobs && impl_->manualCore.activeInvocationCount() > 0 &&
		       xTaskGetTickCount() < deadline) {
			impl_->drainManualEvents(impl_->date.now());
			impl_->reapCompletedExecutors();
			vTaskDelay(pdMS_TO_TICKS(10));
		}
		if (!waitForRunningJobs && impl_->runtime) {
			impl_->runtime->accepting.store(false, std::memory_order_release);
		}
		impl_->stopExecutors(waitForRunningJobs);
		impl_->clearRuntimePublisher();
		impl_->eventQueue.reset();
	}

	impl_->runtime.reset();
	impl_->started = false;
	impl_->draining = false;
}

bool TempoScheduler::running() const {
	return impl_ && impl_->started && !impl_->draining;
}

bool TempoScheduler::draining() const {
	return impl_ && impl_->draining;
}

SchedulerResult<uint8_t> TempoScheduler::registerExecutor(ISchedulerExecutor *executor) {
	if (!impl_ || !executor) {
		return SchedulerResult<uint8_t>::failure(SchedulerError::ExecutorUnavailable);
	}
	if (impl_->started) {
		return SchedulerResult<uint8_t>::failure(SchedulerError::Busy);
	}
	const uint8_t executorId = static_cast<uint8_t>(2 + impl_->externalExecutors.size());
	if (!impl_->externalExecutors.pushBack(executor)) {
		return SchedulerResult<uint8_t>::failure(SchedulerError::NoMemory);
	}
	return SchedulerResult<uint8_t>::success(executorId);
}

SchedulerResult<uint32_t> TempoScheduler::addJob(
    const TempoSchedule &schedule,
    const SchedulerJobOptions &options,
    SchedulerCallbackFn callback,
    void *userData
) {
	if (!callback) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	CallbackRef ref{};
	ref.kind = CallbackKind::RawFunction;
	ref.rawFn = callback;
	ref.userData = userData;
	return addJobImpl(schedule, options, ref);
}

SchedulerResult<uint32_t> TempoScheduler::addJob(
    const TempoSchedule &schedule,
    const SchedulerJobOptions &options,
    SchedulerFunction callback,
    void *userData
) {
	if (!callback || !impl_) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	CallbackRef ref{};
	ref.kind = CallbackKind::OwningFunction;
	ref.userData = userData;
	ref.owningFn = Strata::makeShared<SchedulerFunction>(
	    impl_->config.memory.allocation,
	    std::move(callback)
	);
	if (!ref.owningFn) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}
	return addJobImpl(schedule, options, ref);
}

SchedulerResult<uint32_t> TempoScheduler::addJob(
    const TempoSchedule &schedule, const SchedulerJobOptions &options, SchedulerFunctionNoData callback
) {
	if (!callback || !impl_) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	SchedulerFunction wrapped = [fn = std::move(callback)](void *) { fn(); };
	CallbackRef ref{};
	ref.kind = CallbackKind::OwningFunction;
	ref.owningFn = Strata::makeShared<SchedulerFunction>(
	    impl_->config.memory.allocation,
	    std::move(wrapped)
	);
	if (!ref.owningFn) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}
	return addJobImpl(schedule, options, ref);
}

SchedulerResult<uint32_t> TempoScheduler::schedule(
    const TempoSchedule &schedule, const SchedulerJobOptions &options, SchedulerFunctionNoData callback
) {
	return addJob(schedule, options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::everyMinutes(
    int minutes, const char *name, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::everyMinutes(minutes), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::everyHours(
    int hours, const char *name, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::everyHours(hours), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::dailyAt(
    int hour, int minute, const char *name, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::dailyAt(hour, minute), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::cron(
    const char *expression, const char *name, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::cron(expression), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::everySunRise(
    const char *name, SchedulerFunctionNoData callback
) {
	return everySunRise(name, TempoDuration{}, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::everySunRise(
    const char *name, const TempoDuration &offset, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::sunRise(offset), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::everySunSet(
    const char *name, SchedulerFunctionNoData callback
) {
	return everySunSet(name, TempoDuration{}, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::everySunSet(
    const char *name, const TempoDuration &offset, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::sunSet(offset), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::atDays(
    uint8_t dowMask, int hour, int minute, const char *name, SchedulerFunctionNoData callback
) {
	SchedulerJobOptions options;
	options.name = name;
	return schedule(TempoSchedule::atDays(dowMask, hour, minute), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::atDay(
    TempoWeekDay day, int hour, int minute, const char *name, SchedulerFunctionNoData callback
) {
	return atDays(
	    static_cast<uint8_t>(1U << static_cast<uint8_t>(day)),
	    hour,
	    minute,
	    name,
	    std::move(callback)
	);
}

SchedulerResult<uint32_t> TempoScheduler::addJobOnceUtc(
    const DateTime &whenUtc, const SchedulerJobOptions &options, SchedulerCallbackFn callback, void *userData
) {
	return addJob(TempoSchedule::onceUtc(whenUtc), options, callback, userData);
}

SchedulerResult<uint32_t> TempoScheduler::addJobOnceUtc(
    const DateTime &whenUtc, const SchedulerJobOptions &options, SchedulerFunction callback, void *userData
) {
	return addJob(TempoSchedule::onceUtc(whenUtc), options, std::move(callback), userData);
}

SchedulerResult<uint32_t> TempoScheduler::addJobOnceUtc(
    const DateTime &whenUtc, const SchedulerJobOptions &options, SchedulerFunctionNoData callback
) {
	return addJob(TempoSchedule::onceUtc(whenUtc), options, std::move(callback));
}

SchedulerResult<uint32_t> TempoScheduler::addJobImpl(
    const TempoSchedule &schedule, const SchedulerJobOptions &options, const CallbackRef &callback
) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NotInitialized);
	}
	SchedulerJobOptions normalizedOptions = options;
	if (normalizedOptions.mode == SchedulerJobMode::WorkerPool) {
		normalizedOptions.executorId = 0;
	} else if (normalizedOptions.mode == SchedulerJobMode::DedicatedTask) {
		normalizedOptions.executorId = 1;
		if (!normalizedOptions.dedicatedTask) {
			normalizedOptions.dedicatedTask = &impl_->config.defaultDedicatedTask;
		}
		if (!validStackSize(normalizedOptions.dedicatedTask->stackSize) ||
		    !validOptionalPlacement(normalizedOptions.dedicatedTask->stackPlacement)) {
			return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidConfiguration);
		}
	}
	if (normalizedOptions.mode != SchedulerJobMode::Inline &&
	    impl_->executorFor(normalizedOptions.executorId) == nullptr) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::ExecutorUnavailable);
	}

	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<AddJobCommand, SchedulerResult<uint32_t>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [&](AddJobCommand &command) {
			    command.schedule = schedule;
			    command.options = normalizedOptions;
			    if (normalizedOptions.dedicatedTask) {
				    command.dedicatedTaskCopy = *normalizedOptions.dedicatedTask;
				    command.options.dedicatedTask = &command.dedicatedTaskCopy;
			    }
			    command.callback = callback;
		    }
		);
	}

	return impl_->manualCore.addJob(schedule, normalizedOptions, callback, impl_->date.now());
}

SchedulerResult<void> TempoScheduler::cancelJob(uint32_t jobId) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<CancelJobCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [&](CancelJobCommand &command) { command.jobId = jobId; }
		);
	}
	return impl_->manualCore.cancelJob(jobId);
}

SchedulerResult<void> TempoScheduler::pauseJob(uint32_t jobId) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<PauseJobCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [&](PauseJobCommand &command) { command.jobId = jobId; }
		);
	}
	return impl_->manualCore.pauseJob(jobId);
}

SchedulerResult<void> TempoScheduler::resumeJob(uint32_t jobId) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<ResumeJobCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [&](ResumeJobCommand &command) { command.jobId = jobId; }
		);
	}
	return impl_->manualCore.resumeJob(jobId, impl_->date.now());
}

SchedulerResult<void> TempoScheduler::cancelAll() {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<CancelAllCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [](CancelAllCommand &) {}
		);
	}
	return impl_->manualCore.cancelAll();
}

SchedulerResult<void> TempoScheduler::refreshAllSchedules() {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<RefreshAllSchedulesCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [](RefreshAllSchedulesCommand &) {}
		);
	}

	const DateTime nowUtc = impl_->date.now();
	impl_->drainManualEvents(nowUtc);
	impl_->reapCompletedExecutors();
	return impl_->manualCore.refreshAllSchedules(nowUtc);
}

void TempoScheduler::tick() {
	if (!impl_) {
		return;
	}
	tick(impl_->date.now());
}

void TempoScheduler::tick(const DateTime &nowUtc) {
	if (!impl_ || !impl_->started || impl_->config.mode == TempoSchedulerMode::Background) {
		return;
	}
	impl_->drainManualEvents(nowUtc);
	impl_->reapCompletedExecutors();
	if (impl_->refreshTimeContextIfNeeded(nowUtc)) {
		(void)impl_->manualCore.refreshAllSchedules(nowUtc);
	}
	impl_->manualCore.dispatchDue(nowUtc, *impl_);
	impl_->drainManualEvents(nowUtc);
	impl_->reapCompletedExecutors();
}

SchedulerResult<size_t> TempoScheduler::jobCount() const {
	if (!impl_ || !impl_->started) {
		return SchedulerResult<size_t>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		return executeBackgroundCommand<JobCountCommand, SchedulerResult<size_t>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [](JobCountCommand &) {}
		);
	}
	return impl_->manualCore.jobCount();
}

SchedulerResult<void> TempoScheduler::getJobInfo(uint32_t jobId, JobInfo &out) const {
	if (!impl_ || !impl_->started) {
		out = JobInfo{};
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		const SchedulerResult<JobInfo> result =
		    executeBackgroundCommand<GetJobInfoCommand, SchedulerResult<JobInfo>>(
		        *impl_->service,
		        impl_->config.memory.allocation,
		        impl_->config.service.controlTimeoutMs,
		        SchedulerError::QueueFull,
		        SchedulerError::Timeout,
		        [&](GetJobInfoCommand &command) { command.jobId = jobId; }
		    );
		if (!result) {
			out = JobInfo{};
			return SchedulerResult<void>::failure(result.error);
		}
		out = result.value;
		return SchedulerResult<void>::success();
	}
	return impl_->manualCore.getJobInfo(jobId, out);
}

void TempoScheduler::setMinValidUnixSeconds(int64_t minEpochSeconds) {
	if (!impl_) {
		return;
	}
	impl_->config.minValidEpochSeconds = minEpochSeconds;
	if (!impl_->started) {
		impl_->manualCore.setMinValidUnixSeconds(minEpochSeconds);
		return;
	}
	if (impl_->config.mode == TempoSchedulerMode::Background && impl_->service) {
		(void)executeBackgroundCommand<SetMinValidCommand, SchedulerResult<void>>(
		    *impl_->service,
		    impl_->config.memory.allocation,
		    impl_->config.service.controlTimeoutMs,
		    SchedulerError::QueueFull,
		    SchedulerError::Timeout,
		    [&](SetMinValidCommand &command) { command.minEpochSeconds = minEpochSeconds; }
		);
		return;
	}
	impl_->manualCore.setMinValidUnixSeconds(minEpochSeconds);
}

void TempoScheduler::setMinValidUtc(const DateTime &minUtc) {
	setMinValidUnixSeconds(minUtc.epochSeconds);
}

int64_t TempoScheduler::minValidUnixSeconds() const {
	if (!impl_) {
		return kDefaultMinValidEpochSeconds;
	}
	return impl_->config.minValidEpochSeconds;
}

uint8_t TempoScheduler::defaultWorkerExecutor() const {
	if (!impl_) {
		return kInvalidExecutorId;
	}
	return 0;
}

uint8_t TempoScheduler::defaultDedicatedExecutor() const {
	if (!impl_) {
		return kInvalidExecutorId;
	}
	return 1;
}

bool TempoScheduler::computeNextOccurrence(
    const TempoSchedule &schedule, const DateTime &fromUtc, DateTime &outNextUtc
) const {
	if (!impl_) {
		return false;
	}
	return ScheduleCalculator::computeNext(impl_->date, schedule, fromUtc, outNextUtc);
}
