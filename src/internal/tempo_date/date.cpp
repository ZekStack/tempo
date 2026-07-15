#include "date.h"
#include "utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__has_include)
#if __has_include(<esp_sntp.h>)
#include <esp_sntp.h>
#define TEMPO_HAS_CONFIG_TZ_TIME 1
#define TEMPO_HAS_SNTP_NOTIFICATION_CB 1
#define TEMPO_HAS_SNTP_SYNC_INTERVAL 1
#elif __has_include(<esp_netif_sntp.h>)
#include <esp_netif_sntp.h>
#define TEMPO_HAS_CONFIG_TZ_TIME 1
#define TEMPO_HAS_SNTP_NOTIFICATION_CB 0
#define TEMPO_HAS_SNTP_SYNC_INTERVAL 0
#else
#define TEMPO_HAS_CONFIG_TZ_TIME 0
#define TEMPO_HAS_SNTP_NOTIFICATION_CB 0
#define TEMPO_HAS_SNTP_SYNC_INTERVAL 0
#endif
#else
#define TEMPO_HAS_CONFIG_TZ_TIME 0
#define TEMPO_HAS_SNTP_NOTIFICATION_CB 0
#define TEMPO_HAS_SNTP_SYNC_INTERVAL 0
#endif

#if defined(__SIZEOF_TIME_T__) && __SIZEOF_TIME_T__ < 8
#warning "Tempo detected 32-bit time_t; dates beyond 2038 may overflow."
#endif

using Utils = TempoUtils;

namespace {
const char *patternForStyle(TempoFormat style, bool localIso8601) {
	switch (style) {
	case TempoFormat::Iso8601:
		return localIso8601 ? "%Y-%m-%dT%H:%M:%S%z" : "%Y-%m-%dT%H:%M:%SZ";
	case TempoFormat::DateTime:
		return "%Y-%m-%d %H:%M:%S";
	case TempoFormat::Date:
		return "%Y-%m-%d";
	case TempoFormat::Time:
		return "%H:%M:%S";
	}
	return "%Y-%m-%d %H:%M:%S";
}

bool formatWithTm(const tm &value, const char *pattern, char *outBuffer, size_t outSize) {
	if (!pattern || !outBuffer || outSize == 0) {
		return false;
	}
	tm copy = value;
	const size_t written = strftime(outBuffer, outSize, pattern, &copy);
	return written > 0;
}
} // namespace

TempoResult TempoResult::success(const char *message) {
	TempoResult result;
	result.result = true;
	result.status = TempoStatus::Ok;
	result.message = message != nullptr ? message : "ok";
	return result;
}

TempoResult TempoResult::failure(TempoStatus status, const char *message) {
	TempoResult result;
	result.result = false;
	result.status = status;
	result.message = message != nullptr ? message : "error";
	return result;
}

Tempo::NtpSyncCallback Tempo::activeNtpSyncCallback_ = nullptr;
Tempo::NtpSyncCallable Tempo::activeNtpSyncCallbackCallable_{};
Tempo *Tempo::activeNtpSyncOwner_ = nullptr;
std::recursive_mutex Tempo::ntpMutex_{};

#if TEMPO_HAS_SNTP_NOTIFICATION_CB
void Tempo::handleSntpSync(struct timeval *tv) {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	int64_t syncedEpoch = static_cast<int64_t>(time(nullptr));
	if (tv) {
		syncedEpoch = static_cast<int64_t>(tv->tv_sec);
	}
	const DateTime syncedAtUtc{syncedEpoch};
	if (activeNtpSyncOwner_) {
		activeNtpSyncOwner_->dispatchNtpSync(syncedAtUtc);
	}
}
#endif

int DateTime::yearUtc() const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return 0;
	}
	return t.tm_year + 1900;
}

int DateTime::monthUtc() const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return 0;
	}
	return t.tm_mon + 1;
}

int DateTime::dayUtc() const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return 0;
	}
	return t.tm_mday;
}

int DateTime::hourUtc() const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return 0;
	}
	return t.tm_hour;
}

int DateTime::minuteUtc() const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return 0;
	}
	return t.tm_min;
}

int DateTime::secondUtc() const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return 0;
	}
	return t.tm_sec;
}

bool DateTime::utcString(char *outBuffer, size_t outSize, TempoFormat style) const {
	tm t{};
	if (!Utils::toUtcTm(*this, t)) {
		return false;
	}
	return formatWithTm(t, patternForStyle(style, false), outBuffer, outSize);
}

bool DateTime::localString(char *outBuffer, size_t outSize, TempoFormat style) const {
	tm t{};
	if (!Utils::toLocalTm(*this, t)) {
		return false;
	}
	return formatWithTm(t, patternForStyle(style, true), outBuffer, outSize);
}

std::string DateTime::utcString(TempoFormat style) const {
	char buffer[40];
	if (!utcString(buffer, sizeof(buffer), style)) {
		return std::string();
	}
	return std::string(buffer);
}

std::string DateTime::localString(TempoFormat style) const {
	char buffer[48];
	if (!localString(buffer, sizeof(buffer), style)) {
		return std::string();
	}
	return std::string(buffer);
}

