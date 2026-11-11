#include "date.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kRadians = 3.14159265358979323846 / 180.0;
constexpr int64_t kJ2000Utc = 946728000;
constexpr int64_t kFirstSupportedEpoch = -62135596800; // 0001-01-01 UTC
constexpr int64_t kEndSupportedEpoch = 253402300800;   // 10000-01-01 UTC
constexpr int64_t kDay = TempoUtils::kSecondsPerDay;
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kEarthEccentricitySquared = 0.00669437999014; // WGS 84
constexpr double kMoonRadiusKm = 1737.4;
constexpr double kHorizonRefraction = (34.0 / 60.0) * kRadians;

struct LunarArgument {
	int8_t elongation;
	int8_t solarAnomaly;
	int8_t lunarAnomaly;
	int8_t latitude;
};
struct LongitudeDistanceTerm {
	LunarArgument argument;
	int32_t longitude;
	int32_t distance;
};
struct LatitudeTerm {
	LunarArgument argument;
	int32_t latitude;
};

// Jean Meeus, Astronomical Algorithms (2nd ed.), Tables 47.A and 47.B.
// Longitude and latitude coefficients are in millionths of a degree.
// Distance coefficients are in metres.
constexpr LongitudeDistanceTerm kLongitudeDistanceTerms[] = {
	{{0, 0, 1, 0}, 6288774, -20905355},
	{{2, 0, -1, 0}, 1274027, -3699111},
	{{2, 0, 0, 0}, 658314, -2955968},
	{{0, 0, 2, 0}, 213618, -569925},
	{{0, 1, 0, 0}, -185116, 48888},
	{{0, 0, 0, 2}, -114332, -3149},
	{{2, 0, -2, 0}, 58793, 246158},
	{{2, -1, -1, 0}, 57066, -152138},
	{{2, 0, 1, 0}, 53322, -170733},
	{{2, -1, 0, 0}, 45758, -204586},
	{{0, 1, -1, 0}, -40923, -129620},
	{{1, 0, 0, 0}, -34720, 108743},
	{{0, 1, 1, 0}, -30383, 104755},
	{{2, 0, 0, -2}, 15327, 10321},
	{{0, 0, 1, 2}, -12528, 0},
	{{0, 0, 1, -2}, 10980, 79661},
	{{4, 0, -1, 0}, 10675, -34782},
	{{0, 0, 3, 0}, 10034, -23210},
	{{4, 0, -2, 0}, 8548, -21636},
	{{2, 1, -1, 0}, -7888, 24208},
	{{2, 1, 0, 0}, -6766, 30824},
	{{1, 0, -1, 0}, -5163, -8379},
	{{1, 1, 0, 0}, 4987, -16675},
	{{2, -1, 1, 0}, 4036, -12831},
	{{2, 0, 2, 0}, 3994, -10445},
	{{4, 0, 0, 0}, 3861, -11650},
	{{2, 0, -3, 0}, 3665, 14403},
	{{0, 1, -2, 0}, -2689, -7003},
	{{2, 0, -1, 2}, -2602, 0},
	{{2, -1, -2, 0}, 2390, 10056},
	{{1, 0, 1, 0}, -2348, 6322},
	{{2, -2, 0, 0}, 2236, -9884},
	{{0, 1, 2, 0}, -2120, 5751},
	{{0, 2, 0, 0}, -2069, 0},
	{{2, -2, -1, 0}, 2048, -4950},
	{{2, 0, 1, -2}, -1773, 4130},
	{{2, 0, 0, 2}, -1595, 0},
	{{4, -1, -1, 0}, 1215, -3958},
	{{0, 0, 2, 2}, -1110, 0},
	{{3, 0, -1, 0}, -892, 3258},
	{{2, 1, 1, 0}, -810, 2616},
	{{4, -1, -2, 0}, 759, -1897},
	{{0, 2, -1, 0}, -713, -2117},
	{{2, 2, -1, 0}, -700, 2354},
	{{2, 1, -2, 0}, 691, 0},
	{{2, -1, 0, -2}, 596, 0},
	{{4, 0, 1, 0}, 549, -1423},
	{{0, 0, 4, 0}, 537, -1117},
	{{4, -1, 0, 0}, 520, -1571},
	{{1, 0, -2, 0}, -487, -1739},
	{{2, 1, 0, -2}, -399, 0},
	{{0, 0, 2, -2}, -381, -4421},
	{{1, 1, 1, 0}, 351, 0},
	{{3, 0, -2, 0}, -340, 0},
	{{4, 0, -3, 0}, 330, 0},
	{{2, -1, 2, 0}, 327, 0},
	{{0, 2, 1, 0}, -323, 1165},
	{{1, 1, -1, 0}, 299, 0},
	{{2, 0, 3, 0}, 294, 0},
	{{2, 0, -1, -2}, 0, 8752},
};
constexpr LatitudeTerm kLatitudeTerms[] = {
	{{0, 0, 0, 1}, 5128122},
	{{0, 0, 1, 1}, 280602},
	{{0, 0, 1, -1}, 277693},
	{{2, 0, 0, -1}, 173237},
	{{2, 0, -1, 1}, 55413},
	{{2, 0, -1, -1}, 46271},
	{{2, 0, 0, 1}, 32573},
	{{0, 0, 2, 1}, 17198},
	{{2, 0, 1, -1}, 9266},
	{{0, 0, 2, -1}, 8822},
	{{2, -1, 0, -1}, 8216},
	{{2, 0, -2, -1}, 4324},
	{{2, 0, 1, 1}, 4200},
	{{2, 1, 0, -1}, -3359},
	{{2, -1, -1, 1}, 2463},
	{{2, -1, 0, 1}, 2211},
	{{2, -1, -1, -1}, 2065},
	{{0, 1, -1, -1}, -1870},
	{{4, 0, -1, -1}, 1828},
	{{0, 1, 0, 1}, -1794},
	{{0, 0, 0, 3}, -1749},
	{{0, 1, -1, 1}, -1565},
	{{1, 0, 0, 1}, -1491},
	{{0, 1, 1, 1}, -1475},
	{{0, 1, 1, -1}, -1410},
	{{0, 1, 0, -1}, -1344},
	{{1, 0, 0, -1}, -1335},
	{{0, 0, 3, 1}, 1107},
	{{4, 0, 0, -1}, 1021},
	{{4, 0, -1, 1}, 833},
	{{0, 0, 1, -3}, 777},
	{{4, 0, -2, 1}, 671},
	{{2, 0, 0, -3}, 607},
	{{2, 0, 2, -1}, 596},
	{{2, -1, 1, -1}, 491},
	{{2, 0, -2, 1}, -451},
	{{0, 0, 3, -1}, 439},
	{{2, 0, 2, 1}, 422},
	{{2, 0, -3, -1}, 421},
	{{2, 1, -1, 1}, -366},
	{{2, 1, 0, 1}, -351},
	{{4, 0, 0, 1}, 331},
	{{2, -1, 1, 1}, 315},
	{{2, -2, 0, -1}, 302},
	{{0, 0, 1, 3}, -283},
	{{2, 1, 1, -1}, -229},
	{{1, 1, 0, -1}, 223},
	{{1, 1, 0, 1}, 223},
	{{0, 1, -2, -1}, -220},
	{{2, 1, -1, -1}, -220},
	{{1, 0, 1, 1}, -185},
	{{2, -1, -2, -1}, 181},
	{{0, 1, 2, 1}, -177},
	{{4, 0, -2, -1}, 176},
	{{4, -1, -1, -1}, 166},
	{{1, 0, 1, -1}, -164},
	{{4, 0, 1, -1}, 132},
	{{1, 0, -1, -1}, -119},
	{{4, -1, 0, -1}, 115},
	{{2, -2, 0, 1}, 107},
};

