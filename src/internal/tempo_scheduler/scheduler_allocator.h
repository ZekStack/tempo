#pragma once

#include <Strata.h>

#include <cstddef>
#include <limits>
#include <new>
#include <utility>
#include <vector>

template <typename T>
T *schedulerAllocate(
    std::size_t count,
    Strata::Placement placement = Strata::Placement::PreferExternal
) noexcept {
	if (count == 0 || count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
		return nullptr;
	}
	return Strata::allocateArray<T>(count, placement);
}

template <typename T> void schedulerDeallocate(T *ptr) noexcept {
	Strata::free(ptr);
}

template <typename T>
T *schedulerReallocate(
    T *ptr,
    std::size_t oldCount,
    std::size_t newCount,
    Strata::Placement placement = Strata::Placement::PreferExternal
) noexcept {
	if (newCount == 0) {
		schedulerDeallocate(ptr);
		return nullptr;
	}
	if (newCount > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
		return nullptr;
	}

	T *next = schedulerAllocate<T>(newCount, placement);
	if (!next) {
		return nullptr;
	}
	if (ptr) {
		const std::size_t toMove = oldCount < newCount ? oldCount : newCount;
		for (std::size_t index = 0; index < toMove; ++index) {
			new (&next[index]) T(std::move(ptr[index]));
			ptr[index].~T();
		}
		schedulerDeallocate(ptr);
	}
	return next;
}

template <typename T> class SchedulerAllocator {
  public:
	using value_type = T;

	SchedulerAllocator() noexcept = default;
	explicit SchedulerAllocator(Strata::Placement placement) noexcept : placement_(placement) {
	}

	template <typename U>
	SchedulerAllocator(const SchedulerAllocator<U> &other) noexcept : placement_(other.placement()) {
	}

	T *allocate(std::size_t n) {
		return schedulerAllocate<T>(n, placement_);
	}

	void deallocate(T *ptr, std::size_t) noexcept {
		schedulerDeallocate(ptr);
	}

	Strata::Placement placement() const noexcept {
		return placement_;
	}

	template <typename U> bool operator==(const SchedulerAllocator<U> &other) const noexcept {
		return placement_ == other.placement();
	}

	template <typename U> bool operator!=(const SchedulerAllocator<U> &other) const noexcept {
		return !(*this == other);
	}

  private:
	template <typename> friend class SchedulerAllocator;

	Strata::Placement placement_ = Strata::Placement::PreferExternal;
};

template <typename T> using SchedulerVector = std::vector<T, SchedulerAllocator<T>>;