bool LocalDateTime::localString(char *outBuffer, size_t outSize) const {
	if (!ok || !outBuffer || outSize == 0) {
		return false;
	}
	const int written = std::snprintf(
	    outBuffer,
	    outSize,
	    "%04d-%02d-%02d %02d:%02d:%02d",
	    year,
	    month,
	    day,
	    hour,
	    minute,
	    second
	);
	return written > 0 && static_cast<size_t>(written) < outSize;
}

std::string LocalDateTime::localString() const {
	char buffer[32];
	if (!localString(buffer, sizeof(buffer))) {
		return std::string();
	}
	return std::string(buffer);
}

Tempo::Tempo() = default;

Tempo::~Tempo() {
	deinit();
}

TempoResult Tempo::init(const TempoConfig &config) {
	if (initialized_) {
		return TempoResult::failure(TempoStatus::AlreadyInitialized, "tempo already initialized");
	}
	const bool hasLatitude = std::isfinite(config.latitude);
	const bool hasLongitude = std::isfinite(config.longitude);
	if (hasLatitude != hasLongitude ||
	    (hasLatitude && (config.latitude < -90.0f || config.latitude > 90.0f ||
	                     config.longitude < -180.0f || config.longitude > 180.0f))) {
		return TempoResult::failure(TempoStatus::InvalidArgument, "invalid location configuration");
	}
	applyConfig(config);
	return TempoResult::success("tempo initialized");
}

void Tempo::deinit() {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	ntpSyncCallback_ = nullptr;
	ntpSyncCallbackCallable_ = NtpSyncCallable{};
	hasLastNtpSync_ = false;
	lastNtpSync_ = DateTime{};
	nextNtpSyncListenerId_ = 1;
	for (size_t i = 0; i < kMaxNtpSyncListeners; ++i) {
		ntpSyncListeners_[i].id = 0;
		ntpSyncListeners_[i].listener = NtpSyncCallable{};
	}
	hasLocation_ = false;
	latitude_ = 0.0f;
	longitude_ = 0.0f;
	ntpSyncIntervalMs_ = 0;
	const bool usePSRAM = usePSRAMBuffers_;
	timeZone_ = DateString(DateAllocator<char>(usePSRAM));
	for (size_t i = 0; i < kMaxNtpServers; ++i) {
		ntpServers_[i] = DateString(DateAllocator<char>(usePSRAM));
	}
	usePSRAMBuffers_ = false;
	initialized_ = false;

	if (activeNtpSyncOwner_ == this) {
		activeNtpSyncOwner_ = nullptr;
		activeNtpSyncCallback_ = nullptr;
		activeNtpSyncCallbackCallable_ = NtpSyncCallable{};
#if TEMPO_HAS_SNTP_NOTIFICATION_CB
		sntp_set_time_sync_notification_cb(nullptr);
#endif
	}
}

void Tempo::applyConfig(const TempoConfig &config) {
	latitude_ = config.latitude;
	longitude_ = config.longitude;
	hasLocation_ = std::isfinite(latitude_) && std::isfinite(longitude_);
	usePSRAMBuffers_ = config.usePSRAMBuffers;
	minValidUnixSeconds_ = config.minValidUnixSeconds;
	sunCycleMatchWindowSeconds_ = config.sunCycleMatchWindowSeconds;
	sunCycleCache_ = TempoSunCycle{};
	timeZone_ = DateString(DateAllocator<char>(usePSRAMBuffers_));
	for (size_t i = 0; i < kMaxNtpServers; ++i) {
		ntpServers_[i] = DateString(DateAllocator<char>(usePSRAMBuffers_));
	}
	ntpSyncIntervalMs_ = config.ntpSyncIntervalMs;
	hasLastNtpSync_ = false;
	lastNtpSync_ = DateTime{};

	const char *configuredTimeZone = config.timezone && config.timezone[0] != '\0'
	                                     ? config.timezone
	                                     : config.timeZone;
	const bool hasTz = configuredTimeZone && configuredTimeZone[0] != '\0';
	const char *configuredNtpServers[kMaxNtpServers] =
	    {config.ntpServer, config.ntpServer2, config.ntpServer3};
	size_t ntpServerCount = 0;
	if (hasTz) {
		timeZone_ = configuredTimeZone;
	}
	for (size_t i = 0; i < config.ntpServers.size() && ntpServerCount < kMaxNtpServers; ++i) {
		const char *server = config.ntpServers[i];
		if (server && server[0] != '\0') {
			ntpServers_[ntpServerCount++] = server;
		}
	}
	for (size_t i = 0; i < kMaxNtpServers; ++i) {
		if (ntpServerCount >= kMaxNtpServers) {
			break;
		}
		const char *server = configuredNtpServers[i];
		if (!server || server[0] == '\0') {
			continue;
		}
		ntpServers_[ntpServerCount++] = server;
	}

	if (!applyNtpConfig() && hasTz) {
		Utils::setProcessTimeZone(timeZone_.c_str());
	}
	initialized_ = true;
}

