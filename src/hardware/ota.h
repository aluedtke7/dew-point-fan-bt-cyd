// Over-the-air firmware + filesystem updates (ArduinoOTA / espota).
// Lets `pio run -e cyd_ota -t upload` (firmware) and `-t uploadfs` (LittleFS
// config) flash the device over WiFi. Only usable when WiFi is up; the first
// image carrying this code must still be flashed over USB.
#pragma once

namespace dpf {

// Start the OTA responder (hostname "dpf-cyd"). If password is non-empty it is
// required for uploads. Call once after WiFi is connected.
void otaBegin(const char* password);

// Service pending OTA requests. Call frequently from loop() while WiFi is up.
void otaHandle();

}  // namespace dpf
