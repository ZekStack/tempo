#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;

void setup() {
	Serial.begin(115200);

	TempoConfig config;
	config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	config.ntpServers = {"pool.ntp.org", "time.google.com"};

	tempo.begin(config);
	tempo.setNtpSyncCallback([](const DateTime &syncedAtUtc) {
		Serial.printf("NTP sync: %lld\n", static_cast<long long>(syncedAtUtc.epochSeconds));
	});
	tempo.syncNtp();
}

void loop() {
	delay(1000);
}
