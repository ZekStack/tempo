#include "schedule_calculator.h"

#include <cmath>

namespace {
constexpr int64_t kMaxSearchMinutes = 366 * 24 * 60;
constexpr int64_t kMaxSunSearchDays = 732;
constexpr int64_t kMaxMoonSearchMinutes = 62 * 24 * 60;
constexpr int kMinSunOffsetMinutes = -1440;
constexpr int kMaxSunOffsetMinutes = 1440;
constexpr int kMinMoonPhaseAngle = 0;
constexpr int kMaxMoonPhaseAngle = 359;
constexpr int kMaxMoonPhaseTolerance = 30;
constexpr double kMinIlluminationPercent = 0.0;
constexpr double kMaxIlluminationPercent = 100.0;
constexpr double kMaxIlluminationTolerancePercent = 50.0;
constexpr double kFullCircleDegrees = 360.0;
constexpr double kComparisonEpsilon = 1e-9;

ScheduleKind resolvedScheduleKind(const TempoSchedule &spec) {
	if (spec.kind == ScheduleKind::Cron && spec.isOneShot) {
		return ScheduleKind::OneShotUtc;
	}
	return spec.kind;
}

DateTime roundToNextMinute(Tempo &date, const DateTime &fromUtc) {
	DateTime rounded = fromUtc;
	if (fromUtc.secondUtc() > 0) {
		rounded = date.addMinutes(rounded, 1);
	}
	return date.setTimeOfDayUtc(rounded, rounded.hourUtc(), rounded.minuteUtc(), 0);
}

uint64_t allowedMask(int min, int max) {
	if (min < 0) {
		min = 0;
	}
	if (max > 63) {
		max = 63;
	}
	if (max >= 63) {
		return ~static_cast<uint64_t>(0);
	}
	const uint64_t upper = (1ULL << (max + 1)) - 1;
	const uint64_t lower = min == 0 ? 0 : ((1ULL << min) - 1);
	return upper & ~lower;
}

bool fieldWithinRange(const ScheduleField &field, int min, int max) {
	if (field.isAny()) {
		return true;
	}
	const uint64_t mask = field.rawMask();
	const uint64_t allowed = allowedMask(min, max);
	return mask != 0 && (mask & ~allowed) == 0;
}

double normalizeAngle360(double angle) {
	double normalized = std::fmod(angle, kFullCircleDegrees);
	if (normalized < 0.0) {
		normalized += kFullCircleDegrees;
	}
	return normalized;
}

double unwrapAngle(double previousUnwrapped, double currentWrapped) {
	const double previousWrapped = normalizeAngle360(previousUnwrapped);
	double delta = currentWrapped - previousWrapped;
	if (delta > 180.0) {
		delta -= kFullCircleDegrees;
	} else if (delta < -180.0) {
		delta += kFullCircleDegrees;
	}
	return previousUnwrapped + delta;
}

bool valueWithinPeriodicWindow(double value, double center, double tolerance) {
	const double distance = std::fabs(value - center);
	double wrapped = std::fmod(distance, kFullCircleDegrees);
	if (wrapped < 0.0) {
		wrapped += kFullCircleDegrees;
	}
	const double minimumDistance = std::min(wrapped, kFullCircleDegrees - wrapped);
	return minimumDistance <= tolerance + kComparisonEpsilon;
}

bool segmentIntersectsRange(double a, double b, double minValue, double maxValue) {
	const double lo = std::min(a, b);
	const double hi = std::max(a, b);
	return hi >= minValue - kComparisonEpsilon && lo <= maxValue + kComparisonEpsilon;
}

bool segmentIntersectsPeriodicWindow(double a, double b, double center, double tolerance) {
	const double lo = std::min(a, b);
	const double hi = std::max(a, b);
	const int64_t firstPeriod =
	    static_cast<int64_t>(std::floor((lo - (center + tolerance)) / kFullCircleDegrees)) - 1;
	const int64_t lastPeriod =
	    static_cast<int64_t>(std::ceil((hi - (center - tolerance)) / kFullCircleDegrees)) + 1;
	for (int64_t period = firstPeriod; period <= lastPeriod; ++period) {
		const double shift = static_cast<double>(period) * kFullCircleDegrees;
		const double minWindow = center - tolerance + shift;
		const double maxWindow = center + tolerance + shift;
		if (segmentIntersectsRange(lo, hi, minWindow, maxWindow)) {
			return true;
		}
	}
	return false;
}

bool computeNextCronOccurrence(
    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
) {
	const DateTime rounded = roundToNextMinute(date, fromUtc);
	DateTime dayCursor = date.startOfDayLocal(rounded);
	for (int dayIndex = 0; dayIndex <= 366; ++dayIndex) {
		const int month = date.getMonthLocal(dayCursor);
		const int day = date.getDayLocal(dayCursor);
		const int dayOfWeek = date.getWeekdayLocal(dayCursor);
		const bool domAny = spec.dayOfMonth.isAny();
		const bool dowAny = spec.dayOfWeek.isAny();
		const bool domOk = spec.dayOfMonth.matches(day);
		const bool dowOk = spec.dayOfWeek.matches(dayOfWeek);
		const bool dayOk = (domAny && dowAny) || (domAny && dowOk) || (dowAny && domOk) ||
		                   (!domAny && !dowAny && (domOk || dowOk));

		if (spec.month.matches(month) && dayOk) {
			for (int hour = 0; hour < 24; ++hour) {
				if (!spec.hour.matches(hour)) {
					continue;
				}
				for (int minute = 0; minute < 60; ++minute) {
					if (!spec.minute.matches(minute)) {
						continue;
					}
					const DateTime candidate = date.setTimeOfDayLocal(dayCursor, hour, minute, 0);
					const LocalDateTime resolved = date.toLocal(candidate);
					if (!resolved.ok || resolved.year != date.getYearLocal(dayCursor) ||
					    resolved.month != month || resolved.day != day || resolved.hour != hour ||
					    resolved.minute != minute || date.isBefore(candidate, rounded)) {
						continue;
					}
					outNextUtc = candidate;
					return true;
				}
			}
		}
		dayCursor = date.nextDailyAtLocal(0, 0, 0, date.addSeconds(dayCursor, 1));
	}
	return false;
}

bool computeNextSunOccurrence(
    Tempo &date,
    const TempoSchedule &spec,
    const DateTime &fromUtc,
    DateTime &outNextUtc,
    bool sunrise
) {
	const DateTime rounded = roundToNextMinute(date, fromUtc);
	DateTime cursor = date.startOfDayLocal(rounded);
	for (int64_t dayOffset = 0; dayOffset < kMaxSunSearchDays; ++dayOffset) {
		const TempoSunEventResult cycle = sunrise ? date.sunrise(cursor) : date.sunset(cursor);
		if (cycle.ok) {
			const DateTime candidate = date.addMinutes(cycle.value, spec.sunOffsetMinutes);
			if (!date.isBefore(candidate, rounded)) {
				outNextUtc = candidate;
				return true;
			}
		}
		cursor = date.nextDailyAtLocal(0, 0, 0, date.addSeconds(cursor, 1));
	}
	return false;
}

bool computeNextMoonPhaseOccurrence(
    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
) {
	const DateTime rounded = roundToNextMinute(date, fromUtc);
	DateTime previous = date.addMinutes(rounded, -1);
	DateTime current = rounded;

	TempoMoonPhase previousPhase = date.moonPhase(previous);
	if (!previousPhase.ok) {
		return false;
	}
	double previousUnwrapped = static_cast<double>(previousPhase.angleDegrees);

	constexpr int kCoarseStepMinutes = 60;
	for (int64_t elapsed = 0; elapsed < kMaxMoonSearchMinutes; elapsed += kCoarseStepMinutes) {
		current = date.addMinutes(rounded, elapsed + kCoarseStepMinutes);
		TempoMoonPhase currentPhase = date.moonPhase(current);
		if (!currentPhase.ok) {
			return false;
		}
		const double currentUnwrapped =
		    unwrapAngle(previousUnwrapped, static_cast<double>(currentPhase.angleDegrees));
		if (!valueWithinPeriodicWindow(previousUnwrapped, spec.moonPhaseAngleDegrees,
		                               spec.moonPhaseToleranceDegrees) &&
		    segmentIntersectsPeriodicWindow(previousUnwrapped, currentUnwrapped,
		                                    spec.moonPhaseAngleDegrees,
		                                    spec.moonPhaseToleranceDegrees)) {
			DateTime fine = date.addMinutes(current, -kCoarseStepMinutes);
			double finePrevious = previousUnwrapped;
			for (int minute = 0; minute <= kCoarseStepMinutes; ++minute) {
				TempoMoonPhase finePhase = date.moonPhase(fine);
				if (!finePhase.ok) {
					return false;
				}
				const double fineCurrent =
				    unwrapAngle(finePrevious, static_cast<double>(finePhase.angleDegrees));
				if (!valueWithinPeriodicWindow(finePrevious, spec.moonPhaseAngleDegrees,
				                               spec.moonPhaseToleranceDegrees) &&
				    segmentIntersectsPeriodicWindow(finePrevious, fineCurrent,
				                                    spec.moonPhaseAngleDegrees,
				                                    spec.moonPhaseToleranceDegrees)) {
					outNextUtc = fine;
					return true;
				}
				finePrevious = fineCurrent;
				fine = date.addMinutes(fine, 1);
			}
		}
		previousUnwrapped = currentUnwrapped;
	}
	return false;
}

bool computeNextMoonIlluminationOccurrence(
    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
) {
	const DateTime rounded = roundToNextMinute(date, fromUtc);
	DateTime previous = date.addMinutes(rounded, -1);
	DateTime current = rounded;

	TempoMoonPhase previousPhase = date.moonPhase(previous);
	if (!previousPhase.ok) {
		return false;
	}
	double previousIllumination = previousPhase.illumination * 100.0;

	const double minWindow =
	    spec.moonIlluminationTargetPercent - spec.moonIlluminationTolerancePercent;
	const double maxWindow =
	    spec.moonIlluminationTargetPercent + spec.moonIlluminationTolerancePercent;
	constexpr int kCoarseStepMinutes = 60;
	for (int64_t elapsed = 0; elapsed < kMaxMoonSearchMinutes; elapsed += kCoarseStepMinutes) {
		current = date.addMinutes(rounded, elapsed + kCoarseStepMinutes);
		TempoMoonPhase currentPhase = date.moonPhase(current);
		if (!currentPhase.ok) {
			return false;
		}
		const double currentIllumination = currentPhase.illumination * 100.0;
		const bool wasInside = previousIllumination >= minWindow - kComparisonEpsilon &&
		                       previousIllumination <= maxWindow + kComparisonEpsilon;
		if (!wasInside && segmentIntersectsRange(previousIllumination, currentIllumination,
		                                         minWindow, maxWindow)) {
			DateTime fine = date.addMinutes(current, -kCoarseStepMinutes);
			double finePrevious = previousIllumination;
			for (int minute = 0; minute <= kCoarseStepMinutes; ++minute) {
				TempoMoonPhase finePhase = date.moonPhase(fine);
				if (!finePhase.ok) {
					return false;
				}
				const double fineCurrent = finePhase.illumination * 100.0;
				const bool fineWasInside = finePrevious >= minWindow - kComparisonEpsilon &&
				                           finePrevious <= maxWindow + kComparisonEpsilon;
				if (!fineWasInside &&
				    segmentIntersectsRange(finePrevious, fineCurrent, minWindow, maxWindow)) {
					outNextUtc = fine;
					return true;
				}
				finePrevious = fineCurrent;
				fine = date.addMinutes(fine, 1);
			}
		}
		previousIllumination = currentIllumination;
	}
	return false;
}
} // namespace

