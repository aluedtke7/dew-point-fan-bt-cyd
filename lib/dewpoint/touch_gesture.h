// Touch gesture classifier: turns raw XPT2046 samples into Tap / LongPress.
//
// Pure and unit-testable on the host; the SPI driver lives in
// `src/hardware/touch.*`. It separates genuine finger presses from electrical
// noise, which on the CYD shows up as bursts of saturated ADC readings
// (raw ~3900-4095 on both axes, clamped onto the bottom-right "Off" button)
// with a pressure that passes the plain z gate:
//   - samples with low pressure or raw values outside the panel's real range
//     are treated as "not touched" (one such dropout inside a press is
//     tolerated);
//   - until Tap, a press that moves more than kMaxDrift between two samples
//     restarts (lets a landing finger settle, keeps wandering noise out);
//     after Tap it must stay within kMaxDrift of where it settled;
//   - Tap and LongPress need both enough time and enough consecutive samples,
//     so a stalled loop can't turn two samples into a long press.
// State-changing buttons act only on LongPress.
#pragma once

namespace dpf {

// One poll of the touch controller. `down` is the controller's own
// "touched" flag; raw coordinates are only meaningful when it is set.
struct TouchSample {
  bool down = false;
  int rawX = 0;
  int rawY = 0;
  int z = 0;
};

enum class TouchEvent {
  None,
  Tap,        // press held kTapSeconds (fired once per press)
  LongPress,  // same press held kLongPressSeconds without moving
  Cancel,     // a press that fired Tap ended (release/drift) before LongPress
};

// Minimum pressure (XPT2046 Z). The library's own gate (400) lets noise through.
constexpr int kMinPressure = 600;
// Raw ADC window a real finger can produce. Field-logged noise bursts always
// had at least one axis >= 3960; keep kRawValidMax below that and above the
// raw range the screen is mapped to (kRawMax* in touch.cpp).
constexpr int kRawValidMin = 150;
constexpr int kRawValidMax = 3900;
// Maximum raw movement before a press restarts (pre-Tap) or is abandoned.
constexpr int kMaxDrift = 150;
// Implausible samples tolerated inside a press before it counts as released.
constexpr int kMaxDropouts = 1;
constexpr double kTapSeconds = 0.1;
constexpr int kTapSamples = 3;  // 3 polls at 50 ms
constexpr double kLongPressSeconds = 0.8;
constexpr int kLongPressSamples = 16;  // 80 % of the polls in 0.8 s

// True when the sample looks like a finger: down, firm, inside the raw window.
bool isPlausible(const TouchSample& s);

class TouchGesture {
 public:
  // Feeds one poll taken at `now` (monotonic seconds) and returns at most one
  // event.
  TouchEvent update(const TouchSample& s, double now);

  // Raw position of the current/last press (where it settled at Tap).
  int rawX() const { return x0_; }
  int rawY() const { return y0_; }

 private:
  void start(const TouchSample& s, double now);

  bool pressing_ = false;
  bool aborted_ = false;  // drifted after Tap: ignore until release
  bool tapSent_ = false;
  bool longSent_ = false;
  int samples_ = 0;   // plausible samples in this press
  int dropouts_ = 0;  // consecutive implausible samples
  double start_ = 0;
  int x0_ = 0;
  int y0_ = 0;
};

}  // namespace dpf
