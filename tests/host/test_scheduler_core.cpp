#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "internal/tempo_date/date.h"
#include "internal/tempo_scheduler/core/scheduler_core.h"
#include "internal/tempo_scheduler/executors/scheduler_executor.h"
#include "internal/tempo_scheduler/schedule/schedule_spec.h"

namespace {
constexpr const char *kBudapestTz = "CET-1CEST,M3.5.0/2,M10.5.0/3";

int failures = 0;

void expect(bool condition, const char *message) {
	if (condition) {
		return;
	}
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

void initTempo(Tempo &tempo) {
	TempoConfig config;
	config.timeZone = kBudapestTz;
	expect(static_cast<bool>(tempo.init(config)), "Tempo initialization should succeed");
}

void increment(void *context) {
	if (context) {
		++(*static_cast<int *>(context));
	}
}

CallbackRef callbackFor(int &counter) {
	CallbackRef callback{};
	callback.kind = CallbackKind::RawFunction;
	callback.rawFn = &increment;
	callback.userData = &counter;
	return callback;
}

class RecordingExecutor final : public ISchedulerExecutor {
  public:
	explicit RecordingExecutor(bool invokeCallbacks = false)
	    : invokeCallbacks_(invokeCallbacks) {
	}

	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &) override {
		return true;
	}

	void end(bool) override {
	}

	bool submit(const JobInvocation &invocation) override {
		++submitAttempts_;
		if (acceptedThisRound_ >= maxAcceptedPerRound_) {
			return false;
		}
		++acceptedThisRound_;
		acceptedJobIds_.push_back(invocation.jobId);
		if (invokeCallbacks_) {
			invocation.callback.invoke();
		}
		return true;
	}

	const char *name() const override {
		return "recording";
	}

	void setMaxAcceptedPerRound(size_t maximum) {
		maxAcceptedPerRound_ = maximum;
	}

	void resetRound() {
		acceptedThisRound_ = 0;
	}

	size_t acceptedCount() const {
		return acceptedJobIds_.size();
	}

	size_t submitAttempts() const {
		return submitAttempts_;
	}

	bool acceptedIdsAreUnique() const {
		for (size_t left = 0; left < acceptedJobIds_.size(); ++left) {
			for (size_t right = left + 1; right < acceptedJobIds_.size(); ++right) {
				if (acceptedJobIds_[left] == acceptedJobIds_[right]) {
					return false;
				}
			}
		}
		return true;
	}

  private:
	bool invokeCallbacks_ = false;
	size_t maxAcceptedPerRound_ = std::numeric_limits<size_t>::max();
	size_t acceptedThisRound_ = 0;
	size_t submitAttempts_ = 0;
	std::vector<uint32_t> acceptedJobIds_{};
};

class TestExecutorResolver final : public IExecutorResolver {
  public:
	TestExecutorResolver(ISchedulerExecutor *inlineExecutor, ISchedulerExecutor *workerExecutor)
	    : inlineExecutor_(inlineExecutor), workerExecutor_(workerExecutor) {
	}

	ISchedulerExecutor *inlineExecutor() override {
		return inlineExecutor_;
	}

	ISchedulerExecutor *executorFor(uint8_t executorId) override {
		return executorId == 0 ? workerExecutor_ : nullptr;
	}

	void reapCompletedExecutors() override {
	}

