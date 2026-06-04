# Tempo

Tempo is a time, calendar, sun, moon, and scheduling toolkit for ESP32.

Tempo helps you keep UTC-first time logic explicit in Arduino ESP32 projects while still providing timezone-aware local conversion, DST-aware calendar helpers, cached sun cycle data, moon phase data, and scheduled job execution.

[![CI](https://github.com/ZekStack/tempo/actions/workflows/ci.yml/badge.svg)](https://github.com/ZekStack/tempo/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ZekStack/tempo?sort=semver)](https://github.com/ZekStack/tempo/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Why use Tempo?

* **UTC-first** - `DateTime` stores absolute UTC time, while local conversion is explicit.
* **DST-aware** - POSIX timezone strings are used for local time and recurring schedules.
* **ESP32-friendly** - FreeRTOS service tasks, queue-based scheduling, and result-based errors.
* **Sun and moon data** - sunrise, sunset, solar noon, daylight checks, moon angle, and illumination.
* **Production-minded** - no exceptions, bindable callbacks, and C++20 with embedded constraints.

## Install

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps =
  https://github.com/ZekStack/tempo.git

build_flags =
  -std=gnu++20
build_unflags =
  -std=gnu++11
```

### Arduino IDE

Tempo is not published to Arduino Library Manager yet.

Install it by downloading the repository ZIP or cloning it into your Arduino libraries folder.

```txt
Arduino/libraries/Tempo
```

## Quick start

```cpp
#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;

void setup() {
	Serial.begin(115200);

	TempoConfig config;
	config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	config.latitude = 47.4979f;
	config.longitude = 19.0402f;

	TempoResult result = tempo.init(config);
	if (!result) {
		Serial.println(result.message.c_str());
		return;
	}

	scheduler.init(tempo);
	scheduler.everyMinutes(10, "sync", []() {
		Serial.println("scheduled job");
	});
}

void loop() {
	delay(1000);
}
```

## Important notes

> [!IMPORTANT]
> Recurring scheduler jobs wait for valid wall-clock time. Set `minValidUnixSeconds` for your product so jobs do not run against an unset clock.

* Tempo uses POSIX timezone strings for DST-aware local conversion.
* Sunrise and sunset calls use cached daily data through `sunCycleToday()`.
* One-shot UTC schedules remain exact; recurring schedules evaluate in local time.

## Examples

| Example | Description |
| --- | --- |
| `BasicClock` | Initialize Tempo and print UTC/local time. |
| `NtpAndCallbacks` | Configure NTP and sync callbacks. |
| `LocalTimeAndDst` | UTC/local conversion and DST checks. |
| `SunMoon` | Cached sun cycle and moon phase calls. |
| `SchedulerBasic` | Simple interval and daily jobs. |
| `SchedulerCronAndDays` | Cron expressions and weekday masks. |
| `SchedulerSunCycle` | Sunrise/sunset scheduled jobs. |
| `SchedulerExecutionModes` | Inline, worker pool, and dedicated-task modes. |

Start with:

```txt
examples/BasicClock
```

## Documentation

Detailed documentation is available in the `docs/` folder.

| Document | Description |
| --- | --- |
| [`docs/getting-started.md`](docs/getting-started.md) | Setup and first time flow. |
| [`docs/configuration.md`](docs/configuration.md) | Config options and defaults. |
| [`docs/api.md`](docs/api.md) | Public types and methods. |
| [`docs/examples.md`](docs/examples.md) | Example guide. |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | Common issues. |

## API overview

```cpp
DateTime utcNow = tempo.nowUtc();
LocalDateTime localNow = tempo.nowLocal();
DateTime utc = tempo.toUtc(localNow);

TempoSunCycle sun = tempo.sunCycleToday();
TempoMoonPhase moon = tempo.moonPhase();

SchedulerJobOptions options;
options.name = "job";
options.mode = SchedulerJobMode::WorkerPool;
scheduler.schedule(TempoSchedule::dailyAt(8, 30), options, []() {});
```

## Compatibility

| Item | Support |
| --- | --- |
| Framework | Arduino ESP32 |
| Platform | `espressif32` |
| Language | C++20 |
| Filesystem | none |
| PSRAM | Used for selected internal buffers when available |
| Dependencies | none |
| Exceptions | Not used |
| Status | Early-stage `0.0.1` |

## License

MIT - see [`LICENSE.md`](LICENSE.md).

## ZekStack

Part of the ZekStack ESP32 library stack.
