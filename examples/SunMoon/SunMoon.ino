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
	tempo.init(config);

	TempoSunCycle cycle = tempo.sunCycleToday();
	if (cycle.valid) {
		Serial.println(cycle.sunRiseLocal.localString().c_str());
		Serial.println(cycle.sunSetLocal.localString().c_str());
	}

	TempoMoonPhase moon = tempo.moonPhase();
	Serial.printf("Moon angle: %d illumination: %.2f\n", moon.angleDegrees, moon.illumination);
}

void loop() {
	delay(1000);
}
