#!/usr/bin/env python3
from pathlib import Path

path = Path("src/internal/tempo_scheduler/schedule/schedule_calculator.cpp")
text = path.read_text(encoding="utf-8")

old_sun = '''\tfor (int64_t dayOffset = 0; dayOffset < kMaxSunSearchDays; ++dayOffset) {
\t\tconst TempoSunEventResult cycle = sunrise ? date.sunrise(cursor) : date.sunset(cursor);
\t\tif (!cycle.ok) {
\t\t\tcontinue;
\t\t}

\t\tconst DateTime candidate = date.addMinutes(cycle.value, spec.sunOffsetMinutes);
\t\tif (date.isBefore(candidate, rounded)) {
\t\t\tcontinue;
\t\t}
\t\toutNextUtc = candidate;
\t\treturn true;
\t\tcursor = date.nextDailyAtLocal(0, 0, 0, date.addSeconds(cursor, 1));
\t}
'''
new_sun = '''\tfor (int64_t dayOffset = 0; dayOffset < kMaxSunSearchDays; ++dayOffset) {
\t\tconst TempoSunEventResult cycle = sunrise ? date.sunrise(cursor) : date.sunset(cursor);
\t\tif (cycle.ok) {
\t\t\tconst DateTime candidate = date.addMinutes(cycle.value, spec.sunOffsetMinutes);
\t\t\tif (!date.isBefore(candidate, rounded)) {
\t\t\t\toutNextUtc = candidate;
\t\t\t\treturn true;
\t\t\t}
\t\t}
\t\tcursor = date.nextDailyAtLocal(0, 0, 0, date.addSeconds(cursor, 1));
\t}
'''
if text.count(old_sun) != 1:
    raise RuntimeError("sun search block did not match")
text = text.replace(old_sun, new_sun, 1)

old_fine = '''\t\t\tDateTime fine = date.addMinutes(current, -kCoarseStepMinutes + 1);
\t\t\tdouble finePrevious = previousUnwrapped;
\t\t\tfor (int minute = 1; minute <= kCoarseStepMinutes; ++minute) {'''
new_fine = '''\t\t\tDateTime fine = date.addMinutes(current, -kCoarseStepMinutes);
\t\t\tdouble finePrevious = previousUnwrapped;
\t\t\tfor (int minute = 0; minute <= kCoarseStepMinutes; ++minute) {'''
if text.count(old_fine) != 1:
    raise RuntimeError("moon phase refinement block did not match")
text = text.replace(old_fine, new_fine, 1)

old_illum = '''\t\t\tDateTime fine = date.addMinutes(current, -kCoarseStepMinutes + 1);
\t\t\tdouble finePrevious = previousIllumination;
\t\t\tfor (int minute = 1; minute <= kCoarseStepMinutes; ++minute) {'''
new_illum = '''\t\t\tDateTime fine = date.addMinutes(current, -kCoarseStepMinutes);
\t\t\tdouble finePrevious = previousIllumination;
\t\t\tfor (int minute = 0; minute <= kCoarseStepMinutes; ++minute) {'''
if text.count(old_illum) != 1:
    raise RuntimeError("moon illumination refinement block did not match")
text = text.replace(old_illum, new_illum, 1)

path.write_text(text, encoding="utf-8")
Path("scripts/fix_schedule_refinement.py").unlink(missing_ok=True)
Path(".github/workflows/fix-schedule-refinement.yml").unlink(missing_ok=True)
