#pragma once

#include "date_allocator.h"
#include <Arduino.h>
#include <functional>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <stdint.h>
#include <string>
#include <time.h>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

struct timeval;

enum class TempoFormat { Iso8601, DateTime, Date, Time };

enum class TempoStatus : uint8_t {
	Ok,
	NotInitialized,
	AlreadyInitialized,
	InvalidArgument,
	TimeNotValid,
	LocationNotConfigured,
	NtpUnavailable,
	InternalError,
};

enum class TempoDiff : uint8_t {
	Seconds,
	Minutes,
	Hours,
	Days,
	Months,
	Years,
};

struct TempoResult {
	bool result = false;
	TempoStatus status = TempoStatus::InternalError;
	std::string message;

	explicit operator bool() const {
		return result;
	}

	static TempoResult success(const char *message = "ok");
	static TempoResult failure(TempoStatus status, const char *message);
};

struct TempoDuration {
	int64_t secondsValue = 0;

	static TempoDuration seconds(int64_t value) {
		return TempoDuration{value};
	}
	static TempoDuration minutes(int64_t value) {
		return TempoDuration{value * 60};
	}
	static TempoDuration hours(int64_t value) {
		return TempoDuration{value * 3600};
	}
	int64_t seconds() const {
		return secondsValue;
	}
};

struct DateTime {
	int64_t epochSeconds = 0; // seconds since 1970-01-01T00:00:00Z

	int yearUtc() const;
	int monthUtc() const;  // 1..12
	int dayUtc() const;    // 1..31
	int hourUtc() const;   // 0..23
	int minuteUtc() const; // 0..59
	int secondUtc() const; // 0..59

	// Uses current system TZ for local formatting.
	bool
	utcString(char *outBuffer, size_t outSize, TempoFormat style = TempoFormat::DateTime) const;
	bool localString(
	    char *outBuffer, size_t outSize, TempoFormat style = TempoFormat::DateTime
	) const;
	std::string utcString(TempoFormat style = TempoFormat::DateTime) const;
	std::string localString(TempoFormat style = TempoFormat::DateTime) const;
};

struct TempoSyncResult {
	bool result = false;
	TempoStatus status = TempoStatus::InternalError;
	std::string message;
	DateTime syncedAtUtc{};

	explicit operator bool() const {
		return result;
	}
};

struct LocalDateTime {
	bool ok = false;
	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	int offsetMinutes = 0; // local - UTC
	DateTime utc{};
	bool hasResolvedUtc = false;

	bool localString(char *outBuffer, size_t outSize) const;
	std::string localString() const;
};

struct TempoConfig {
	float latitude = std::numeric_limits<float>::quiet_NaN();
	float longitude = std::numeric_limits<float>::quiet_NaN();
	const char *timezone = nullptr;
	const char *timeZone = nullptr; // POSIX TZ string, e.g. "CET-1CEST,M3.5.0/2,M10.5.0/3"
	std::vector<const char *> ntpServers{};
	const char *ntpServer =
	    nullptr; // optional primary NTP server; used with timeZone to call configTzTime
	uint32_t ntpSyncIntervalMs = 3600000;
	bool usePSRAMBuffers = false;   // prefer PSRAM for Tempo-owned config/state text buffers
	const char *ntpServer2 = nullptr; // optional secondary NTP server
	const char *ntpServer3 = nullptr; // optional tertiary NTP server
	uint32_t sunCycleMatchWindowSeconds = 60;
	int64_t minValidUnixSeconds = 1577836800;
};

struct TempoSunEventResult {
	bool ok;
	DateTime value;
};

struct TempoMoonPhase {
	bool ok;
	int angleDegrees;    // 0..360
	double illumination; // 0.0..1.0
};

struct TempoSunCycle {
	DateTime calculatedForDateUtc{};
	LocalDateTime calculatedForLocalDate{};
	DateTime sunRiseUtc{};
	DateTime sunSetUtc{};
	DateTime solarNoonUtc{};
	LocalDateTime sunRiseLocal{};
	LocalDateTime sunSetLocal{};
	LocalDateTime solarNoonLocal{};
	bool valid = false;
};