void Tempo::setNtpSyncCallback(NtpSyncCallback callback) {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	activeNtpSyncOwner_ = this;
	ntpSyncCallback_ = callback;
	ntpSyncCallbackCallable_ = NtpSyncCallable{};
	activeNtpSyncCallback_ = callback;
	activeNtpSyncCallbackCallable_ = NtpSyncCallable{};
#if TEMPO_HAS_SNTP_NOTIFICATION_CB
	const bool keepTrackingEnabled = hasAnyNtpServerConfigured();
	sntp_set_time_sync_notification_cb(
	    (callback || keepTrackingEnabled) ? &Tempo::handleSntpSync : nullptr
	);
#endif
}

void Tempo::setNtpSyncCallbackCallable(const NtpSyncCallable &callback) {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	activeNtpSyncOwner_ = this;
	ntpSyncCallback_ = nullptr;
	ntpSyncCallbackCallable_ = callback;
	activeNtpSyncCallback_ = nullptr;
	activeNtpSyncCallbackCallable_ = callback;
#if TEMPO_HAS_SNTP_NOTIFICATION_CB
	const bool keepTrackingEnabled = hasAnyNtpServerConfigured();
	sntp_set_time_sync_notification_cb(
	    (static_cast<bool>(callback) || keepTrackingEnabled) ? &Tempo::handleSntpSync : nullptr
	);
#endif
}

bool Tempo::setNtpSyncIntervalMs(uint32_t intervalMs) {
	ntpSyncIntervalMs_ = intervalMs;
#if TEMPO_HAS_SNTP_SYNC_INTERVAL
	if (intervalMs > 0) {
		sntp_set_sync_interval(intervalMs);
	}
	return true;
#else
	return intervalMs == 0;
#endif
}

bool Tempo::hasLastNtpSync() const {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	return hasLastNtpSync_;
}

DateTime Tempo::lastNtpSync() const {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	return lastNtpSync_;
}

Tempo::NtpSyncListenerId Tempo::addNtpSyncListener(const NtpSyncCallable &listener) {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	if (!listener) {
		return 0;
	}
	for (size_t i = 0; i < kMaxNtpSyncListeners; ++i) {
		if (ntpSyncListeners_[i].id != 0) {
			continue;
		}
		NtpSyncListenerId id = nextNtpSyncListenerId_++;
		if (id == 0) {
			id = nextNtpSyncListenerId_++;
		}
		ntpSyncListeners_[i].id = id;
		ntpSyncListeners_[i].listener = listener;
		return id;
	}
	return 0;
}

bool Tempo::removeNtpSyncListener(NtpSyncListenerId id) {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	if (id == 0) {
		return false;
	}
	for (size_t i = 0; i < kMaxNtpSyncListeners; ++i) {
		if (ntpSyncListeners_[i].id != id) {
			continue;
		}
		ntpSyncListeners_[i].id = 0;
		ntpSyncListeners_[i].listener = NtpSyncCallable{};
		return true;
	}
	return false;
}

bool Tempo::syncNTP() {
	return applyNtpConfig();
}

TempoResult Tempo::syncNtp() {
	return syncNTP() ? TempoResult::success("ntp sync requested")
	                 : TempoResult::failure(TempoStatus::NtpUnavailable, "ntp is not configured");
}

void Tempo::setMinValidUnixSeconds(int64_t value) {
	minValidUnixSeconds_ = value;
}

int64_t Tempo::minValidUnixSeconds() const {
	return minValidUnixSeconds_;
}

bool Tempo::isValidTime() const {
	return now().epochSeconds >= minValidUnixSeconds_;
}

uint64_t Tempo::unixSeconds() const {
	return static_cast<uint64_t>(now().epochSeconds);
}

uint64_t Tempo::unixSeconds(const DateTime &dt) const {
	return static_cast<uint64_t>(dt.epochSeconds);
}

void Tempo::dispatchNtpSync(const DateTime &syncedAtUtc) {
	std::lock_guard<std::recursive_mutex> lock(ntpMutex_);
	lastNtpSync_ = syncedAtUtc;
	hasLastNtpSync_ = true;

	if (activeNtpSyncCallbackCallable_) {
		activeNtpSyncCallbackCallable_(syncedAtUtc);
	} else if (activeNtpSyncCallback_) {
		activeNtpSyncCallback_(syncedAtUtc);
	}

	for (size_t i = 0; i < kMaxNtpSyncListeners; ++i) {
		if (!ntpSyncListeners_[i].listener) {
			continue;
		}
		ntpSyncListeners_[i].listener(syncedAtUtc);
	}
}

bool Tempo::hasAnyNtpServerConfigured() const {
	for (size_t i = 0; i < kMaxNtpServers; ++i) {
		if (!ntpServers_[i].empty()) {
			return true;
		}
	}
	return false;
}

