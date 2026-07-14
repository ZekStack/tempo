#!/usr/bin/env python3
from pathlib import Path


def write(path: str, content: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}\n--- old ---\n{old}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/internal/tempo_date/date.h",
    "#include <initializer_list>\n#include <stdint.h>",
    "#include <initializer_list>\n#include <limits>\n#include <mutex>\n#include <stdint.h>",
)
replace_once(
    "src/internal/tempo_date/date.h",
    "\tint offsetMinutes = 0; // local - UTC\n\tDateTime utc{};",
    "\tint offsetMinutes = 0; // local - UTC\n\tDateTime utc{};\n\tbool hasResolvedUtc = false;",
)
replace_once(
    "src/internal/tempo_date/date.h",
    "\tfloat latitude = 0.0f;\n\tfloat longitude = 0.0f;",
    "\tfloat latitude = std::numeric_limits<float>::quiet_NaN();\n\tfloat longitude = std::numeric_limits<float>::quiet_NaN();",
)
replace_once(
    "src/internal/tempo_date/date.h",
    "\tbool isDayWithOffsets(const DateTime &day, int sunRiseOffsetSec, int sunSetOffsetSec) const;\n\tbool refreshSunCycleCache(const DateTime &day);",
    "\tbool isDayWithOffsets(const DateTime &day, int sunRiseOffsetSec, int sunSetOffsetSec) const;\n\tTempoSunCycle sunCycleFor(const DateTime &day);\n\tbool refreshSunCycleCache(const DateTime &day);\n\tDateTime addCalendarDaysLocal(const DateTime &dt, int days) const;",
)
replace_once(
    "src/internal/tempo_date/date.h",
    "\tstatic Tempo *activeNtpSyncOwner_;\n\tbool hasLocation_ = false;",
    "\tstatic Tempo *activeNtpSyncOwner_;\n\tstatic std::recursive_mutex ntpMutex_;\n\tbool hasLocation_ = false;",
)

replace_once(
    "src/internal/tempo_date/utils.h",
    "#include <limits>\n",
    "#include <limits>\n#include <mutex>\n",
)
replace_once(
    "src/internal/tempo_date/utils.h",
    '''\tclass ScopedTz {
\t  public:
\t\texplicit ScopedTz(const char *tz, bool usePSRAMBuffers = false)
\t\t    : previous_(DateAllocator<char>(usePSRAMBuffers)) {
''',
    '''\tclass TimezoneLock {
\t  public:
\t\tTimezoneLock() : lock_(timezoneMutex()) {
\t\t}

\t  private:
\t\tstd::unique_lock<std::recursive_mutex> lock_;
\t};

\tclass ScopedTz {
\t  public:
\t\texplicit ScopedTz(const char *tz, bool usePSRAMBuffers = false)
\t\t    : lock_(timezoneMutex()), previous_(DateAllocator<char>(usePSRAMBuffers)) {
''',
)
replace_once(
    "src/internal/tempo_date/utils.h",
    "\t  private:\n\t\tbool active_ = false;",
    "\t  private:\n\t\tstd::unique_lock<std::recursive_mutex> lock_;\n\t\tbool active_ = false;",
)
replace_once(
    "src/internal/tempo_date/utils.h",
    '''\tstatic bool toLocalTm(const DateTime &dt, tm &out) {
\t\tif (dt.epochSeconds > static_cast<int64_t>(std::numeric_limits<time_t>::max()) ||
''',
    '''\tstatic bool toLocalTm(const DateTime &dt, tm &out) {
\t\tTimezoneLock lock;
\t\tif (dt.epochSeconds > static_cast<int64_t>(std::numeric_limits<time_t>::max()) ||
''',
)
replace_once(
    "src/internal/tempo_date/utils.h",
    '''\tstatic DateTime fromLocalTm(const tm &t) {
\t\ttm copy = t;
''',
    '''\tstatic DateTime fromLocalTm(const tm &t) {
\t\tTimezoneLock lock;
\t\ttm copy = t;
''',
)
replace_once(
    "src/internal/tempo_date/utils.h",
    '''\tstatic bool
\tisDstActiveFor(const DateTime &dt, const char *timeZone, bool usePSRAMBuffers = false) {
''',
    '''\tstatic void setProcessTimeZone(const char *timeZone) {
\t\tTimezoneLock lock;
\t\tconst char *tz = (timeZone && timeZone[0] != '\\0') ? timeZone : "UTC0";
#if defined(_WIN32)
\t\t_putenv_s("TZ", tz);
\t\t_tzset();
#else
\t\tsetenv("TZ", tz, 1);
\t\ttzset();
#endif
\t}

\tstatic bool
\tisDstActiveFor(const DateTime &dt, const char *timeZone, bool usePSRAMBuffers = false) {
''',
)
replace_once(
    "src/internal/tempo_date/utils.h",
    "  private:\n\tstatic int64_t daysFromCivil",
    "  private:\n\tstatic std::recursive_mutex &timezoneMutex() {\n\t\tstatic std::recursive_mutex mutex;\n\t\treturn mutex;\n\t}\n\n\tstatic int64_t daysFromCivil",
)

