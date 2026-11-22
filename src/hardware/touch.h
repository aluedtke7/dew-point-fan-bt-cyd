// Resistive touch (XPT2046) driver on the CYD's dedicated VSPI bus.
// Reports gestures (see lib/dewpoint/touch_gesture.h) at a point in screen
// pixels (landscape, rotation 1); the caller (main.cpp) decides what each
// region does — the bottom button row (Auto/On/Off, long press only) and the
// top banner (switch screen).
#pragma once

#include "touch_gesture.h"

namespace dpf {

// A touch gesture. When `event` is not None, `x`/`y` are the screen pixels
// (0..319, 0..239) where the press settled.
struct TouchPoint {
  TouchEvent event = TouchEvent::None;
  int x = 0;
  int y = 0;
};

class Touch {
 public:
  void begin();
  // Samples the controller once; `now` is monotonic seconds (esp_timer).
  TouchPoint poll(double now);

 private:
  TouchGesture gesture_;
#if CORE_DEBUG_LEVEL >= 2
  bool wasRejected_ = false;  // debug log: one line per noise burst
#endif
};

}  // namespace dpf