bool Tempo::applyNtpConfig() const {
#if TEMPO_HAS_CONFIG_TZ_TIME
	if (!hasAnyNtpServerConfigured()) {
		return false;
	}
	activeNtpSyncOwner_ = const_cast<Tempo *>(this);

#if TEMPO_HAS_SNTP_NOTIFICATION_CB
	activeNtpSyncCallback_ = ntpSyncCallback_;
	activeNtpSyncCallbackCallable_ = ntpSyncCallbackCallable_;
	const bool hasCallback =
	    (ntpSyncCallback_ != nullptr) || static_cast<bool>(ntpSyncCallbackCallable_);
	sntp_set_time_sync_notification_cb(
	    (hasCallback || activeNtpSyncOwner_ != nullptr) ? &Tempo::handleSntpSync : nullptr
	);
#endif
#if TEMPO_HAS_SNTP_SYNC_INTERVAL
	if (ntpSyncIntervalMs_ > 0) {
		sntp_set_sync_interval(ntpSyncIntervalMs_);
	}
#endif

	Utils::TimezoneLock timezoneLock;
	const char *tz = timeZone_.empty() ? "UTC0" : timeZone_.c_str();
	const char *ntpServer1 = ntpServers_[0].empty() ? nullptr : ntpServers_[0].c_str();
	const char *ntpServer2 = ntpServers_[1].empty() ? nullptr : ntpServers_[1].c_str();
	const char *ntpServer3 = ntpServers_[2].empty() ? nullptr : ntpServers_[2].c_str();
	configTzTime(tz, ntpServer1, ntpServer2, ntpServer3);
	return true;
#else
	return false;
#endif
}

DateTime Tempo::now() const {
	return DateTime{static_cast<int64_t>(time(nullptr))};
}

DateTime Tempo::nowUtc() const {
	return now();
}

LocalDateTime Tempo::nowLocal() const {
	return toLocal(now(), nullptr);
}

LocalDateTime Tempo::toLocal(const DateTime &dt) const {
	return toLocal(dt, nullptr);
}

LocalDateTime Tempo::toLocal(const DateTime &dt, const char *timeZone) const {
	LocalDateTime result{};
	const char *tz = timeZone;
	if (!tz || tz[0] == '\0') {
		tz = timeZone_.empty() ? nullptr : timeZone_.c_str();
	}

	Utils::ScopedTz scoped(tz, usePSRAMBuffers_);
	time_t raw = static_cast<time_t>(dt.epochSeconds);
	tm local{};
	if (localtime_r(&raw, &local) == nullptr) {
		return result;
	}

	const int offsetSeconds = static_cast<int>(Utils::timegm64(local) - static_cast<int64_t>(raw));
	result.ok = true;
	result.year = local.tm_year + 1900;
	result.month = local.tm_mon + 1;
	result.day = local.tm_mday;
	result.hour = local.tm_hour;
	result.minute = local.tm_min;
	result.second = local.tm_sec;
	result.offsetMinutes = offsetSeconds / 60;
	result.utc = dt;
	result.hasResolvedUtc = true;
	return result;
}

DateTime Tempo::fromUnixSeconds(int64_t seconds) const {
	return DateTime{seconds};
}

DateTime Tempo::fromUtc(int year, int month, int day, int hour, int minute, int second) const {
	if (!Utils::validHms(hour, minute, second) || month < 1 || month > 12 || year < 0 ||
	    year > 9999) {
		return DateTime{};
	}
	const int clampedDay = Utils::clampDay(year, month, day, *this);
	tm t{};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = clampedDay;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;
	t.tm_isdst = 0;
	return Utils::fromUtcTm(t);
}

DateTime Tempo::fromLocal(int year, int month, int day, int hour, int minute, int second) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	if (!Utils::validHms(hour, minute, second) || month < 1 || month > 12 || year < 0 ||
	    year > 9999) {
		return DateTime{};
	}
	const int clampedDay = Utils::clampDay(year, month, day, *this);
	tm t{};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = clampedDay;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;
	t.tm_isdst = -1; // let the runtime figure DST
	return Utils::fromLocalTm(t);
}

DateTime Tempo::toUtc(const LocalDateTime &dt) const {
	if (!dt.ok) {
		return DateTime{};
	}
	if (dt.hasResolvedUtc) {
		return dt.utc;
	}
	return fromLocal(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

int64_t Tempo::toUnixSeconds(const DateTime &dt) const {
	return dt.epochSeconds;
}

bool Tempo::isDstActive() const {
	return isDstActive(now());
}

bool Tempo::isDstActive(const DateTime &dt) const {
	return isDstActive(dt, nullptr);
}

bool Tempo::isDstActive(const char *timeZone) const {
	return isDstActive(now(), timeZone);
}

bool Tempo::isDstActive(const DateTime &dt, const char *timeZone) const {
	const char *tz = timeZone;
	if (!tz || tz[0] == '\0') {
		if (!timeZone_.empty()) {
			tz = timeZone_.c_str();
		} else {
			tz = nullptr;
		}
	}
	return Utils::isDstActiveFor(dt, tz, usePSRAMBuffers_);
}

DateTime Tempo::addSeconds(const DateTime &dt, int64_t seconds) const {
	return DateTime{dt.epochSeconds + seconds};
}

DateTime Tempo::addMinutes(const DateTime &dt, int64_t minutes) const {
	return addSeconds(dt, minutes * Utils::kSecondsPerMinute);
}

DateTime Tempo::addHours(const DateTime &dt, int64_t hours) const {
	return addSeconds(dt, hours * Utils::kSecondsPerHour);
}

DateTime Tempo::addDays(const DateTime &dt, int32_t days) const {
	return addSeconds(dt, static_cast<int64_t>(days) * Utils::kSecondsPerDay);
}

DateTime Tempo::addMonths(const DateTime &dt, int32_t months) const {
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return dt;
	}

	int totalMonths = t.tm_mon + months;
	int yearsDelta = totalMonths / 12;
	int newMonth = totalMonths % 12;
	if (newMonth < 0) {
		newMonth += 12;
		--yearsDelta;
	}

	t.tm_year += yearsDelta;
	t.tm_mon = newMonth;
	t.tm_mday = Utils::clampDay(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, *this);

	return Utils::fromUtcTm(t);
}

DateTime Tempo::addYears(const DateTime &dt, int32_t years) const {
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return dt;
	}
	t.tm_year += years;
	t.tm_mday = Utils::clampDay(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, *this);
	return Utils::fromUtcTm(t);
}