class Tempo {
  public:
	using NtpSyncCallback = void (*)(const DateTime &syncedAtUtc);
	using NtpSyncCallable = std::function<void(const DateTime &syncedAtUtc)>;
	using NtpSyncListenerId = uint32_t;

	Tempo();
	~Tempo();
	TempoResult init(const TempoConfig &config = TempoConfig());
	void deinit();
	bool isInitialized() const {
		return initialized_;
	}
	// Optional SNTP sync notification. Pass nullptr to clear.
	void setNtpSyncCallback(NtpSyncCallback callback);
	// Accepts capturing lambdas / std::bind / functors.
	// Non-capturing lambdas bind to the function-pointer overload above.
	template <
	    typename Callable,
	    typename std::enable_if<
	        !std::is_convertible<typename std::decay<Callable>::type, NtpSyncCallback>::value,
	        int>::type = 0>
	void setNtpSyncCallback(Callable &&callback) {
		setNtpSyncCallbackCallable(NtpSyncCallable(std::forward<Callable>(callback)));
	}
	// Adjusts SNTP sync interval in milliseconds. Pass 0 to keep the runtime default.
	// Returns false when the runtime does not expose interval control.
	bool setNtpSyncIntervalMs(uint32_t intervalMs);
	// True after at least one successful SNTP sync callback was received.
	bool hasLastNtpSync() const;
	// Returns the last SNTP sync timestamp (UTC epoch-backed DateTime).
	// When hasLastNtpSync() is false this returns DateTime{}.
	DateTime lastNtpSync() const;
	NtpSyncListenerId addNtpSyncListener(const NtpSyncCallable &listener);
	bool removeNtpSyncListener(NtpSyncListenerId id);
	// Triggers an immediate NTP sync with the configured server list.
	// Returns false when no NTP server is configured or SNTP runtime support is unavailable.
	bool syncNTP();
	TempoResult syncNtp();

	void setMinValidUnixSeconds(int64_t value);
	int64_t minValidUnixSeconds() const;
	bool isValidTime() const;
	uint64_t unixSeconds() const;
	uint64_t unixSeconds(const DateTime &dt) const;

