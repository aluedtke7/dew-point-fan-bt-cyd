#include "touch_gesture.h"

#include <cstdlib>

namespace dpf {

bool isPlausible(const TouchSample& s) {
  return s.down && s.z >= kMinPressure && s.rawX >= kRawValidMin &&
         s.rawX <= kRawValidMax && s.rawY >= kRawValidMin &&
         s.rawY <= kRawValidMax;
}

void TouchGesture::start(const TouchSample& s, double now) {
  pressing_ = true;
  aborted_ = tapSent_ = longSent_ = false;
  samples_ = 1;
  dropouts_ = 0;
  start_ = now;
  x0_ = s.rawX;
  y0_ = s.rawY;
}

TouchEvent TouchGesture::update(const TouchSample& s, double now) {
  if (!isPlausible(s)) {
    if (!pressing_) return TouchEvent::None;
    if (dropouts_ < kMaxDropouts) {
      ++dropouts_;
      return TouchEvent::None;
    }
    const bool cancel = tapSent_ && !longSent_ && !aborted_;
    pressing_ = false;
    return cancel ? TouchEvent::Cancel : TouchEvent::None;
  }

  if (!pressing_) {
    start(s, now);
    return TouchEvent::None;
  }
  dropouts_ = 0;
  if (aborted_) return TouchEvent::None;

  const bool drifted =
      std::abs(s.rawX - x0_) > kMaxDrift || std::abs(s.rawY - y0_) > kMaxDrift;
  if (!tapSent_) {
    // Still settling: follow the finger, restart on a jump.
    if (drifted) {
      start(s, now);
      return TouchEvent::None;
    }
    x0_ = s.rawX;
    y0_ = s.rawY;
  } else if (drifted) {
    aborted_ = true;
    return longSent_ ? TouchEvent::None : TouchEvent::Cancel;
  }

  ++samples_;
  const double held = now - start_;
  if (!tapSent_ && samples_ >= kTapSamples && held >= kTapSeconds) {
    tapSent_ = true;
    return TouchEvent::Tap;
  }
  if (tapSent_ && !longSent_ && samples_ >= kLongPressSamples &&
      held >= kLongPressSeconds) {
    longSent_ = true;
    return TouchEvent::LongPress;
  }
  return TouchEvent::None;
}

}  // namespace dpf