DateTime Tempo::subSeconds(const DateTime &dt, int64_t seconds) const {
	return addSeconds(dt, -seconds);
}

DateTime Tempo::subMinutes(const DateTime &dt, int64_t minutes) const {
	return addMinutes(dt, -minutes);
}

DateTime Tempo::subHours(const DateTime &dt, int64_t hours) const {
	return addHours(dt, -hours);
}

DateTime Tempo::subDays(const DateTime &dt, int32_t days) const {
	return addDays(dt, -days);
}

DateTime Tempo::subMonths(const DateTime &dt, int32_t months) const {
	return addMonths(dt, -months);
}

DateTime Tempo::subYears(const DateTime &dt, int32_t years) const {
	return addYears(dt, -years);
}

DateTime Tempo::addSecondsUtc(const DateTime &dt, int64_t seconds) const {
	return addSeconds(dt, seconds);
}

DateTime Tempo::addMinutesUtc(const DateTime &dt, int64_t minutes) const {
	return addMinutes(dt, minutes);
}

DateTime Tempo::addHoursUtc(const DateTime &dt, int64_t hours) const {
	return addHours(dt, hours);
}

DateTime Tempo::addDaysUtc(const DateTime &dt, int32_t days) const {
	return addDays(dt, days);
}

DateTime Tempo::addMonthsUtc(const DateTime &dt, int32_t months) const {
	return addMonths(dt, months);
}

DateTime Tempo::addYearsUtc(const DateTime &dt, int32_t years) const {
	return addYears(dt, years);
}

DateTime Tempo::subSecondsUtc(const DateTime &dt, int64_t seconds) const {
	return subSeconds(dt, seconds);
}

DateTime Tempo::subMinutesUtc(const DateTime &dt, int64_t minutes) const {
	return subMinutes(dt, minutes);
}

DateTime Tempo::subHoursUtc(const DateTime &dt, int64_t hours) const {
	return subHours(dt, hours);
}

DateTime Tempo::subDaysUtc(const DateTime &dt, int32_t days) const {
	return subDays(dt, days);
}

DateTime Tempo::subMonthsUtc(const DateTime &dt, int32_t months) const {
	return subMonths(dt, months);
}

DateTime Tempo::subYearsUtc(const DateTime &dt, int32_t years) const {
	return subYears(dt, years);
}

DateTime Tempo::addSeconds(int64_t seconds) const {
	return addSeconds(now(), seconds);
}

DateTime Tempo::addMinutes(int64_t minutes) const {
	return addMinutes(now(), minutes);
}

DateTime Tempo::addHours(int64_t hours) const {
	return addHours(now(), hours);
}

DateTime Tempo::addDays(int32_t days) const {
	return addDays(now(), days);
}

DateTime Tempo::addMonths(int32_t months) const {
	return addMonths(now(), months);
}

DateTime Tempo::addYears(int32_t years) const {
	return addYears(now(), years);
}

DateTime Tempo::subSeconds(int64_t seconds) const {
	return subSeconds(now(), seconds);
}

DateTime Tempo::subMinutes(int64_t minutes) const {
	return subMinutes(now(), minutes);
}

DateTime Tempo::subHours(int64_t hours) const {
	return subHours(now(), hours);
}

DateTime Tempo::subDays(int32_t days) const {
	return subDays(now(), days);
}

DateTime Tempo::subMonths(int32_t months) const {
	return subMonths(now(), months);
}

DateTime Tempo::subYears(int32_t years) const {
	return subYears(now(), years);
}

int64_t Tempo::differenceInSeconds(const DateTime &a, const DateTime &b) const {
	return a.epochSeconds - b.epochSeconds;
}

int64_t Tempo::differenceInMinutes(const DateTime &a, const DateTime &b) const {
	return differenceInSeconds(a, b) / Utils::kSecondsPerMinute;
}

int64_t Tempo::differenceInHours(const DateTime &a, const DateTime &b) const {
	return differenceInSeconds(a, b) / Utils::kSecondsPerHour;
}

int64_t Tempo::differenceInDays(const DateTime &a, const DateTime &b) const {
	return differenceInSeconds(a, b) / Utils::kSecondsPerDay;
}

