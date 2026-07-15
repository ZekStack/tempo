#include "schedule_spec.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
bool parseNumber(const char *text, int &out) {
	if (!text || *text == '\0') {
		return false;
	}
	char *end = nullptr;
	long value = std::strtol(text, &end, 10);
	if (end == text || *end != '\0') {
		return false;
	}
	out = static_cast<int>(value);
	return true;
}

bool addCronPart(const std::string &part, int minValue, int maxValue, bool allowed[64]) {
	if (part.empty()) {
		return false;
	}

	std::string base = part;
	int step = 1;
	const size_t slash = base.find('/');
	if (slash != std::string::npos) {
		if (!parseNumber(base.c_str() + slash + 1, step) || step <= 0) {
			return false;
		}
		base = base.substr(0, slash);
	}

	int from = minValue;
	int to = maxValue;
	if (base != "*") {
		const size_t dash = base.find('-');
		if (dash == std::string::npos) {
			if (!parseNumber(base.c_str(), from)) {
				return false;
			}
			to = from;
		} else {
			std::string left = base.substr(0, dash);
			std::string right = base.substr(dash + 1);
			if (!parseNumber(left.c_str(), from) || !parseNumber(right.c_str(), to)) {
				return false;
			}
		}
	}

	if (from < minValue || to > maxValue || from > to) {
		return false;
	}
	for (int value = from; value <= to; value += step) {
		allowed[value] = true;
	}
	return true;
}

ScheduleField parseCronField(const char *field, int minValue, int maxValue) {
	if (!field || *field == '\0') {
		return ScheduleField{};
	}
	if (std::strcmp(field, "*") == 0) {
		return ScheduleField::any();
	}

	bool allowed[64] = {};
	std::string text(field);
	size_t start = 0;
	while (start <= text.size()) {
		size_t comma = text.find(',', start);
		std::string part = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
		if (!addCronPart(part, minValue, maxValue, allowed)) {
			return ScheduleField{};
		}
		if (comma == std::string::npos) {
			break;
		}
		start = comma + 1;
	}

	int values[64] = {};
	size_t count = 0;
	for (int value = minValue; value <= maxValue; ++value) {
		if (allowed[value]) {
			values[count++] = value;
		}
	}
	return ScheduleField::list(values, count);
}

int moonPhaseAngleForName(MoonPhaseName name) {
	switch (name) {
	case MoonPhaseName::NewMoon:
		return 0;
	case MoonPhaseName::WaxingCrescent:
		return 45;
	case MoonPhaseName::FirstQuarter:
		return 90;
	case MoonPhaseName::WaxingGibbous:
		return 135;
	case MoonPhaseName::FullMoon:
		return 180;
	case MoonPhaseName::WaningGibbous:
		return 225;
	case MoonPhaseName::LastQuarter:
		return 270;
	case MoonPhaseName::WaningCrescent:
		return 315;
	default:
		return 0;
	}
}
} // namespace

TempoSchedule TempoSchedule::onceUtc(const DateTime &whenUtc) {
	TempoSchedule spec;
	spec.kind = ScheduleKind::OneShotUtc;
	spec.isOneShot = true;
	spec.onceAtUtc = whenUtc;
	return spec;
}

TempoSchedule TempoSchedule::everyMinutes(int minutes) {
	TempoSchedule spec;
	if (minutes <= 0 || minutes > 60) {
		spec.minute = ScheduleField{};
		return spec;
	}
	spec.minute = ScheduleField::rangeEvery(0, 59, minutes);
	return spec;
}

TempoSchedule TempoSchedule::everyHours(int hours) {
	TempoSchedule spec;
	if (hours <= 0 || hours > 24) {
		spec.hour = ScheduleField{};
		return spec;
	}
	spec.minute = ScheduleField::only(0);
	spec.hour = ScheduleField::rangeEvery(0, 23, hours);
	return spec;
}

TempoSchedule TempoSchedule::dailyAt(int hour, int minute) {
	return dailyAtLocal(hour, minute);
}

TempoSchedule TempoSchedule::dailyAtLocal(int hour, int minute) {
	TempoSchedule spec;
	spec.hour = ScheduleField::only(hour);
	spec.minute = ScheduleField::only(minute);
	return spec;
}

TempoSchedule TempoSchedule::weeklyAtLocal(uint8_t dowMask, int hour, int minute) {
	int days[7];
	size_t count = 0;
	for (int bit = 0; bit < 7; ++bit) {
		if ((dowMask & (1 << bit)) != 0) {
			days[count++] = bit;
		}
	}

	TempoSchedule spec;
	spec.hour = ScheduleField::only(hour);
	spec.minute = ScheduleField::only(minute);
	spec.dayOfWeek = count == 0 ? ScheduleField{} : ScheduleField::list(days, count);
	return spec;
}

