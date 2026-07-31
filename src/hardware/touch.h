// Resistive touch (XPT2046) driver on the CYD's dedicated VSPI bus.
// Reports a fresh press as a point in screen pixels (landscape, rotation 1);
// the caller (main.cpp) decides what each region does — the bottom button row
// (Auto/On/Off) and the top banner (switch screen). Only the rising edge of a
// press is reported.
#pragma once

namespace dpf {

// A touch sample. `pressed` is true only on the first poll of a new press;
// `x`/`y` are then valid screen pixels (0..319, 0..239).
struct TouchPoint {
  bool pressed = false;
  int x = 0;
  int y = 0;
};

class Touch {
 public:
  void begin();
  // Returns a TouchPoint with pressed=true once per fresh press, else pressed=false.
  TouchPoint poll();

 private:
  bool wasTouched_ = false;
  int debounceCount_ = 0;
  static constexpr int kDebouncePolls = 3;  // 3 × 50ms = 150ms sustained
};

}  // namespace dpf
