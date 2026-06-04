# Examples

Tempo examples are topic-focused Arduino sketches.

| Example | Description |
| --- | --- |
| `BasicClock` | Minimal initialization and UTC/local formatting. |
| `NtpAndCallbacks` | NTP configuration and bindable callbacks. |
| `LocalTimeAndDst` | DST-aware conversion and local-day checks. |
| `SunMoon` | Cached sunrise/sunset and moon phase. |
| `SchedulerBasic` | Interval and daily scheduler calls. |
| `SchedulerCronAndDays` | Cron parser and weekday masks. |
| `SchedulerSunCycle` | Sunrise/sunset jobs with offsets. |
| `SchedulerExecutionModes` | Inline, worker pool, and dedicated-task execution. |

Compile an example with PlatformIO CI:

```sh
pio ci examples/BasicClock --board esp32dev --lib . --project-option build_unflags=-std=gnu++11 --project-option build_flags=-std=gnu++20
```
