#include "internal/tempo_date/date.h"
#include "internal/tempo_scheduler/schedule/schedule_calculator.h"

#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace {
std::atomic<time_t> testClock{0};
}

extern "C" time_t __wrap_time(time_t *out) {
	const time_t now = testClock.load();
	if (out) {
		*out = now;
	}
	return now;
}

namespace {
constexpr const char *kBudapestTz = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr const char *kMidnightTransitionsTz = "CET-1CEST,M3.5.0/0,M10.5.0/0:30";
using Event = TempoMoonEventType;
int failures = 0;

void expect(bool condition, const char *message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

TempoConfig location(float latitude, float longitude, const char *timezone) {
	TempoConfig config;
	config.latitude = latitude;
	config.longitude = longitude;
	config.timezone = timezone;
	return config;
}

bool sameCycle(const TempoMoonCycle &a, const TempoMoonCycle &b) {
	if (!a.valid || !b.valid || a.eventCount != b.eventCount ||
	    a.calculatedForLocalDate.utc.epochSeconds != b.calculatedForLocalDate.utc.epochSeconds ||
	    a.alwaysAboveHorizon != b.alwaysAboveHorizon ||
	    a.alwaysBelowHorizon != b.alwaysBelowHorizon) {
		return false;
	}
	for (size_t i = 0; i < a.eventCount; ++i) {
		if (a.events[i].type != b.events[i].type ||
		    a.events[i].utc.epochSeconds != b.events[i].utc.epochSeconds) {
			return false;
		}
	}
	return true;
}

void testUsnoEvents() {
	struct Reference {
		const char *site;
		float latitude;
		float longitude;
		const char *timezone;
		int year;
		int month;
		int day;
		int usnoOffsetHours;
		size_t count;
		std::array<const char *, 3> events; // type followed by local HH:MM at the USNO offset
		bool alwaysAbove = false;
		bool alwaysBelow = false;
	};
	// USNO Complete Sun and Moon Data for One Day, API v4.0.1.
	// GET https://aa.usno.navy.mil/api/rstt/oneday?date=YYYY-MM-DD&coords=LAT,LON&tz=OFFSET
	// Fixed offsets are applied to the reference clock readings only.
	const Reference references[] = {
	    {"Budapest DST start", 47.4979f, 19.0402f, kBudapestTz, 2025, 3, 30, 2, 2,
	     {"R06:42", "S20:56"}},
	    {"Budapest DST end", 47.4979f, 19.0402f, kBudapestTz, 2025, 10, 26, 1, 2,
	     {"R11:35", "S18:59"}},
	    {"Budapest no set", 47.4979f, 19.0402f, kBudapestTz, 2025, 4, 2, 2, 1, {"R08:04"}},
	    {"Budapest previous UTC date", 47.4979f, 19.0402f, kBudapestTz, 2025, 4, 3, 2, 2,
	     {"S01:15", "R08:51"}},
	    {"London", 51.5074f, -0.1278f, "BST-1", 2024, 4, 8, 1, 2, {"R06:15", "S19:42"}},
	    {"New York", 40.7128f, -74.006f, "EST5", 2024, 1, 25, -5, 2, {"S07:34", "R16:56"}},
	    {"Sydney", -33.8688f, 151.2093f, "AEST-10", 2024, 9, 17, 10, 2, {"S05:17", "R16:47"}},
	    {"Cape Town", -33.9249f, 18.4241f, "SAST-2", 2024, 6, 14, 2, 2, {"S00:27", "R13:11"}},
	    {"Singapore", 1.3521f, 103.8198f, "SGT-8", 2024, 7, 28, 8, 2, {"R00:28", "S12:55"}},
	    {"Quito", -0.1807f, -78.4678f, "ECT5", 2024, 11, 1, -5, 2, {"R05:50", "S18:12"}},
	    {"Tromso above", 69.6492f, 18.9553f, kBudapestTz, 2025, 1, 13, 1, 0, {}, true},
	    {"Tromso below", 69.6492f, 18.9553f, kBudapestTz, 2025, 6, 11, 2, 0, {}, false, true},
	    {"Tromso two sets", 69.6492f, 18.9553f, kBudapestTz, 2025, 6, 7, 2, 3,
	     {"S00:40", "R19:46", "S23:52"}},
	    {"Tromso two rises", 69.6492f, 18.9553f, kBudapestTz, 2025, 6, 20, 2, 3,
	     {"R00:22", "S16:18", "R23:49"}},
	    {"Utqiagvik below", 71.2906f, -156.7886f, "AKDT8", 2025, 3, 22, -8, 0, {}, false, true},
	};
	for (const auto &ref : references) {
		Tempo tempo;
		expect(static_cast<bool>(tempo.init(location(ref.latitude, ref.longitude, ref.timezone))),
		       ref.site);
		const DateTime input = tempo.fromLocal(ref.year, ref.month, ref.day, 12);
		const TempoMoonCycle cycle = tempo.moonCycle(input);
		expect(cycle.valid && cycle.eventCount == ref.count, ref.site);
		expect(cycle.alwaysAboveHorizon == ref.alwaysAbove &&
		           cycle.alwaysBelowHorizon == ref.alwaysBelow,
		       ref.site);
		expect(cycle.calculatedForLocalDate.ok && cycle.calculatedForLocalDate.year == ref.year &&
		           cycle.calculatedForLocalDate.month == ref.month &&
		           cycle.calculatedForLocalDate.day == ref.day,
		       ref.site);
		for (size_t i = 0; i < ref.count && i < cycle.eventCount; ++i) {
			const char *time = ref.events[i];
			const int hour = 10 * (time[1] - '0') + time[2] - '0';
			const int minute = 10 * (time[4] - '0') + time[5] - '0';
			const DateTime referenceUtc = tempo.subHours(
			    tempo.fromUtc(ref.year, ref.month, ref.day, hour, minute), ref.usnoOffsetHours
			);
			const auto &event = cycle.events[i];
			expect(event.type == (time[0] == 'R' ? Event::Rise : Event::Set), ref.site);
			const int toleranceSeconds = std::abs(ref.latitude) > 66.0f ? 600 : 300;
			expect(std::abs(tempo.differenceInSeconds(event.utc, referenceUtc)) <= toleranceSeconds,
			       ref.site);
			expect(i == 0 || tempo.isBefore(cycle.events[i - 1].utc, event.utc), ref.site);
			const LocalDateTime local = tempo.toLocal(event.utc);
			expect(local.year == ref.year && local.month == ref.month && local.day == ref.day &&
			           tempo.isEqual(tempo.toUtc(local), event.utc),
			       ref.site);
			const bool rise = event.type == Event::Rise;
			expect(tempo.moonPosition(tempo.subSeconds(event.utc, 30)).isAboveHorizon != rise &&
			           tempo.moonPosition(tempo.addSeconds(event.utc, 30)).isAboveHorizon == rise,
			       ref.site);
		}
		for (Event type : {Event::Rise, Event::Set}) {
			size_t i = 0;
			while (i < cycle.eventCount && cycle.events[i].type != type) {
				++i;
			}
			const auto first = cycle.firstEvent(type);
			expect(first.found == (i < cycle.eventCount) &&
			           (!first.found || tempo.isEqual(first.utc, cycle.events[i].utc)),
			       "firstEvent should distinguish an absent event and return the first matching one");
		}
	}
}

void testLunarPositionAndClockValidity() {
	Tempo tempo;
	const DateTime instant = tempo.fromUtc(2025, 3, 30, 12);
	expect(!tempo.moonPosition(instant).ok && !tempo.moonCycle(instant).valid,
	       "stored-location queries should require initialization");
	// NASA JPL Horizons DE441, Moon (301), geodetic observer
	// 19.0402 E, 47.4979 N, sea level, 2025-Mar-30 12:00 UTC.
	// Airless observer quantities 4 and 20 (azimuth, elevation, distance).
	const auto position = tempo.moonPosition(47.4979f, 19.0402f, instant);
	expect(position.ok && position.isAboveHorizon &&
	           std::abs(position.azimuthDegrees - 188.562704) < 0.02 &&
	           std::abs(position.elevationDegrees - 53.502536) < 0.02 &&
	           std::abs(position.distanceKm - (0.00236004604379 * 149597870.7)) < 25,
	       "topocentric position should agree with JPL Horizons DE441");

	expect(static_cast<bool>(tempo.init()), "initialization without coordinates should succeed");
	expect(!tempo.moonCycle(instant).valid && !tempo.moonPosition(instant).ok,
	       "missing coordinates should be reported explicitly");
	expect(!tempo.moonPosition(91, 0, instant).ok && !tempo.moonPosition(0, -181, instant).ok &&
	           !tempo.moonPosition(std::numeric_limits<float>::quiet_NaN(), 0, instant).ok &&
	           !tempo.moonPosition(0, 0, DateTime{std::numeric_limits<int64_t>::max()}).ok,
	       "invalid coordinates or UTC input should fail");
	expect(tempo.moonPosition(0, 0, DateTime{}).ok, "epoch zero is a valid explicit UTC instant");

	expect(static_cast<bool>(tempo.updateLocation(0, 0, "UTC0")),
	       "a stored zero-degree latitude and longitude should be accepted");
	testClock = 0;
	expect(!tempo.moonCycleToday().valid && !tempo.moonPosition().ok,
	       "current-clock queries should respect the minimum valid time");
	expect(tempo.moonCycle(DateTime{}).valid && tempo.moonPosition(DateTime{}).ok,
	       "historical queries should be independent of current-clock validity");
	expect(!tempo.moonCycle(DateTime{std::numeric_limits<int64_t>::min()}).valid,
	       "out-of-range daily input should fail");
}

void testLocalCalendarBoundaries() {
	Tempo tempo;
	expect(static_cast<bool>(tempo.init(location(47.4979f, 19.0402f, kBudapestTz))),
	       "Budapest configuration should initialize");
	const auto spring = tempo.moonCycle(tempo.fromUtc(2025, 3, 30, 12));
	const auto autumn = tempo.moonCycle(tempo.fromUtc(2025, 10, 26, 12));
	expect(tempo.isEqual(spring.calculatedForLocalDate.utc, tempo.fromUtc(2025, 3, 29, 23)) &&
	           sameCycle(spring, tempo.moonCycle(tempo.fromUtc(2025, 3, 30, 21, 59, 59))) &&
	           !sameCycle(spring, tempo.moonCycle(tempo.fromUtc(2025, 3, 30, 22))),
	       "spring DST date should contain exactly 23 UTC hours");
	expect(tempo.isEqual(autumn.calculatedForLocalDate.utc, tempo.fromUtc(2025, 10, 25, 22)) &&
	           sameCycle(autumn, tempo.moonCycle(tempo.fromUtc(2025, 10, 26, 22, 59, 59))) &&
	           !sameCycle(autumn, tempo.moonCycle(tempo.fromUtc(2025, 10, 26, 23))),
	       "autumn DST date should contain exactly 25 UTC hours");

	const auto firstFold = tempo.toLocal(tempo.fromUtc(2025, 10, 26, 0, 30));
	const auto secondFold = tempo.toLocal(tempo.fromUtc(2025, 10, 26, 1, 30));
	expect(firstFold.hour == 2 && secondFold.hour == 2 && firstFold.offsetMinutes == 120 &&
	           secondFold.offsetMinutes == 60 &&
	           sameCycle(tempo.moonCycle(firstFold.utc), tempo.moonCycle(secondFold.utc)),
	       "both UTC instants in the repeated local hour should select the same local date");

	expect(static_cast<bool>(tempo.updateLocation(47.4979f, 19.0402f, kMidnightTransitionsTz)),
	       "midnight transition timezone should be accepted");
	const auto noMidnight = tempo.moonCycle(tempo.fromUtc(2025, 3, 30, 12));
	expect(noMidnight.valid && noMidnight.calculatedForLocalDate.hour == 1 &&
	           tempo.isEqual(noMidnight.calculatedForLocalDate.utc, tempo.fromUtc(2025, 3, 29, 23)),
	       "a date with a skipped midnight should start at its first existing local instant");
	const auto beforeRollback = tempo.moonCycle(tempo.fromUtc(2025, 10, 25, 22, 15));
	const auto duringRollback = tempo.moonCycle(tempo.fromUtc(2025, 10, 25, 22, 45));
	const auto afterRollback = tempo.moonCycle(tempo.fromUtc(2025, 10, 25, 23, 15));
	expect(sameCycle(beforeRollback, afterRollback) &&
	           beforeRollback.calculatedForLocalDate.day == 26 &&
	           duringRollback.calculatedForLocalDate.day == 25,
	       "a rollback across midnight should assign both spans to the correct local date");

	expect(static_cast<bool>(tempo.updateLocation(47.4979f, -170.0f, kMidnightTransitionsTz)),
	       "synthetic observation site should be accepted");
	const auto oct25 = tempo.moonCycle(tempo.fromUtc(2025, 10, 25, 12));
	const auto oct26 = tempo.moonCycle(tempo.fromUtc(2025, 10, 26, 12));
	const auto gapRise = oct25.firstEvent(Event::Rise);
	expect(gapRise.found && !oct26.firstEvent(Event::Rise).found &&
	           gapRise.utc.epochSeconds >= tempo.fromUtc(2025, 10, 25, 22, 30).epochSeconds &&
	           gapRise.utc.epochSeconds < tempo.fromUtc(2025, 10, 25, 23).epochSeconds &&
	           tempo.toLocal(gapRise.utc).offsetMinutes == 60,
	       "a crossing during the rollback should belong to the previous local date only");

	expect(static_cast<bool>(tempo.updateLocation(47.4979f, -165.0f, kMidnightTransitionsTz)),
	       "second synthetic observation site should be accepted");
	const auto earlyRise = tempo.moonCycle(tempo.fromUtc(2025, 10, 26, 12)).firstEvent(Event::Rise);
	expect(earlyRise.found &&
	           earlyRise.utc.epochSeconds < tempo.fromUtc(2025, 10, 25, 22, 30).epochSeconds &&
	           tempo.toLocal(earlyRise.utc).offsetMinutes == 120,
	       "the first span should retain a crossing and its pre-transition offset");
}

void testGrazingCrossingsWithinOneScanInterval() {
	Tempo tempo;
	expect(static_cast<bool>(tempo.init(location(72.1825f, 18.9553f, kBudapestTz))),
	       "grazing test location should initialize");
	const DateTime begin = tempo.fromUtc(2025, 6, 7, 19, 40);
	const DateTime end = tempo.addMinutes(begin, 10);
	const auto cycle = tempo.moonCycle(begin);
	expect(!tempo.moonPosition(begin).isAboveHorizon && !tempo.moonPosition(end).isAboveHorizon,
	       "the Moon should be below the horizon at both ends of the coarse interval");
	// Use exact, uninterpolated positions to independently enumerate crossings.
	size_t index = 0;
	while (index < cycle.eventCount && tempo.isBefore(cycle.events[index].utc, begin)) {
		++index;
	}
	size_t count = 0;
	bool wasAbove = false;
	for (DateTime cursor = begin; tempo.isBefore(cursor, end); cursor = tempo.addSeconds(cursor, 1)) {
		const bool above = tempo.moonPosition(cursor).isAboveHorizon;
		if (above != wasAbove) {
			expect(index < cycle.eventCount &&
			           cycle.events[index].type == (above ? Event::Rise : Event::Set) &&
			           std::abs(tempo.differenceInSeconds(cycle.events[index].utc, cursor)) <= 5,
			       "a shallow crossing should agree with the one-second exact-position scan");
			++index;
			++count;
		}
		wasAbove = above;
	}
	expect(count == 2 && index == cycle.eventCount,
	       "a same-sign coarse interval should preserve both crossings of a brief rise");
}

void testCacheAndConcurrentLocationSnapshots() {
	Tempo tempo;
	const TempoConfig first = location(47.4979f, 19.0402f, kBudapestTz);
	const TempoConfig second = location(-33.8688f, 151.2093f, "AEST-10");
	expect(static_cast<bool>(tempo.init(first)), "cache test configuration should initialize");
	const DateTime instant = tempo.fromUtc(2025, 3, 30, 21, 59, 59);
	testClock = instant.epochSeconds;
	const auto firstCycle = tempo.moonCycle(instant);
	expect(sameCycle(firstCycle, tempo.moonCycleToday()) &&
	           sameCycle(firstCycle, tempo.moonCycleToday()),
	       "current-day cache should match an explicit calculation");

	testClock = instant.epochSeconds + 1;
	const auto nextDay = tempo.moonCycleToday();
	expect(nextDay.calculatedForLocalDate.day == 31 && !sameCycle(firstCycle, nextDay),
	       "daily cache should refresh at local midnight");
	testClock = instant.epochSeconds;
	expect(static_cast<bool>(tempo.updateLocation(second.latitude, second.longitude, second.timezone)),
	       "location and timezone update should succeed");
	const auto secondCycle = tempo.moonCycle(instant);
	expect(!sameCycle(firstCycle, secondCycle) && sameCycle(secondCycle, tempo.moonCycleToday()),
	       "location and timezone update should invalidate the cache");
	const auto firstPosition = tempo.moonPosition(first.latitude, first.longitude, instant);
	const auto secondPosition = tempo.moonPosition(second.latitude, second.longitude, instant);
	std::atomic<bool> consistent{true};
	std::barrier start(3);
	auto reader = [&](bool cached) {
		start.arrive_and_wait();
		for (int i = 0; i < 100; ++i) {
			const auto cycle = cached ? tempo.moonCycleToday() : tempo.moonCycle(instant);
			const auto position = tempo.moonPosition(instant);
			if ((!sameCycle(cycle, firstCycle) && !sameCycle(cycle, secondCycle)) || !position.ok ||
			    (position.azimuthDegrees != firstPosition.azimuthDegrees &&
			     position.azimuthDegrees != secondPosition.azimuthDegrees)) {
				consistent = false;
			}
		}
	};
	std::thread cached(reader, true);
	std::thread explicitDate(reader, false);
	start.arrive_and_wait();
	for (int i = 0; i < 100; ++i) {
		const auto &config = i % 2 ? first : second;
		if (!tempo.updateLocation(config.latitude, config.longitude, config.timezone)) {
			consistent = false;
		}
		std::this_thread::yield();
	}
	cached.join();
	explicitDate.join();
	expect(consistent.load(), "concurrent lunar reads should use whole configuration snapshots");

	tempo.deinit();
	expect(!tempo.moonCycleToday().valid, "deinitialization should invalidate the cache");
	expect(static_cast<bool>(tempo.init(second)) && sameCycle(secondCycle, tempo.moonCycleToday()),
	       "reinitialization should recalculate for the new configuration");
}

void testLunarScheduleOffsets() {
	Tempo tempo;
	expect(static_cast<bool>(tempo.init(location(47.4979f, 19.0402f, kBudapestTz))),
	       "scheduler test configuration should initialize");
	const auto apr2 = tempo.fromLocal(2025, 4, 2, 12);
	const auto apr3 = tempo.fromLocal(2025, 4, 3, 12);
	const auto set = tempo.moonCycle(apr3).firstEvent(Event::Set);
	expect(set.found, "expected a moonset on April 3");
	DateTime next{};
	expect(ScheduleCalculator::computeNext(tempo, TempoSchedule::moonset(), apr2, next) &&
	           tempo.isEqual(next, set.utc),
	       "scheduler should skip a local date with no requested event");
	expect(ScheduleCalculator::computeNext(tempo, TempoSchedule::moonset(-120), apr2, next) &&
	           tempo.isEqual(next, tempo.subHours(set.utc, 2)) && tempo.toLocal(next).day == 2,
	       "negative offset should bring a following day's event forward");
	const auto apr1Set = tempo.moonCycle(tempo.fromLocal(2025, 4, 1, 12)).firstEvent(Event::Set);
	expect(apr1Set.found &&
	           ScheduleCalculator::computeNext(
	               tempo, TempoSchedule::moonSet(TempoDuration::minutes(30)),
	               tempo.fromLocal(2025, 4, 2), next
	           ) &&
	           tempo.isEqual(next, tempo.addMinutes(apr1Set.utc, 30)) && tempo.toLocal(next).day == 2,
	       "positive offset should retain a prior day's event");
	expect(ScheduleCalculator::validate(TempoSchedule::moonrise(-1440)) &&
	           ScheduleCalculator::validate(TempoSchedule::moonset(1440)) &&
	           !ScheduleCalculator::validate(TempoSchedule::moonset(-1441)) &&
	           !ScheduleCalculator::validate(
	               TempoSchedule::moonRise(TempoDuration::seconds(std::numeric_limits<int64_t>::max()))
	           ),
	       "lunar offsets should use the solar minute range without overflowing");
}
} // namespace

int main() {
	testUsnoEvents();
	testLunarPositionAndClockValidity();
	testLocalCalendarBoundaries();
	testGrazingCrossingsWithinOneScanInterval();
	testCacheAndConcurrentLocationSnapshots();
	testLunarScheduleOffsets();
	if (failures) {
		std::cerr << failures << " Tempo lunar regression test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "All Tempo lunar regression tests passed\n";
	return EXIT_SUCCESS;
}
