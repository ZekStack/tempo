# Troubleshooting

## Jobs do not run

Wall-clock jobs wait until `tempo.isValidTime()` is true. Lower `config.minValidUnixSeconds` for tests or make sure NTP/RTC/manual sync has set the system clock.

## Local time looks wrong

Check that `config.timezone` is a POSIX timezone string, not an IANA name. For Budapest, use:

```cpp
config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
```

## Sunrise or sunset is invalid

Set both `config.latitude` and `config.longitude`, and call `tempo.begin(config)` before sun-cycle calls.
