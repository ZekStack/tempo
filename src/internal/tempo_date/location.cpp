#include "date.h"
#include "utils.h"

#include <cmath>
#include <mutex>

TempoResult Tempo::updateLocation(
    float latitude,
    float longitude,
    const char *timeZone
) {
	if (!initialized_) {
		return TempoResult::failure(
		    TempoStatus::NotInitialized,
		    "tempo is not initialized"
		);
	}
	if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90.0f ||
	    latitude > 90.0f || longitude < -180.0f || longitude > 180.0f ||
	    timeZone == nullptr || timeZone[0] == '\0') {
		return TempoResult::failure(
		    TempoStatus::InvalidArgument,
		    "invalid location configuration"
		);
	}

	std::lock_guard<std::recursive_mutex> ntpLock(ntpMutex_);
	std::lock_guard<std::recursive_mutex> sunCycleLock(sunCycleMutex_);
	latitude_ = latitude;
	longitude_ = longitude;
	hasLocation_ = true;
	timeZone_ = timeZone;
	sunCycleCache_ = TempoSunCycle{};
	TempoUtils::setProcessTimeZone(timeZone_.c_str());
	return TempoResult::success("tempo location updated");
}