struct Cartesian {
	double x;
	double y;
	double z;
};

double radians(double degrees) {
	return std::remainder(degrees, 360.0) * kRadians;
}

bool supportedEpoch(int64_t epoch) {
	return epoch >= kFirstSupportedEpoch && epoch < kEndSupportedEpoch;
}

bool validCoordinates(float latitude, float longitude) {
	return std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= 90.0f &&
	       std::abs(longitude) <= 180.0f;
}

Cartesian moonEquatorial(int64_t epoch) {
	// Approximate TT - UTC for modern dates. Earth rotation below uses UTC as UT1.
	const double t = (static_cast<double>(epoch - kJ2000Utc) + 69.184) / (36525.0 * kDay);
	const double meanLongitude = radians(
	    218.3164477 + t * (481267.88123421 + t * (-0.0015786 + t * (1.0 / 538841 - t / 65194000)))
	);
	const double d = radians(
	    297.8501921 + t * (445267.1114034 + t * (-0.0018819 + t * (1.0 / 545868 - t / 113065000)))
	);
	const double m = radians(357.5291092 + t * (35999.0502909 + t * (-0.0001536 + t / 24490000)));
	const double mp = radians(
	    134.9633964 + t * (477198.8675055 + t * (0.0087414 + t * (1.0 / 69699 - t / 14712000)))
	);
	const double f = radians(
	    93.2720950 + t * (483202.0175233 + t * (-0.0036539 + t * (-1.0 / 3526000 + t / 863310000)))
	);
	const double a1 = radians(119.75 + 131.849 * t);
	const double a2 = radians(53.09 + 479264.290 * t);
	const double a3 = radians(313.45 + 481266.484 * t);
	const double e = 1.0 - t * (0.002516 + 0.0000074 * t);
	const double eccentricity[] = {1.0, e, e * e};
	const auto angle = [=](const LunarArgument &arg) {
		return arg.elongation * d + arg.solarAnomaly * m + arg.lunarAnomaly * mp + arg.latitude * f;
	};

	double longitude = 3958 * std::sin(a1) + 1962 * std::sin(meanLongitude - f) + 318 * std::sin(a2);
	double distance = 0;
	for (const auto &term : kLongitudeDistanceTerms) {
		const double weight = eccentricity[std::abs(term.argument.solarAnomaly)];
		const double argument = angle(term.argument);
		longitude += term.longitude * weight * std::sin(argument);
		distance += term.distance * weight * std::cos(argument);
	}
	double latitude = -2235 * std::sin(meanLongitude) + 382 * std::sin(a3) +
	                  175 * (std::sin(a1 - f) + std::sin(a1 + f)) +
	                  127 * std::sin(meanLongitude - mp) - 115 * std::sin(meanLongitude + mp);
	for (const auto &term : kLatitudeTerms) {
		latitude += term.latitude * eccentricity[std::abs(term.argument.solarAnomaly)] *
		            std::sin(angle(term.argument));
	}

	const double lambda = meanLongitude + longitude * 1e-6 * kRadians;
	const double beta = latitude * 1e-6 * kRadians;
	const double radius = 385000.56 + distance * 1e-3;
	const double obliquity =
	    (23.439291111 - t * (0.013004167 + t * (0.000000164 - 0.000000504 * t))) * kRadians;
	const double x = radius * std::cos(beta) * std::cos(lambda);
	const double y = radius * std::cos(beta) * std::sin(lambda);
	const double z = radius * std::sin(beta);
	return {
	    x,
	    y * std::cos(obliquity) - z * std::sin(obliquity),
	    y * std::sin(obliquity) + z * std::cos(obliquity),
	};
}

