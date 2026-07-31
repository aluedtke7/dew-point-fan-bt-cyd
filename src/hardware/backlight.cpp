#include "backlight.h"

#include <Arduino.h>

namespace dpf {

namespace {
constexpr int kBlPin = 21;      // TFT_BL, active HIGH
constexpr int kChannel = 0;     // LEDC channel (unused by fan / TFT_eSPI here)
constexpr int kFreqHz = 5000;   // high enough to avoid visible flicker
constexpr int kResBits = 8;     // 0..255 duty
constexpr int kDutyMax = (1 << kResBits) - 1;

int pctToDuty(uint8_t pct) {
  if (pct > 100) pct = 100;
  return static_cast<int>(pct) * kDutyMax / 100;
}
}  // namespace

void Backlight::begin(uint8_t normalPct) {
  ledcSetup(kChannel, kFreqHz, kResBits);
  ledcAttachPin(kBlPin, kChannel);
  set(normalPct);
}

void Backlight::set(uint8_t pct) {
  fading_ = false;
  write(pctToDuty(pct));
}

void Backlight::fadeTo(uint8_t pct, uint32_t durationMs, uint32_t nowMs) {
  const int target = pctToDuty(pct);
  if (durationMs == 0) {
    set(pct);
    return;
  }
  startDuty_ = curDuty_;
  targetDuty_ = target;
  startMs_ = nowMs;
  durationMs_ = durationMs;
  fading_ = true;
}

void Backlight::update(uint32_t nowMs) {
  if (!fading_) return;
  const uint32_t elapsed = nowMs - startMs_;
  if (elapsed >= durationMs_) {
    write(targetDuty_);
    fading_ = false;
    return;
  }
  const int duty =
      startDuty_ + (targetDuty_ - startDuty_) * static_cast<int>(elapsed) /
                       static_cast<int>(durationMs_);
  write(duty);
}

void Backlight::write(uint32_t duty) {
  if (static_cast<int>(duty) == curDuty_) return;  // avoid redundant writes
  curDuty_ = static_cast<int>(duty);
  ledcWrite(kChannel, duty);
}

}  // namespace dpf
