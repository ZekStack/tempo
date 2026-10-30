#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

#include "internal/tempo_date/date.h"
#include "internal/tempo_scheduler/schedule/schedule_calculator.h"
#include "internal/tempo_scheduler/schedule/schedule_spec.h"

namespace {
constexpr const char *kBudapestTz = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr const char *kTokyoTz = "JST-9";

int failures = 0;

void expect(bool condition, const char *message) {
	if (condition) {
		return;
	}
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

void expectNear(double actual, double expected, double tolerance, const char *message) {
	expect(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance, message);
}

void initBudapestTempo(Tempo &tempo, bool withLocation = false) {
	TempoConfig config;
	config.timeZone = kBudapestTz;
	if (withLocation) {
		config.latitude = 47.4979f;
		config.longitude = 19.0402f;
	}
	expect(static_cast<bool>(tempo.init(config)), "Tempo initialization should succeed");
}

void testInvalidParsing() {
	Tempo tempo;
	initBudapestTempo(tempo);
	expect(!tempo.parseLocal("not-a-date").ok, "invalid local input must remain invalid");
	expect(!tempo.parseDateTimeLocal("2026-03-29 02:30:00").ok,
	       "nonexistent DST spring-forward local time must be rejected");
	expect(!tempo.parseIso8601Utc("2026-01-01T00:00:60Z").ok,
	       "leap-second input must be rejected when leap seconds are unsupported");
}

void testDstDayBoundaries() {
	Tempo tempo;
	initBudapestTempo(tempo);

	const DateTime springNoon = tempo.fromUtc(2026, 3, 29, 12, 0, 0);
	const DateTime springStart = tempo.startOfDayLocal(springNoon);
	const DateTime springEnd = tempo.endOfDayLocal(springNoon);
	expect(springEnd.epochSeconds - springStart.epochSeconds + 1 == 23 * 3600,
	       "spring-forward local day must contain 23 hours");

	const DateTime fallNoon = tempo.fromUtc(2026, 10, 25, 12, 0, 0);
	const DateTime fallStart = tempo.startOfDayLocal(fallNoon);
	const DateTime fallEnd = tempo.endOfDayLocal(fallNoon);
	expect(fallEnd.epochSeconds - fallStart.epochSeconds + 1 == 25 * 3600,
	       "fall-back local day must contain 25 hours");
}

void testAmbiguousRoundTrip() {
	Tempo tempo;
	initBudapestTempo(tempo);
	const DateTime firstOccurrence = tempo.fromUtc(2026, 10, 25, 0, 30, 0);
	const DateTime secondOccurrence = tempo.fromUtc(2026, 10, 25, 1, 30, 0);

	const LocalDateTime firstLocal = tempo.toLocal(firstOccurrence);
	const LocalDateTime secondLocal = tempo.toLocal(secondOccurrence);
	expect(firstLocal.ok && secondLocal.ok, "both repeated local-hour values must resolve");
	expect(firstLocal.hour == 2 && secondLocal.hour == 2,
	       "both UTC instants should map to the repeated 02:30 local time");
	expect(firstLocal.offsetMinutes != secondLocal.offsetMinutes,
	       "repeated local times must retain distinct UTC offsets");
	expect(tempo.toUtc(firstLocal).epochSeconds == firstOccurrence.epochSeconds,
	       "first repeated local time must round-trip exactly");
	expect(tempo.toUtc(secondLocal).epochSeconds == secondOccurrence.epochSeconds,
	       "second repeated local time must round-trip exactly");
}

void testCalendarSchedulingAcrossDst() {
	Tempo tempo;
	initBudapestTempo(tempo);
	const DateTime beforeSpring = tempo.fromLocal(2026, 3, 28, 8, 30, 0);
	const DateTime next = tempo.nextDailyAtLocal(8, 0, 0, beforeSpring);
	const LocalDateTime local = tempo.toLocal(next);
	expect(local.ok && local.year == 2026 && local.month == 3 && local.day == 29 &&
	           local.hour == 8 && local.minute == 0,
	       "next daily local occurrence must preserve wall-clock time across DST");
}

void testIndependentInstanceTimezones() {
	Tempo budapest;
	initBudapestTempo(budapest);
	Tempo tokyo;
	TempoConfig tokyoConfig;
	tokyoConfig.timeZone = kTokyoTz;
	expect(static_cast<bool>(tokyo.init(tokyoConfig)), "Tokyo Tempo initialization should succeed");

	const DateTime instant = budapest.fromUtc(2026, 1, 15, 12, 0, 0);
	const LocalDateTime budapestLocal = budapest.toLocal(instant);
	const LocalDateTime tokyoLocal = tokyo.toLocal(instant);
	expect(budapestLocal.ok && budapestLocal.hour == 13,
	       "Budapest conversion must use the Budapest instance timezone");
	expect(tokyoLocal.ok && tokyoLocal.hour == 21,
	       "Tokyo conversion must use the Tokyo instance timezone");

	const LocalDateTime budapestStart = budapest.toLocal(budapest.startOfDayLocal(instant));
	const LocalDateTime tokyoStart = tokyo.toLocal(tokyo.startOfDayLocal(instant));
	expect(budapestStart.ok && budapestStart.hour == 0 && budapestStart.day == 15,
	       "Budapest calendar helpers must remain instance-scoped");
	expect(tokyoStart.ok && tokyoStart.hour == 0 && tokyoStart.day == 15,
	       "Tokyo calendar helpers must remain instance-scoped");

	char budapestText[32]{};
	char tokyoText[32]{};
	expect(budapest.dateTimeToStringLocal(
	           instant, budapestText, sizeof(budapestText), TempoFormat::DateTime),
	       "Budapest local formatting should succeed");
	expect(tokyo.dateTimeToStringLocal(instant, tokyoText, sizeof(tokyoText), TempoFormat::DateTime),
	       "Tokyo local formatting should succeed");
	expect(std::strcmp(budapestText, "2026-01-15 13:00:00") == 0,
	       "Budapest local formatting must use the Budapest instance timezone");
	expect(std::strcmp(tokyoText, "2026-01-15 21:00:00") == 0,
	       "Tokyo local formatting must use the Tokyo instance timezone");
}

void testLocationAndSunDateHandling() {
	Tempo withoutLocation;
	initBudapestTempo(withoutLocation);
	expect(!withoutLocation.sunrise().ok,
	       "default configuration must not silently use latitude/longitude zero");

	Tempo tempo;
	initBudapestTempo(tempo, true);
	const DateTime summerNoon = tempo.fromLocal(2026, 6, 21, 12, 0, 0);
	const DateTime summerMidnight = tempo.fromLocal(2026, 6, 21, 0, 0, 0);
	expect(tempo.sunrise(summerNoon).ok && tempo.sunset(summerNoon).ok,
	       "configured location must produce a sun cycle");
	expect(tempo.isDay(summerNoon), "summer local noon should be daylight in Budapest");
	expect(!tempo.isDay(summerMidnight), "summer local midnight should not be daylight in Budapest");
	expect(!tempo.isSunRise(tempo.fromLocal(2026, 6, 22, 12, 0, 0)),
	       "sunrise matching must use the supplied date rather than today's cache");
}

void testRuntimeLocationUpdate() {
	Tempo tempo;
	initBudapestTempo(tempo, true);
	const DateTime instant = tempo.fromUtc(2026, 1, 15, 12, 0, 0);
	expect(tempo.toLocal(instant).hour == 13,
	       "runtime location test must start in the Budapest timezone");

	TempoResult updated = tempo.updateLocation(35.6762f, 139.6503f, kTokyoTz);
	expect(static_cast<bool>(updated), "runtime location update should succeed");
	const LocalDateTime tokyoLocal = tempo.toLocal(instant);
	expect(tokyoLocal.ok && tokyoLocal.hour == 21,
	       "runtime location update must replace the instance timezone immediately");
	expect(tempo.sunrise(instant).ok && tempo.sunset(instant).ok,
	       "runtime location update must retain a valid configured sun location");

	TempoResult invalid = tempo.updateLocation(91.0f, 139.6503f, kTokyoTz);
	expect(!invalid && invalid.status == TempoStatus::InvalidArgument,
	       "invalid runtime coordinates must be rejected");
	expect(tempo.toLocal(instant).hour == 21,
	       "rejected runtime location update must not alter the existing timezone");

	tempo.deinit();
	const TempoResult afterDeinit = tempo.updateLocation(47.4979f, 19.0402f, kBudapestTz);
	expect(!afterDeinit && afterDeinit.status == TempoStatus::NotInitialized,
	       "location updates must reject deinitialized Tempo instances");
	expect(!tempo.sunPosition(instant).ok,
	       "rejected location update must not restore a deinitialized Tempo instance");
}

void testSunPosition() {
	Tempo noLocation;
	initBudapestTempo(noLocation);
	const DateTime referenceUtc = noLocation.fromUtc(2003, 10, 17, 19, 30, 30);
	expect(!noLocation.sunPosition(referenceUtc).ok,
	       "stored-location sun position requires configured coordinates");

	// NREL SPA reference: Golden, Colorado, 2003-10-17 12:30:30 MST.
	// The published zenith includes refraction; geometric elevation is about 39.872 degrees.
	const TempoSunPosition reference =
	    noLocation.sunPosition(39.742476f, -105.1786f, referenceUtc);
	expect(reference.ok, "explicit coordinates should work without stored location");
	expectNear(reference.azimuthDegrees, 194.340, 0.05,
	           "solar azimuth should agree with reference ephemeris");
	expectNear(reference.elevationDegrees, 39.872, 0.01,
	           "geometric solar elevation should exclude atmospheric refraction");

	Tempo tempo;
	initBudapestTempo(tempo, true);
	const DateTime equinoxNoon = tempo.fromUtc(2026, 3, 20, 12, 0, 0);
	const TempoSunPosition greenwich = tempo.sunPosition(51.4779f, 0.0f, equinoxNoon);
	expect(greenwich.ok && greenwich.isAboveHorizon,
	       "Greenwich equinox noon must be above the horizon");
	expectNear(greenwich.azimuthDegrees, 177.6, 1.0,
	           "Greenwich equinox solar azimuth must point south");
	expectNear(greenwich.elevationDegrees, 38.5, 1.0,
	           "Greenwich equinox solar elevation must match latitude");

	const DateTime equinoxMorning = tempo.fromUtc(2026, 3, 20, 6, 0, 0);
	const DateTime equinoxEvening = tempo.fromUtc(2026, 3, 20, 18, 0, 0);
	const TempoSunPosition east = tempo.sunPosition(0.0f, 0.0f, equinoxMorning);
	const TempoSunPosition west = tempo.sunPosition(0.0f, 0.0f, equinoxEvening);
	expect(east.ok && west.ok, "equatorial sunrise and sunset position should be valid");
	expectNear(east.azimuthDegrees, 90.0, 2.0, "morning sun should be east");
	expectNear(west.azimuthDegrees, 270.0, 2.0, "evening sun should be west");
	expectNear(east.elevationDegrees, 0.0, 4.0, "morning sun should be near horizon");
	expectNear(west.elevationDegrees, 0.0, 4.0, "evening sun should be near horizon");

	const TempoSunPosition daytime = tempo.sunPosition(tempo.fromUtc(2026, 6, 21, 12));
	const TempoSunPosition nighttime = tempo.sunPosition(tempo.fromUtc(2026, 6, 21, 0));
	expect(daytime.ok && daytime.isAboveHorizon, "summer daytime sun is above horizon");
	expect(nighttime.ok && !nighttime.isAboveHorizon,
	       "summer midnight sun is below horizon in Budapest");
	const TempoSunPosition later = tempo.sunPosition(tempo.fromUtc(2026, 6, 21, 12, 1));
	expect(later.ok && std::fabs(later.azimuthDegrees - daytime.azimuthDegrees) > 0.01,
	       "each requested minute must compute a fresh solar position");

	const DateTime springBefore = tempo.fromUtc(2026, 3, 29, 0, 30, 0);
	const DateTime springAfter = tempo.fromUtc(2026, 3, 29, 1, 30, 0);
	const TempoSunPosition springPositionBefore = tempo.sunPosition(springBefore);
	const TempoSunPosition springPositionAfter = tempo.sunPosition(springAfter);
	expect(springPositionBefore.ok && springPositionAfter.ok &&
	           springPositionAfter.elevationDegrees > springPositionBefore.elevationDegrees,
	       "solar position must advance normally through spring DST transition");

	const DateTime fallBefore = tempo.fromUtc(2026, 10, 25, 0, 30, 0);
	const DateTime fallAfter = tempo.fromUtc(2026, 10, 25, 1, 30, 0);
	const TempoSunPosition fallPositionBefore = tempo.sunPosition(fallBefore);
	const TempoSunPosition fallPositionAfter = tempo.sunPosition(fallAfter);
	expect(fallPositionBefore.ok && fallPositionAfter.ok &&
	           fallPositionAfter.elevationDegrees > fallPositionBefore.elevationDegrees,
	       "solar position must advance normally through repeated local DST hour");

	const TempoSunPosition northPole = tempo.sunPosition(90.0f, 0.0f, equinoxNoon);
	const TempoSunPosition southPole = tempo.sunPosition(-90.0f, 0.0f, equinoxNoon);
	expect(northPole.ok && southPole.ok &&
	           std::isfinite(northPole.azimuthDegrees) &&
	           std::isfinite(southPole.elevationDegrees),
	       "geographic poles must produce finite results");

	expect(!tempo.sunPosition(91.0f, 0.0f, equinoxNoon).ok,
	       "out-of-range latitude must be rejected");
	expect(!tempo.sunPosition(0.0f, 181.0f, equinoxNoon).ok,
	       "out-of-range longitude must be rejected");
	expect(!tempo.sunPosition(std::numeric_limits<float>::quiet_NaN(), 0.0f, equinoxNoon).ok,
	       "nonfinite coordinates must be rejected");
	expect(!tempo.sunPosition(0.0f, 0.0f,
	                          DateTime{std::numeric_limits<int64_t>::max()})
	            .ok,
	       "unrepresentable UTC dates must be rejected");

	const TempoSunPosition original = tempo.sunPosition(equinoxNoon);
	expect(static_cast<bool>(tempo.updateLocation(35.6762f, 139.6503f, kTokyoTz)),
	       "updating the location for solar position must succeed");
	const TempoSunPosition updated = tempo.sunPosition(equinoxNoon);
	const TempoSunPosition expected = tempo.sunPosition(35.6762f, 139.6503f, equinoxNoon);
	expect(updated.ok && expected.ok && original.ok,
	       "stored and explicit solar positions must remain valid after location update");
	expectNear(updated.azimuthDegrees, expected.azimuthDegrees, 0.0001,
	           "stored azimuth must use the updated location");
	expectNear(updated.elevationDegrees, expected.elevationDegrees, 0.0001,
	           "stored elevation must use the updated location");
	expect(std::fabs(updated.elevationDegrees - original.elevationDegrees) > 1.0,
	       "location updates must not reuse old position results");

	Tempo invalidClock;
	TempoConfig config;
	config.latitude = 47.4979f;
	config.longitude = 19.0402f;
	config.minValidUnixSeconds = 253402300800LL;
	expect(static_cast<bool>(invalidClock.init(config)),
	       "Tempo should initialize with a future clock validity threshold");
	expect(!invalidClock.sunPosition().ok,
	       "current solar position must reject an untrusted system clock");
	invalidClock.setMinValidUnixSeconds(0);
	expect(invalidClock.sunPosition().ok,
	       "current solar position must accept a clock that meets the updated threshold");
	invalidClock.setMinValidUnixSeconds(253402300800LL);
	expect(!invalidClock.sunPosition().ok,
	       "current solar position must observe a raised clock-validity threshold");
	expect(invalidClock.sunPosition(equinoxNoon).ok,
	       "explicit UTC position must not depend on current clock validity");
}

void testConcurrentSolarPositionUpdates() {
	Tempo tempo;
	initBudapestTempo(tempo, true);
	const DateTime instant = tempo.fromUtc(2026, 6, 21, 12, 0, 0);
	const TempoSunPosition budapest = tempo.sunPosition(47.4979f, 19.0402f, instant);
	const TempoSunPosition tokyo = tempo.sunPosition(35.6762f, 139.6503f, instant);
	std::atomic<bool> consistent{true};
	std::atomic<bool> started{false};

	std::thread reader([&]() {
		started.store(true);
		for (int i = 0; i < 2000; ++i) {
			const TempoSunPosition position = tempo.sunPosition(instant);
			const auto matches = [&](const TempoSunPosition &reference) {
				return std::fabs(position.azimuthDegrees - reference.azimuthDegrees) < 0.0001 &&
				       std::fabs(position.elevationDegrees - reference.elevationDegrees) < 0.0001;
			};
			if (!position.ok || !(matches(budapest) || matches(tokyo))) {
				consistent.store(false);
				return;
			}
		}
	});
	while (!started.load()) {
		std::this_thread::yield();
	}
	for (int i = 0; i < 80; ++i) {
		if (!tempo.updateLocation(35.6762f, 139.6503f, kTokyoTz) ||
		    !tempo.updateLocation(47.4979f, 19.0402f, kBudapestTz)) {
			consistent.store(false);
			break;
		}
	}
	reader.join();
	expect(consistent.load(),
	       "concurrent solar position reads must use a consistent coordinate snapshot");
}

void testScheduleValidation() {
	expect(!ScheduleCalculator::validate(TempoSchedule::everyMinutes(90)),
	       "everyMinutes values above 60 must be rejected");
	expect(!ScheduleCalculator::validate(TempoSchedule::everyHours(25)),
	       "everyHours values above 24 must be rejected");
	expect(!ScheduleCalculator::validate(TempoSchedule::weeklyAtLocal(0, 8, 0)),
	       "an empty weekday mask must be rejected");
	expect(!ScheduleCalculator::validate(TempoSchedule::cron("0 8 * * * extra")),
	       "cron expressions with extra fields must be rejected");
	expect(!ScheduleCalculator::validate(TempoSchedule::custom(
	           ScheduleField::only(63),
	           ScheduleField::only(0),
	           ScheduleField::any(),
	           ScheduleField::any(),
	           ScheduleField::any())),
	       "out-of-range bits in custom schedule fields must be rejected");

	Tempo tempo;
	initBudapestTempo(tempo);
	DateTime next{};
	const DateTime from = tempo.fromLocal(2026, 7, 14, 10, 7, 15);
	expect(ScheduleCalculator::computeNext(tempo, TempoSchedule::everyMinutes(15), from, next),
	       "valid interval schedule must compute a next occurrence");
	const LocalDateTime local = tempo.toLocal(next);
	expect(local.ok && local.hour == 10 && local.minute == 15 && local.second == 0,
	       "every-15-minutes schedule must advance to the expected local minute");
}
} // namespace

int main() {
	testInvalidParsing();
	testDstDayBoundaries();
	testAmbiguousRoundTrip();
	testCalendarSchedulingAcrossDst();
	testIndependentInstanceTimezones();
	testLocationAndSunDateHandling();
	testRuntimeLocationUpdate();
	testSunPosition();
	testConcurrentSolarPositionUpdates();
	testScheduleValidation();

	if (failures != 0) {
		std::cerr << failures << " regression test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "All Tempo host regression tests passed\n";
	return EXIT_SUCCESS;
}
