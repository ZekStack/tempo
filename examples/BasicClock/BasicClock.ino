#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;

void setup() {
	Serial.begin(115200);

	TempoConfig config;
	config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	config.latitude = 47.4979f;
	config.longitude = 19.0402f;
	config.minValidUnixSeconds = 0;

	TempoResult result = tempo.init(config);
	if (!result) {
		Serial.println(result.message.c_str());
		return;
	}

	DateTime utcNow = tempo.nowUtc();
	LocalDateTime localNow = tempo.nowLocal();
	Serial.println(tempo.dateTimeToStringUtc(utcNow).c_str());
	Serial.println(tempo.localDateTimeToString(localNow).c_str());
}

void loop() {
	delay(1000);
}
