// Pure value/unit formatting for the dashboard screen strings.
//
// Host-testable (no Arduino includes); the TFT drawing that consumes these
// strings lives in `src/hardware/display_tft.*`. The degree symbol is the
// backtick byte 0x60, which TFT_eSPI's Font2 renders as `°` (see
// TFT_ESPI_GRAVE_IS_DEGREE in Font16.c).
//
// All functions write into a caller-provided char buffer (zero heap allocation).
// They return the number of characters written (excluding null terminator).
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace dpf {

// A measured value with a one-decimal fixed format and an appended unit:
//   valid  -> "%.1f<unit>"   e.g. formatMeasure(buf, cap, 26.0, "`C", true) -> "26.0`C"
//   !valid -> "--"
size_t formatMeasure(char* buf, size_t cap, double value, const char* unit,
                     bool valid);

// The dew-point difference line:
//   valid  -> "Diff %.1f`C  (thr %.1f)"
//   !valid -> "Diff --  (thr %.1f)"
size_t formatDiff(char* buf, size_t cap, double diff, double thr, bool valid);

// A log-line timestamp: "dd.MM.yy HH:mm:ss" (24h, configured TZ via localtime).
// Before the clock is NTP-synced (epoch below the sync threshold used across the
// firmware) this writes "00.00.00 00:00:00".
size_t formatLogTimestamp(char* buf, size_t cap, time_t epoch);

// Heap metrics suffix for log lines: "heap=<free> min=<minFree> blk=<largest>".
// freeHeap   — current free heap in bytes (ESP.getFreeHeap())
// minFreeHeap — lifetime low-watermark of free heap (ESP.getMinFreeHeap()),
//               the best slow-leak indicator
// largestBlock — largest allocatable 8-bit block; when this shrinks while
//                `free` stays high, the heap is fragmenting
size_t formatHeapSuffix(char* buf, size_t cap, uint32_t freeHeap,
                        uint32_t minFreeHeap, uint32_t largestBlock);

}  // namespace dpf
