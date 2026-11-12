#pragma once

#include <cstddef>
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;

static constexpr BaseType_t tskNO_AFFINITY = -1;

// Arduino exposes this macro through Arduino.h.
#define radians(deg) ((deg) * 0.017453292519943295)
