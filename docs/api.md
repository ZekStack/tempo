# API Reference

This page summarizes the public API declared by `src/Tempo.h`.

## Result model

Tempo uses result objects rather than exception-based public control flow. Core operations return `TempoResult`; scheduler operations return `SchedulerResult<T>`.

```cpp
TempoResult result = tempo.init(config);
if (!result) {
	Serial.println(result.message);
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
| `moonCycleToday()` | Calculate and cache the current local date's lunar horizon crossings. |
| `moonCycle(atUtc)` | Calculate crossings for the local date containing the supplied UTC instant. |
| `moonPosition()` / `moonPosition(atUtc)` | Calculate topocentric lunar position using the configured location. |
| `moonPosition(latitude, longitude, atUtc)` | Calculate position with explicit coordinates and UTC, including before initialization. |

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

Solar-position calls are stateless, synchronous, allocation-free, and do not schedule background work or cache results. The zero-argument overload checks the configured valid-clock threshold; explicit UTC overloads can calculate historical positions even if the system clock has not synchronized. Solar azimuth is zero by convention when the Sun is directly overhead or underfoot, where horizontal direction is undefined. The result is a geometric solar-center position and does not include atmospheric refraction, the Sun's apparent radius, terrain, or obstruction; therefore `isAboveHorizon` may differ from `isDay()` near sunrise and sunset. Use UTC instants across DST transitions.

UTC years 0001 through 9999 (proleptic Gregorian) are accepted as an **input range, not an accuracy guarantee**. The NOAA/Meeus-style approximate equations are most reliable around 1800–2100; potential errors grow for dates far outside that period. Tempo's NREL SPA regression reference validates a modern (2003) instant, not the full accepted range. For precision-sensitive calculations far outside the modern era, use a validated ephemeris. See [NOAA solar calculation details](https://gml.noaa.gov/grad/solcalc/calcdetails.html).

### Lunar events and position

```cpp
const TempoMoonCycle cycle = tempo.moonCycleToday();
if (cycle.valid) {
	for (uint8_t i = 0; i < cycle.eventCount; ++i) {
		const TempoMoonEvent &event = cycle.events[i];
		const LocalDateTime local = tempo.toLocal(event.utc);
		const char *label = event.type == TempoMoonEventType::Rise ? "Moonrise" : "Moonset";
		Serial.printf("%s %s (UTC offset %+d min)\n", label, local.localString().c_str(), local.offsetMinutes);
	}

	const auto firstSet = cycle.firstEvent(TempoMoonEventType::Set);
	if (firstSet.found) {
		Serial.println(firstSet.utc.utcString().c_str());
	}
}
```

The first `eventCount` entries in the eight-element array are ordered by UTC. A valid cycle can contain zero events, a single event, or multiple rises or sets. `alwaysAboveHorizon` and `alwaysBelowHorizon` describe a no-crossing date spent entirely on one side of the horizon. `calculatedForLocalDate` holds the first representable local instant of the date. A local date may span 23 or 25 UTC hours around DST or have a gap when a clock change crosses midnight. Converting each event with `toLocal(event.utc)` retains its offset and the exact UTC instant, including during a repeated hour.

Lunar horizon queries use the configured coordinates. The current-time overloads respect the valid-clock threshold. Queries with explicit UTC inputs are independent of the current clock. The daily cache refreshes on local date, location, or timezone changes and reinitialization. The astronomy runs synchronously and uses fixed-size storage. Local-date queries reuse Tempo's serialized timezone conversion.

`TempoMoonPosition` contains `ok`, `azimuthDegrees` clockwise from true north, geometric centre `elevationDegrees`, topocentric `distanceKm`, and `isAboveHorizon`. Azimuth is zero when the horizontal direction is undefined. The horizon flag and event calculations use the apparent upper limb at a level sea-level horizon, with distance-dependent parallax and lunar radius plus 34 arcminutes of standard refraction. The flag can be true when the geometric elevation is slightly negative.

Tempo uses the lunar periodic terms in Jean Meeus's *Astronomical Algorithms*, with UTC as an approximation for Earth rotation time and a modern TT offset for the lunar ephemeris. Explicit inputs accept proleptic Gregorian UTC years 0001 through 9999 where the platform can represent the local date. The five-minute accuracy target applies to modern dates at ordinary latitudes. Actual visibility depends on refraction, terrain, and observer height. Grazing crossings at high latitudes can be especially sensitive. See the [USNO rise and set definitions](https://aa.usno.navy.mil/faq/RST_defs).

## Scheduler

```cpp
SchedulerJobOptions options;
options.name = "morning";
options.mode = SchedulerJobMode::WorkerPool;

scheduler.schedule(TempoSchedule::dailyAt(8, 30), options, []() {});
```

Convenience methods include `everyMinutes`, `everyHours`, `dailyAt`, `cron`, `everySunRise`, `everySunSet`, `everyMoonRise`, `everyMoonSet`, `atDay`, and `atDays`.

```cpp
scheduler.everyMoonRise("before-moonrise", TempoDuration::minutes(-20), []() {});
scheduler.everyMoonSet("moonset", []() {});
```

Use `TempoSchedule::moonrise(offsetMinutes)` or `TempoSchedule::moonset(offsetMinutes)` with `schedule()` for custom job options. The corresponding `moonRise(offset)` and `moonSet(offset)` factories take `TempoDuration` and truncate to whole minutes, like the solar helpers. Offsets from -1440 through +1440 minutes are applied to each event's UTC time and can move a job across a local date boundary. The scheduler skips missing events and can dispatch multiple events of the same type on one date, subject to the job's overlap policy.

Lunar jobs use Tempo's existing clock synchronization and schedule refresh behavior. A location update also refreshes lunar jobs on the next manual tick or background poll. A successfully dispatched lunar occurrence is not replayed if the clock moves backward. The search looks ahead up to 32 days and leaves the job dormant when no event is found or a location is unavailable.

Schedule validation rejects invalid intervals, empty weekday masks, out-of-range custom field bits, and cron expressions that do not contain exactly five fields. `everyMinutes()` accepts `1..60`; `everyHours()` accepts `1..24`.

Background scheduler commands have explicit cancellation ownership. A control command that is still pending when its timeout expires is canceled and cannot execute later. If execution already started, the call waits for the result rather than reporting an ambiguous timeout.

`JobInfo::name` is an owned fixed-size snapshot. `nameTruncated` reports when a stored job name exceeded `kSchedulerJobNameCapacity - 1` characters.
