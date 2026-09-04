#pragma once

#include <cstddef>
#include <cstdlib>
#include <limits>

namespace Strata {

enum class Placement : unsigned char {
	Default = 0,
	Internal,
	PreferExternal,
	RequireExternal,
};

struct MemoryPolicy {
	Placement allocation{Placement::Default};
	Placement taskStack{Placement::Internal};
};

inline bool validPlacement(Placement placement) noexcept {
	switch (placement) {
	case Placement::Default:
	case Placement::Internal:
	case Placement::PreferExternal:
	case Placement::RequireExternal:
		return true;
	}
	return false;
}

inline bool validMemoryPolicy(const MemoryPolicy &policy) noexcept {
	return validPlacement(policy.allocation) && validPlacement(policy.taskStack);
}

template <typename T>
T *allocateArray(std::size_t count, Placement) noexcept {
	if (count == 0 || count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
		return nullptr;
	}
	return static_cast<T *>(std::malloc(count * sizeof(T)));
}

inline void free(void *ptr) noexcept {
	std::free(ptr);
}

} // namespace Strata
