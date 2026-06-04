## Tempo

Tempo is a time, calendar, sun/moon, and scheduling toolkit for ESP32.

The library is UTC-first internally, but it provides explicit helpers for local time conversion, formatting, DST-aware calculations, sun cycle data, moon phase data, and scheduled job execution.

---

## Core rules

*   Tempo must be DST aware.
*   Tempo must work without network access.
*   Tempo must support NTP-synced systems, RTC-only systems, and manually synced systems.
*   Tempo must be UTC-based by default.
*   UTC and local time methods must be explicit in their names.
*   `DateTime` represents an absolute UTC-based date-time value.
*   `LocalDateTime` represents a local calendar date-time value in the configured timezone.
*   `DateTime` should not convert itself to local time unless it owns timezone context. Prefer `tempo.toLocal(...)` and `tempo.toUtc(...)`.
*   Tempo wraps `time_t` / `struct tm` and adds safe arithmetic, comparisons, parsing, and formatting in a class-based API.
*   Tempo must be aware of moon phases and sun cycle times such as sunrise, sunset, solar noon, and day/night state.
*   Sun cycle data must be calculated at a fixed configured local time and then cached for the day.
*   Normal sun cycle calls such as `isSunRise()`, `isSunSet()`, and `isDay()` must use cached sun cycle data. They must not recalculate sunrise/sunset on every call.
*   Sunrise, sunset, and moon phase scheduled callbacks mostly belong to `TempoScheduler`, not the core `Tempo` clock object.
*   Scheduler must use one task and queue-based control-plane serialization.
*   Scheduler must use a fixed worker pool by default instead of one FreeRTOS task per job.
*   Scheduler must support one clear execution model: `Inline`, `WorkerPool`, and `DedicatedTask`.
*   Advanced scheduler usage should prefer `TempoSchedule` + `SchedulerJobOptions` instead of many overloads.
*   Convenience methods like `everyMinutes(...)`, `dailyAt(...)`, and `everySunRise(...)` can still exist, but they should internally use the same `TempoSchedule` engine.

---

## Main concepts

```
Tempo owns time.
TempoScheduler owns execution.
TempoSchedule describes when a job should run.
SchedulerJobOptions describes how a job should run.
```

---

## Date-time model

```src
DateTime utcNow = tempo.nowUtc();
LocalDateTime localNow = tempo.nowLocal();

LocalDateTime local = tempo.toLocal(utcNow);
DateTime utc = tempo.toUtc(local);
```

### Rules

*   `DateTime` is UTC-based.
*   `LocalDateTime` is timezone-based.
*   Conversion between UTC and local time happens through `Tempo`, because `Tempo` owns the timezone and DST configuration.
*   Parsing should be explicit:

```src
DateTime parsedUtc = tempo.parseUtc("2026-02-15T23:12:55Z");
LocalDateTime parsedLocal = tempo.parseLocal("2026-02-15 23:12:55");
```

---

## Sun cycle calculation model

Sunrise, sunset, solar noon, and day/night values should not be recalculated on every call.

Tempo should calculate the daily sun cycle once per day at a configured local time. The default should be early morning, for example `04:00`, because sunrise/sunset values are needed before the normal day starts but after usual dst change times which is usually 02:00 or 03:00 in the morning.

```src
tempo.setSunCycleCalculationTime(4, 0); // recalculate every day at 04:00 local time
```

The same setting should also be available in `TempoConfig`:

```src
TempoConfig tempoConfig;
tempoConfig.sunCycleCalculationHour = 4;
tempoConfig.sunCycleCalculationMinute = 0;
```

### Sun cycle cache behavior

