# Configuration

`TempoConfig` controls timezone, location, NTP, valid-time threshold, sun-cycle cache behavior, and task defaults.

```cpp
TempoConfig config;
config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
config.latitude = 47.4979f;
config.longitude = 19.0402f;
config.ntpServers = {"pool.ntp.org", "time.google.com"};
config.ntpSyncIntervalMs = 3600000;
config.sunCycleCalculationHour = 4;
config.sunCycleCalculationMinute = 0;
config.sunCycleMatchWindowSeconds = 60;
config.minValidUnixSeconds = 1577836800;
```

## Scheduler

`TempoSchedulerConfig` is an alias of the scheduler configuration type.

```cpp
TempoSchedulerConfig config;
config.service.taskStackSize = 6848;
config.defaultWorkerPool.workerCount = 2;
```

Worker pool execution is the default job mode. Use `SchedulerJobMode::Inline` only for very small callbacks, and `SchedulerJobMode::DedicatedTask` for isolated long-running jobs.
