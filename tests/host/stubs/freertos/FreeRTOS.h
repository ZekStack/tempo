#pragma once

#include <cstdint>
#include <limits>

using TickType_t = uint32_t;
using StackType_t = uint32_t;

static constexpr TickType_t portMAX_DELAY = std::numeric_limits<TickType_t>::max();

#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
