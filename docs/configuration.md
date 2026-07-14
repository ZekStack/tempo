# Configuration

`TempoConfig` controls timezone, optional location, NTP, the valid-time threshold, sun-event matching, and Tempo-owned buffer placement.

```cpp
TempoConfig config;
config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
config.latitude = 47.4979f;
config.longitude = 19.0402f;
config.ntpServers = {"pool.ntp.org", "time.google.com"};
config.ntpSyncIntervalMs = 3600000;
config.sunCycleMatchWindowSeconds = 60;
config.minValidUnixSeconds = 1577836800;
config.usePSRAMBuffers = false;
```

## Location

Latitude and longitude are optional. Leave both unset when sun calculations are not needed. When a stored location is configured, both values must be finite and within these ranges:

- latitude: `-90` through `90`;
- longitude: `-180` through `180`.

Supplying only one coordinate, or an out-of-range coordinate, causes `Tempo::init()` to return `TempoStatus::InvalidArgument`. The coordinate `(0, 0)` remains valid when explicitly configured.

## Timezone

Use a POSIX timezone string such as:

```cpp
config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
```

The underlying C runtime timezone is process-global. Tempo serializes local-time conversion and scoped timezone switching so conversions from different tasks cannot observe a temporary timezone used by another Tempo operation.

## Sun cycle

Sun cycles are calculated lazily and cached by local calendar date. `sunCycleToday()` refreshes the cache when the local date changes. APIs that receive a `DateTime`, such as `isDay(date)` and `isSunRise(date)`, calculate against the supplied date rather than reusing an unrelated daily cache entry.

`sunCycleMatchWindowSeconds` controls the tolerance used by `isSunRise()` and `isSunSet()`.

## Scheduler

`TempoSchedulerConfig` is an alias of the scheduler configuration type.

```cpp
TempoSchedulerConfig config;
config.service.commandQueueDepth = 16;
config.service.eventQueueDepth = 16;
config.service.taskStackSize = 4096;
config.service.controlTimeoutMs = 2000;
config.defaultWorkerPool.workerCount = 2;
config.defaultWorkerPool.queueDepth = 8;
config.defaultWorkerPool.stackSize = 6144;
```

Queue depths, stack sizes, worker count, and the control timeout must be nonzero. Invalid scheduler configuration returns `SchedulerError::InvalidConfiguration` before tasks or queues are created.

Worker-pool execution is the default job mode. Use `SchedulerJobMode::Inline` only for very small callbacks, and `SchedulerJobMode::DedicatedTask` for isolated long-running jobs. Scheduler control operations invoked from an inline callback return `SchedulerError::Busy` instead of waiting on the scheduler service task itself.