class Observer {
  public:
	Observer(float latitude, float longitude)
	    : sinLatitude_(std::sin(latitude * kRadians)), cosLatitude_(std::cos(latitude * kRadians)),
	      longitude_(longitude) {
		const double radius =
		    kEarthRadiusKm / std::sqrt(1 - kEarthEccentricitySquared * sinLatitude_ * sinLatitude_);
		radialKm_ = radius * cosLatitude_;
		axialKm_ = radius * (1 - kEarthEccentricitySquared) * sinLatitude_;
	}

	Cartesian horizontal(int64_t epoch, const Cartesian &moon) const {
		const double days = static_cast<double>(epoch - kJ2000Utc) / kDay;
		const double centuries = days / 36525.0;
		const double sidereal = radians(
		    280.46061837 + 360.98564736629 * days +
		    centuries * centuries * (0.000387933 - centuries / 38710000.0) + longitude_
		);
		const double sinSidereal = std::sin(sidereal);
		const double cosSidereal = std::cos(sidereal);
		const double radial = moon.x * cosSidereal + moon.y * sinSidereal - radialKm_;
		const double axial = moon.z - axialKm_;
		return {
		    -moon.x * sinSidereal + moon.y * cosSidereal,         // east
		    axial * cosLatitude_ - radial * sinLatitude_,        // north
		    axial * sinLatitude_ + radial * cosLatitude_,        // up
		};
	}

  private:
	double sinLatitude_;
	double cosLatitude_;
	double longitude_;
	double radialKm_;
	double axialKm_;
};

