#include "date.h"
#include "utils.h"

#include <cmath>
#include <limits>

using Utils = TempoUtils;

namespace {

bool validCoordinates(float latitude, float longitude) {
	return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90.0f &&
	       latitude <= 90.0f && longitude >= -180.0f && longitude <= 180.0f;
}

struct LocalDateResult {
	int year = 0;
	int month = 0;
	int day = 0;
	bool ok = false;
};

struct OffsetDateResult {
	double offsetMinutes = 0.0;
	LocalDateResult date{};
};

LocalDateResult deriveLocalDateWithOffset(const DateTime &dt, int offsetSeconds) {
	int64_t shifted = dt.epochSeconds + static_cast<int64_t>(offsetSeconds);
	time_t raw = static_cast<time_t>(shifted);
	tm t{};
	if (gmtime_r(&raw, &t) == nullptr) {
		return {};
	}
	return LocalDateResult{t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, true};
}

OffsetDateResult
computeOffsetAndDate(const DateTime &dt, const char *timeZone, bool usePSRAMBuffers) {
	Utils::ScopedTz scoped(timeZone, usePSRAMBuffers);
	time_t raw = static_cast<time_t>(dt.epochSeconds);
	tm local{};
	if (localtime_r(&raw, &local) == nullptr) {
		return {};
	}

	const int64_t offsetSeconds = Utils::timegm64(local) - static_cast<int64_t>(raw);
	OffsetDateResult result;
	result.offsetMinutes = static_cast<double>(offsetSeconds) / 60.0;
	result.date = LocalDateResult{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, true};
	return result;
}

DateTime buildLocalEventUtc(
    const LocalDateResult &date,
    int minutes,
    const char *timeZone,
    bool usePSRAMBuffers,
    const Tempo &dateHelper
) {
	if (!date.ok || minutes < 0 || minutes >= 1440) {
		return DateTime{};
	}
	Utils::ScopedTz scoped(timeZone, usePSRAMBuffers);
	const int hour = minutes / 60;
	const int minute = minutes % 60;
	return dateHelper.fromLocal(date.year, date.month, date.day, hour, minute, 0);
}

double offsetMinutesForLocalClock(
    const LocalDateResult &date,
    int hour,
    int minute,
    const char *timeZone,
    bool usePSRAMBuffers,
    const Tempo &dateHelper
) {
	Utils::ScopedTz scoped(timeZone, usePSRAMBuffers);
	DateTime local = dateHelper.fromLocal(date.year, date.month, date.day, hour, minute, 0);
	LocalDateTime resolved = dateHelper.toLocal(local, timeZone);
	if (!resolved.ok) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return static_cast<double>(resolved.offsetMinutes);
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kSunAngle = 90.833; // standard atmospheric refraction angle in degrees

double radToDeg(double rad) {
	return 180.0 * rad / kPi;
}

double degToRad(double deg) {
	return kPi * deg / 180.0;
}

double jDay(int year, int month, int day) {
	if (month <= 2) {
		year -= 1;
		month += 12;
	}

	int A = static_cast<int>(std::floor(year / 100.0));
	int B = 2 - A + static_cast<int>(std::floor(A / 4.0));
	return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + B -
	       1524.5;
}

double fractionOfCentury(double jd) {
	return (jd - 2451545.0) / 36525.0;
}

double geomMeanLongSun(double t) {
	double L0 = 280.46646 + t * (36000.76983 + t * 0.0003032);
	while (L0 > 360.0) {
		L0 -= 360.0;
	}
	while (L0 < 0.0) {
		L0 += 360.0;
	}
	return L0;
}

double geomMeanAnomalySun(double t) {
	return 357.52911 + t * (35999.05029 - 0.0001537 * t);
}

double eccentricityEarthOrbit(double t) {
	return 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
}

double meanObliquityOfEcliptic(double t) {
	double seconds = 21.448 - t * (46.8150 + t * (0.00059 - t * 0.001813));
	return 23.0 + (26.0 + (seconds / 60.0)) / 60.0;
}

double obliquityCorrection(double t) {
	double e0 = meanObliquityOfEcliptic(t);
	double omega = 125.04 - 1934.136 * t;
	return e0 + 0.00256 * std::cos(degToRad(omega));
}

double sunEqOfCenter(double t) {
	double m = geomMeanAnomalySun(t);
	double mrad = degToRad(m);
	double sinm = std::sin(mrad);
	double sin2m = std::sin(mrad * 2.0);
	double sin3m = std::sin(mrad * 3.0);
	return sinm * (1.914602 - t * (0.004817 + 0.000014 * t)) + sin2m * (0.019993 - 0.000101 * t) +
	       sin3m * 0.000289;
}

double sunTrueLong(double t) {
	double l0 = geomMeanLongSun(t);
	double c = sunEqOfCenter(t);
	return l0 + c;
}

double sunApparentLong(double t) {
	double o = sunTrueLong(t);
	double omega = 125.04 - 1934.136 * t;
	return o - 0.00569 - 0.00478 * std::sin(degToRad(omega));
}

double sunDeclination(double t) {
	double e = obliquityCorrection(t);
	double lambda = sunApparentLong(t);
	double sint = std::sin(degToRad(e)) * std::sin(degToRad(lambda));
	return radToDeg(std::asin(sint));
}

double equationOfTime(double t) {
	double epsilon = obliquityCorrection(t);
	double l0 = geomMeanLongSun(t);
	double e = eccentricityEarthOrbit(t);
	double m = geomMeanAnomalySun(t);

	double y = std::tan(degToRad(epsilon) / 2.0);
	y *= y;

	double sin2l0 = std::sin(2.0 * degToRad(l0));
	double sinm = std::sin(degToRad(m));
	double cos2l0 = std::cos(2.0 * degToRad(l0));
	double sin4l0 = std::sin(4.0 * degToRad(l0));
	double sin2m = std::sin(2.0 * degToRad(m));

	double etime = y * sin2l0 - 2.0 * e * sinm + 4.0 * e * y * sinm * cos2l0 -
	               0.5 * y * y * sin4l0 - 1.25 * e * e * sin2m;
	return radToDeg(etime) * 4.0;
}

double hourAngleSunrise(double lat, double solarDec) {
	double latRad = degToRad(lat);
	double sdRad = degToRad(solarDec);
	double haArg =
	    (std::cos(degToRad(kSunAngle)) / (std::cos(latRad) * std::cos(sdRad)) -
	     std::tan(latRad) * std::tan(sdRad));
	return std::acos(haArg);
}

double sunriseSetUTC(bool isRise, double jday, double latitude, double longitude) {
	double t = fractionOfCentury(jday);
	double eqTime = equationOfTime(t);
	double solarDec = sunDeclination(t);
	double hourAngle = hourAngleSunrise(latitude, solarDec);

	hourAngle = isRise ? hourAngle : -hourAngle;
	double delta = longitude + radToDeg(hourAngle);
	return 720.0 - (4.0 * delta) - eqTime;
}

int sunriseSetLocalMinutes(
    bool isRise,
    int year,
    int month,
    int day,
    double latitude,
    double longitude,
    double offsetMinutes
) {
	double jday = jDay(year, month, day);
	double timeUTC = sunriseSetUTC(isRise, jday, latitude, longitude);

	double newJday = jday + timeUTC / (60.0 * 24.0);
	double newTimeUTC = sunriseSetUTC(isRise, newJday, latitude, longitude);

	if (!std::isnan(newTimeUTC)) {
		double timeLocal = std::round(newTimeUTC + offsetMinutes);
		return static_cast<int>(timeLocal);
	}
	return -1;
}

TempoSunEventResult buildTempoSunEventResult(
    int minutes, double offsetMinutes, const LocalDateResult &date, const Tempo &dateHelper
) {
	TempoSunEventResult result{false, DateTime{}};
	if (!date.ok || minutes < 0 || minutes >= 1440) {
		return result;
	}

	const int64_t offsetSeconds = static_cast<int64_t>(std::llround(offsetMinutes * 60.0));
	DateTime midnightUtc = dateHelper.fromUtc(date.year, date.month, date.day, 0, 0, 0);
	DateTime localMidnightUtc = dateHelper.subSeconds(midnightUtc, offsetSeconds);

	result.ok = true;
	result.value = dateHelper.addSeconds(
	    localMidnightUtc,
	    static_cast<int64_t>(minutes) * Utils::kSecondsPerMinute
	);
	return result;
}

TempoSunEventResult buildTimeZoneAwareTempoSunEventResult(
    bool isRise,
    const LocalDateResult &date,
    double latitude,
    double longitude,
    const char *timeZone,
    bool usePSRAMBuffers,
    const Tempo &dateHelper
) {
	TempoSunEventResult result{false, DateTime{}};
	if (!date.ok) {
		return result;
	}

	double offsetMinutes =
	    offsetMinutesForLocalClock(date, 12, 0, timeZone, usePSRAMBuffers, dateHelper);
	if (!std::isfinite(offsetMinutes)) {
		return result;
	}

	int previousMinutes = -1;
	for (int iteration = 0; iteration < 3; ++iteration) {
		const int minutes = sunriseSetLocalMinutes(
		    isRise,
		    date.year,
		    date.month,
		    date.day,
		    latitude,
		    longitude,
		    offsetMinutes
		);
		if (minutes < 0 || minutes >= 1440) {
			return result;
		}

		DateTime eventUtc =
		    buildLocalEventUtc(date, minutes, timeZone, usePSRAMBuffers, dateHelper);
		LocalDateTime resolved = dateHelper.toLocal(eventUtc, timeZone);
		if (!resolved.ok) {
			return result;
		}
		if (resolved.offsetMinutes == static_cast<int>(std::llround(offsetMinutes)) ||
		    minutes == previousMinutes) {
			result.ok = true;
			result.value = eventUtc;
			return result;
		}

		offsetMinutes = static_cast<double>(resolved.offsetMinutes);
		previousMinutes = minutes;
	}

	const int minutes = sunriseSetLocalMinutes(
	    isRise,
	    date.year,
	    date.month,
	    date.day,
	    latitude,
	    longitude,
	    offsetMinutes
	);
	if (minutes < 0 || minutes >= 1440) {
		return result;
	}
	result.ok = true;
	result.value = buildLocalEventUtc(date, minutes, timeZone, usePSRAMBuffers, dateHelper);
	return result;
}
} // namespace

TempoSunEventResult Tempo::sunrise() const {
	return sunrise(now());
}

TempoSunEventResult Tempo::sunset() const {
	return sunset(now());
}

TempoSunEventResult Tempo::sunrise(const DateTime &day) const {
	return sunriseFromConfig(day);
}

TempoSunEventResult Tempo::sunset(const DateTime &day) const {
	return sunsetFromConfig(day);
}

TempoSunEventResult
Tempo::sunrise(float latitude, float longitude, float timezoneHours, bool isDst) const {
	return sunrise(latitude, longitude, timezoneHours, isDst, now());
}

TempoSunEventResult
Tempo::sunset(float latitude, float longitude, float timezoneHours, bool isDst) const {
	return sunset(latitude, longitude, timezoneHours, isDst, now());
}

TempoSunEventResult Tempo::sunrise(
    float latitude, float longitude, float timezoneHours, bool isDst, const DateTime &day
) const {
	if (!validCoordinates(latitude, longitude)) {
		return TempoSunEventResult{false, DateTime{}};
	}
	const double offsetMinutes = static_cast<double>(timezoneHours) * 60.0 + (isDst ? 60.0 : 0.0);
	const int offsetSeconds =
	    static_cast<int>(std::llround(offsetMinutes * Utils::kSecondsPerMinute));
	LocalDateResult localDate = deriveLocalDateWithOffset(day, offsetSeconds);
	if (!localDate.ok) {
		return TempoSunEventResult{false, DateTime{}};
	}
	const int minutes = sunriseSetLocalMinutes(
	    true,
	    localDate.year,
	    localDate.month,
	    localDate.day,
	    latitude,
	    longitude,
	    offsetMinutes
	);
	return buildTempoSunEventResult(minutes, offsetMinutes, localDate, *this);
}

TempoSunEventResult Tempo::sunset(
    float latitude, float longitude, float timezoneHours, bool isDst, const DateTime &day
) const {
	if (!validCoordinates(latitude, longitude)) {
		return TempoSunEventResult{false, DateTime{}};
	}
	const double offsetMinutes = static_cast<double>(timezoneHours) * 60.0 + (isDst ? 60.0 : 0.0);
	const int offsetSeconds =
	    static_cast<int>(std::llround(offsetMinutes * Utils::kSecondsPerMinute));
	LocalDateResult localDate = deriveLocalDateWithOffset(day, offsetSeconds);
	if (!localDate.ok) {
		return TempoSunEventResult{false, DateTime{}};
	}
	const int minutes = sunriseSetLocalMinutes(
	    false,
	    localDate.year,
	    localDate.month,
	    localDate.day,
	    latitude,
	    longitude,
	    offsetMinutes
	);
	return buildTempoSunEventResult(minutes, offsetMinutes, localDate, *this);
}

TempoSunEventResult Tempo::sunrise(float latitude, float longitude, const char *timeZone) const {
	return sunrise(latitude, longitude, timeZone, now());
}

TempoSunEventResult Tempo::sunset(float latitude, float longitude, const char *timeZone) const {
	return sunset(latitude, longitude, timeZone, now());
}

TempoSunEventResult
Tempo::sunrise(float latitude, float longitude, const char *timeZone, const DateTime &day) const {
	if (!validCoordinates(latitude, longitude)) {
		return TempoSunEventResult{false, DateTime{}};
	}
	OffsetDateResult data = computeOffsetAndDate(day, timeZone, usePSRAMBuffers_);
	if (!data.date.ok) {
		return TempoSunEventResult{false, DateTime{}};
	}
	return buildTimeZoneAwareTempoSunEventResult(
	    true,
	    data.date,
	    latitude,
	    longitude,
	    timeZone,
	    usePSRAMBuffers_,
	    *this
	);
}

TempoSunEventResult
Tempo::sunset(float latitude, float longitude, const char *timeZone, const DateTime &day) const {
	if (!validCoordinates(latitude, longitude)) {
		return TempoSunEventResult{false, DateTime{}};
	}
	OffsetDateResult data = computeOffsetAndDate(day, timeZone, usePSRAMBuffers_);
	if (!data.date.ok) {
		return TempoSunEventResult{false, DateTime{}};
	}
	return buildTimeZoneAwareTempoSunEventResult(
	    false,
	    data.date,
	    latitude,
	    longitude,
	    timeZone,
	    usePSRAMBuffers_,
	    *this
	);
}

TempoSunEventResult Tempo::sunriseFromConfig(const DateTime &day) const {
	if (!hasLocation_) {
		return TempoSunEventResult{false, DateTime{}};
	}
	if (!validCoordinates(latitude_, longitude_)) {
		return TempoSunEventResult{false, DateTime{}};
	}
	const char *tz = timeZone_.empty() ? nullptr : timeZone_.c_str();
	OffsetDateResult data = computeOffsetAndDate(day, tz, usePSRAMBuffers_);
	if (!data.date.ok) {
		return TempoSunEventResult{false, DateTime{}};
	}
	return buildTimeZoneAwareTempoSunEventResult(
	    true,
	    data.date,
	    latitude_,
	    longitude_,
	    tz,
	    usePSRAMBuffers_,
	    *this
	);
}

TempoSunEventResult Tempo::sunsetFromConfig(const DateTime &day) const {
	if (!hasLocation_) {
		return TempoSunEventResult{false, DateTime{}};
	}
	if (!validCoordinates(latitude_, longitude_)) {
		return TempoSunEventResult{false, DateTime{}};
	}
	const char *tz = timeZone_.empty() ? nullptr : timeZone_.c_str();
	OffsetDateResult data = computeOffsetAndDate(day, tz, usePSRAMBuffers_);
	if (!data.date.ok) {
		return TempoSunEventResult{false, DateTime{}};
	}
	return buildTimeZoneAwareTempoSunEventResult(
	    false,
	    data.date,
	    latitude_,
	    longitude_,
	    tz,
	    usePSRAMBuffers_,
	    *this
	);
}

bool Tempo::isDay() const {
	return isDayWithOffsets(now(), 0, 0);
}

bool Tempo::isDay(const DateTime &day) const {
	return isDayWithOffsets(day, 0, 0);
}

bool Tempo::isDay(int sunRiseOffsetSec, int sunSetOffsetSec) const {
	return isDayWithOffsets(now(), sunRiseOffsetSec, sunSetOffsetSec);
}

bool Tempo::isDay(int sunRiseOffsetSec, int sunSetOffsetSec, const DateTime &day) const {
	return isDayWithOffsets(day, sunRiseOffsetSec, sunSetOffsetSec);
}

bool Tempo::isDay(
    const TempoDuration &sunRiseOffset, const TempoDuration &sunSetOffset
) const {
	return isDay(now(), sunRiseOffset, sunSetOffset);
}

bool Tempo::isDay(
    const DateTime &day, const TempoDuration &sunRiseOffset, const TempoDuration &sunSetOffset
) const {
	const TempoSunEventResult rise = sunriseFromConfig(day);
	const TempoSunEventResult set = sunsetFromConfig(day);
	if (!rise.ok || !set.ok) {
		return false;
	}
	DateTime start = addSeconds(rise.value, sunRiseOffset.seconds());
	DateTime end = addSeconds(set.value, sunSetOffset.seconds());
	return !isBefore(day, start) && !isAfter(day, end);
}

bool Tempo::refreshSunCycleCache(const DateTime &day) {
	std::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);
	TempoSunEventResult rise = sunriseFromConfig(day);
	TempoSunEventResult set = sunsetFromConfig(day);
	if (!rise.ok || !set.ok) {
		sunCycleCache_ = TempoSunCycle{};
		return false;
	}

