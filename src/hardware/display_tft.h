// TFT display driver for the CYD's ILI9341 (via TFT_eSPI).
//
// Two graphical screens (Dashboard + Diagnostics) drawn directly from the data
// structs, plus the original `showLines()` text renderer kept for boot / config
// error / start states. No LVGL — a plain TFT_eSPI label keeps RAM free for the
// BLE (and optional WiFi) radio. Rendering is flicker-free: the static chrome is
// drawn once per screen and only changed value fields are repainted in place.
#pragma once

#include <string>

#include "control.h"
#include "sensordata.h"

namespace dpf {

// Screen ids also used by main.cpp to track the active screen.
enum class Screen { Boot, Dashboard, Diagnostics };

// Geometry of the on-screen touch targets, shared with main.cpp so touch
// regions and drawn widgets stay in sync. Landscape, 320x240.
namespace ui {
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kBannerH = 34;   // top banner: tap to switch Dashboard/Diagnostics
constexpr int kBtnY = 205;     // bottom button row: Auto / On / Off
constexpr int kBtnH = 33;
constexpr int kBtnCount = 3;
}  // namespace ui

class DisplayTft {
 public:
  void begin();

  // Simple 4-line text screen (boot banner, CONFIG ERROR, start info).
  void showLines(const ScreenLines& lines);

  // Live dashboard: fan state + Inside/Outside temperature, humidity, dew
  // point, the dew-point difference vs. threshold, the decision reason, and the
  // Auto/On/Off button row.
  void showDashboard(const SensorData& inside, const SensorData& outside,
                     const ResultData& result, const FanConfig& fanConfig,
                     int overrideMode, double now);

  // Diagnostics: RSSI, battery, uptime, last-seen per sensor plus IP/version,
  // sharing the same Auto/On/Off button row.
  void showDiagnostics(const SensorData& inside, const SensorData& outside,
                       int overrideMode, const std::string& ip,
                       const std::string& ssid, bool online, double now);

 private:
  Screen screen_ = Screen::Boot;
  uint16_t bannerColor_ = 0;  // last banner fill; forces a repaint on change
  int buttonMode_ = -1;       // last override mode drawn on the button row

  void drawButtons(int overrideMode);
};

}  // namespace dpf