bool ScheduleCalculator::validate(const TempoSchedule &spec) {
	switch (resolvedScheduleKind(spec)) {
	case ScheduleKind::OneShotUtc:
		return true;
	case ScheduleKind::Cron:
		return fieldWithinRange(spec.minute, 0, 59) && fieldWithinRange(spec.hour, 0, 23) &&
		       fieldWithinRange(spec.dayOfMonth, 1, 31) && fieldWithinRange(spec.month, 1, 12) &&
		       fieldWithinRange(spec.dayOfWeek, 0, 6);
	case ScheduleKind::Sunrise:
	case ScheduleKind::Sunset:
		return spec.sunOffsetMinutes >= kMinSunOffsetMinutes &&
		       spec.sunOffsetMinutes <= kMaxSunOffsetMinutes;
	case ScheduleKind::MoonPhaseAngle:
		return spec.moonPhaseAngleDegrees >= kMinMoonPhaseAngle &&
		       spec.moonPhaseAngleDegrees <= kMaxMoonPhaseAngle &&
		       spec.moonPhaseToleranceDegrees >= 0 &&
		       spec.moonPhaseToleranceDegrees <= kMaxMoonPhaseTolerance;
	case ScheduleKind::MoonIlluminationPercent:
		return std::isfinite(spec.moonIlluminationTargetPercent) &&
		       std::isfinite(spec.moonIlluminationTolerancePercent) &&
		       spec.moonIlluminationTargetPercent >= kMinIlluminationPercent &&
		       spec.moonIlluminationTargetPercent <= kMaxIlluminationPercent &&
		       spec.moonIlluminationTolerancePercent > 0.0 &&
		       spec.moonIlluminationTolerancePercent <= kMaxIlluminationTolerancePercent;
	default:
		return false;
	}
}

bool ScheduleCalculator::computeNext(
    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
) {
	switch (resolvedScheduleKind(spec)) {
	case ScheduleKind::OneShotUtc:
		outNextUtc = spec.onceAtUtc;
		return true;
	case ScheduleKind::Cron:
		return computeNextCronOccurrence(date, spec, fromUtc, outNextUtc);
	case ScheduleKind::Sunrise:
		return computeNextSunOccurrence(date, spec, fromUtc, outNextUtc, true);
	case ScheduleKind::Sunset:
		return computeNextSunOccurrence(date, spec, fromUtc, outNextUtc, false);
	case ScheduleKind::MoonPhaseAngle:
		return computeNextMoonPhaseOccurrence(date, spec, fromUtc, outNextUtc);
	case ScheduleKind::MoonIlluminationPercent:
		return computeNextMoonIlluminationOccurrence(date, spec, fromUtc, outNextUtc);
	default:
		return false;
	}
}