	TempoSunCycle cycle;
	cycle.calculatedForDateUtc = day;
	cycle.calculatedForLocalDate = toLocal(day);
	cycle.sunRiseUtc = rise.value;
	cycle.sunSetUtc = set.value;
	cycle.solarNoonUtc = addSeconds(rise.value, differenceInSeconds(set.value, rise.value) / 2);
	cycle.sunRiseLocal = toLocal(cycle.sunRiseUtc);
	cycle.sunSetLocal = toLocal(cycle.sunSetUtc);
	cycle.solarNoonLocal = toLocal(cycle.solarNoonUtc);
	cycle.valid = true;
	sunCycleCache_ = cycle;
	return true;
}

TempoSunCycle Tempo::sunCycleFor(const DateTime &day) {
	std::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);
	if (!sunCycleCache_.valid || !isSameLocalDay(sunCycleCache_.calculatedForDateUtc, day)) {
		refreshSunCycleCache(day);
	}
	return sunCycleCache_;
}

TempoSunCycle Tempo::sunCycleToday() {
	return sunCycleFor(now());
}

DateTime Tempo::sunRiseTodayUtc() {
	return sunCycleToday().sunRiseUtc;
}

DateTime Tempo::sunSetTodayUtc() {
	return sunCycleToday().sunSetUtc;
}

