#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}\n--- old ---\n{old}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/internal/tempo_scheduler/scheduler_result.h",
    "\tInvalidSchedule,\n\tNoMemory,",
    "\tInvalidSchedule,\n\tInvalidConfiguration,\n\tNoMemory,",
)

replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_spec.cpp",
    "#include <cstdlib>\n",
    "#include <cctype>\n#include <cstdlib>\n",
)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_spec.cpp",
    '''TempoSchedule TempoSchedule::everyMinutes(int minutes) {
\tTempoSchedule spec;
\tspec.minute = ScheduleField::every(minutes);
\treturn spec;
}

TempoSchedule TempoSchedule::everyHours(int hours) {
\tTempoSchedule spec;
\tspec.minute = ScheduleField::only(0);
\tspec.hour = ScheduleField::every(hours);
\treturn spec;
}
''',
    '''TempoSchedule TempoSchedule::everyMinutes(int minutes) {
\tTempoSchedule spec;
\tif (minutes <= 0 || minutes > 60) {
\t\tspec.minute = ScheduleField{};
\t\treturn spec;
\t}
\tspec.minute = ScheduleField::rangeEvery(0, 59, minutes);
\treturn spec;
}

TempoSchedule TempoSchedule::everyHours(int hours) {
\tTempoSchedule spec;
\tif (hours <= 0 || hours > 24) {
\t\tspec.hour = ScheduleField{};
\t\treturn spec;
\t}
\tspec.minute = ScheduleField::only(0);
\tspec.hour = ScheduleField::rangeEvery(0, 23, hours);
\treturn spec;
}
''',
)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_spec.cpp",
    "\tspec.dayOfWeek = count == 0 ? ScheduleField::any() : ScheduleField::list(days, count);",
    "\tspec.dayOfWeek = count == 0 ? ScheduleField{} : ScheduleField::list(days, count);",
)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_spec.cpp",
    '''\tTempoSchedule spec;
\tif (dayOfMonth < 1) {
\t\tdayOfMonth = 1;
\t} else if (dayOfMonth > 31) {
\t\tdayOfMonth = 31;
\t}
\tspec.dayOfMonth = ScheduleField::only(dayOfMonth);
''',
    '''\tTempoSchedule spec;
\tif (dayOfMonth < 1 || dayOfMonth > 31) {
\t\tspec.dayOfMonth = ScheduleField{};
\t\treturn spec;
\t}
\tspec.dayOfMonth = ScheduleField::only(dayOfMonth);
''',
)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_spec.cpp",
    '''\tchar buffer[96];
\tstd::strncpy(buffer, expression, sizeof(buffer) - 1);
\tbuffer[sizeof(buffer) - 1] = '\\0';
''',
    '''\tchar buffer[96];
\tif (std::strlen(expression) >= sizeof(buffer)) {
\t\tspec.minute = ScheduleField{};
\t\treturn spec;
\t}
\tstd::strncpy(buffer, expression, sizeof(buffer) - 1);
\tbuffer[sizeof(buffer) - 1] = '\\0';
''',
)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_spec.cpp",
    '''\twhile (*cursor != '\\0' && count < 5) {
\t\twhile (*cursor == ' ') {
\t\t\t++cursor;
\t\t}
\t\tif (*cursor == '\\0') {
\t\t\tbreak;
\t\t}
\t\tfields[count++] = cursor;
\t\twhile (*cursor != '\\0' && *cursor != ' ') {
\t\t\t++cursor;
\t\t}
\t\tif (*cursor == ' ') {
\t\t\t*cursor++ = '\\0';
\t\t}
\t}
\tif (count != 5) {
''',
    '''\twhile (*cursor != '\\0' && count < 5) {
\t\twhile (*cursor != '\\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
\t\t\t++cursor;
\t\t}
\t\tif (*cursor == '\\0') {
\t\t\tbreak;
\t\t}
\t\tfields[count++] = cursor;
\t\twhile (*cursor != '\\0' && !std::isspace(static_cast<unsigned char>(*cursor))) {
\t\t\t++cursor;
\t\t}
\t\tif (*cursor != '\\0') {
\t\t\t*cursor++ = '\\0';
\t\t}
\t}
\twhile (*cursor != '\\0' && std::isspace(static_cast<unsigned char>(*cursor))) {
\t\t++cursor;
\t}
\tif (count != 5 || *cursor != '\\0') {
''',
)

replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_calculator.cpp",
    "\treturn mask != 0 && (mask & allowed) != 0;",
    "\treturn mask != 0 && (mask & ~allowed) == 0;",
)

cron_old = '''bool computeNextCronOccurrence(
    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
) {
\tDateTime cursor = roundToNextMinute(date, fromUtc);
\tfor (int64_t minuteIndex = 0; minuteIndex < kMaxSearchMinutes; ++minuteIndex) {
\t\tconst int month = date.getMonthLocal(cursor);
\t\tconst int day = date.getDayLocal(cursor);
\t\tconst int dayOfWeek = date.getWeekdayLocal(cursor);

\t\tconst DateTime startOfDay = date.startOfDayLocal(cursor);
\t\tconst int64_t minutesIntoDay = date.differenceInMinutes(cursor, startOfDay);
\t\tif (minutesIntoDay < 0) {
\t\t\tcursor = date.addMinutes(cursor, 1);
\t\t\tcontinue;
\t\t}

\t\tconst int hour = static_cast<int>(minutesIntoDay / 60);
\t\tconst int minute = static_cast<int>(minutesIntoDay % 60);

\t\tconst bool domAny = spec.dayOfMonth.isAny();
\t\tconst bool dowAny = spec.dayOfWeek.isAny();
\t\tconst bool domOk = spec.dayOfMonth.matches(day);
\t\tconst bool dowOk = spec.dayOfWeek.matches(dayOfWeek);

\t\tbool dayOk = false;
\t\tif (domAny && dowAny) {
\t\t\tdayOk = true;
\t\t} else if (domAny) {
\t\t\tdayOk = dowOk;
\t\t} else if (dowAny) {
\t\t\tdayOk = domOk;
\t\t} else {
\t\t\tdayOk = domOk || dowOk;
\t\t}

\t\tif (spec.month.matches(month) && spec.hour.matches(hour) && spec.minute.matches(minute) &&
\t\t    dayOk) {
\t\t\toutNextUtc = date.setTimeOfDayLocal(cursor, hour, minute, 0);
\t\t\treturn true;
\t\t}
\t\tcursor = date.addMinutes(cursor, 1);
\t}
\treturn false;
}
'''
cron_new = '''bool computeNextCronOccurrence(
    Tempo &date, const TempoSchedule &spec, const DateTime &fromUtc, DateTime &outNextUtc
) {
\tconst DateTime rounded = roundToNextMinute(date, fromUtc);
\tDateTime dayCursor = date.startOfDayLocal(rounded);
\tfor (int dayIndex = 0; dayIndex <= 366; ++dayIndex) {
\t\tconst int month = date.getMonthLocal(dayCursor);
\t\tconst int day = date.getDayLocal(dayCursor);
\t\tconst int dayOfWeek = date.getWeekdayLocal(dayCursor);
\t\tconst bool domAny = spec.dayOfMonth.isAny();
\t\tconst bool dowAny = spec.dayOfWeek.isAny();
\t\tconst bool domOk = spec.dayOfMonth.matches(day);
\t\tconst bool dowOk = spec.dayOfWeek.matches(dayOfWeek);
\t\tconst bool dayOk = (domAny && dowAny) || (domAny && dowOk) || (dowAny && domOk) ||
\t\t                   (!domAny && !dowAny && (domOk || dowOk));

\t\tif (spec.month.matches(month) && dayOk) {
\t\t\tfor (int hour = 0; hour < 24; ++hour) {
\t\t\t\tif (!spec.hour.matches(hour)) {
\t\t\t\t\tcontinue;
\t\t\t\t}
\t\t\t\tfor (int minute = 0; minute < 60; ++minute) {
\t\t\t\t\tif (!spec.minute.matches(minute)) {
\t\t\t\t\t\tcontinue;
\t\t\t\t\t}
\t\t\t\t\tconst DateTime candidate = date.setTimeOfDayLocal(dayCursor, hour, minute, 0);
\t\t\t\t\tconst LocalDateTime resolved = date.toLocal(candidate);
\t\t\t\t\tif (!resolved.ok || resolved.year != date.getYearLocal(dayCursor) ||
\t\t\t\t\t    resolved.month != month || resolved.day != day || resolved.hour != hour ||
\t\t\t\t\t    resolved.minute != minute || date.isBefore(candidate, rounded)) {
\t\t\t\t\t\tcontinue;
\t\t\t\t\t}
\t\t\t\t\toutNextUtc = candidate;
\t\t\t\t\treturn true;
\t\t\t\t}
\t\t\t}
\t\t}
\t\tdayCursor = date.nextDailyAtLocal(0, 0, 0, date.addSeconds(dayCursor, 1));
\t}
\treturn false;
}
'''
replace_once("src/internal/tempo_scheduler/schedule/schedule_calculator.cpp", cron_old, cron_new)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_calculator.cpp",
    '''\tconst DateTime rounded = roundToNextMinute(date, fromUtc);
\tconst DateTime startOfDay = date.startOfDayLocal(rounded);
\tfor (int64_t dayOffset = 0; dayOffset < kMaxSunSearchDays; ++dayOffset) {
\t\tconst DateTime cursor = date.addDays(startOfDay, static_cast<int32_t>(dayOffset));
''',
    '''\tconst DateTime rounded = roundToNextMinute(date, fromUtc);
\tDateTime cursor = date.startOfDayLocal(rounded);
\tfor (int64_t dayOffset = 0; dayOffset < kMaxSunSearchDays; ++dayOffset) {
''',
)
replace_once(
    "src/internal/tempo_scheduler/schedule/schedule_calculator.cpp",
    '''\t\toutNextUtc = candidate;
\t\treturn true;
\t}
\treturn false;
}

bool computeNextMoonPhaseOccurrence''',
    '''\t\toutNextUtc = candidate;
\t\treturn true;
\t\tcursor = date.nextDailyAtLocal(0, 0, 0, date.addSeconds(cursor, 1));
\t}
\treturn false;
}

bool computeNextMoonPhaseOccurrence''',
)

