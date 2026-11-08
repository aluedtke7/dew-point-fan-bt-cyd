#include "ota.h"

#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>

#include "logging.h"

namespace dpf {

namespace {
constexpr const char* kHostname = "dpf-cyd";
// How long the device waits for upload data before giving up. ArduinoOTA's
// default of 1 s with no retry before the first chunk is shorter than macOS's
// initial TCP retransmit timeout, so a single lost segment on the shared
// WiFi/BLE radio aborted the upload with OTA_RECEIVE_ERROR (error 3).
constexpr int kReceiveTimeoutMs = 10000;
}  // namespace

void otaBegin(const char* password) {
  const bool hasPassword = password && password[0] != '\0';

  // ArduinoOTA::begin() early-returns once initialised, so on the reconnect
  // path it would silently do nothing. A link drop kills both the mDNS
  // responder and the UDP 3232 invitation socket (espota then gets "No response
  // from the ESP" while HTTP, which httpBegin() re-binds, keeps working).
  // end() closes both; begin() re-binds the socket and re-announces
  // "dpf-cyd.local". Hostname, password and callbacks are kept by ArduinoOTA.
  static bool initialised = false;
  if (initialised) {
    ArduinoOTA.end();
    ArduinoOTA.begin();
    logf("[ota] restarted, listening on %s.local:3232\n", kHostname);
    return;
  }
  initialised = true;

  ArduinoOTA.setHostname(kHostname);
  if (hasPassword) ArduinoOTA.setPassword(password);
  ArduinoOTA.setTimeout(kReceiveTimeoutMs);

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
