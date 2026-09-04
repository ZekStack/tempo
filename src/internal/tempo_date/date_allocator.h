#pragma once

#include <Strata.h>

#include <cstddef>
#include <limits>
#include <string>

template <typename T> class DateAllocator {
  public:
	using value_type = T;

	DateAllocator() noexcept = default;
	explicit DateAllocator(Strata::Placement placement) noexcept : placement_(placement) {
	}

	template <typename U>
	DateAllocator(const DateAllocator<U> &other) noexcept : placement_(other.placement()) {
	}

	T *allocate(std::size_t n) {
		if (n == 0 || n > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
			return nullptr;
		}
		return Strata::allocateArray<T>(n, placement_);
	}

	void deallocate(T *ptr, std::size_t) noexcept {
		Strata::free(ptr);
	}

	Strata::Placement placement() const noexcept {
		return placement_;
	}

	template <typename U> bool operator==(const DateAllocator<U> &other) const noexcept {
		return placement_ == other.placement();
	}

	template <typename U> bool operator!=(const DateAllocator<U> &other) const noexcept {
		return !(*this == other);
	}

  private:
	template <typename> friend class DateAllocator;

	Strata::Placement placement_ = Strata::Placement::PreferExternal;
};

using DateString = std::basic_string<char, std::char_traits<char>, DateAllocator<char>>;