moon_phase_old = '''\tfor (int64_t minuteIndex = 0; minuteIndex < kMaxMoonSearchMinutes; ++minuteIndex) {
\t\tTempoMoonPhase currentPhase = date.moonPhase(current);
\t\tif (!currentPhase.ok) {
\t\t\treturn false;
\t\t}

\t\tconst double currentUnwrapped =
\t\t    unwrapAngle(previousUnwrapped, static_cast<double>(currentPhase.angleDegrees));
\t\tconst bool crossed = !valueWithinPeriodicWindow(
\t\t                         previousUnwrapped,
\t\t                         static_cast<double>(spec.moonPhaseAngleDegrees),
\t\t                         static_cast<double>(spec.moonPhaseToleranceDegrees)
\t\t                     ) &&
\t\t                     segmentIntersectsPeriodicWindow(
\t\t                         previousUnwrapped,
\t\t                         currentUnwrapped,
\t\t                         static_cast<double>(spec.moonPhaseAngleDegrees),
\t\t                         static_cast<double>(spec.moonPhaseToleranceDegrees)
\t\t                     );
\t\tif (crossed) {
\t\t\toutNextUtc = current;
\t\t\treturn true;
\t\t}

\t\tpreviousUnwrapped = currentUnwrapped;
\t\tcurrent = date.addMinutes(current, 1);
\t}
'''
moon_phase_new = '''\tconstexpr int kCoarseStepMinutes = 60;
\tfor (int64_t elapsed = 0; elapsed < kMaxMoonSearchMinutes; elapsed += kCoarseStepMinutes) {
\t\tcurrent = date.addMinutes(rounded, elapsed + kCoarseStepMinutes);
\t\tTempoMoonPhase currentPhase = date.moonPhase(current);
\t\tif (!currentPhase.ok) {
\t\t\treturn false;
\t\t}
\t\tconst double currentUnwrapped =
\t\t    unwrapAngle(previousUnwrapped, static_cast<double>(currentPhase.angleDegrees));
\t\tif (!valueWithinPeriodicWindow(previousUnwrapped, spec.moonPhaseAngleDegrees,
\t\t                               spec.moonPhaseToleranceDegrees) &&
\t\t    segmentIntersectsPeriodicWindow(previousUnwrapped, currentUnwrapped,
\t\t                                    spec.moonPhaseAngleDegrees,
\t\t                                    spec.moonPhaseToleranceDegrees)) {
\t\t\tDateTime fine = date.addMinutes(current, -kCoarseStepMinutes + 1);
\t\t\tdouble finePrevious = previousUnwrapped;
\t\t\tfor (int minute = 1; minute <= kCoarseStepMinutes; ++minute) {
\t\t\t\tTempoMoonPhase finePhase = date.moonPhase(fine);
\t\t\t\tif (!finePhase.ok) {
\t\t\t\t\treturn false;
\t\t\t\t}
\t\t\t\tconst double fineCurrent =
\t\t\t\t    unwrapAngle(finePrevious, static_cast<double>(finePhase.angleDegrees));
\t\t\t\tif (!valueWithinPeriodicWindow(finePrevious, spec.moonPhaseAngleDegrees,
\t\t\t\t                               spec.moonPhaseToleranceDegrees) &&
\t\t\t\t    segmentIntersectsPeriodicWindow(finePrevious, fineCurrent,
\t\t\t\t                                    spec.moonPhaseAngleDegrees,
\t\t\t\t                                    spec.moonPhaseToleranceDegrees)) {
\t\t\t\t\toutNextUtc = fine;
\t\t\t\t\treturn true;
\t\t\t\t}
\t\t\t\tfinePrevious = fineCurrent;
\t\t\t\tfine = date.addMinutes(fine, 1);
\t\t\t}
\t\t}
\t\tpreviousUnwrapped = currentUnwrapped;
\t}
'''
replace_once("src/internal/tempo_scheduler/schedule/schedule_calculator.cpp", moon_phase_old, moon_phase_new)

