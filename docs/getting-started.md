# Getting Started

Include Tempo and create global instances.

```cpp
#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;
```

Configure timezone, location, and optional NTP servers in `setup()`.

```cpp
TempoConfig config;
config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
config.latitude = 47.4979f;
config.longitude = 19.0402f;
config.ntpServers = {"pool.ntp.org", "time.google.com"};

TempoResult result = tempo.init(config);
if (!result) {
	Serial.println(result.message.c_str());
	return;
}
```

Start the scheduler after Tempo is initialized.

```cpp
scheduler.init(tempo);
scheduler.everyMinutes(5, "heartbeat", []() {
	Serial.println("tick");
});
```

Tempo is UTC-first. Use `DateTime` for stored absolute values and `LocalDateTime` for local UI values.

```cpp
DateTime utcNow = tempo.nowUtc();
LocalDateTime localNow = tempo.toLocal(utcNow);
DateTime roundTrip = tempo.toUtc(localNow);
```
