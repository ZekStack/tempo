#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}\n--- old ---\n{old}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def inject_timezone_scope(signature: str) -> None:
    scope = (
        signature
        + "\n\tUtils::ScopedTz scopedTimeZone(\n"
        + "\t    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_\n"
        + "\t);"
    )
    replace_once("src/internal/tempo_date/date.cpp", signature, scope)


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
\tTempoSunCycle sunCycleCache_{};
''',
    '''\tuint32_t sunCycleMatchWindowSeconds_ = 60;
\tmutable std::recursive_mutex sunCycleMutex_{};
\tTempoSunCycle sunCycleCache_{};
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

for signature in (
    "DateTime Tempo::fromLocal(int year, int month, int day, int hour, int minute, int second) const {",
    "DateTime Tempo::startOfDayLocal(const DateTime &dt) const {",
    "DateTime Tempo::addCalendarDaysLocal(const DateTime &dt, int days) const {",
    "DateTime Tempo::startOfMonthLocal(const DateTime &dt) const {",
    "DateTime Tempo::endOfMonthLocal(const DateTime &dt) const {",
    "DateTime Tempo::startOfYearLocal(const DateTime &dt) const {",
    "DateTime Tempo::setTimeOfDayLocal(const DateTime &dt, int hour, int minute, int second) const {",
    "int Tempo::getYearLocal(const DateTime &dt) const {",
    "int Tempo::getMonthLocal(const DateTime &dt) const {",
    "int Tempo::getDayLocal(const DateTime &dt) const {",
    "int Tempo::getWeekdayLocal(const DateTime &dt) const {",
    "Tempo::ParseResult Tempo::parseDateTimeLocal(const char *str) const {",
):
    inject_timezone_scope(signature)

inject_timezone_scope(
    '''bool Tempo::formatWithPatternLocal(
    const DateTime &dt, const char *pattern, char *outBuffer, size_t outSize
) const {'''
)

replace_once(
    "src/internal/tempo_date/date.cpp",
    '''bool Tempo::dateTimeToStringLocal(
    const DateTime &dt, char *outBuffer, size_t outSize, TempoFormat style
) const {
\treturn dt.localString(outBuffer, outSize, style);
}
''',
    '''bool Tempo::dateTimeToStringLocal(
    const DateTime &dt, char *outBuffer, size_t outSize, TempoFormat style
) const {
\treturn formatLocal(dt, style, outBuffer, outSize);
}
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''bool Tempo::lastNtpSyncStringUtc(char *outBuffer, size_t outSize, TempoFormat style) const {
\tif (!hasLastNtpSync_) {
\t\treturn false;
\t}
\treturn dateTimeToStringUtc(lastNtpSync_, outBuffer, outSize, style);
}

bool Tempo::lastNtpSyncStringLocal(char *outBuffer, size_t outSize, TempoFormat style) const {
\tif (!hasLastNtpSync_) {
\t\treturn false;
\t}
\treturn dateTimeToStringLocal(lastNtpSync_, outBuffer, outSize, style);
}
''',
    '''bool Tempo::lastNtpSyncStringUtc(char *outBuffer, size_t outSize, TempoFormat style) const {
\tif (!hasLastNtpSync()) {
\t\treturn false;
\t}
\treturn dateTimeToStringUtc(lastNtpSync(), outBuffer, outSize, style);
}

bool Tempo::lastNtpSyncStringLocal(char *outBuffer, size_t outSize, TempoFormat style) const {
\tif (!hasLastNtpSync()) {
\t\treturn false;
\t}
\treturn dateTimeToStringLocal(lastNtpSync(), outBuffer, outSize, style);
}
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''std::string Tempo::lastNtpSyncStringUtc(TempoFormat style) const {
\tif (!hasLastNtpSync_) {
\t\treturn std::string();
\t}
\treturn dateTimeToStringUtc(lastNtpSync_, style);
}

std::string Tempo::lastNtpSyncStringLocal(TempoFormat style) const {
\tif (!hasLastNtpSync_) {
\t\treturn std::string();
\t}
\treturn dateTimeToStringLocal(lastNtpSync_, style);
}
''',
    '''std::string Tempo::lastNtpSyncStringUtc(TempoFormat style) const {
\tif (!hasLastNtpSync()) {
\t\treturn std::string();
\t}
\treturn dateTimeToStringUtc(lastNtpSync(), style);
}

std::string Tempo::lastNtpSyncStringLocal(TempoFormat style) const {
\tif (!hasLastNtpSync()) {
\t\treturn std::string();
\t}
\treturn dateTimeToStringLocal(lastNtpSync(), style);
}
''',
)
replace_once(
    "src/internal/tempo_date/sun.cpp",
    "bool Tempo::refreshSunCycleCache(const DateTime &day) {",
    "bool Tempo::refreshSunCycleCache(const DateTime &day) {\n\tstd::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);",
)
replace_once(
    "src/internal/tempo_date/sun.cpp",
    "TempoSunCycle Tempo::sunCycleFor(const DateTime &day) {",
    "TempoSunCycle Tempo::sunCycleFor(const DateTime &day) {\n\tstd::lock_guard<std::recursive_mutex> lock(sunCycleMutex_);",
)

Path("scripts/remove_inert_tempo_config.py").unlink(missing_ok=True)
Path(".github/workflows/remove-inert-tempo-config.yml").unlink(missing_ok=True)