moon_illum_old = '''\tfor (int64_t minuteIndex = 0; minuteIndex < kMaxMoonSearchMinutes; ++minuteIndex) {
\t\tTempoMoonPhase currentPhase = date.moonPhase(current);
\t\tif (!currentPhase.ok) {
\t\t\treturn false;
\t\t}

\t\tconst double currentIllumination = currentPhase.illumination * 100.0;
\t\tconst double minWindow =
\t\t    spec.moonIlluminationTargetPercent - spec.moonIlluminationTolerancePercent;
\t\tconst double maxWindow =
\t\t    spec.moonIlluminationTargetPercent + spec.moonIlluminationTolerancePercent;
\t\tconst bool wasInside = previousIllumination >= minWindow - kComparisonEpsilon &&
\t\t                       previousIllumination <= maxWindow + kComparisonEpsilon;
\t\tif (!wasInside && segmentIntersectsRange(
\t\t                      previousIllumination,
\t\t                      currentIllumination,
\t\t                      minWindow,
\t\t                      maxWindow
\t\t                  )) {
\t\t\toutNextUtc = current;
\t\t\treturn true;
\t\t}

\t\tpreviousIllumination = currentIllumination;
\t\tcurrent = date.addMinutes(current, 1);
\t}
'''
moon_illum_new = '''\tconst double minWindow =
\t    spec.moonIlluminationTargetPercent - spec.moonIlluminationTolerancePercent;
\tconst double maxWindow =
\t    spec.moonIlluminationTargetPercent + spec.moonIlluminationTolerancePercent;
\tconstexpr int kCoarseStepMinutes = 60;
\tfor (int64_t elapsed = 0; elapsed < kMaxMoonSearchMinutes; elapsed += kCoarseStepMinutes) {
\t\tcurrent = date.addMinutes(rounded, elapsed + kCoarseStepMinutes);
\t\tTempoMoonPhase currentPhase = date.moonPhase(current);
\t\tif (!currentPhase.ok) {
\t\t\treturn false;
\t\t}
\t\tconst double currentIllumination = currentPhase.illumination * 100.0;
\t\tconst bool wasInside = previousIllumination >= minWindow - kComparisonEpsilon &&
\t\t                       previousIllumination <= maxWindow + kComparisonEpsilon;
\t\tif (!wasInside && segmentIntersectsRange(previousIllumination, currentIllumination,
\t\t                                         minWindow, maxWindow)) {
\t\t\tDateTime fine = date.addMinutes(current, -kCoarseStepMinutes + 1);
\t\t\tdouble finePrevious = previousIllumination;
\t\t\tfor (int minute = 1; minute <= kCoarseStepMinutes; ++minute) {
\t\t\t\tTempoMoonPhase finePhase = date.moonPhase(fine);
\t\t\t\tif (!finePhase.ok) {
\t\t\t\t\treturn false;
\t\t\t\t}
\t\t\t\tconst double fineCurrent = finePhase.illumination * 100.0;
\t\t\t\tconst bool fineWasInside = finePrevious >= minWindow - kComparisonEpsilon &&
\t\t\t\t                           finePrevious <= maxWindow + kComparisonEpsilon;
\t\t\t\tif (!fineWasInside &&
\t\t\t\t    segmentIntersectsRange(finePrevious, fineCurrent, minWindow, maxWindow)) {
\t\t\t\t\toutNextUtc = fine;
\t\t\t\t\treturn true;
\t\t\t\t}
\t\t\t\tfinePrevious = fineCurrent;
\t\t\t\tfine = date.addMinutes(fine, 1);
\t\t\t}
\t\t}
\t\tpreviousIllumination = currentIllumination;
\t}
'''
replace_once("src/internal/tempo_scheduler/schedule/schedule_calculator.cpp", moon_illum_old, moon_illum_new)

replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    "namespace {\ntemplate <typename TCommand, typename TResult, typename FBuild>",
    '''namespace {
bool validSchedulerConfig(const SchedulerConfig &config) {
\treturn config.service.commandQueueDepth > 0 && config.service.eventQueueDepth > 0 &&
\t       config.service.taskStackSize > 0 && config.service.controlTimeoutMs > 0 &&
\t       config.defaultWorkerPool.workerCount > 0 &&
\t       config.defaultWorkerPool.queueDepth > 0 && config.defaultWorkerPool.stackSize > 0 &&
\t       config.defaultDedicatedTask.stackSize > 0;
}

template <typename TCommand, typename TResult, typename FBuild>''',
)
replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''SchedulerResult<void> TempoScheduler::init(Tempo &date, const SchedulerConfig &config) {
\tif (impl_ && impl_->started) {
''',
    '''SchedulerResult<void> TempoScheduler::init(Tempo &date, const SchedulerConfig &config) {
\tif (!validSchedulerConfig(config)) {
\t\treturn SchedulerResult<void>::failure(SchedulerError::InvalidConfiguration);
\t}
\tif (impl_ && impl_->started) {
''',
)
replace_once(
    "src/internal/tempo_scheduler/scheduler.cpp",
    '''\tif (!impl_) {
\t\treturn false;
\t}
\tif (impl_->started) {
''',
    '''\tif (!impl_ || !validSchedulerConfig(impl_->config)) {
\t\treturn false;
\t}
\tif (impl_->started) {
''',
)

Path("scripts/apply_v010_schedule_hardening.py").unlink(missing_ok=True)
Path(".github/workflows/apply-v010-schedule-hardening.yml").unlink(missing_ok=True)
Path("SCHEDULE_HARDENING_ERROR.txt").unlink(missing_ok=True)
