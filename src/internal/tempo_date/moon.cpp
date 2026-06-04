#include "date.h"

#include <cmath>
#include <ctime>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

template <typename T, typename T2> T mapValue(T2 val, T2 in_min, T2 in_max, T out_min, T out_max) {
	return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool toUtcTm(const DateTime &dt, tm &out) {
	time_t raw = static_cast<time_t>(dt.epochSeconds);
	return gmtime_r(&raw, &out) != nullptr;
}

double fractionalHour(const tm &timeinfo) {
	const int totalSeconds = timeinfo.tm_min * 60 + timeinfo.tm_sec;
	return static_cast<double>(timeinfo.tm_hour) +
	       mapValue<double>(totalSeconds, 0, 3600, 0.0, 1.0);
}

double julianDay(int32_t year, int32_t month, double day) {
	int32_t b = 0;
	if (month < 3) {
		year -= 1;
		month += 12;
	}
	if (year > 1582 || (year == 1582 && month > 10) || (year == 1582 && month == 10 && day > 15)) {
		const int32_t a = year / 100;
		b = 2 - a + a / 4;
	}
	const double c = 365.25 * static_cast<double>(year);
	const double e = 30.6001 * static_cast<double>(month + 1);
	return b + c + e + day + 1720994.5;
}

double sunPosition(double j) {
	double n = 360.0 / 365.2422 * j;
	int32_t i = static_cast<int32_t>(n / 360.0);
	n = n - static_cast<double>(i) * 360.0;
	double x = n - 3.762863;
	if (x < 0) {
		x += 360.0;
	}
	x *= kDegToRad;
	double e = x;
	double dl = 0.0;
	do {
		dl = e - 0.016718 * std::sin(e) - x;
		e = e - dl / (1 - 0.016718 * std::cos(e));
	} while (std::fabs(dl) >= 1e-12);
	const double v = 360.0 / kPi * std::atan(1.01686011182 * std::tan(e / 2.0));
	double l = v + 282.596403;
	i = static_cast<int32_t>(l / 360.0);
	l = l - static_cast<double>(i) * 360.0;
	return l;
}

double moonPosition(double j, double ls) {
	double ms = 0.985647332099 * j - 3.762863;
	if (ms < 0) {
		ms += 360.0;
	}
	double l = 13.176396 * j + 64.975464;
	int32_t i = static_cast<int32_t>(l / 360.0);
	l = l - static_cast<double>(i) * 360.0;
	if (l < 0) {
		l += 360.0;
	}
	double mm = l - 0.1114041 * j - 349.383063;
	i = static_cast<int32_t>(mm / 360.0);
	mm -= static_cast<double>(i) * 360.0;
	const double ev = 1.2739 * std::sin(kDegToRad * (2.0 * (l - ls) - mm));
	const double sms = std::sin(kDegToRad * ms);
	const double ae = 0.1858 * sms;
	mm += ev - ae - 0.37 * sms;
	const double ec = 6.2886 * std::sin(kDegToRad * mm);
	l += ev + ec - ae + 0.214 * std::sin(kDegToRad * 2.0 * mm);
	l = 0.6583 * std::sin(kDegToRad * 2.0 * (l - ls)) + l;
	return l;
}

TempoMoonPhase computeMoonPhase(const DateTime &dt) {
	tm t{};
	if (!toUtcTm(dt, t)) {
		return TempoMoonPhase{false, 0, 0.0};
	}

	const double hour = fractionalHour(t);
	const double j =
	    julianDay(t.tm_year + 1900, t.tm_mon + 1, static_cast<double>(t.tm_mday) + hour / 24.0) -
	    2444238.5;
	const double ls = sunPosition(j);
	const double lm = moonPosition(j, ls);
	double angle = lm - ls;
	if (angle < 0) {
		angle += 360.0;
	}
	if (angle >= 360.0) {
		angle = std::fmod(angle, 360.0);
	}
	const double illumination = (1.0 - std::cos((lm - ls) * kDegToRad)) / 2.0;
	return TempoMoonPhase{true, static_cast<int>(angle), illumination};
}
} // namespace

TempoMoonPhase Tempo::moonPhase() const {
	return moonPhase(now());
}

TempoMoonPhase Tempo::moonPhase(const DateTime &dt) const {
	return computeMoonPhase(dt);
}
