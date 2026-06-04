# API Reference

This page summarizes the public API declared by `src/Tempo.h`.

## Result model

Tempo does not throw exceptions. Core operations return `TempoResult`; scheduler operations return `SchedulerResult<T>`.

```cpp
TempoResult result = tempo.init(config);
if (!result) {
	Serial.println(result.message.c_str());
}
```

## Core time

| Method | Purpose |
| --- | --- |
| `init(config)` | Initialize timezone, location, and NTP settings. |
| `nowUtc()` | Return current system time as UTC `DateTime`. |
| `nowLocal()` | Return current local calendar time. |
| `toLocal(DateTime)` | Convert UTC to local using Tempo timezone. |
| `toUtc(LocalDateTime)` | Convert local calendar time to UTC. |
| `parseUtc(text)` | Parse `YYYY-MM-DDTHH:MM:SSZ`. |
| `parseLocal(text)` | Parse `YYYY-MM-DD HH:MM:SS`. |

## Sun and moon

| Method | Purpose |
| --- | --- |
| `sunCycleToday()` | Return cached daily sun cycle data. |
| `sunRiseTodayUtc()` / `sunSetTodayUtc()` | Return cached UTC event times. |
| `isSunRise()` / `isSunSet()` | Match the cached event inside the configured window. |
| `isDay()` | Check daylight using sunrise and sunset. |
| `moonPhase()` | Return lunar phase angle and illumination. |

## Scheduler

```cpp
SchedulerJobOptions options;
options.name = "morning";
options.mode = SchedulerJobMode::WorkerPool;

scheduler.schedule(TempoSchedule::dailyAt(8, 30), options, []() {});
```

Convenience methods include `everyMinutes`, `everyHours`, `dailyAt`, `cron`, `everySunRise`, `everySunSet`, `atDay`, and `atDays`.
