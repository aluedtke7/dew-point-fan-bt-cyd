// WiFi association, HTTP status/override API and InfluxDB push.
// Ported from Go `dpf-web.go` / `dpf-influxdb.go` (MicroPython `hardware/net.py`).
//
//   GET  /         plain-text status summary
//   GET  /info     JSON snapshot of sensors + fan state + override
//   GET  /logs     plain-text recent (<=50) serial log messages
//   POST /override body {"override": 0|1|2}  (0=auto, 1=force on, 2=force off)
//
// Opt-in: enabled only when config.wifi.enabled is true. On a no-PSRAM CYD,
// validate BLE+WiFi coexistence and free heap before relying on this (README).
#pragma once

#include <string>

#include "app.h"

namespace dpf {

// At least this many samples must be stored before an InfluxDB point is sent
// (matches the Go/MicroPython gate).
constexpr size_t kInfluxMinSamples = 10;

struct WifiResult {
  std::string ip;
  std::string ssid;
};

// Associate with WiFi (station mode). Returns the IP and SSID, or empty strings
// on failure/timeout.
WifiResult connectWifi(const WifiConfig& cfg, uint32_t timeoutMs = 15000);

// Synchronise the system clock via NTP (SNTP). Call once after WiFi is up.
// Blocks until the clock is set or timeoutMs elapses. `timezone` is an IANA
// name (e.g. "Europe/Berlin") or a raw POSIX TZ string; it is applied so that
// localtime()/strftime() report the configured zone (with DST). Empty -> UTC.
void ntpSync(uint32_t timeoutMs = 5000, const std::string& timezone = "");

// Register the HTTP routes and start the server. Call after WiFi is up.
void httpBegin(App& app, uint16_t port = 80);

// Service pending HTTP requests. Call frequently from loop().
void httpHandle();

// Push the rolling 20-sample averages to InfluxDB (no-op until both stores hold
// >= kInfluxMinSamples readings). Call once per interval from the scheduler.
void influxPush(App& app);

}  // namespace dpf