replace_once(
    "src/internal/tempo_date/date.cpp",
    "#include <cstdio>\n",
    "#include <cmath>\n#include <cstdio>\n",
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    "Tempo *Tempo::activeNtpSyncOwner_ = nullptr;\n",
    "Tempo *Tempo::activeNtpSyncOwner_ = nullptr;\nstd::recursive_mutex Tempo::ntpMutex_{};\n",
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''void Tempo::handleSntpSync(struct timeval *tv) {
\tint64_t syncedEpoch = static_cast<int64_t>(time(nullptr));
''',
    '''void Tempo::handleSntpSync(struct timeval *tv) {
\tstd::lock_guard<std::recursive_mutex> lock(ntpMutex_);
\tint64_t syncedEpoch = static_cast<int64_t>(time(nullptr));
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''TempoResult Tempo::init(const TempoConfig &config) {
\tif (initialized_) {
\t\treturn TempoResult::failure(TempoStatus::AlreadyInitialized, "tempo already initialized");
\t}
\tapplyConfig(config);
''',
    '''TempoResult Tempo::init(const TempoConfig &config) {
\tif (initialized_) {
\t\treturn TempoResult::failure(TempoStatus::AlreadyInitialized, "tempo already initialized");
\t}
\tconst bool hasLatitude = std::isfinite(config.latitude);
\tconst bool hasLongitude = std::isfinite(config.longitude);
\tif (hasLatitude != hasLongitude ||
\t    (hasLatitude && (config.latitude < -90.0f || config.latitude > 90.0f ||
\t                     config.longitude < -180.0f || config.longitude > 180.0f))) {
\t\treturn TempoResult::failure(TempoStatus::InvalidArgument, "invalid location configuration");
\t}
\tif (config.sunCycleCalculationHour > 23 || config.sunCycleCalculationMinute > 59) {
\t\treturn TempoResult::failure(TempoStatus::InvalidArgument, "invalid sun calculation time");
\t}
\tapplyConfig(config);
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''void Tempo::deinit() {
\tntpSyncCallback_ = nullptr;
''',
    '''void Tempo::deinit() {
\tstd::lock_guard<std::recursive_mutex> lock(ntpMutex_);
\tntpSyncCallback_ = nullptr;
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''\tlatitude_ = config.latitude;
\tlongitude_ = config.longitude;
\thasLocation_ = true;
''',
    '''\tlatitude_ = config.latitude;
\tlongitude_ = config.longitude;
\thasLocation_ = std::isfinite(latitude_) && std::isfinite(longitude_);
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''\tif (!applyNtpConfig() && hasTz) {
\t\tsetenv("TZ", timeZone_.c_str(), 1);
\t\ttzset();
\t}
''',
    '''\tif (!applyNtpConfig() && hasTz) {
\t\tUtils::setProcessTimeZone(timeZone_.c_str());
\t}
''',
)
for signature in [
    "void Tempo::setNtpSyncCallback(NtpSyncCallback callback) {",
    "void Tempo::setNtpSyncCallbackCallable(const NtpSyncCallable &callback) {",
    "bool Tempo::hasLastNtpSync() const {",
    "DateTime Tempo::lastNtpSync() const {",
    "Tempo::NtpSyncListenerId Tempo::addNtpSyncListener(const NtpSyncCallable &listener) {",
    "bool Tempo::removeNtpSyncListener(NtpSyncListenerId id) {",
    "void Tempo::dispatchNtpSync(const DateTime &syncedAtUtc) {",
]:
    replace_once(
        "src/internal/tempo_date/date.cpp",
        signature,
        signature + "\n\tstd::lock_guard<std::recursive_mutex> lock(ntpMutex_);",
    )
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''\tconst char *tz = timeZone_.empty() ? "UTC0" : timeZone_.c_str();
\tconst char *ntpServer1 = ntpServers_[0].empty() ? nullptr : ntpServers_[0].c_str();
''',
    '''\tUtils::TimezoneLock timezoneLock;
\tconst char *tz = timeZone_.empty() ? "UTC0" : timeZone_.c_str();
\tconst char *ntpServer1 = ntpServers_[0].empty() ? nullptr : ntpServers_[0].c_str();
''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    "\tresult.utc = dt;\n\treturn result;",
    "\tresult.utc = dt;\n\tresult.hasResolvedUtc = true;\n\treturn result;",
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''DateTime Tempo::toUtc(const LocalDateTime &dt) const {
\tif (!dt.ok) {
\t\treturn DateTime{};
\t}
\treturn fromLocal(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}
''',
    '''DateTime Tempo::toUtc(const LocalDateTime &dt) const {
\tif (!dt.ok) {
\t\treturn DateTime{};
\t}
\tif (dt.hasResolvedUtc) {
\t\treturn dt.utc;
\t}
\treturn fromLocal(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}
''',
)

replace_once(
    "src/internal/tempo_date/date.cpp",
    '''DateTime Tempo::startOfDayLocal(const DateTime &dt) const {
\ttm t{};
\tif (!Utils::toLocalTm(dt, t)) {
\t\treturn dt;
\t}
\tt.tm_hour = 0;
\tt.tm_min = 0;
\tt.tm_sec = 0;
\treturn Utils::fromLocalTm(t);
}

DateTime Tempo::endOfDayLocal(const DateTime &dt) const {
\treturn addSeconds(startOfDayLocal(dt), Utils::kSecondsPerDay - 1);
}
''',
    '''DateTime Tempo::startOfDayLocal(const DateTime &dt) const {
\ttm t{};
\tif (!Utils::toLocalTm(dt, t)) {
\t\treturn dt;
\t}
\tt.tm_hour = 0;
\tt.tm_min = 0;
\tt.tm_sec = 0;
\tt.tm_isdst = -1;
\treturn Utils::fromLocalTm(t);
}

DateTime Tempo::addCalendarDaysLocal(const DateTime &dt, int days) const {
\ttm t{};
\tif (!Utils::toLocalTm(dt, t)) {
\t\treturn dt;
\t}
\tt.tm_mday += days;
\tt.tm_isdst = -1;
\treturn Utils::fromLocalTm(t);
}

DateTime Tempo::endOfDayLocal(const DateTime &dt) const {
\tconst DateTime start = startOfDayLocal(dt);
\treturn subSeconds(addCalendarDaysLocal(start, 1), 1);
}
''',
)
for old, new in [
    ("\tt.tm_sec = 0;\n\treturn Utils::fromLocalTm(t);\n}\n\nDateTime Tempo::endOfMonthLocal", "\tt.tm_sec = 0;\n\tt.tm_isdst = -1;\n\treturn Utils::fromLocalTm(t);\n}\n\nDateTime Tempo::endOfMonthLocal"),
    ("\tt.tm_mon += 1;\n\tDateTime nextMonth", "\tt.tm_mon += 1;\n\tt.tm_isdst = -1;\n\tDateTime nextMonth"),
    ("\tt.tm_sec = 0;\n\treturn Utils::fromLocalTm(t);\n}\n\nDateTime Tempo::setTimeOfDayLocal", "\tt.tm_sec = 0;\n\tt.tm_isdst = -1;\n\treturn Utils::fromLocalTm(t);\n}\n\nDateTime Tempo::setTimeOfDayLocal"),
    ("\tt.tm_sec = second;\n\treturn Utils::fromLocalTm(t);\n}\n\nDateTime Tempo::setTimeOfDayUtc", "\tt.tm_sec = second;\n\tt.tm_isdst = -1;\n\treturn Utils::fromLocalTm(t);\n}\n\nDateTime Tempo::setTimeOfDayUtc"),
    ("\tDateTime nextDay = addDays(from, 1);", "\tDateTime nextDay = addCalendarDaysLocal(from, 1);"),
    ("\tDateTime candidateDay = addDays(from, daysAhead);", "\tDateTime candidateDay = addCalendarDaysLocal(from, daysAhead);"),
    ("candidate = setTimeOfDayLocal(addDays(from, 7), hour, minute, second);", "candidate = setTimeOfDayLocal(addCalendarDaysLocal(from, 7), hour, minute, second);"),
]:
    replace_once("src/internal/tempo_date/date.cpp", old, new)

replace_once(
    "src/internal/tempo_date/date.cpp",
    "!Utils::parseIntSlice(str + 17, 2, 0, 60, second)",
    "!Utils::parseIntSlice(str + 17, 2, 0, 59, second)",
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    "!Utils::parseIntSlice(str + 17, 2, 0, 60, second)",
    "!Utils::parseIntSlice(str + 17, 2, 0, 59, second)",
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''\tresult.ok = true;
\tresult.value = Utils::fromLocalTm(t);
\treturn result;
}

DateTime Tempo::parseUtc''',
    '''\tresult.value = Utils::fromLocalTm(t);
\tconst LocalDateTime resolved = toLocal(result.value);
\tif (!resolved.ok || resolved.year != year || resolved.month != month || resolved.day != day ||
\t    resolved.hour != hour || resolved.minute != minute || resolved.second != second) {
\t\treturn ParseResult{false, DateTime{}};
\t}
\tresult.ok = true;
\treturn result;
}

DateTime Tempo::parseUtc''',
)
replace_once(
    "src/internal/tempo_date/date.cpp",
    '''LocalDateTime Tempo::parseLocal(const char *str) const {
\treturn toLocal(parseDateTimeLocal(str).value);
}
''',
    '''LocalDateTime Tempo::parseLocal(const char *str) const {
\tconst ParseResult parsed = parseDateTimeLocal(str);
\tif (!parsed.ok) {
\t\treturn LocalDateTime{};
\t}
\treturn toLocal(parsed.value);
}
''',
)

replace_once(
    "src/internal/tempo_date/sun.cpp",
    '''bool Tempo::isDay(
    const DateTime &day, const TempoDuration &sunRiseOffset, const TempoDuration &sunSetOffset
) const {
\tTempoSunCycle cycle = sunCycleCache_;
\tif (!cycle.valid) {
\t\treturn false;
\t}
\tDateTime start = addSeconds(cycle.sunRiseUtc, sunRiseOffset.seconds());
\tDateTime end = addSeconds(cycle.sunSetUtc, sunSetOffset.seconds());
\treturn !isBefore(day, start) && !isAfter(day, end);
}
''',
    '''bool Tempo::isDay(
    const DateTime &day, const TempoDuration &sunRiseOffset, const TempoDuration &sunSetOffset
) const {
\tconst TempoSunEventResult rise = sunriseFromConfig(day);
\tconst TempoSunEventResult set = sunsetFromConfig(day);
\tif (!rise.ok || !set.ok) {
\t\treturn false;
\t}
\tDateTime start = addSeconds(rise.value, sunRiseOffset.seconds());
\tDateTime end = addSeconds(set.value, sunSetOffset.seconds());
\treturn !isBefore(day, start) && !isAfter(day, end);
}
''',
)
replace_once(
    "src/internal/tempo_date/sun.cpp",
    "\tcycle.calculatedForDateUtc = startOfDayUtc(day);",
    "\tcycle.calculatedForDateUtc = day;",
)
replace_once(
    "src/internal/tempo_date/sun.cpp",
    '''TempoSunCycle Tempo::sunCycleToday() {
\tif (!sunCycleCache_.valid || !isSameLocalDay(sunCycleCache_.calculatedForDateUtc, now())) {
\t\trefreshSunCycleCache(now());
\t}
\treturn sunCycleCache_;
}
''',
    '''TempoSunCycle Tempo::sunCycleFor(const DateTime &day) {
\tif (!sunCycleCache_.valid || !isSameLocalDay(sunCycleCache_.calculatedForDateUtc, day)) {
\t\trefreshSunCycleCache(day);
\t}
\treturn sunCycleCache_;
}

TempoSunCycle Tempo::sunCycleToday() {
\treturn sunCycleFor(now());
}
''',
)
replace_once(
    "src/internal/tempo_date/sun.cpp",
    "\tTempoSunCycle cycle = sunCycleToday();\n\treturn cycle.valid && inSunWindow(dt, cycle.sunRiseUtc, offset);",
    "\tTempoSunCycle cycle = sunCycleFor(dt);\n\treturn cycle.valid && inSunWindow(dt, cycle.sunRiseUtc, offset);",
)
replace_once(
    "src/internal/tempo_date/sun.cpp",
    "\tTempoSunCycle cycle = sunCycleToday();\n\treturn cycle.valid && inSunWindow(dt, cycle.sunSetUtc, offset);",
    "\tTempoSunCycle cycle = sunCycleFor(dt);\n\treturn cycle.valid && inSunWindow(dt, cycle.sunSetUtc, offset);",
)

Path("scripts/apply_v010_date_hardening.py").unlink(missing_ok=True)
Path(".github/workflows/apply-v010-date-hardening.yml").unlink(missing_ok=True)