*   Tempo calculates sun cycle data after startup when the clock first becomes valid.
*   Tempo recalculates sun cycle data once per day at the configured local time.
*   Tempo recalculates sun cycle data immediately when latitude, longitude, or timezone changes.
*   Tempo recalculates sun cycle data after a large time correction, for example after the first successful NTP sync or RTC sync.
*   Sun cycle data is stored in a `TempoSunCycle` cache object.
*   Calls like `tempo.isSunRise()`, `tempo.isSunSet()`, `tempo.isDay()`, `tempo.sunRiseToday()`, and `tempo.sunSetToday()` read from the cached data.
*   These calls should be fast and deterministic.
*   If the time is not valid or location is not configured, sun cycle calls should return a clear error/result or a safe invalid state.

Example cache type:

```src
struct TempoSunCycle {
    DateTime calculatedForDateUtc;
    LocalDateTime calculatedForLocalDate;

    DateTime sunRiseUtc;
    DateTime sunSetUtc;
    DateTime solarNoonUtc;

    LocalDateTime sunRiseLocal;
    LocalDateTime sunSetLocal;
    LocalDateTime solarNoonLocal;

    bool valid;
};
```

Recommended API:

```src
TempoSunCycle sunCycle = tempo.sunCycleToday();
DateTime sunRiseUtc = tempo.sunRiseTodayUtc();
DateTime sunSetUtc = tempo.sunSetTodayUtc();
LocalDateTime sunRiseLocal = tempo.sunRiseTodayLocal();
LocalDateTime sunSetLocal = tempo.sunSetTodayLocal();

bool isDay = tempo.isDay();
bool isDayLater = tempo.isDay(tempo.addMinutesUtc(tempo.nowUtc(), 30));

bool isSunRise = tempo.isSunRise();
bool isSunSet = tempo.isSunSet();
```

### Matching sunrise/sunset moments

Because sunrise and sunset are exact moments, `isSunRise()` and `isSunSet()` should use a configurable match window.

```src
TempoConfig tempoConfig;
tempoConfig.sunCycleMatchWindowSeconds = 60; // default: 60 seconds
```

This means:

```src
bool isSunRise = tempo.isSunRise();
```

returns true when current time is inside the cached sunrise window, not only when the current second exactly equals the sunrise second.

### Sun cycle offsets

Offsets should be supported without recalculating sun cycle data.

```src
TempoDuration offset = TempoDuration::seconds(3600);

bool isSunRiseWithOffset = tempo.isSunRise(offset);
bool isSunSetWithOffset = tempo.isSunSet(offset);

DateTime twoMinutesFromNow = tempo.addMinutesUtc(tempo.nowUtc(), 2);

bool willBeSunRise = tempo.isSunRise(twoMinutesFromNow);
bool willBeSunRiseWithOffset = tempo.isSunRise(twoMinutesFromNow, offset);
```

Internally this should compare the given time with:

```
cached sunrise time + offset
cached sunset time + offset
```

---

## Tempo configuration

```src
TempoConfig tempoConfig;

tempoConfig.latitude = 47.4979f;
tempoConfig.longitude = 19.0402f;
tempoConfig.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";

tempoConfig.ntpServers = {"pool.ntp.org", "time.google.com"};
tempoConfig.ntpSyncIntervalMs = 3600000;

tempoConfig.sunCycleCalculationHour = 4;
tempoConfig.sunCycleCalculationMinute = 0;
tempoConfig.sunCycleMatchWindowSeconds = 60;

tempoConfig.taskStackSize = 6848;
tempoConfig.taskPriority = 1;
tempoConfig.taskCoreId = tskNO_AFFINITY;
tempoConfig.taskName = "tempo-task";
```

### Notes

*   `ntpSyncIntervalMs` default should be 1 hour.
*   Initial NTP sync should happen immediately when network is available.
*   NTP sync must run inside the internal Tempo task.
*   Manual sync calls such as `tempo.syncNtp()` should enqueue a sync request to the internal Tempo task.
*   Sun cycle calculation should also happen inside the internal Tempo task.

---

## Scheduler configuration

