#include "touch.h"

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "logging.h"

namespace dpf {

namespace {
// XPT2046 touch is on VSPI, a SEPARATE bus from the ILI9341 display (HSPI).
constexpr int kTouchSclk = 25;
constexpr int kTouchMiso = 39;
constexpr int kTouchMosi = 32;
constexpr int kTouchCs = 33;
constexpr int kTouchIrq = 36;

// Raw XPT2046 ADC range mapped onto the 320x240 landscape (rotation 1). These
// are approximate; if the on-screen buttons feel mis-hit, tune them from the
// raw values printed at CORE_DEBUG_LEVEL >= 2. Swap min/max to flip an axis.
constexpr int kRawMinX = 300;
constexpr int kRawMaxX = 3800;
constexpr int kRawMinY = 300;
constexpr int kRawMaxY = 3800;
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;

// Minimum touch pressure (XPT2046 Z) to accept a press. The library's own
// touched() gate is Z_THRESHOLD=400, which is too low on many CYD boards: noise
// spikes barely cross it and register as phantom taps (consistently near the
// bottom-right "Off" button). Requiring a firmer press here rejects that noise.
// Tune from the z= values printed at CORE_DEBUG_LEVEL >= 2 if genuine taps are
// missed (lower it) or phantoms persist (raise it).
constexpr int kMinPressure = 600;

SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(kTouchCs, kTouchIrq);

int mapClamp(int raw, int lo, int hi, int outMax) {
  if (hi == lo) return 0;
  long v = static_cast<long>(raw - lo) * outMax / (hi - lo);
  if (v < 0) return 0;
  if (v > outMax) return outMax;
  return static_cast<int>(v);
}
}  // namespace

void Touch::begin() {
  touchSpi.begin(kTouchSclk, kTouchMiso, kTouchMosi, kTouchCs);
  ts.begin(touchSpi);
  ts.setRotation(1);  // match the display landscape orientation
}

TouchPoint Touch::poll() {
  if (!ts.touched()) {
    wasTouched_ = false;
    debounceCount_ = 0;
    return {};
  }

  TS_Point p = ts.getPoint();
  // Reject low-pressure reads: these are electrical noise, not real presses.
  if (p.z < kMinPressure) {
    debounceCount_ = 0;
    return {};
  }

  // Require the touch to be sustained across multiple consecutive polls before
  // accepting it. A single noise spike lasting 1-2 polls is silently dropped;
  // a real finger press (150ms+) easily clears the threshold.
  debounceCount_++;
  if (debounceCount_ < kDebouncePolls) return {};

  if (wasTouched_) return {};  // still held; only act on the rising edge
  wasTouched_ = true;

  TouchPoint tp;
  tp.pressed = true;
  tp.x = mapClamp(p.x, kRawMinX, kRawMaxX, kScreenW - 1);
  tp.y = mapClamp(p.y, kRawMinY, kRawMaxY, kScreenH - 1);
#if CORE_DEBUG_LEVEL >= 2
  logf("[touch] raw=(%d,%d) px=(%d,%d) z=%d\n", p.x, p.y, tp.x, tp.y, p.z);
#endif
  return tp;
}

}  // namespace dpf