TempoSchedule TempoSchedule::atDays(uint8_t dowMask, int hour, int minute) {
	return weeklyAtLocal(dowMask, hour, minute);
}

TempoSchedule TempoSchedule::monthlyOnDayLocal(int dayOfMonth, int hour, int minute) {
	TempoSchedule spec;
	if (dayOfMonth < 1 || dayOfMonth > 31) {
		spec.dayOfMonth = ScheduleField{};
		return spec;
	}
	spec.dayOfMonth = ScheduleField::only(dayOfMonth);
	spec.hour = ScheduleField::only(hour);
	spec.minute = ScheduleField::only(minute);
	return spec;
}

TempoSchedule TempoSchedule::sunrise(int offsetMinutes) {
	TempoSchedule spec;
	spec.kind = ScheduleKind::Sunrise;
	spec.sunOffsetMinutes = offsetMinutes;
	return spec;
}

TempoSchedule TempoSchedule::sunRise(const TempoDuration &offset) {
	return sunrise(static_cast<int>(offset.seconds() / 60));
}

TempoSchedule TempoSchedule::sunset(int offsetMinutes) {
	TempoSchedule spec;
	spec.kind = ScheduleKind::Sunset;
	spec.sunOffsetMinutes = offsetMinutes;
	return spec;
}

TempoSchedule TempoSchedule::sunSet(const TempoDuration &offset) {
	return sunset(static_cast<int>(offset.seconds() / 60));
}

TempoSchedule TempoSchedule::cron(const char *expression) {
	TempoSchedule spec;
	if (!expression) {
		spec.minute = ScheduleField{};
		return spec;
	}

	char buffer[96];
	if (std::strlen(expression) >= sizeof(buffer)) {
		spec.minute = ScheduleField{};
		return spec;
	}
	std::strncpy(buffer, expression, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	const char *fields[5] = {};
	size_t count = 0;
	char *cursor = buffer;
	while (*cursor != '\0' && count < 5) {
		while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
			++cursor;
		}
		if (*cursor == '\0') {
			break;
		}
		fields[count++] = cursor;
		while (*cursor != '\0' && !std::isspace(static_cast<unsigned char>(*cursor))) {
			++cursor;
		}
		if (*cursor != '\0') {
			*cursor++ = '\0';
		}
	}
	while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
		++cursor;
	}
	if (count != 5 || *cursor != '\0') {
		spec.minute = ScheduleField{};
		return spec;
	}

	spec.minute = parseCronField(fields[0], 0, 59);
	spec.hour = parseCronField(fields[1], 0, 23);
	spec.dayOfMonth = parseCronField(fields[2], 1, 31);
	spec.month = parseCronField(fields[3], 1, 12);
	spec.dayOfWeek = parseCronField(fields[4], 0, 6);
	return spec;
}

TempoSchedule TempoSchedule::moonPhaseAngle(int angleDegrees, int toleranceDegrees) {
	TempoSchedule spec;
	spec.kind = ScheduleKind::MoonPhaseAngle;
	spec.moonPhaseAngleDegrees = angleDegrees;
	spec.moonPhaseToleranceDegrees = toleranceDegrees;
	return spec;
}

TempoSchedule TempoSchedule::moonPhase(MoonPhaseName name, int toleranceDegrees) {
	return moonPhaseAngle(moonPhaseAngleForName(name), toleranceDegrees);
}

TempoSchedule TempoSchedule::moonIlluminationPercent(double percent, double tolerancePercent) {
	TempoSchedule spec;
	spec.kind = ScheduleKind::MoonIlluminationPercent;
	spec.moonIlluminationTargetPercent = percent;
	spec.moonIlluminationTolerancePercent = tolerancePercent;
	return spec;
}

TempoSchedule TempoSchedule::moonIllumination(double percent, double tolerancePercent) {
	return moonIlluminationPercent(percent, tolerancePercent);
}

TempoSchedule TempoSchedule::custom(
    const ScheduleField &minute,
    const ScheduleField &hour,
    const ScheduleField &dom,
    const ScheduleField &month,
    const ScheduleField &dow
) {
	TempoSchedule spec;
	spec.minute = minute;
	spec.hour = hour;
	spec.dayOfMonth = dom;
	spec.month = month;
	spec.dayOfWeek = dow;
	return spec;
}