double length(const Cartesian &v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double horizonSignal(const Cartesian &horizontal, double distance) {
	// USNO convention: apparent upper limb on a level sea-level horizon.
	// Parallax is already included in the topocentric vector.
	const double sinRadius = kMoonRadiusKm / distance;
	const double cosRadius = std::sqrt(1 - sinRadius * sinRadius);
	return horizontal.z / distance + std::sin(kHorizonRefraction) * cosRadius +
	       std::cos(kHorizonRefraction) * sinRadius;
}

constexpr int64_t kEphemerisStep = 2 * TempoUtils::kSecondsPerHour;

// Sample the slow-moving geocentric vector every two hours. Cubic interpolation
// leaves Earth rotation at full time resolution while avoiding a periodic
// series evaluation for every scan and refinement point.
class LunarEphemeris {
  public:
	int64_t selectSegment(int64_t epoch) {
		const int64_t remainder = (epoch % kEphemerisStep + kEphemerisStep) % kEphemerisStep;
		const int64_t start = epoch - remainder;
		if (start == start_) {
			return start + kEphemerisStep;
		}
		if (start == start_ + kEphemerisStep) {
			samples_[0] = samples_[1];
			samples_[1] = samples_[2];
			samples_[2] = samples_[3];
			samples_[3] = moonEquatorial(start + 2 * kEphemerisStep);
		} else {
			for (size_t i = 0; i < samples_.size(); ++i) {
				samples_[i] = moonEquatorial(start + (static_cast<int64_t>(i) - 1) * kEphemerisStep);
			}
		}
		start_ = start;
		return start + kEphemerisStep;
	}

	Cartesian at(int64_t epoch) const {
		const double f = static_cast<double>(epoch - start_) / kEphemerisStep;
		const auto interpolate = [&](double Cartesian::*axis) {
			const double a = samples_[0].*axis;
			const double b = samples_[1].*axis;
			const double c = samples_[2].*axis;
			const double d = samples_[3].*axis;
			return b + 0.5 * f * (c - a + f * (2 * a - 5 * b + 4 * c - d +
			                                  f * (3 * (b - c) + d - a)));
		};
		return {interpolate(&Cartesian::x), interpolate(&Cartesian::y), interpolate(&Cartesian::z)};
	}

  private:
	int64_t start_ = std::numeric_limits<int64_t>::min();
	std::array<Cartesian, 4> samples_{};
};

struct HorizonSample {
	int64_t epoch;
	double signal;
	bool above() const { return signal >= 0.0; }
};

class HorizonScan {
  public:
	HorizonScan(float latitude, float longitude) : observer_(latitude, longitude) {
	}

	// Enumerate integer seconds at which the horizon state first changes.
	// Return early when the visitor has collected everything it needs.
	template <typename Visitor> void scan(int64_t begin, int64_t end, const Visitor &visitor) {
		int64_t segmentEnd = ephemeris_.selectSegment(begin - 1);
		HorizonSample left = sample(begin - 1);
		while (left.epoch < end - 1) {
			if (left.epoch == segmentEnd) {
				segmentEnd = ephemeris_.selectSegment(left.epoch);
			}
			const HorizonSample right = sample(std::min({left.epoch + 600, segmentEnd, end - 1}));
			if (crossings(left, right, visitor)) {
				return;
			}
			left = right;
		}
	}

	bool above(int64_t epoch) {
		ephemeris_.selectSegment(epoch);
		return sample(epoch).above();
	}

  private:
	HorizonSample sample(int64_t epoch) const {
		const Cartesian horizontal = observer_.horizontal(epoch, ephemeris_.at(epoch));
		return {epoch, horizonSignal(horizontal, length(horizontal))};
	}

	template <typename Visitor>
	bool crossings(HorizonSample left, HorizonSample right, const Visitor &visitor) const {
		const double duration = static_cast<double>(right.epoch - left.epoch);
		const bool changesSign = left.above() != right.above();
		// Conservative curvature bound for sin(elevation) over a short interval.
		// A chord can deviate by at most curvature * duration^2 / 8. Split
		// same-sign intervals near the horizon to catch short grazing events.
		constexpr double kCurvature = 1e-8; // per second squared
		if (changesSign &&
		    (duration <= 1 ||
		     std::abs(right.signal - left.signal) > kCurvature * duration * duration)) {
			// The slope cannot reverse in this bracket. Refine its single root.
			while (right.epoch - left.epoch > 1) {
				const HorizonSample middle = sample(left.epoch + (right.epoch - left.epoch) / 2);
				if (middle.above() == left.above()) {
					left = middle;
				} else {
					right = middle;
				}
			}
			return visitor(TempoMoonEvent{
			    right.above() ? TempoMoonEventType::Rise : TempoMoonEventType::Set,
			    DateTime{right.epoch},
			});
		}

		if (duration <= 1 ||
		    (!changesSign && std::min(std::abs(left.signal), std::abs(right.signal)) >
		                         kCurvature * duration * duration / 8)) {
			return false;
		}
		const HorizonSample middle = sample(left.epoch + (right.epoch - left.epoch) / 2);
		return crossings(left, middle, visitor) || crossings(middle, right, visitor);
	}

	Observer observer_;
	LunarEphemeris ephemeris_;
};

bool sameDate(const LocalDateTime &a, const tm &b) {
	return a.year == b.tm_year + 1900 && a.month == b.tm_mon + 1 && a.day == b.tm_mday;
}

bool localOffset(int64_t epoch, int64_t &offset) {
	tm local{};
	if (!TempoUtils::toLocalTm(DateTime{epoch}, local)) {
		return false;
	}
	offset = TempoUtils::timegm64(local) - epoch;
	return true;
}

using DaySpans = std::array<std::pair<int64_t, int64_t>, 4>;

bool localDaySpans(tm local, DaySpans &spans) {
	local.tm_hour = local.tm_min = local.tm_sec = 0;
	const int64_t midnight = TempoUtils::timegm64(local);
	const int64_t nextMidnight = midnight + kDay;
	// Intersect the wall-date interval with each constant-offset UTC segment.
	// This also covers a skipped/repeated midnight or a rollback into the
	// previous date, which can give a date two disjoint UTC intervals.
	constexpr int64_t kMargin = 26 * TempoUtils::kSecondsPerHour;
	const int64_t limit = nextMidnight + kMargin;
	int64_t segmentStart = midnight - kMargin;
	size_t spanCount = 0;
	const auto addSpan = [&](int64_t segmentEnd, int64_t offset) {
		const int64_t start = std::max(segmentStart, midnight - offset);
		const int64_t end = std::min(segmentEnd, nextMidnight - offset);
		if (start >= end) {
			return true;
		}
		if (spanCount && spans[spanCount - 1].second == start) {
			spans[spanCount - 1].second = end;
			return true;
		}
		if (spanCount == spans.size()) {
			return false;
		}
		spans[spanCount++] = {start, end};
		return true;
	};

	int64_t offset;
	if (!localOffset(segmentStart, offset)) {
		return false;
	}
	int64_t previous = segmentStart;
	while (previous < limit) {
		const int64_t probe = std::min(previous + TempoUtils::kSecondsPerHour, limit);
		int64_t probeOffset;
		if (!localOffset(probe, probeOffset)) {
			return false;
		}
		if (probeOffset != offset) {
			int64_t left = previous;
			int64_t right = probe;
			while (right - left > 1) {
				const int64_t mid = left + (right - left) / 2;
				int64_t midOffset;
				if (!localOffset(mid, midOffset)) {
					return false;
				}
				if (midOffset == offset) {
					left = mid;
				} else {
					right = mid;
				}
			}
			if (!addSpan(right, offset)) {
				return false;
			}
			segmentStart = right;
			offset = probeOffset;
		}
		previous = probe;
	}
	return addSpan(limit, offset) && spanCount != 0;
}
} // namespace