```src
TempoSchedulerConfig schedulerConfig;

schedulerConfig.taskStackSize = 6848;
schedulerConfig.taskPriority = 1;
schedulerConfig.taskCoreId = tskNO_AFFINITY;
schedulerConfig.taskName = "tempo-scheduler";

schedulerConfig.workerCount = 2;
schedulerConfig.workerStackSize = 4096;
schedulerConfig.workerPriority = 1;
schedulerConfig.workerCoreId = tskNO_AFFINITY;
```

---

## Scheduler execution model

```src
enum class SchedulerJobMode {
    Inline,
    WorkerPool,
    DedicatedTask
};
```

### `Inline`

Runs directly inside the scheduler task.

Use only for very small callbacks that never block.

```src
SchedulerJobOptions options;
options.name = "quick-flag-update";
options.mode = SchedulerJobMode::Inline;
```

### `WorkerPool`

Runs through the scheduler worker pool.

This is the default mode.

```src
SchedulerJobOptions options;
options.name = "sync-data";
options.mode = SchedulerJobMode::WorkerPool;
```

### `DedicatedTask`

Runs in its own FreeRTOS task.

Use only for long-running or isolated jobs.

```src
SchedulerJobOptions options;
options.name = "heavy-job";
options.mode = SchedulerJobMode::DedicatedTask;
options.task.stackSize = 4096;
options.task.priority = 1;
options.task.coreId = tskNO_AFFINITY;
options.task.taskName = "heavy-job-task";
```

---

## Basic usage

```src
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;

void setup() {
    Serial.begin(115200);

    TempoConfig tempoConfig;
    tempoConfig.latitude = 47.4979f;
    tempoConfig.longitude = 19.0402f;
    tempoConfig.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    tempoConfig.ntpServers = {"pool.ntp.org", "time.google.com"};
    tempoConfig.ntpSyncIntervalMs = 3600000;
    tempoConfig.sunCycleCalculationHour = 4;
    tempoConfig.sunCycleCalculationMinute = 0;

    tempoConfig.taskStackSize = 6848;
    tempoConfig.taskPriority = 1;
    tempoConfig.taskCoreId = tskNO_AFFINITY;
    tempoConfig.taskName = "tempo-task";

    TempoResult tempoResult = tempo.init(tempoConfig);
    if (!tempoResult) {
        Serial.println(tempoResult.message);
        return;
    }

    TempoSchedulerConfig schedulerConfig;
    schedulerConfig.taskStackSize = 6848;
    schedulerConfig.taskPriority = 1;
    schedulerConfig.taskCoreId = tskNO_AFFINITY;
    schedulerConfig.taskName = "tempo-scheduler";
    schedulerConfig.workerCount = 2;

    SchedulerResult<void> schedulerResult = scheduler.init(tempo, schedulerConfig);
    if (!schedulerResult) {
        Serial.println(static_cast<int>(schedulerResult.error));
        return;
    }

    tempo.setMinValidUnixSeconds(1780475336);
    tempo.setSunCycleCalculationTime(4, 0);

    bool hasValidTime = tempo.isValidTime();

    // Enqueues an NTP sync request into the internal Tempo task.
    tempo.syncNtp();
}
```

---

## Tempo callbacks

Tempo should keep callbacks for clock/time-state related events.

```src
void tempoCallbacks() {
    tempo.onNtpSync([](TempoSyncResult result) {
        Serial.println(result.message);
    });

    tempo.onDstChange([]() {
        Serial.println("Daylight saving changed just now!");
    });

    tempo.onTimeValid([]() {
        Serial.println("Tempo has valid time now!");
    });

    tempo.onSunCycleCalculated([](const TempoSunCycle& sunCycle) {
        Serial.println("Sun cycle data was recalculated.");
        Serial.println(sunCycle.sunRiseLocal.toString().c_str());
        Serial.println(sunCycle.sunSetLocal.toString().c_str());
    });
```

---

