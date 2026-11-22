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
// Real presses must map inside the plausibility window, or the screen edges
// become unreachable (see kRawValidMax in touch_gesture.h).
static_assert(kRawMinX >= kRawValidMin && kRawMaxX <= kRawValidMax &&
                  kRawMinY >= kRawValidMin && kRawMaxY <= kRawValidMax,
              "touch calibration must lie inside the plausible raw window");
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;

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

TouchPoint Touch::poll(double now) {
  TouchSample s;
  if (ts.touched()) {
    const TS_Point p = ts.getPoint();
    s = {true, p.x, p.y, p.z};
  }

#if CORE_DEBUG_LEVEL >= 2
  // One line per noise burst, so rejected phantoms stay visible in the log.
  const bool rejected = s.down && s.z >= kMinPressure && !isPlausible(s);
  if (rejected && !wasRejected_) {
    logf("[touch] rejected raw=(%d,%d) z=%d\n", s.rawX, s.rawY, s.z);
  }
  wasRejected_ = rejected;
#endif

  TouchPoint tp;
  tp.event = gesture_.update(s, now);
  if (tp.event == TouchEvent::None) return tp;
  tp.x = mapClamp(gesture_.rawX(), kRawMinX, kRawMaxX, kScreenW - 1);
  tp.y = mapClamp(gesture_.rawY(), kRawMinY, kRawMaxY, kScreenH - 1);
#if CORE_DEBUG_LEVEL >= 2
  if (tp.event != TouchEvent::Cancel) {
    logf("[touch] %s raw=(%d,%d) px=(%d,%d) z=%d\n",
         tp.event == TouchEvent::Tap ? "tap" : "long", gesture_.rawX(),
         gesture_.rawY(), tp.x, tp.y, s.z);
  }
#endif
  return tp;
}

}  // namespace dpf
