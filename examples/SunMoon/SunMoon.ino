#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;

void setup() {
	Serial.begin(115200);

	TempoConfig config;
	config.timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	config.latitude = 47.4979f;
	config.longitude = 19.0402f;
	tempo.init(config);

	TempoSunCycle cycle = tempo.sunCycleToday();
	if (cycle.valid) {
		Serial.println(cycle.sunRiseLocal.localString().c_str());
		Serial.println(cycle.sunSetLocal.localString().c_str());
	}

	// Use a fixed date on a board whose clock has not synchronized.
	const bool validClock = tempo.isValidTime();
	const DateTime atUtc = validClock ? tempo.nowUtc() : tempo.fromUtc(2025, 3, 30, 12);
	const TempoMoonCycle lunar = validClock ? tempo.moonCycleToday() : tempo.moonCycle(atUtc);
	if (lunar.valid) {
		for (uint8_t i = 0; i < lunar.eventCount; ++i) {
			const auto &event = lunar.events[i];
			const auto local = tempo.toLocal(event.utc);
			Serial.printf(
			    "%s %s (UTC offset %+d min)\n",
			    event.type == TempoMoonEventType::Rise ? "Moonrise" : "Moonset",
			    local.localString().c_str(),
			    local.offsetMinutes
			);
		}
	}
	const TempoMoonPosition position = tempo.moonPosition(atUtc);
	if (position.ok) {
		Serial.printf(
		    "Moon azimuth: %.2f elevation: %.2f distance: %.0f km\n",
		    position.azimuthDegrees, position.elevationDegrees, position.distanceKm
		);
	}

	TempoMoonPhase moon = tempo.moonPhase(atUtc);
	Serial.printf("Moon angle: %d illumination: %.2f\n", moon.angleDegrees, moon.illumination);
}

void loop() {
	delay(1000);
}