	DateTime now() const;
	DateTime nowUtc() const; // alias of now(), returns the raw system clock (UTC)
	LocalDateTime nowLocal() const;
	LocalDateTime toLocal(const DateTime &dt) const;
	LocalDateTime toLocal(const DateTime &dt, const char *timeZone) const;
	DateTime fromUnixSeconds(int64_t seconds) const;
	DateTime
	fromUtc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) const;
	DateTime
	fromLocal(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) const;
	DateTime toUtc(const LocalDateTime &dt) const;
	int64_t toUnixSeconds(const DateTime &dt) const;

	// Arithmetic relative to a provided DateTime
	DateTime addSeconds(const DateTime &dt, int64_t seconds) const;
	DateTime addMinutes(const DateTime &dt, int64_t minutes) const;
	DateTime addHours(const DateTime &dt, int64_t hours) const;
	DateTime addDays(const DateTime &dt, int32_t days) const;
	DateTime addMonths(const DateTime &dt, int32_t months) const;
	DateTime addYears(const DateTime &dt, int32_t years) const;

	DateTime subSeconds(const DateTime &dt, int64_t seconds) const;
	DateTime subMinutes(const DateTime &dt, int64_t minutes) const;
	DateTime subHours(const DateTime &dt, int64_t hours) const;
	DateTime subDays(const DateTime &dt, int32_t days) const;
	DateTime subMonths(const DateTime &dt, int32_t months) const;
	DateTime subYears(const DateTime &dt, int32_t years) const;

	// Convenience arithmetic relative to now()
	DateTime addSeconds(int64_t seconds) const;
	DateTime addMinutes(int64_t minutes) const;
	DateTime addHours(int64_t hours) const;
	DateTime addDays(int32_t days) const;
	DateTime addMonths(int32_t months) const;
	DateTime addYears(int32_t years) const;

	DateTime subSeconds(int64_t seconds) const;
	DateTime subMinutes(int64_t minutes) const;
	DateTime subHours(int64_t hours) const;
	DateTime subDays(int32_t days) const;
	DateTime subMonths(int32_t months) const;
	DateTime subYears(int32_t years) const;

	DateTime addSecondsUtc(const DateTime &dt, int64_t seconds) const;
	DateTime addMinutesUtc(const DateTime &dt, int64_t minutes) const;
	DateTime addHoursUtc(const DateTime &dt, int64_t hours) const;
	DateTime addDaysUtc(const DateTime &dt, int32_t days) const;
	DateTime addMonthsUtc(const DateTime &dt, int32_t months) const;
	DateTime addYearsUtc(const DateTime &dt, int32_t years) const;
	DateTime subSecondsUtc(const DateTime &dt, int64_t seconds) const;
	DateTime subMinutesUtc(const DateTime &dt, int64_t minutes) const;
	DateTime subHoursUtc(const DateTime &dt, int64_t hours) const;
	DateTime subDaysUtc(const DateTime &dt, int32_t days) const;
	DateTime subMonthsUtc(const DateTime &dt, int32_t months) const;
	DateTime subYearsUtc(const DateTime &dt, int32_t years) const;

	// Differences
	int64_t differenceInSeconds(const DateTime &a, const DateTime &b) const;
	int64_t differenceInMinutes(const DateTime &a, const DateTime &b) const;
	int64_t differenceInHours(const DateTime &a, const DateTime &b) const;
	int64_t differenceInDays(const DateTime &a, const DateTime &b) const;
	int64_t diff(const DateTime &a, const DateTime &b, TempoDiff unit = TempoDiff::Seconds) const;

	// Comparisons
	bool isBefore(const DateTime &a, const DateTime &b) const;
	bool isAfter(const DateTime &a, const DateTime &b) const;
	bool isEqual(const DateTime &a, const DateTime &b) const; // seconds precision
	bool isEqualMinutes(
	    const DateTime &a, const DateTime &b
	) const; // minutes precision (UTC epoch / 60)
	bool isEqualMinutesUtc(
	    const DateTime &a, const DateTime &b
	) const; // alias for minute-level UTC compare
	bool isSameDay(const DateTime &a, const DateTime &b) const;
	bool isSameLocalDay(const DateTime &a, const DateTime &b) const;

	// Calendar helpers (UTC)
	DateTime startOfDayUtc(const DateTime &dt) const;
	DateTime endOfDayUtc(const DateTime &dt) const;
	DateTime startOfMonthUtc(const DateTime &dt) const;
	DateTime endOfMonthUtc(const DateTime &dt) const;

	int getYearUtc(const DateTime &dt) const;
	int getMonthUtc(const DateTime &dt) const;   // 1..12
	int getDayUtc(const DateTime &dt) const;     // 1..31
	int getWeekdayUtc(const DateTime &dt) const; // 0=Sunday..6=Saturday

	// Local time helpers (respect TZ)
	DateTime startOfDayLocal(const DateTime &dt) const;
	DateTime endOfDayLocal(const DateTime &dt) const;
	DateTime startOfMonthLocal(const DateTime &dt) const;
	DateTime endOfMonthLocal(const DateTime &dt) const;
	DateTime startOfYearUtc(const DateTime &dt) const;
	DateTime startOfYearLocal(const DateTime &dt) const;

	DateTime setTimeOfDayLocal(const DateTime &dt, int hour, int minute, int second) const;
	DateTime setTimeOfDayUtc(const DateTime &dt, int hour, int minute, int second) const;
	DateTime nextDailyAtLocal(int hour, int minute, int second, const DateTime &from) const;
	DateTime
	nextWeekdayAtLocal(int weekday, int hour, int minute, int second, const DateTime &from) const;

	int getYearLocal(const DateTime &dt) const;
	int getMonthLocal(const DateTime &dt) const;   // 1..12
	int getDayLocal(const DateTime &dt) const;     // 1..31
	int getWeekdayLocal(const DateTime &dt) const; // 0=Sunday..6=Saturday

	bool isLeapYear(int year) const;
	int daysInMonth(int year, int month) const; // month: 1..12

	// Formatting
	bool formatUtc(const DateTime &dt, TempoFormat style, char *outBuffer, size_t outSize) const;
	bool
	formatLocal(const DateTime &dt, TempoFormat style, char *outBuffer, size_t outSize) const;
	bool formatWithPatternUtc(
	    const DateTime &dt, const char *pattern, char *outBuffer, size_t outSize
	) const;
	bool formatWithPatternLocal(
	    const DateTime &dt, const char *pattern, char *outBuffer, size_t outSize
	) const;

	// String helpers (embedded-safe buffer first, then std::string convenience)
	bool dateTimeToStringUtc(
	    const DateTime &dt,
	    char *outBuffer,
	    size_t outSize,
	    TempoFormat style = TempoFormat::DateTime
	) const;
	bool dateTimeToStringLocal(
	    const DateTime &dt,
	    char *outBuffer,
	    size_t outSize,
	    TempoFormat style = TempoFormat::DateTime
	) const;
	bool localDateTimeToString(const LocalDateTime &dt, char *outBuffer, size_t outSize) const;
	bool nowUtcString(
	    char *outBuffer, size_t outSize, TempoFormat style = TempoFormat::DateTime
	) const;
	bool nowLocalString(
	    char *outBuffer, size_t outSize, TempoFormat style = TempoFormat::DateTime
	) const;
	bool lastNtpSyncStringUtc(
	    char *outBuffer, size_t outSize, TempoFormat style = TempoFormat::DateTime
	) const;
	bool lastNtpSyncStringLocal(
	    char *outBuffer, size_t outSize, TempoFormat style = TempoFormat::DateTime
	) const;

	std::string
	dateTimeToStringUtc(const DateTime &dt, TempoFormat style = TempoFormat::DateTime) const;
	std::string
	dateTimeToStringLocal(const DateTime &dt, TempoFormat style = TempoFormat::DateTime) const;
	std::string localDateTimeToString(const LocalDateTime &dt) const;
	std::string nowUtcString(TempoFormat style = TempoFormat::DateTime) const;
	std::string nowLocalString(TempoFormat style = TempoFormat::DateTime) const;
	std::string lastNtpSyncStringUtc(TempoFormat style = TempoFormat::DateTime) const;
	std::string lastNtpSyncStringLocal(TempoFormat style = TempoFormat::DateTime) const;

	struct ParseResult {
		bool ok;
		DateTime value;
	};

	ParseResult parseIso8601Utc(const char *str) const;
	ParseResult parseDateTimeLocal(const char *str) const;
	DateTime parseUtc(const char *str) const;
	LocalDateTime parseLocal(const char *str) const;

	// Sun cycle using stored configuration (lat/lon/timezone)
	TempoSunEventResult sunrise() const;
	TempoSunEventResult sunset() const;
	TempoSunEventResult sunrise(const DateTime &day) const;
	TempoSunEventResult sunset(const DateTime &day) const;

	// Sun cycle with explicit parameters (timezone in hours, DST flag)
	TempoSunEventResult sunrise(float latitude, float longitude, float timezoneHours, bool isDst) const;
	TempoSunEventResult sunset(float latitude, float longitude, float timezoneHours, bool isDst) const;
	TempoSunEventResult sunrise(
	    float latitude, float longitude, float timezoneHours, bool isDst, const DateTime &day
	) const;
	TempoSunEventResult sunset(
	    float latitude, float longitude, float timezoneHours, bool isDst, const DateTime &day
	) const;

	// Sun cycle using a POSIX TZ string (auto-DST) instead of numeric offset
	TempoSunEventResult sunrise(float latitude, float longitude, const char *timeZone) const;
	TempoSunEventResult sunset(float latitude, float longitude, const char *timeZone) const;
	TempoSunEventResult
	sunrise(float latitude, float longitude, const char *timeZone, const DateTime &day) const;
	TempoSunEventResult
	sunset(float latitude, float longitude, const char *timeZone, const DateTime &day) const;

	// Daylight checks using stored configuration
	bool isDay() const;
	bool isDay(const DateTime &day) const;
	bool isDay(const TempoDuration &sunRiseOffset, const TempoDuration &sunSetOffset) const;
	bool isDay(const DateTime &day, const TempoDuration &sunRiseOffset, const TempoDuration &sunSetOffset) const;
	bool isDay(int sunRiseOffsetSec, int sunSetOffsetSec) const;
	bool isDay(int sunRiseOffsetSec, int sunSetOffsetSec, const DateTime &day) const;
	TempoSunCycle sunCycleToday();
	DateTime sunRiseTodayUtc();
	DateTime sunSetTodayUtc();
	LocalDateTime sunRiseTodayLocal();
	LocalDateTime sunSetTodayLocal();
	bool isSunRise();
	bool isSunRise(const DateTime &dt);
	bool isSunRise(const TempoDuration &offset);
	bool isSunRise(const DateTime &dt, const TempoDuration &offset);
	bool isSunSet();
	bool isSunSet(const DateTime &dt);
	bool isSunSet(const TempoDuration &offset);
	bool isSunSet(const DateTime &dt, const TempoDuration &offset);

	// Daylight saving time helpers
	bool isDstActive() const;
	bool isDstActive(const DateTime &dt) const;
	bool isDstActive(const char *timeZone) const;
	bool isDstActive(const DateTime &dt, const char *timeZone) const;

	// Moon phase
	TempoMoonPhase moonPhase() const;
	TempoMoonPhase moonPhase(const DateTime &dt) const;

	// Month names
	const char *
	monthName(int month) const; // 1..12, returns "January" ..."December" or nullptr on invalid
	const char *monthName(const DateTime &dt) const;

  private:
