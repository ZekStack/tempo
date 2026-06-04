#pragma once

#include "../../tempo_date/date.h"

#include "schedule_spec.h"

class ScheduleCalculator {
  public:
	static bool validate(const TempoSchedule &spec);
	static bool computeNext(
	    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
	);
};
