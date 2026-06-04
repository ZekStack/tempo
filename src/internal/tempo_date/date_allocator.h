#pragma once

#if __has_include(<ESPBufferManager.h>)
#include <ESPBufferManager.h>
#define ESP_DATE_HAS_BUFFER_MANAGER 1
#elif __has_include(<esp_buffer_manager/buffer_manager.h>)
#include <esp_buffer_manager/buffer_manager.h>
#define ESP_DATE_HAS_BUFFER_MANAGER 1
#else
#define ESP_DATE_HAS_BUFFER_MANAGER 0
#endif

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>

namespace date_allocator_detail {
inline void *allocate(std::size_t bytes, bool usePSRAMBuffers) noexcept {
#if ESP_DATE_HAS_BUFFER_MANAGER
	return ESPBufferManager::allocate(bytes, usePSRAMBuffers);
#else
	(void)usePSRAMBuffers;
	return std::malloc(bytes);
#endif
}

inline void deallocate(void *ptr) noexcept {
#if ESP_DATE_HAS_BUFFER_MANAGER
	ESPBufferManager::deallocate(ptr);
#else
	std::free(ptr);
#endif
}
} // namespace date_allocator_detail

template <typename T> class DateAllocator {
  public:
	using value_type = T;

	DateAllocator() noexcept = default;
	explicit DateAllocator(bool usePSRAMBuffers) noexcept : usePSRAMBuffers_(usePSRAMBuffers) {
	}

	template <typename U>
	DateAllocator(const DateAllocator<U> &other) noexcept
	    : usePSRAMBuffers_(other.usePSRAMBuffers()) {
	}

	T *allocate(std::size_t n) {
		if (n == 0) {
			return nullptr;
		}
		if (n > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
			return nullptr;
		}

		void *memory = date_allocator_detail::allocate(n * sizeof(T), usePSRAMBuffers_);
		if (memory == nullptr) {
			return nullptr;
		}
		return static_cast<T *>(memory);
	}

	void deallocate(T *ptr, std::size_t) noexcept {
		date_allocator_detail::deallocate(ptr);
	}

	bool usePSRAMBuffers() const noexcept {
		return usePSRAMBuffers_;
	}

	template <typename U> bool operator==(const DateAllocator<U> &other) const noexcept {
		return usePSRAMBuffers_ == other.usePSRAMBuffers();
	}

	template <typename U> bool operator!=(const DateAllocator<U> &other) const noexcept {
		return !(*this == other);
	}

  private:
	template <typename> friend class DateAllocator;

	bool usePSRAMBuffers_ = false;
};

using DateString = std::basic_string<char, std::char_traits<char>, DateAllocator<char>>;
