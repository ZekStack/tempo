#pragma once

#include "../../tempo_date/date.h"

#include "schedule_field.h"

enum class ScheduleKind : uint8_t {
	Cron = 0,
	OneShotUtc,
	Sunrise,
	Sunset,
	MoonPhaseAngle,
	MoonIlluminationPercent,
};

enum class MoonPhaseName : uint8_t {
	NewMoon = 0,
	WaxingCrescent,
	FirstQuarter,
	WaxingGibbous,
	FullMoon,
	WaningGibbous,
	LastQuarter,
	WaningCrescent,
};

struct TempoSchedule {
	ScheduleKind kind = ScheduleKind::Cron;
	bool isOneShot = false;
	DateTime onceAtUtc{};

	ScheduleField minute = ScheduleField::any();
	ScheduleField hour = ScheduleField::any();
	ScheduleField dayOfMonth = ScheduleField::any();
	ScheduleField month = ScheduleField::any();
	ScheduleField dayOfWeek = ScheduleField::any();

	int sunOffsetMinutes = 0;
	int moonPhaseAngleDegrees = 0;
	int moonPhaseToleranceDegrees = 1;
	double moonIlluminationTargetPercent = 0.0;
	double moonIlluminationTolerancePercent = 0.5;

	static TempoSchedule onceUtc(const DateTime &whenUtc);
	static TempoSchedule everyMinutes(int minutes);
	static TempoSchedule everyHours(int hours);
	static TempoSchedule dailyAt(int hour, int minute);
	static TempoSchedule dailyAtLocal(int hour, int minute);
	static TempoSchedule atDays(uint8_t dowMask, int hour, int minute);
	static TempoSchedule weeklyAtLocal(uint8_t dowMask, int hour, int minute);
	static TempoSchedule monthlyOnDayLocal(int dayOfMonth, int hour, int minute);
	static TempoSchedule cron(const char *expression);
	static TempoSchedule sunRise(const TempoDuration &offset = TempoDuration{});
	static TempoSchedule sunSet(const TempoDuration &offset = TempoDuration{});
	static TempoSchedule sunrise(int offsetMinutes = 0);
	static TempoSchedule sunset(int offsetMinutes = 0);
	static TempoSchedule moonIllumination(double percent, double tolerancePercent = 0.5);
	static TempoSchedule moonPhaseAngle(int angleDegrees, int toleranceDegrees = 1);
	static TempoSchedule moonPhase(MoonPhaseName name, int toleranceDegrees = 1);
	static TempoSchedule moonIlluminationPercent(double percent, double tolerancePercent = 0.5);
	static TempoSchedule custom(
	    const ScheduleField &minute,
	    const ScheduleField &hour,
	    const ScheduleField &dom,
	    const ScheduleField &month,
	    const ScheduleField &dow
	);
};
