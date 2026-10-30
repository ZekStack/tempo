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
| `sunPosition()` | Compute current geometric azimuth/elevation for configured location. |
| `sunPosition(atUtc)` | Compute geometric solar position at an explicit UTC instant using configured location. |
| `sunPosition(latitude, longitude, atUtc)` | Compute solar position using explicit coordinates and UTC instant. |
| `moonPhase()` | Return lunar phase angle and illumination. |

Stored-location sun APIs return an invalid result until both latitude and longitude have been explicitly configured.

```cpp
const TempoSunPosition position = tempo.sunPosition(tempo.nowUtc());
if (position.ok) {
	// Azimuth clockwise from true north (0=N, 90=E, 180=S, 270=W).
	// Geometric elevation in degrees above the astronomical horizon.
	const double azimuth = position.azimuthDegrees;
	const double elevation = position.elevationDegrees;
	const bool isAboveHorizon = position.isAboveHorizon;
}
```

Solar-position calls are stateless, synchronous, allocation-free, and do not schedule background work or cache results. The zero-argument overload checks the configured valid-clock threshold; explicit UTC overloads can calculate historical positions even if the system clock has not synchronized. Supported UTC years are 0001 through 9999. Solar azimuth is zero by convention when the Sun is directly overhead or underfoot, where horizontal direction is undefined. The result is a geometric solar-center position and does not include atmospheric refraction, the Sun's apparent radius, terrain, or obstruction; therefore `isAboveHorizon` may differ from `isDay()` near sunrise and sunset. Use UTC instants across DST transitions.

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
