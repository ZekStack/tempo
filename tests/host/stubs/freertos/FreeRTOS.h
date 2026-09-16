#pragma once

typedef unsigned int TickType_t;
typedef unsigned int StackType_t;

#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
