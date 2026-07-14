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
    "src/internal/tempo_date/date.h",
    '''\tuint8_t sunCycleCalculationHour = 4;
\tuint8_t sunCycleCalculationMinute = 0;
\tuint32_t sunCycleMatchWindowSeconds = 60;
\tuint32_t taskStackSize = 6848;
\tUBaseType_t taskPriority = 1;
\tBaseType_t taskCoreId = tskNO_AFFINITY;
\tconst char *taskName = "tempo-task";
''',
    '''\tuint32_t sunCycleMatchWindowSeconds = 60;
''',
)
replace_once(
    "src/internal/tempo_date/date.h",
    "\tvoid setSunCycleCalculationTime(uint8_t hour, uint8_t minute);\n",
    "",
)
replace_once(
    "src/internal/tempo_date/date.h",
    '''\tuint8_t sunCycleCalculationHour_ = 4;
\tuint8_t sunCycleCalculationMinute_ = 0;
\tuint32_t sunCycleMatchWindowSeconds_ = 60;
''',
    '''\tuint32_t sunCycleMatchWindowSeconds_ = 60;
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''\tif (config.sunCycleCalculationHour > 23 || config.sunCycleCalculationMinute > 59) {
\t\treturn TempoResult::failure(TempoStatus::InvalidArgument, "invalid sun calculation time");
\t}
''',
    "",
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''\tsunCycleCalculationHour_ = config.sunCycleCalculationHour;
\tsunCycleCalculationMinute_ = config.sunCycleCalculationMinute;
\tsunCycleMatchWindowSeconds_ = config.sunCycleMatchWindowSeconds;
''',
    '''\tsunCycleMatchWindowSeconds_ = config.sunCycleMatchWindowSeconds;
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''void Tempo::setSunCycleCalculationTime(uint8_t hour, uint8_t minute) {
\tsunCycleCalculationHour_ = hour;
\tsunCycleCalculationMinute_ = minute;
}

''',
    "",
)

Path("scripts/remove_inert_tempo_config.py").unlink(missing_ok=True)
Path(".github/workflows/remove-inert-tempo-config.yml").unlink(missing_ok=True)
