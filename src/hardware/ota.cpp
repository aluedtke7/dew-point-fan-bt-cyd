#include "ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>

#include "logging.h"

namespace dpf {

namespace {
constexpr const char* kHostname = "dpf-cyd";
}  // namespace

void otaBegin(const char* password) {
  const bool hasPassword = password && password[0] != '\0';

  // ArduinoOTA::begin() early-returns once initialised, so on the reconnect
  // path it would silently do nothing — including the MDNS.begin() it normally
  // performs. Re-announce the responder ourselves so "dpf-cyd.local" comes back
  // after a link drop (platformio.ini's cyd_ota env uploads to that name).
  static bool initialised = false;
  if (initialised) {
    MDNS.end();
    if (MDNS.begin(kHostname)) {
      MDNS.enableArduino(3232, hasPassword);
      logf("[ota] mDNS re-announced as %s.local\n", kHostname);
    } else {
      logf("[ota] mDNS re-announce failed\n");
    }
    return;
  }
  initialised = true;

  ArduinoOTA.setHostname(kHostname);
  if (hasPassword) ArduinoOTA.setPassword(password);

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
