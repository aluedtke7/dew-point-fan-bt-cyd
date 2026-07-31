#include "ble_scanner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "beacon.h"
#include "logging.h"

namespace dpf {

namespace {

App* g_app = nullptr;
double (*g_nowFn)() = nullptr;

// Scan timing: 30 ms interval/window (units of 0.625 ms), matching the
// MicroPython aioble scan(interval_us=30000, window_us=30000).
constexpr uint16_t kScanIntervalUnits = 48;
constexpr uint16_t kScanWindowUnits = 48;

// Set to 1 to log every advertised device that carries manufacturer data,
// before any MAC filtering (name='' is normal under passive scan). Handy for
// discovering a sensor's payload MAC / manufacturer-data length when bringing a
// new beacon into range. Leave at 0 in normal operation.
#define DPF_BLE_DEBUG 0

// The WS02 payload our tested parseWS02 expects (18 bytes: 2 header, 6 MAC,
// 2 bat, 2 temp, 2 hum, 4 uptime) is preceded by the 2-byte BLE company id in
// NimBLE's manufacturer data. Real ThermoBeacons advertise 20 bytes total, so
// we skip the leading company id before parsing.
constexpr size_t kCompanyIdLen = 2;

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (!dev->haveManufacturerData()) return;

    // Zero-copy: walk the raw advertisement payload (a sequence of AD
    // structures: [len][type][data...]) to find manufacturer data (type 0xFF).
    // This avoids getManufacturerData() which copies into a std::string on
    // every advertisement — the heap churn from thousands of small alloc/free
    // cycles is the primary source of fragmentation over hours of runtime.
    const std::vector<uint8_t>& payload = dev->getPayload();
    const uint8_t* mfg = nullptr;
    size_t mfgLen = 0;
    for (size_t i = 0; i + 1 < payload.size();) {
      const uint8_t len = payload[i];
      if (len == 0) break;
      if (i + len >= payload.size()) break;
      if (payload[i + 1] == 0xFF) {  // AD Type: Manufacturer Specific Data
        mfg = &payload[i + 2];       // skip length + type bytes
        mfgLen = len - 1;
        break;
      }
      i += len + 1;
    }
    if (!mfg) return;

#if DPF_BLE_DEBUG
    {
      char payloadMac[18] = "(short)";
      if (mfgLen >= 10) {  // MAC sits at bytes 4..9 in the full frame
        snprintf(payloadMac, sizeof(payloadMac),
                 "%02X:%02X:%02X:%02X:%02X:%02X", mfg[9], mfg[8], mfg[7],
                 mfg[6], mfg[5], mfg[4]);
      }
      logf("[BLE] name='%s' addr=%s mfgLen=%u payloadMAC=%s rssi=%d\n",
           dev->getName().c_str(), dev->getAddress().toString().c_str(),
           static_cast<unsigned>(mfgLen), payloadMac, dev->getRSSI());
    }
#endif

    // No name filter: under passive scan the local name is usually absent
    // (name=''), so we identify sensors purely by the MAC inside the payload.
    if (mfgLen < kCompanyIdLen) return;
    const uint8_t* ws02 = mfg + kCompanyIdLen;
    const size_t ws02Len = mfgLen - kCompanyIdLen;

    // Locked across the parse too: parseWS02 reads the configured MACs and
    // calibrations out of g_app->sensors, which the writes below replace.
    LockGuard lock(*g_app);
    auto reading = parseWS02(ws02, ws02Len, static_cast<int16_t>(dev->getRSSI()),
                             g_app->sensors, g_nowFn());
    if (!reading || reading->name[0] == '\0') return;  // wrong length / unknown MAC

    if (std::strcmp(reading->name, "Inside") == 0) {
      g_app->sensors.insideData = *reading;
      g_app->store.inside.add(*reading);
    } else {
      g_app->sensors.outsideData = *reading;
      g_app->store.outside.add(*reading);
    }
  }
};

ScanCallbacks g_callbacks;

}  // namespace

void bleScannerBegin(App& app, double (*nowFn)()) {
  g_app = &app;
  g_nowFn = nowFn;

#if DPF_BLE_DEBUG
      logf("[BLE] configured inside MAC='%s' outside MAC='%s'\n",
           app.sensors.insideData.macAddress,
           app.sensors.outsideData.macAddress);
  logf("[BLE] starting scan...\n");
#endif

  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  // wantDuplicates = true: ThermoBeacons re-advertise periodically and we need
  // each fresh reading to keep the freshness timestamp current.
  scan->setScanCallbacks(&g_callbacks, /*wantDuplicates=*/true);
  scan->setActiveScan(false);  // passive: the payload is in the advertisement
  // Callback-only scanning. NimBLE's default maxResults is 0xFF, which it
  // treats as *unlimited* (its cap check is skipped for 0xFF), so it keeps a
  // heap-allocated NimBLEAdvertisedDevice for every unique advertiser forever
  // — and phones rotate their private address every ~15 min, so the list grows
  // without bound. 0 means "don't store": the device is erased as soon as
  // onResult() returns, which is all we need since the callback copies what it
  // wants into App state.
  scan->setMaxResults(0);
  scan->setInterval(kScanIntervalUnits);
  scan->setWindow(kScanWindowUnits);
  scan->start(0);  // 0 = scan forever on the BLE task
}

void bleScannerPause() {
  // stop() returns false if ble_gap_disc_cancel() failed, which leaves the
  // radio scanning at 100 % duty and starves lwIP of air time — the HTTP call
  // that follows would then time out on the TCP handshake. Worth knowing about.
  if (!NimBLEDevice::getScan()->stop()) {
    logf("[BLE] scan stop failed; radio may still be scanning\n");
  }
}

void bleScannerResume() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setInterval(kScanIntervalUnits);
  scan->setWindow(kScanWindowUnits);
  scan->start(0);
}

}  // namespace dpf