LocalDateTime Tempo::sunRiseTodayLocal() {
	return sunCycleToday().sunRiseLocal;
}

LocalDateTime Tempo::sunSetTodayLocal() {
	return sunCycleToday().sunSetLocal;
}

bool Tempo::inSunWindow(
    const DateTime &dt, const DateTime &event, const TempoDuration &offset
) const {
	DateTime target = addSeconds(event, offset.seconds());
	int64_t delta = differenceInSeconds(dt, target);
	if (delta < 0) {
		delta = -delta;
	}
	return static_cast<uint64_t>(delta) <= sunCycleMatchWindowSeconds_;
}

bool Tempo::isSunRise() {
	return isSunRise(now(), TempoDuration{});
}

bool Tempo::isSunRise(const DateTime &dt) {
	return isSunRise(dt, TempoDuration{});
}

bool Tempo::isSunRise(const TempoDuration &offset) {
	return isSunRise(now(), offset);
}

bool Tempo::isSunRise(const DateTime &dt, const TempoDuration &offset) {
	TempoSunCycle cycle = sunCycleFor(dt);
	return cycle.valid && inSunWindow(dt, cycle.sunRiseUtc, offset);
}

bool Tempo::isSunSet() {
	return isSunSet(now(), TempoDuration{});
}

bool Tempo::isSunSet(const DateTime &dt) {
	return isSunSet(dt, TempoDuration{});
}

bool Tempo::isSunSet(const TempoDuration &offset) {
	return isSunSet(now(), offset);
}

bool Tempo::isSunSet(const DateTime &dt, const TempoDuration &offset) {
	TempoSunCycle cycle = sunCycleFor(dt);
	return cycle.valid && inSunWindow(dt, cycle.sunSetUtc, offset);
}

bool Tempo::isDayWithOffsets(
    const DateTime &day, int sunRiseOffsetSec, int sunSetOffsetSec
) const {
	if (!hasLocation_ || !validCoordinates(latitude_, longitude_)) {
		return false;
	}

	TempoSunEventResult rise = sunriseFromConfig(day);
	TempoSunEventResult set = sunsetFromConfig(day);
	if (!rise.ok || !set.ok) {
		return false;
	}

	DateTime start = addSeconds(rise.value, sunRiseOffsetSec);
	DateTime end = addSeconds(set.value, sunSetOffsetSec);
	if (isAfter(start, end)) {
		return false;
	}

	const bool afterStart = !isBefore(day, start);
	const bool beforeEnd = !isAfter(day, end);
	return afterStart && beforeEnd;
}