int64_t Tempo::diff(const DateTime &a, const DateTime &b, TempoDiff unit) const {
	switch (unit) {
	case TempoDiff::Seconds:
		return differenceInSeconds(a, b);
	case TempoDiff::Minutes:
		return differenceInMinutes(a, b);
	case TempoDiff::Hours:
		return differenceInHours(a, b);
	case TempoDiff::Days:
		return differenceInDays(a, b);
	case TempoDiff::Months:
		return (getYearUtc(a) - getYearUtc(b)) * 12 + (getMonthUtc(a) - getMonthUtc(b));
	case TempoDiff::Years:
		return getYearUtc(a) - getYearUtc(b);
	}
	return differenceInSeconds(a, b);
}

bool Tempo::isBefore(const DateTime &a, const DateTime &b) const {
	return a.epochSeconds < b.epochSeconds;
}

bool Tempo::isAfter(const DateTime &a, const DateTime &b) const {
	return a.epochSeconds > b.epochSeconds;
}

bool Tempo::isEqual(const DateTime &a, const DateTime &b) const {
	return a.epochSeconds == b.epochSeconds;
}

bool Tempo::isEqualMinutes(const DateTime &a, const DateTime &b) const {
	return (a.epochSeconds / Utils::kSecondsPerMinute) ==
	       (b.epochSeconds / Utils::kSecondsPerMinute);
}

bool Tempo::isEqualMinutesUtc(const DateTime &a, const DateTime &b) const {
	return isEqualMinutes(a, b);
}

bool Tempo::isSameDay(const DateTime &a, const DateTime &b) const {
	return isEqual(startOfDayUtc(a), startOfDayUtc(b));
}

bool Tempo::isSameLocalDay(const DateTime &a, const DateTime &b) const {
	LocalDateTime localA = toLocal(a);
	LocalDateTime localB = toLocal(b);
	return localA.ok && localB.ok && localA.year == localB.year && localA.month == localB.month &&
	       localA.day == localB.day;
}

DateTime Tempo::startOfDayUtc(const DateTime &dt) const {
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return dt;
	}
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;
	return Utils::fromUtcTm(t);
}

DateTime Tempo::endOfDayUtc(const DateTime &dt) const {
	return addSeconds(startOfDayUtc(dt), Utils::kSecondsPerDay - 1);
}

DateTime Tempo::startOfMonthUtc(const DateTime &dt) const {
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return dt;
	}
	t.tm_mday = 1;
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;
	return Utils::fromUtcTm(t);
}

DateTime Tempo::endOfMonthUtc(const DateTime &dt) const {
	DateTime start = startOfMonthUtc(dt);
	DateTime nextMonth = addMonths(start, 1);
	return subSeconds(nextMonth, 1);
}

int Tempo::getYearUtc(const DateTime &dt) const {
	return dt.yearUtc();
}

int Tempo::getMonthUtc(const DateTime &dt) const {
	return dt.monthUtc();
}

int Tempo::getDayUtc(const DateTime &dt) const {
	return dt.dayUtc();
}

int Tempo::getWeekdayUtc(const DateTime &dt) const {
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return 0;
	}
	return t.tm_wday;
}

DateTime Tempo::startOfDayLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return dt;
	}
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;
	t.tm_isdst = -1;
	return Utils::fromLocalTm(t);
}

DateTime Tempo::addCalendarDaysLocal(const DateTime &dt, int days) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return dt;
	}
	t.tm_mday += days;
	t.tm_isdst = -1;
	return Utils::fromLocalTm(t);
}

DateTime Tempo::endOfDayLocal(const DateTime &dt) const {
	const DateTime start = startOfDayLocal(dt);
	return subSeconds(addCalendarDaysLocal(start, 1), 1);
}

DateTime Tempo::startOfMonthLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return dt;
	}
	t.tm_mday = 1;
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;
	t.tm_isdst = -1;
	return Utils::fromLocalTm(t);
}

DateTime Tempo::endOfMonthLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	DateTime start = startOfMonthLocal(dt);
	tm t{};
	if (!Utils::toLocalTm(start, t)) {
		return start;
	}
	t.tm_mon += 1;
	t.tm_isdst = -1;
	DateTime nextMonth = Utils::fromLocalTm(t);
	return subSeconds(nextMonth, 1);
}

DateTime Tempo::startOfYearUtc(const DateTime &dt) const {
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return dt;
	}
	t.tm_mon = 0;
	t.tm_mday = 1;
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;
	return Utils::fromUtcTm(t);
}

DateTime Tempo::startOfYearLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return dt;
	}
	t.tm_mon = 0;
	t.tm_mday = 1;
	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;
	t.tm_isdst = -1;
	return Utils::fromLocalTm(t);
}

DateTime Tempo::setTimeOfDayLocal(const DateTime &dt, int hour, int minute, int second) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	if (!Utils::validHms(hour, minute, second)) {
		return dt;
	}
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return dt;
	}
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;
	t.tm_isdst = -1;
	return Utils::fromLocalTm(t);
}