  private:
	ISchedulerExecutor *inlineExecutor_ = nullptr;
	ISchedulerExecutor *workerExecutor_ = nullptr;
};

void testTwoSchedulesAtSameTimestamp() {
	Tempo tempo;
	initTempo(tempo);
	const DateTime nowUtc = tempo.fromUtc(2026, 9, 15, 18, 0, 0);
	const DateTime dueUtc = tempo.addMinutes(nowUtc, 1);

	SchedulerCore core(tempo, 0, Strata::Placement::Default);
	RecordingExecutor inlineExecutor(true);
	TestExecutorResolver executors(&inlineExecutor, nullptr);
	SchedulerJobOptions options;
	options.mode = SchedulerJobMode::Inline;

	int firstRuns = 0;
	int secondRuns = 0;
	const SchedulerResult<uint32_t> first =
	    core.addJob(TempoSchedule::onceUtc(dueUtc), options, callbackFor(firstRuns), nowUtc);
	const SchedulerResult<uint32_t> second =
	    core.addJob(TempoSchedule::onceUtc(dueUtc), options, callbackFor(secondRuns), nowUtc);
	expect(first.ok() && second.ok(), "same-time one-shot jobs should be accepted");

	core.dispatchDue(dueUtc, executors);

	expect(firstRuns == 1, "first same-time job should run exactly once");
	expect(secondRuns == 1, "second same-time job should run exactly once");
	expect(inlineExecutor.acceptedCount() == 2,
	       "both same-time jobs should be submitted during the same dispatch pass");
}

void testManySchedulesAtSameTimestamp() {
	Tempo tempo;
	initTempo(tempo);
	const DateTime nowUtc = tempo.fromUtc(2026, 9, 15, 18, 0, 0);
	const DateTime dueUtc = tempo.addMinutes(nowUtc, 1);

	SchedulerCore core(tempo, 0, Strata::Placement::Default);
	RecordingExecutor inlineExecutor(true);
	TestExecutorResolver executors(&inlineExecutor, nullptr);
	SchedulerJobOptions options;
	options.mode = SchedulerJobMode::Inline;

	constexpr int kJobCount = 32;
	int runs = 0;
	for (int index = 0; index < kJobCount; ++index) {
		const SchedulerResult<uint32_t> added =
		    core.addJob(TempoSchedule::onceUtc(dueUtc), options, callbackFor(runs), nowUtc);
		expect(added.ok(), "all same-time jobs should be accepted");
	}

	core.dispatchDue(dueUtc, executors);

	expect(runs == kJobCount, "all jobs sharing one due timestamp should run exactly once");
	expect(inlineExecutor.acceptedCount() == static_cast<size_t>(kJobCount),
	       "all jobs sharing one due timestamp should be submitted");
}

void testRejectedSubmissionsRetryWithoutLoss() {
	Tempo tempo;
	initTempo(tempo);
	const DateTime nowUtc = tempo.fromUtc(2026, 9, 15, 18, 0, 0);
	const DateTime dueUtc = tempo.addMinutes(nowUtc, 1);

	SchedulerCore core(tempo, 0, Strata::Placement::Default);
	RecordingExecutor workerExecutor(false);
	workerExecutor.setMaxAcceptedPerRound(2);
	TestExecutorResolver executors(nullptr, &workerExecutor);
	SchedulerJobOptions options;
	options.mode = SchedulerJobMode::WorkerPool;
	options.executorId = 0;

	int unusedRuns = 0;
	constexpr int kJobCount = 5;
	for (int index = 0; index < kJobCount; ++index) {
		const SchedulerResult<uint32_t> added =
		    core.addJob(TempoSchedule::onceUtc(dueUtc), options, callbackFor(unusedRuns), nowUtc);
		expect(added.ok(), "worker-pool retry test jobs should be accepted");
	}

	core.dispatchDue(dueUtc, executors);
	expect(workerExecutor.acceptedCount() == 2,
	       "executor pressure should leave rejected due jobs scheduled for retry");

	for (int retry = 1; retry <= 3; ++retry) {
		workerExecutor.resetRound();
		core.dispatchDue(tempo.addSeconds(dueUtc, retry), executors);
	}

	expect(workerExecutor.acceptedCount() == static_cast<size_t>(kJobCount),
	       "all rejected due jobs should eventually be resubmitted");
	expect(workerExecutor.acceptedIdsAreUnique(),
	       "retrying rejected submissions must not duplicate already accepted jobs");
	expect(workerExecutor.submitAttempts() > static_cast<size_t>(kJobCount),
	       "queue-pressure regression should exercise the retry path");
}

void testLunarDispatchAcrossRefreshAndRetry(SchedulerJobMode mode) {
	Tempo tempo;
	initTempo(tempo);
	expect(static_cast<bool>(tempo.updateLocation(69.6492f, 18.9553f, kBudapestTz)),
	       "Tromso location should configure");
	const DateTime fromUtc = tempo.fromLocal(2025, 6, 20);
	const auto cycle = tempo.moonCycle(fromUtc);
	expect(cycle.valid && cycle.eventCount == 3 &&
	           cycle.events[0].type == TempoMoonEventType::Rise &&
	           cycle.events[2].type == TempoMoonEventType::Rise,
	       "Tromso regression date should contain two moonrises");
	const DateTime first = cycle.events[0].utc;
	const DateTime second = cycle.events[2].utc;
	SchedulerCore core(tempo, fromUtc.epochSeconds);
	RecordingExecutor executor(true);
	TestExecutorResolver executors(&executor, &executor);
	SchedulerJobOptions options;
	options.mode = mode;
	options.overlap = OverlapPolicy::AllowParallel;
	int runs = 0;
	const auto job = core.addJob(TempoSchedule::moonrise(), options, callbackFor(runs), DateTime{});
	expect(job.ok(), "lunar schedules should wait for a valid wall clock");
	core.dispatchDue(DateTime{}, executors);
	expect(runs == 0, "lunar job should remain dormant while the clock is invalid");

	core.dispatchDue(fromUtc, executors);
	JobInfo info{};
	expect(core.getJobInfo(job.value, info).ok() && info.hasNext &&
	           tempo.isEqual(info.nextRunUtc, first),
	       "the first valid tick should prime the lunar job at the next real occurrence");

	executor.setMaxAcceptedPerRound(0);
	core.dispatchDue(first, executors);
	expect(runs == 0 && core.getJobInfo(job.value, info).ok() &&
	           tempo.isEqual(info.nextRunUtc, tempo.addSeconds(first, 1)),
	       "a rejected lunar invocation should be retried");
	executor.setMaxAcceptedPerRound(1);
	core.dispatchDue(tempo.addSeconds(first, 1), executors);
	expect(runs == 1 && core.getJobInfo(job.value, info).ok() &&
	           tempo.isEqual(info.nextRunUtc, second),
	       "the retry should preserve the occurrence and schedule the second moonrise");

	expect(core.refreshAllSchedules(fromUtc).ok(), "clock rollback should refresh schedules");
	executor.resetRound();
	core.dispatchDue(first, executors);
	expect(runs == 1, "clock rollback should not replay an accepted lunar event");
	core.dispatchDue(second, executors);
	expect(runs == 2, "both moonrises should dispatch on their actual UTC second");
	expect(core.cancelJob(job.value).ok() && !core.hasActiveMoonEventJobs(),
	       "cancelling a lunar job should release its scheduling state");
	core.dispatchDue(tempo.addDaysUtc(second, 2), executors);
	expect(runs == 2, "a cancelled lunar job should stay cancelled");
}

void testLunarScheduleLocationChanges() {
	Tempo tempo;
	initTempo(tempo);
	const DateTime nowUtc = tempo.fromUtc(2025, 3, 30, 1);
	SchedulerCore core(tempo, 0);
	RecordingExecutor executor(true);
	TestExecutorResolver executors(&executor, nullptr);
	SchedulerJobOptions options;
	options.mode = SchedulerJobMode::Inline;
	int runs = 0;
	const auto job = core.addJob(TempoSchedule::moonset(), options, callbackFor(runs), nowUtc);
	JobInfo info{};
	expect(job.ok() && core.getJobInfo(job.value, info).ok() && !info.hasNext,
	       "a lunar job without coordinates should have no due time");
	expect(static_cast<bool>(tempo.updateLocation(47.4979f, 19.0402f, kBudapestTz)),
	       "Budapest location should configure");
	core.dispatchDue(nowUtc, executors);
	const auto oldSet = tempo.moonCycle(nowUtc).firstEvent(TempoMoonEventType::Set);
	expect(oldSet.found && core.getJobInfo(job.value, info).ok() && info.hasNext &&
	           tempo.isEqual(info.nextRunUtc, oldSet.utc),
	       "the next tick should prime the job after coordinates become available");

	expect(static_cast<bool>(tempo.updateLocation(51.5074f, -0.1278f, kBudapestTz)),
	       "coordinate-only update should succeed");
	core.dispatchDue(nowUtc, executors);
	const auto newSet = tempo.moonCycle(nowUtc).firstEvent(TempoMoonEventType::Set);
	expect(newSet.found && core.getJobInfo(job.value, info).ok() && info.hasNext &&
	           tempo.isEqual(info.nextRunUtc, newSet.utc) && !tempo.isEqual(newSet.utc, oldSet.utc),
	       "coordinate-only updates should refresh the due time without a local date change");
	core.dispatchDue(oldSet.utc, executors);
	expect(runs == 0, "the superseded lunar due time should not dispatch");
	core.dispatchDue(newSet.utc, executors);
	expect(runs == 1, "the updated lunar due time should dispatch once");
}

struct RetryPublishContext {
	int attempts = 0;
	int failuresBeforeSuccess = 0;
	SchedulerEvent lastEvent{};
};

bool flakyPostEvent(void *context, const SchedulerEvent &event) {
	auto *state = static_cast<RetryPublishContext *>(context);
	if (!state) {
		return false;
	}
	++state->attempts;
	state->lastEvent = event;
	return state->attempts > state->failuresBeforeSuccess;
}

void testCompletionPublicationRetriesUntilAccepted() {
	SchedulerExecutorRuntime runtime;
	RetryPublishContext context;
	context.failuresBeforeSuccess = 3;
	runtime.postEventContext.store(&context, std::memory_order_release);
	runtime.postEvent.store(&flakyPostEvent, std::memory_order_release);
	runtime.accepting.store(true, std::memory_order_release);

	const SchedulerEvent completion{
	    .kind = SchedulerEventKind::JobFinished,
	    .jobId = 42,
	    .generation = 7,
	    .slotIndex = 3,
	};

	expect(runtime.publishReliable(completion),
	       "completion publication should retry transient queue-full failures");
	expect(context.attempts == 4,
	       "completion publication should stop immediately after the event is accepted");
	expect(context.lastEvent.jobId == completion.jobId &&
	           context.lastEvent.generation == completion.generation &&
	           context.lastEvent.slotIndex == completion.slotIndex,
	       "completion retry must preserve the original scheduler event");

	context.attempts = 0;
	runtime.accepting.store(false, std::memory_order_release);
	expect(!runtime.publishReliable(completion),
	       "completion publication should stop when the scheduler runtime is shutting down");
	expect(context.attempts == 0,
	       "shutdown should prevent any further completion publication attempts");
}
} // namespace

int main() {
	testTwoSchedulesAtSameTimestamp();
	testManySchedulesAtSameTimestamp();
	testRejectedSubmissionsRetryWithoutLoss();
	testCompletionPublicationRetriesUntilAccepted();
	testLunarDispatchAcrossRefreshAndRetry(SchedulerJobMode::Inline);
	testLunarDispatchAcrossRefreshAndRetry(SchedulerJobMode::WorkerPool);
	testLunarScheduleLocationChanges();

	if (failures != 0) {
		std::cerr << failures << " scheduler regression test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "All Tempo scheduler regression tests passed\n";
	return EXIT_SUCCESS;
}
