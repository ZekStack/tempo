#pragma once

#include "date.h"
#include "date_allocator.h"

#include <cstdlib>
#include <ctime>
#include <limits>
#include <mutex>

class TempoUtils {
  public:
	static constexpr int64_t kSecondsPerMinute = 60;
	static constexpr int64_t kSecondsPerHour = 60 * kSecondsPerMinute;
	static constexpr int64_t kSecondsPerDay = 24 * kSecondsPerHour;

	class TimezoneLock {
	  public:
		TimezoneLock() : lock_(timezoneMutex()) {
		}

	  private:
		std::unique_lock<std::recursive_mutex> lock_;
	};

	class ScopedTz {
	  public:
		explicit ScopedTz(const char *tz, bool usePSRAMBuffers = false)
		    : lock_(timezoneMutex()), previous_(DateAllocator<char>(usePSRAMBuffers)) {
			if (!tz) {
				return;
			}

			const char *current = std::getenv("TZ");
			if (current) {
				hadPrevious_ = true;
				previous_ = current;
			}

#if defined(_WIN32)
			_putenv_s("TZ", tz);
			_tzset();
#else
			setenv("TZ", tz, 1);
			tzset();
#endif
			active_ = true;
		}

		~ScopedTz() {
			if (!active_) {
				return;
			}

			if (hadPrevious_) {
#if defined(_WIN32)
				_putenv_s("TZ", previous_.c_str());
#else
				setenv("TZ", previous_.c_str(), 1);
#endif
			} else {
#if defined(_WIN32)
				_putenv_s("TZ", "");
#else
				unsetenv("TZ");
#endif
			}
#if defined(_WIN32)
			_tzset();
#else
			tzset();
#endif
		}

	  private:
		std::unique_lock<std::recursive_mutex> lock_;
		bool active_ = false;
		bool hadPrevious_ = false;
		DateString previous_;
	};

	static bool toUtcTm(const DateTime &dt, tm &out) {
		if (dt.epochSeconds > static_cast<int64_t>(std::numeric_limits<time_t>::max()) ||
		    dt.epochSeconds < static_cast<int64_t>(std::numeric_limits<time_t>::min())) {
			return false;
		}
		time_t raw = static_cast<time_t>(dt.epochSeconds);
#if defined(_WIN32)
		return gmtime_s(&out, &raw) == 0;
#else
		return gmtime_r(&raw, &out) != nullptr;
#endif
	}

	static bool toLocalTm(const DateTime &dt, tm &out) {
		TimezoneLock lock;
		if (dt.epochSeconds > static_cast<int64_t>(std::numeric_limits<time_t>::max()) ||
		    dt.epochSeconds < static_cast<int64_t>(std::numeric_limits<time_t>::min())) {
			return false;
		}
		time_t raw = static_cast<time_t>(dt.epochSeconds);
#if defined(_WIN32)
		return localtime_s(&out, &raw) == 0;
#else
		return localtime_r(&raw, &out) != nullptr;
#endif
	}

	static DateTime fromUtcTm(const tm &t) {
		return DateTime{timegm64(t)};
	}

	static DateTime fromLocalTm(const tm &t) {
		TimezoneLock lock;
		tm copy = t;
		time_t raw = mktime(&copy);
		return DateTime{static_cast<int64_t>(raw)};
	}

	static int64_t timegm64(const tm &t) {
		const int year = t.tm_year + 1900;
		const unsigned month = static_cast<unsigned>(t.tm_mon + 1);
		const unsigned day = static_cast<unsigned>(t.tm_mday);

		const int64_t days = daysFromCivil(year, month, day);
		const int64_t seconds =
		    days * kSecondsPerDay + static_cast<int64_t>(t.tm_hour) * kSecondsPerHour +
		    static_cast<int64_t>(t.tm_min) * kSecondsPerMinute + static_cast<int64_t>(t.tm_sec);
		return seconds;
	}

	static bool validHms(int hour, int minute, int second) {
		return hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60;
	}

	static int clampDay(int year, int month, int day, const Tempo &dateHelper) {
		const int maxDay = dateHelper.daysInMonth(year, month);
		if (maxDay <= 0) {
			return day;
		}
		if (day < 1) {
			return 1;
		}
		if (day > maxDay) {
			return maxDay;
		}
		return day;
	}

	static void setProcessTimeZone(const char *timeZone) {
		TimezoneLock lock;
		const char *tz = (timeZone && timeZone[0] != '\0') ? timeZone : "UTC0";
#if defined(_WIN32)
		_putenv_s("TZ", tz);
		_tzset();
#else
		setenv("TZ", tz, 1);
		tzset();
#endif
	}

	static bool
	isDstActiveFor(const DateTime &dt, const char *timeZone, bool usePSRAMBuffers = false) {
		ScopedTz scoped(timeZone, usePSRAMBuffers);
		tm local{};
		if (!toLocalTm(dt, local)) {
			return false;
		}
		return local.tm_isdst > 0;
	}

	static bool parseIntSlice(const char *str, int length, int min, int max, int &out) {
		if (!str || length <= 0) {
			return false;
		}

		int value = 0;
		for (int i = 0; i < length; ++i) {
			const char c = str[i];
			if (c < '0' || c > '9') {
				return false;
			}
			value = value * 10 + (c - '0');
		}

		if (value < min || value > max) {
			return false;
		}
		out = value;
		return true;
	}

  private:
	static std::recursive_mutex &timezoneMutex() {
		static std::recursive_mutex mutex;
		return mutex;
	}

	static int64_t daysFromCivil(int year, unsigned month, unsigned day) {
		year -= month <= 2;
		const int era = (year >= 0 ? year : year - 399) / 400;
		const unsigned yoe = static_cast<unsigned>(year - era * 400);
		const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
		const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + static_cast<int>(doe) - 719468;
	}
};