DateTime Tempo::setTimeOfDayUtc(const DateTime &dt, int hour, int minute, int second) const {
	if (!Utils::validHms(hour, minute, second)) {
		return dt;
	}
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return dt;
	}
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;
	return Utils::fromUtcTm(t);
}

DateTime Tempo::nextDailyAtLocal(int hour, int minute, int second, const DateTime &from) const {
	if (!Utils::validHms(hour, minute, second)) {
		return from;
	}
	DateTime candidate = setTimeOfDayLocal(from, hour, minute, second);
	if (!isAfter(from, candidate)) {
		return candidate;
	}
	DateTime nextDay = addCalendarDaysLocal(from, 1);
	return setTimeOfDayLocal(nextDay, hour, minute, second);
}

DateTime Tempo::nextWeekdayAtLocal(
    int weekday, int hour, int minute, int second, const DateTime &from
) const {
	if (!Utils::validHms(hour, minute, second) || weekday < 0 || weekday > 6) {
		return from;
	}
	const int current = getWeekdayLocal(from);
	int daysAhead = (weekday - current + 7) % 7;
	DateTime candidateDay = addCalendarDaysLocal(from, daysAhead);
	DateTime candidate = setTimeOfDayLocal(candidateDay, hour, minute, second);
	if (daysAhead == 0 && isAfter(from, candidate)) {
		candidate = setTimeOfDayLocal(addCalendarDaysLocal(from, 7), hour, minute, second);
	}
	return candidate;
}

int Tempo::getYearLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return 0;
	}
	return t.tm_year + 1900;
}

int Tempo::getMonthLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return 0;
	}
	return t.tm_mon + 1;
}

int Tempo::getDayLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return 0;
	}
	return t.tm_mday;
}

int Tempo::getWeekdayLocal(const DateTime &dt) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return 0;
	}
	return t.tm_wday;
}

bool Tempo::isLeapYear(int year) const {
	if (year % 4 != 0) {
		return false;
	}
	if (year % 100 != 0) {
		return true;
	}
	return (year % 400) == 0;
}

int Tempo::daysInMonth(int year, int month) const {
	static const int daysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month < 1 || month > 12) {
		return 0;
	}
	if (month == 2 && isLeapYear(year)) {
		return 29;
	}
	return daysPerMonth[month - 1];
}

bool Tempo::formatUtc(
    const DateTime &dt, TempoFormat style, char *outBuffer, size_t outSize
) const {
	return formatWithPatternUtc(dt, patternForStyle(style, false), outBuffer, outSize);
}

bool Tempo::formatLocal(
    const DateTime &dt, TempoFormat style, char *outBuffer, size_t outSize
) const {
	return formatWithPatternLocal(dt, patternForStyle(style, true), outBuffer, outSize);
}

bool Tempo::formatWithPatternUtc(
    const DateTime &dt, const char *pattern, char *outBuffer, size_t outSize
) const {
	if (!pattern || !outBuffer || outSize == 0) {
		return false;
	}
	tm t{};
	if (!Utils::toUtcTm(dt, t)) {
		return false;
	}
	size_t written = strftime(outBuffer, outSize, pattern, &t);
	return written > 0;
}

bool Tempo::formatWithPatternLocal(
    const DateTime &dt, const char *pattern, char *outBuffer, size_t outSize
) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	if (!pattern || !outBuffer || outSize == 0) {
		return false;
	}
	tm t{};
	if (!Utils::toLocalTm(dt, t)) {
		return false;
	}
	size_t written = strftime(outBuffer, outSize, pattern, &t);
	return written > 0;
}

bool Tempo::dateTimeToStringUtc(
    const DateTime &dt, char *outBuffer, size_t outSize, TempoFormat style
) const {
	return dt.utcString(outBuffer, outSize, style);
}

bool Tempo::dateTimeToStringLocal(
    const DateTime &dt, char *outBuffer, size_t outSize, TempoFormat style
) const {
	return formatLocal(dt, style, outBuffer, outSize);
}

bool Tempo::localDateTimeToString(
    const LocalDateTime &dt, char *outBuffer, size_t outSize
) const {
	return dt.localString(outBuffer, outSize);
}

bool Tempo::nowUtcString(char *outBuffer, size_t outSize, TempoFormat style) const {
	return dateTimeToStringUtc(nowUtc(), outBuffer, outSize, style);
}

bool Tempo::nowLocalString(char *outBuffer, size_t outSize, TempoFormat style) const {
	return dateTimeToStringLocal(now(), outBuffer, outSize, style);
}

bool Tempo::lastNtpSyncStringUtc(char *outBuffer, size_t outSize, TempoFormat style) const {
	if (!hasLastNtpSync()) {
		return false;
	}
	return dateTimeToStringUtc(lastNtpSync(), outBuffer, outSize, style);
}

bool Tempo::lastNtpSyncStringLocal(char *outBuffer, size_t outSize, TempoFormat style) const {
	if (!hasLastNtpSync()) {
		return false;
	}
	return dateTimeToStringLocal(lastNtpSync(), outBuffer, outSize, style);
}