## Tempo calls

```src
void tempoCalls() {
    int64_t unixSeconds = 1780475336;

    DateTime fromUnix = tempo.fromUnixSeconds(unixSeconds);

    DateTime utcNow = tempo.nowUtc();
    LocalDateTime localNow = tempo.nowLocal();

    uint64_t unixNow = tempo.unixSeconds();
    uint64_t unixFromDate = tempo.unixSeconds(utcNow);

    DateTime lastYear = tempo.subYearsUtc(utcNow, 1);
    DateTime nextYear = tempo.addYearsUtc(utcNow, 1);
    DateTime twoHoursFromNow = tempo.addHoursUtc(utcNow, 2);
    DateTime twoMinutesFromNow = tempo.addMinutesUtc(utcNow, 2);

    int64_t diffSeconds = tempo.diff(utcNow, lastYear);
    int64_t diffHours = tempo.diff(utcNow, lastYear, TempoDiff::Hours);
    int64_t diffDays = tempo.diff(utcNow, lastYear, TempoDiff::Days);
    int64_t diffMonths = tempo.diff(utcNow, lastYear, TempoDiff::Months);
    int64_t diffYears = tempo.diff(utcNow, lastYear, TempoDiff::Years);

    bool isBefore = tempo.isBefore(lastYear, utcNow);
    bool isAfter = tempo.isAfter(lastYear, utcNow);
    bool isEqual = tempo.isEqual(lastYear, utcNow);
    bool isSameDay = tempo.isSameLocalDay(lastYear, utcNow);

    std::string utcString = tempo.formatUtc(utcNow);
    std::string localString = tempo.formatLocal(utcNow);

    Serial.printf("UTC: %s", utcString.c_str());
    Serial.printf("Local: %s", localString.c_str());

    LocalDateTime convertedLocal = tempo.toLocal(utcNow);
    DateTime convertedUtc = tempo.toUtc(convertedLocal);

    TempoSunCycle sunCycle = tempo.sunCycleToday();
    DateTime sunRiseUtc = tempo.sunRiseTodayUtc();
    DateTime sunSetUtc = tempo.sunSetTodayUtc();
    LocalDateTime sunRiseLocal = tempo.sunRiseTodayLocal();
    LocalDateTime sunSetLocal = tempo.sunSetTodayLocal();

    bool isSunRise = tempo.isSunRise();
    bool willBeSunRise = tempo.isSunRise(twoMinutesFromNow);

    TempoDuration offset = TempoDuration::seconds(3600);
    bool isSunRiseWithOffset = tempo.isSunRise(offset);
    bool isSunRiseWithOffsetAndDate = tempo.isSunRise(twoMinutesFromNow, offset);
    bool isSunSetWithOffset = tempo.isSunSet(offset);
    bool isSunSetWithOffsetAndDate = tempo.isSunSet(twoMinutesFromNow, offset);

    bool isLeapYear = tempo.isLeapYearUtc(utcNow);
    bool isNextYearLeapYear = tempo.isLeapYearUtc(nextYear);

    DateTime parsedUtc = tempo.parseUtc("2026-02-15T23:12:55Z");
    LocalDateTime parsedLocal = tempo.parseLocal("2026-02-15 23:12:55");

    bool isDay = tempo.isDay();
    bool willBeDay = tempo.isDay(twoMinutesFromNow);

    TempoDuration riseOffset = TempoDuration::seconds(3600);
    TempoDuration setOffset = TempoDuration::seconds(-3600);

    bool isDayWithOffsets = tempo.isDay(riseOffset, setOffset);
    bool isDayWithOffsetsAndDate = tempo.isDay(twoMinutesFromNow, riseOffset, setOffset);

    bool isDstActive = tempo.isDstActive();
    bool willDstBeActive = tempo.isDstActive(twoHoursFromNow);

    TempoMoonPhase moonPhase = tempo.moonPhase();
    TempoMoonPhase moonPhaseAt = tempo.moonPhase(twoHoursFromNow);
}
```

