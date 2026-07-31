// PWM backlight control for the CYD's LCD (LED on GPIO 21, active HIGH).
// TFT_eSPI drives this pin fully on in init(); begin() re-attaches it to an
// LEDC PWM channel so brightness can be set 0..100%. Call begin() AFTER
// DisplayTft::begin().
//
// set() changes brightness instantly; fadeTo() ramps to a level over a
// duration (non-blocking) and must be stepped by calling update() each loop.
#pragma once

#include <cstdint>

namespace dpf {

class Backlight {
 public:
  // Configure the LEDC PWM channel on the backlight pin and apply normalPct.
  void begin(uint8_t normalPct);
  // Set brightness as a percentage (clamped to 0..100), instantly; cancels any
  // fade in progress.
  void set(uint8_t pct);
  // Start a non-blocking ramp from the current level to pct over durationMs.
  void fadeTo(uint8_t pct, uint32_t durationMs, uint32_t nowMs);
  // Advance an active fade; call once per loop() with the current millis().
  void update(uint32_t nowMs);

 private:
  void write(uint32_t duty);

  bool fading_ = false;
  uint32_t startMs_ = 0;
  uint32_t durationMs_ = 0;
  int startDuty_ = 0;
  int targetDuty_ = 0;
  int curDuty_ = 0;
};

}  // namespace dpf
