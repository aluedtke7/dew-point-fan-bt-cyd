#include "ota.h"

#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>

#include "logging.h"

namespace dpf {

namespace {
constexpr const char* kHostname = "dpf-cyd";
}  // namespace

void otaBegin(const char* password) {
  ArduinoOTA.setHostname(kHostname);
  if (password && password[0] != '\0') ArduinoOTA.setPassword(password);

  ArduinoOTA.onStart([]() {
    // Free the BLE controller's RAM so the flash write has headroom on this
    // no-PSRAM board; the device reboots into the new image when done anyway.
    NimBLEDevice::deinit(true);
    // A filesystem update rewrites the LittleFS partition — unmount it first.
    if (ArduinoOTA.getCommand() == U_SPIFFS) LittleFS.end();
    logf("[ota] start\n");
  });
  ArduinoOTA.onEnd([]() { logf("[ota] done\n"); });
  ArduinoOTA.onError([](ota_error_t error) {
    logf("[ota] error %u\n", error);
  });

  ArduinoOTA.begin();
}

void otaHandle() { ArduinoOTA.handle(); }

}  // namespace dpf
