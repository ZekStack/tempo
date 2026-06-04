#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;

void setup() {
	Serial.begin(115200);

	TempoConfig tempoConfig;
	tempoConfig.timezone = "UTC0";
	tempoConfig.minValidUnixSeconds = 0;
	tempo.begin(tempoConfig);

	scheduler.begin(tempo);
	scheduler.everyMinutes(10, "ten-minute", []() {
		Serial.println("every ten minutes");
	});
	scheduler.dailyAt(8, 30, "morning", []() {
		Serial.println("daily at 08:30");
	});
}

void loop() {
	delay(1000);
}
