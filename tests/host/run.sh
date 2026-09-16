#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/.host-test-build"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

CXX="${CXX:-g++}"
CXX_FLAGS=(
  -std=c++20
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -fsanitize=address,undefined
  -fno-omit-frame-pointer
  -pthread
  -I"${ROOT_DIR}/tests/host/stubs"
  -I"${ROOT_DIR}/src"
)

DATE_SCHEDULE_SOURCES=(
  "${ROOT_DIR}/src/internal/tempo_date/date.cpp"
  "${ROOT_DIR}/src/internal/tempo_date/location.cpp"
  "${ROOT_DIR}/src/internal/tempo_date/sun.cpp"
  "${ROOT_DIR}/src/internal/tempo_date/moon.cpp"
  "${ROOT_DIR}/src/internal/tempo_scheduler/schedule/schedule_field.cpp"
  "${ROOT_DIR}/src/internal/tempo_scheduler/schedule/schedule_spec.cpp"
  "${ROOT_DIR}/src/internal/tempo_scheduler/schedule/schedule_calculator.cpp"
)

"${CXX}" \
  "${CXX_FLAGS[@]}" \
  "${DATE_SCHEDULE_SOURCES[@]}" \
  "${ROOT_DIR}/tests/host/test_date_schedule.cpp" \
  -o "${BUILD_DIR}/tempo-date-schedule-tests"

"${CXX}" \
  "${CXX_FLAGS[@]}" \
  "${DATE_SCHEDULE_SOURCES[@]}" \
  "${ROOT_DIR}/src/internal/tempo_scheduler/core/scheduler_core.cpp" \
  "${ROOT_DIR}/tests/host/test_scheduler_core.cpp" \
  -o "${BUILD_DIR}/tempo-scheduler-tests"

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  "${BUILD_DIR}/tempo-date-schedule-tests"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  "${BUILD_DIR}/tempo-scheduler-tests"
