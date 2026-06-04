#pragma once

#include <Arduino.h>
#include "../tempo_date/date.h"

#include <functional>
#include <memory>
#include <string>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "schedule/schedule_calculator.h"
#include "schedule/schedule_spec.h"
#include "scheduler_config.h"
#include "scheduler_result.h"

class ISchedulerExecutor;
struct CallbackRef;

using SchedulerCallbackFn = void (*)(void *userData);
using SchedulerFunction = std::function<void(void *userData)>;
using SchedulerFunctionNoData = std::function<void()>;
using TempoSchedulerConfig = SchedulerConfig;

enum class TempoWeekDay : uint8_t {
	Sunday = 0,
	Monday = 1,
	Tuesday = 2,
	Wednesday = 3,
	Thursday = 4,
	Friday = 5,
	Saturday = 6,
};

struct SchedulerJobOptions {
	SchedulerJobMode mode = SchedulerJobMode::WorkerPool;
	OverlapPolicy overlap = OverlapPolicy::SkipIfRunning;
	uint8_t executorId = 0;
	bool startPaused = false;
	const char *name = nullptr;
	const DedicatedTaskOptions *dedicatedTask = nullptr;
};

struct JobInfo {
	uint32_t id = 0;
	const char *name = nullptr;
	bool paused = false;
	bool running = false;
	bool queuedWhileRunning = false;
	SchedulerJobMode mode = SchedulerJobMode::Inline;
	OverlapPolicy overlap = OverlapPolicy::SkipIfRunning;
	uint8_t executorId = 0;
	bool hasNext = false;
	DateTime nextRunUtc{};
	TempoSchedule schedule{};
};

class TempoScheduler {
  public:
	static constexpr int64_t kDefaultMinValidEpochSeconds = 1577836800;
	static constexpr uint8_t kInvalidExecutorId = 0xFF;

	TempoScheduler();
	explicit TempoScheduler(Tempo &date, const SchedulerConfig &config = SchedulerConfig{});
	~TempoScheduler();

	SchedulerResult<void> begin(Tempo &date, const SchedulerConfig &config = SchedulerConfig{});
	bool begin();
	void end(bool waitForRunningJobs = true, uint32_t timeoutMs = 5000);
	bool running() const;
	bool draining() const;

	SchedulerResult<uint8_t> registerExecutor(ISchedulerExecutor *executor);

	SchedulerResult<uint32_t> addJob(
	    const TempoSchedule &schedule,
	    const SchedulerJobOptions &options,
	    SchedulerCallbackFn callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJob(
	    const TempoSchedule &schedule,
	    const SchedulerJobOptions &options,
	    SchedulerFunction callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJob(
	    const TempoSchedule &schedule, const SchedulerJobOptions &options, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> schedule(
	    const TempoSchedule &schedule, const SchedulerJobOptions &options, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> everyMinutes(
	    int minutes, const char *name, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> everyHours(
	    int hours, const char *name, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> dailyAt(
	    int hour, int minute, const char *name, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> cron(
	    const char *expression, const char *name, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> everySunRise(const char *name, SchedulerFunctionNoData callback);
	SchedulerResult<uint32_t> everySunRise(
	    const char *name, const TempoDuration &offset, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> everySunSet(const char *name, SchedulerFunctionNoData callback);
	SchedulerResult<uint32_t> everySunSet(
	    const char *name, const TempoDuration &offset, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> atDays(
	    uint8_t dowMask, int hour, int minute, const char *name, SchedulerFunctionNoData callback
	);
	SchedulerResult<uint32_t> atDay(
	    TempoWeekDay day, int hour, int minute, const char *name, SchedulerFunctionNoData callback
	);

	SchedulerResult<uint32_t> addJobOnceUtc(
	    const DateTime &whenUtc,
	    const SchedulerJobOptions &options,
	    SchedulerCallbackFn callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJobOnceUtc(
	    const DateTime &whenUtc,
	    const SchedulerJobOptions &options,
	    SchedulerFunction callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJobOnceUtc(
	    const DateTime &whenUtc, const SchedulerJobOptions &options, SchedulerFunctionNoData callback
	);

	SchedulerResult<void> cancelJob(uint32_t jobId);
	SchedulerResult<void> pauseJob(uint32_t jobId);
	SchedulerResult<void> resumeJob(uint32_t jobId);
	SchedulerResult<void> cancelAll();
	SchedulerResult<void> refreshAllSchedules();

	void tick();
	void tick(const DateTime &nowUtc);

	SchedulerResult<size_t> jobCount() const;
	SchedulerResult<void> getJobInfo(uint32_t jobId, JobInfo &out) const;

	void setMinValidUnixSeconds(int64_t minEpochSeconds);
	void setMinValidUtc(const DateTime &minUtc);
	int64_t minValidUnixSeconds() const;

	bool computeNextOccurrence(
	    const TempoSchedule &schedule, const DateTime &fromUtc, DateTime &outNextUtc
	) const;

	uint8_t defaultWorkerExecutor() const;
	uint8_t defaultDedicatedExecutor() const;

  private:
	struct Impl;

	SchedulerResult<uint32_t> addJobImpl(
	    const TempoSchedule &schedule, const SchedulerJobOptions &options, const CallbackRef &callback
	);

	std::unique_ptr<Impl> impl_;
};