std::string Tempo::dateTimeToStringUtc(const DateTime &dt, TempoFormat style) const {
	char buffer[40];
	if (!dateTimeToStringUtc(dt, buffer, sizeof(buffer), style)) {
		return std::string();
	}
	return std::string(buffer);
}

std::string Tempo::dateTimeToStringLocal(const DateTime &dt, TempoFormat style) const {
	char buffer[48];
	if (!dateTimeToStringLocal(dt, buffer, sizeof(buffer), style)) {
		return std::string();
	}
	return std::string(buffer);
}

std::string Tempo::localDateTimeToString(const LocalDateTime &dt) const {
	char buffer[32];
	if (!localDateTimeToString(dt, buffer, sizeof(buffer))) {
		return std::string();
	}
	return std::string(buffer);
}

std::string Tempo::nowUtcString(TempoFormat style) const {
	return dateTimeToStringUtc(nowUtc(), style);
}

std::string Tempo::nowLocalString(TempoFormat style) const {
	return dateTimeToStringLocal(now(), style);
}

std::string Tempo::lastNtpSyncStringUtc(TempoFormat style) const {
	if (!hasLastNtpSync()) {
		return std::string();
	}
	return dateTimeToStringUtc(lastNtpSync(), style);
}

std::string Tempo::lastNtpSyncStringLocal(TempoFormat style) const {
	if (!hasLastNtpSync()) {
		return std::string();
	}
	return dateTimeToStringLocal(lastNtpSync(), style);
}

Tempo::ParseResult Tempo::parseIso8601Utc(const char *str) const {
	ParseResult result{false, DateTime{}};
	if (!str) {
		return result;
	}
	const size_t len = std::strlen(str);
	if (len != 20 || str[4] != '-' || str[7] != '-' || (str[10] != 'T' && str[10] != 't') ||
	    str[13] != ':' || str[16] != ':' || (str[19] != 'Z' && str[19] != 'z')) {
		return result;
	}

	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	if (!Utils::parseIntSlice(str, 4, 0, 9999, year) ||
	    !Utils::parseIntSlice(str + 5, 2, 1, 12, month) ||
	    !Utils::parseIntSlice(str + 8, 2, 1, 31, day) ||
	    !Utils::parseIntSlice(str + 11, 2, 0, 23, hour) ||
	    !Utils::parseIntSlice(str + 14, 2, 0, 59, minute) ||
	    !Utils::parseIntSlice(str + 17, 2, 0, 59, second)) {
		return result;
	}

	const int maxDay = daysInMonth(year, month);
	if (day > maxDay) {
		return result;
	}

	tm t{};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;
	t.tm_isdst = 0;

	result.ok = true;
	result.value = Utils::fromUtcTm(t);
	return result;
}

Tempo::ParseResult Tempo::parseDateTimeLocal(const char *str) const {
	Utils::ScopedTz scopedTimeZone(
	    timeZone_.empty() ? nullptr : timeZone_.c_str(), usePSRAMBuffers_
	);
	ParseResult result{false, DateTime{}};
	if (!str) {
		return result;
	}
	const size_t len = std::strlen(str);
	if (len != 19 || str[4] != '-' || str[7] != '-' || str[10] != ' ' || str[13] != ':' ||
	    str[16] != ':') {
		return result;
	}

	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	if (!Utils::parseIntSlice(str, 4, 0, 9999, year) ||
	    !Utils::parseIntSlice(str + 5, 2, 1, 12, month) ||
	    !Utils::parseIntSlice(str + 8, 2, 1, 31, day) ||
	    !Utils::parseIntSlice(str + 11, 2, 0, 23, hour) ||
	    !Utils::parseIntSlice(str + 14, 2, 0, 59, minute) ||
	    !Utils::parseIntSlice(str + 17, 2, 0, 59, second)) {
		return result;
	}

	const int maxDay = daysInMonth(year, month);
	if (day > maxDay) {
		return result;
	}

	tm t{};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;
	t.tm_isdst = -1; // let the runtime decide

	result.value = Utils::fromLocalTm(t);
	const LocalDateTime resolved = toLocal(result.value);
	if (!resolved.ok || resolved.year != year || resolved.month != month || resolved.day != day ||
	    resolved.hour != hour || resolved.minute != minute || resolved.second != second) {
		return ParseResult{false, DateTime{}};
	}
	result.ok = true;
	return result;
}

DateTime Tempo::parseUtc(const char *str) const {
	return parseIso8601Utc(str).value;
}

LocalDateTime Tempo::parseLocal(const char *str) const {
	const ParseResult parsed = parseDateTimeLocal(str);
	if (!parsed.ok) {
		return LocalDateTime{};
	}
	return toLocal(parsed.value);
}

const char *Tempo::monthName(int month) const {
	static const char *kMonths[12] = {
	    "January",
	    "February",
	    "March",
	    "April",
	    "May",
	    "June",
	    "July",
	    "August",
	    "September",
	    "October",
	    "November",
	    "December"
	};
	if (month < 1 || month > 12) {
		return nullptr;
	}
	return kMonths[month - 1];
}

const char *Tempo::monthName(const DateTime &dt) const {
	return monthName(dt.monthUtc());
}