---

## Scheduler convenience methods

Convenience methods should remain available for simple jobs.

```src
void twoHourJob() {
    Serial.println("We are running every two hours!");
}

void schedulerCalls() {
    SchedulerResult minuteResult = scheduler.everyMinutes(10, "sync-data", []() {
        Serial.println("We are running every 10 minutes!");
    });

    if (!minuteResult) {
        Serial.println(minuteResult.message);
    }

    SchedulerResult dailyAtResult = scheduler.dailyAt(8, 30, "morning-job", []() {
        Serial.println("We are running every day at 08:30!");
    });

    SchedulerResult cronResult = scheduler.cron("0 */2 * * *", "two-hour-job", twoHourJob);

    scheduler.everySunSet("sun-set-job", []() {
        Serial.println("We are running every day at exactly sunset!");
    });

    scheduler.everySunRise("sun-rise-job", []() {
        Serial.println("We are running every day at exactly sunrise!");
    });

    TempoDuration offset = TempoDuration::seconds(3600);

    scheduler.everySunSet("sun-set-offset-job", offset, []() {
        Serial.println("We are running every day at sunset + offset.");
    });

    scheduler.atDays(0b0111110, 18, 30, "spec-day-job", someJob);
    scheduler.atDay(TempoWeekDay::Monday, 12, 20, "trash-day", trashDayJob);
}
```

---

## Advanced scheduler API

Advanced usage should prefer `TempoSchedule` and `SchedulerJobOptions`.

```src
void advancedSchedulerCalls() {
    SchedulerJobOptions options;
    options.name = "sunset-offset-job";
    options.mode = SchedulerJobMode::WorkerPool;

    TempoSchedule schedule = TempoSchedule::sunSet().offset(TempoDuration::minutes(30));

    scheduler.schedule(schedule, options, []() {
        Serial.println("Runs 30 minutes after cached sunset time.");
    });
}
```

Dedicated task example:

```src
void dedicatedTaskJob() {
    Serial.println("We are running in a dedicated FreeRTOS task!");
}

void dedicatedTaskSchedulerCall() {
    SchedulerJobOptions options;
    options.name = "hourly-dedicated-job";
    options.mode = SchedulerJobMode::DedicatedTask;
    options.task.stackSize = 4096;
    options.task.priority = 1;
    options.task.coreId = tskNO_AFFINITY;
    options.task.taskName = "hour-job-task";

    scheduler.schedule(
        TempoSchedule::everyHours(1),
        options,
        dedicatedTaskJob
    );
}
```

Moon phase scheduled callback:

```src
void moonPhaseSchedulerCalls() {
    SchedulerJobOptions options;
    options.name = "moon-phase-change";
    options.mode = SchedulerJobMode::WorkerPool;

    scheduler.onMoonPhaseChange(options, [](TempoMoonPhase before, TempoMoonPhase after) {
        Serial.printf(
            "Moon phase changed from %s to %s. Phase degree is %d deg, illumination is %.3f",
            before.label,
            after.label,
            after.angleDegrees,
            after.illumination
        );
    });
}
```

---

## Other scheduler methods

```src
TempoScheduler::everySeconds(sec, jobName, cb);
TempoScheduler::everyMinutes(min, jobName, cb);
TempoScheduler::everyHours(hour, jobName, cb);
TempoScheduler::dailyAt(hour, min, jobName, cb);
TempoScheduler::weeklyAt(TempoWeekDay::Monday, hour, min, jobName, cb);
TempoScheduler::monthlyAt(TempoMonth::July, TempoWeekDay::Tuesday, hour, min, jobName, cb);
```

Internally, these should map to:

```src
scheduler.schedule(
    TempoSchedule::...,
    SchedulerJobOptions::named(jobName),
    cb
);
```
