#include <cstdlib>
#include <iostream>

#include "internal/tempo_date/date.h"
#include "internal/tempo_scheduler/schedule/schedule_calculator.h"
#include "internal/tempo_scheduler/schedule/schedule_spec.h"

namespace {
constexpr const char *kBudapestTz = "CET-1CEST,M3.5.0/2,M10.5.0/3";

int failures = 0;

void expect(bool condition, const char *message) {
	if (condition) {
		return;
	}
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

Tempo makeBudapestTempo(bool withLocation = false) {
	Tempo tempo;
	TempoConfig config;
	config.timeZone = kBudapestTz;
	if (withLocation) {
		config.latitude = 47.4979f;
		config.longitude = 19.0402f;
	}
	expect(static_cast<bool>(tempo.init(config)), "Tempo initialization should succeed");
	return tempo;
}

void testInvalidParsing() {
	Tempo tempo = makeBudapestTempo();
	expect(!tempo.parseLocal("not-a-date").ok, "invalid local input must remain invalid");
	expect(!tempo.parseDateTimeLocal("2026-03-29 02:30:00").ok,
	       "nonexistent DST spring-forward local time must be rejected");
	expect(!tempo.parseIso8601Utc("2026-01-01T00:00:60Z").ok,
	       "leap-second input must be rejected when leap seconds are unsupported");
}

void testDstDayBoundaries() {
	Tempo tempo = makeBudapestTempo();

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
	Tempo tempo = makeBudapestTempo();
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
	Tempo tempo = makeBudapestTempo();
	const DateTime beforeSpring = tempo.fromLocal(2026, 3, 28, 8, 30, 0);
	const DateTime next = tempo.nextDailyAtLocal(8, 0, 0, beforeSpring);
	const LocalDateTime local = tempo.toLocal(next);
	expect(local.ok && local.year == 2026 && local.month == 3 && local.day == 29 &&
	           local.hour == 8 && local.minute == 0,
	       "next daily local occurrence must preserve wall-clock time across DST");
}

void testLocationAndSunDateHandling() {
	Tempo withoutLocation = makeBudapestTempo();
	expect(!withoutLocation.sunrise().ok,
	       "default configuration must not silently use latitude/longitude zero");

	Tempo tempo = makeBudapestTempo(true);
	const DateTime summerNoon = tempo.fromLocal(2026, 6, 21, 12, 0, 0);
	const DateTime summerMidnight = tempo.fromLocal(2026, 6, 21, 0, 0, 0);
	expect(tempo.sunrise(summerNoon).ok && tempo.sunset(summerNoon).ok,
	       "configured location must produce a sun cycle");
	expect(tempo.isDay(summerNoon), "summer local noon should be daylight in Budapest");
	expect(!tempo.isDay(summerMidnight), "summer local midnight should not be daylight in Budapest");
	expect(!tempo.isSunRise(tempo.fromLocal(2026, 6, 22, 12, 0, 0)),
	       "sunrise matching must use the supplied date rather than today's cache");
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

	Tempo tempo = makeBudapestTempo();
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
	testLocationAndSunDateHandling();
	testScheduleValidation();

	if (failures != 0) {
		std::cerr << failures << " regression test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "All Tempo host regression tests passed\n";
	return EXIT_SUCCESS;
}