TempoMoonEventResult TempoMoonCycle::firstEvent(TempoMoonEventType type) const {
	if (valid) {
		for (size_t i = 0; i < eventCount && i < events.size(); ++i) {
			if (events[i].type == type) {
				return {true, events[i].utc};
			}
		}
	}
	return {};
}

TempoMoonPosition Tempo::moonPosition() const {
	return moonPositionAt(nowUtc(), true);
}

TempoMoonPosition Tempo::moonPosition(const DateTime &atUtc) const {
	return moonPositionAt(atUtc, false);
}

TempoMoonPosition Tempo::moonPositionAt(const DateTime &atUtc, bool requireValidClock) const {
	float latitude;
	float longitude;
	{
		std::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);
		if (!initialized_ || !hasLocation_ ||
		    (requireValidClock && atUtc.epochSeconds < minValidUnixSeconds_)) {
			return {};
		}
		latitude = latitude_;
		longitude = longitude_;
	}
	return moonPosition(latitude, longitude, atUtc);
}

TempoMoonPosition Tempo::moonPosition(
    float latitude, float longitude, const DateTime &atUtc
) const {
	if (!supportedEpoch(atUtc.epochSeconds) || !validCoordinates(latitude, longitude)) {
		return {};
	}
	const Cartesian moon = moonEquatorial(atUtc.epochSeconds);
	const Cartesian horizontal = Observer(latitude, longitude).horizontal(atUtc.epochSeconds, moon);
	const double distance = length(horizontal);
	const double horizontalDistance = std::hypot(horizontal.x, horizontal.y);
	double azimuth = 0.0;
	if (horizontalDistance > distance * 1e-12) {
		azimuth = std::atan2(horizontal.x, horizontal.y) / kRadians;
		if (azimuth < 0.0) {
			azimuth += 360.0;
		}
	}
	return {
	    true,
	    azimuth,
	    std::atan2(horizontal.z, horizontalDistance) / kRadians,
	    distance,
	    horizonSignal(horizontal, distance) >= 0.0,
	};
}