#if defined(__has_include)
#if __has_include(<esp_sntp.h>)
	static void handleSntpSync(struct timeval *tv);
#endif
#endif
	void dispatchNtpSync(const DateTime &syncedAtUtc);
	void setNtpSyncCallbackCallable(const NtpSyncCallable &callback);
	void applyConfig(const TempoConfig &config);
	bool applyNtpConfig() const;
	bool hasAnyNtpServerConfigured() const;

	TempoSunEventResult sunriseFromConfig(const DateTime &day) const;
	TempoSunEventResult sunsetFromConfig(const DateTime &day) const;
	bool isDayWithOffsets(const DateTime &day, int sunRiseOffsetSec, int sunSetOffsetSec) const;
	TempoSunCycle sunCycleFor(const DateTime &day);
	bool refreshSunCycleCache(const DateTime &day);
	DateTime addCalendarDaysLocal(const DateTime &dt, int days) const;
	bool inSunWindow(const DateTime &dt, const DateTime &event, const TempoDuration &offset) const;

	float latitude_ = 0.0f;
	float longitude_ = 0.0f;
	DateString timeZone_;
	static constexpr size_t kMaxNtpServers = 3;
	DateString ntpServers_[kMaxNtpServers];
	uint32_t ntpSyncIntervalMs_ = 0;
	bool usePSRAMBuffers_ = false;
	DateTime lastNtpSync_{};
	bool hasLastNtpSync_ = false;
	NtpSyncCallback ntpSyncCallback_ = nullptr;
	NtpSyncCallable ntpSyncCallbackCallable_;
	struct NtpSyncListenerSlot {
		NtpSyncListenerId id = 0;
		NtpSyncCallable listener{};
	};
	static constexpr size_t kMaxNtpSyncListeners = 4;
	NtpSyncListenerSlot ntpSyncListeners_[kMaxNtpSyncListeners]{};
	NtpSyncListenerId nextNtpSyncListenerId_ = 1;
	static NtpSyncCallback activeNtpSyncCallback_;
	static NtpSyncCallable activeNtpSyncCallbackCallable_;
	static Tempo *activeNtpSyncOwner_;
	static std::recursive_mutex ntpMutex_;
	bool hasLocation_ = false;
	bool initialized_ = false;
	int64_t minValidUnixSeconds_ = 1577836800;
	uint32_t sunCycleMatchWindowSeconds_ = 60;
	mutable std::recursive_mutex sunCycleMutex_{};
	TempoSunCycle sunCycleCache_{};

  public:
	void _testDispatchNtpSync(const DateTime &syncedAtUtc) {
		dispatchNtpSync(syncedAtUtc);
	}
};
