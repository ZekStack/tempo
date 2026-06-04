#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;

void setup() {
	Serial.begin(115200);

	TempoConfig tempoConfig;
	tempoConfig.timezone = "UTC0";
	tempoConfig.minValidUnixSeconds = 0;
	tempo.init(tempoConfig);
	scheduler.init(tempo);

	scheduler.cron("0 */2 * * *", "two-hour", []() {
		Serial.println("cron job");
	});
	scheduler.atDays(0b0111110, 18, 30, "weekdays", []() {
		Serial.println("weekday job");
	});
	scheduler.atDay(TempoWeekDay::Monday, 12, 20, "monday", []() {
		Serial.println("monday job");
	});
}

void loop() {
	delay(1000);
}