TempoMoonCycle Tempo::moonCycleToday() {
	return moonCycleFor(nowUtc(), true);
}

TempoMoonCycle Tempo::moonCycle(const DateTime &atUtc) const {
	return moonCycleFor(atUtc, false);
}

TempoMoonCycle Tempo::moonCycleFor(const DateTime &atUtc, bool useCache) const {
	if (!supportedEpoch(atUtc.epochSeconds)) {
		return {};
	}

	TempoMoonCycle cycle{};
	DaySpans spans{};
	float latitude;
	float longitude;
	uint32_t revision;
	{
		std::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);
		if (!initialized_ || !hasLocation_ || (useCache && atUtc.epochSeconds < minValidUnixSeconds_)) {
			return {};
		}
		TempoUtils::ScopedTz zone(timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_);
		tm local{};
		if (!TempoUtils::toLocalTm(atUtc, local)) {
			return {};
		}
		const bool cachedDate = useCache && moonCycleCache_.valid &&
		                        sameDate(moonCycleCache_.calculatedForLocalDate, local);
		if (cachedDate && !timeZone_.empty()) {
			return moonCycleCache_;
		}
		if (!localDaySpans(local, spans)) {
			return {};
		}
		if (cachedDate && spans == moonCycleCacheSpans_) {
			return moonCycleCache_;
		}
		cycle.calculatedForLocalDate = toLocal(DateTime{spans.front().first});
		if (!cycle.calculatedForLocalDate.ok) {
			return {};
		}
		latitude = latitude_;
		longitude = longitude_;
		revision = locationRevision_;
	}

	HorizonScan scan(latitude, longitude);
	bool allAbove = true;
	bool allBelow = true;
	bool overflow = false;
	for (const auto &[begin, end] : spans) {
		if (begin == end) {
			break;
		}
		const bool above = scan.above(begin);
		allAbove &= above;
		allBelow &= !above;
		scan.scan(begin, end, [&](const TempoMoonEvent &event) {
			if (cycle.eventCount == cycle.events.size()) {
				overflow = true;
				return true;
			}
			cycle.events[cycle.eventCount++] = event;
			return false;
		});
		if (overflow) {
			return {};
		}
	}
	cycle.valid = true;
	cycle.alwaysAboveHorizon = cycle.eventCount == 0 && allAbove;
	cycle.alwaysBelowHorizon = cycle.eventCount == 0 && allBelow;

	if (useCache) {
		std::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);
		if (revision == locationRevision_) {
			moonCycleCache_ = cycle;
			moonCycleCacheSpans_ = spans;
		}
	}
	return cycle;
}

bool Tempo::nextMoonEvent(TempoMoonEventType type, const DateTime &fromUtc, DateTime &out) const {
	if (!supportedEpoch(fromUtc.epochSeconds)) {
		return false;
	}
	float latitude;
	float longitude;
	{
		std::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);
		if (!initialized_ || !hasLocation_) {
			return false;
		}
		latitude = latitude_;
		longitude = longitude_;
	}

	// Even at the poles the declination crosses the horizon over a lunar
	// month. Use a bounded search and leave the schedule dormant on failure.
	constexpr int64_t kSearchSeconds = 32 * kDay;
	const int64_t end = std::min(fromUtc.epochSeconds + kSearchSeconds, kEndSupportedEpoch);
	bool found = false;
	HorizonScan(latitude, longitude).scan(fromUtc.epochSeconds, end, [&](const TempoMoonEvent &event) {
		if (event.type != type) {
			return false;
		}
		out = event.utc;
		found = true;
		return true;
	});
	return found;
}
