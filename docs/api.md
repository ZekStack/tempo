# API Reference

This page summarizes the public API declared by `src/Tempo.h`.

## Result model

Tempo uses result objects rather than exception-based public control flow. Core operations return `TempoResult`; scheduler operations return `SchedulerResult<T>`.

```cpp
TempoResult result = tempo.init(config);
if (!result) {
	Serial.println(result.message.c_str());
}
```

## Core time

| Method | Purpose |
| --- | --- |
| `init(config)` | Validate and initialize timezone, optional location, and NTP settings. |
| `nowUtc()` | Return current system time as UTC `DateTime`. |
| `nowLocal()` | Return current local calendar time. |
| `toLocal(DateTime)` | Convert UTC to local and retain the exact source UTC instant and offset. |
| `toUtc(LocalDateTime)` | Return the retained UTC instant for resolved values; otherwise resolve local calendar fields. |
| `parseUtc(text)` | Parse `YYYY-MM-DDTHH:MM:SSZ`; invalid input returns `DateTime{}`. |
| `parseLocal(text)` | Parse `YYYY-MM-DD HH:MM:SS`; invalid or nonexistent DST-local input returns `LocalDateTime{}` with `ok == false`. |

Local calendar helpers such as `startOfDayLocal()`, `endOfDayLocal()`, `nextDailyAtLocal()`, and `nextWeekdayAtLocal()` use calendar arithmetic. A local day may therefore contain 23, 24, or 25 hours across DST transitions.

## Sun and moon

| Method | Purpose |
| --- | --- |
| `sunCycleToday()` | Lazily calculate and cache sun-cycle data for the current local date. |
| `sunRiseTodayUtc()` / `sunSetTodayUtc()` | Return cached UTC event times for the current local date. |
| `isSunRise(date)` / `isSunSet(date)` | Match the event calculated for the supplied date inside the configured window. |
| `isDay(date)` | Check daylight using sunrise and sunset for the supplied date. |
| `moonPhase()` | Return lunar phase angle and illumination. |

Stored-location sun APIs return an invalid result until both latitude and longitude have been explicitly configured.

## Scheduler

```cpp
SchedulerJobOptions options;
options.name = "morning";
options.mode = SchedulerJobMode::WorkerPool;

scheduler.schedule(TempoSchedule::dailyAt(8, 30), options, []() {});
```

Convenience methods include `everyMinutes`, `everyHours`, `dailyAt`, `cron`, `everySunRise`, `everySunSet`, `atDay`, and `atDays`.

Schedule validation rejects invalid intervals, empty weekday masks, out-of-range custom field bits, and cron expressions that do not contain exactly five fields. `everyMinutes()` accepts `1..60`; `everyHours()` accepts `1..24`.

Background scheduler commands have explicit cancellation ownership. A control command that is still pending when its timeout expires is canceled and cannot execute later. If execution already started, the call waits for the result rather than reporting an ambiguous timeout.

`JobInfo::name` is an owned fixed-size snapshot. `nameTruncated` reports when a stored job name exceeded `kSchedulerJobNameCapacity - 1` characters.
