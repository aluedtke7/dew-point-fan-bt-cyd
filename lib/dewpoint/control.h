// Fan decision logic. Ported 1:1 from `computeResults` in Go `dpf-main.go`
// (via the MicroPython `dewpoint/control.py`).
#pragma once

#include "sensordata.h"

namespace dpf {

// A reading older than this many seconds is treated as stale. Matches the Go
// `time.Now().Add(-5 * time.Minute)` freshness window.
constexpr double kFreshnessSeconds = 5.0 * 60.0;

// Update `result.shouldBeOn` and `result.reason` in place using a strict
// priority tree (first matching rule wins). The hysteresis "in between" band
// deliberately leaves `shouldBeOn` unchanged so the relay does not chatter.
//
// remoteOverride: 0 = auto, 1 = force on, 2 = force off.
// now: current time in epoch seconds (same clock as SensorData.scanned).
void computeResults(const SensorData& inside, const SensorData& outside,
                    ResultData& result, const FanConfig& config,
                    int remoteOverride, double now);

}  // namespace dpf
