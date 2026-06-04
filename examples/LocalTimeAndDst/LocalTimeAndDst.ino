#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;

void setup() {
	Serial.begin(115200);

	TempoConfig config;
	config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	tempo.init(config);

	DateTime utc = tempo.parseUtc("2026-03-29T00:30:00Z");
	LocalDateTime local = tempo.toLocal(utc);
	DateTime roundTrip = tempo.toUtc(local);

	Serial.println(tempo.localDateTimeToString(local).c_str());
	Serial.println(tempo.isEqual(utc, roundTrip) ? "roundtrip ok" : "roundtrip changed");
	Serial.println(tempo.isDstActive(utc) ? "dst active" : "dst inactive");
}

void loop() {
	delay(1000);
}
