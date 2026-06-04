#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;

void setup() {
	Serial.begin(115200);

	TempoConfig tempoConfig;
	tempoConfig.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	tempoConfig.latitude = 47.4979f;
	tempoConfig.longitude = 19.0402f;
	tempoConfig.minValidUnixSeconds = 0;
	tempo.init(tempoConfig);
	scheduler.init(tempo);

	scheduler.everySunRise("sunrise", []() {
		Serial.println("sunrise job");
	});
	scheduler.everySunSet("sunset-offset", TempoDuration::minutes(30), []() {
		Serial.println("sunset + 30 minutes");
	});
}

void loop() {
	delay(1000);
}
