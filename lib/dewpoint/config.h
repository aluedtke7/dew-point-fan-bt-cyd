// Configuration parsing and validation.
// Ported from Go `dpf-config.go` (via the MicroPython `dewpoint/config.py`).
//
// `parseConfig` works on an already-decoded `RawConfig` (plain values) so it is
// unit-testable on host with no JSON/Arduino dependency. The device-only
// `hardware/config_fs` reads config.json off LittleFS (ArduinoJson) into a
// RawConfig and calls this. Config is applied once at boot (no hot-reload).
#pragma once

#include <string>
#include <vector>

#include "sensordata.h"

namespace dpf {

// Decoded-but-unvalidated configuration values. Field defaults match the
// MicroPython `parse_config` defaults for missing keys.
struct WifiNetwork {
  std::string ssid;
  std::string password;
};

struct RawConfig {
  std::string insideMac;
  std::string outsideMac;
  double insideTempCal = 0.0;
  double insideHumCal = 0.0;
  double outsideTempCal = 0.0;
  double outsideHumCal = 0.0;

  double minDiff = 0.0;
  double hysteresis = 0.0;
  double minHumidityInside = 0.0;
  double minTempInside = 0.0;
  double minTempOutside = 0.0;

  int brightness = 100;
  int dimLevel = 10;
  int dimTimeout = 0;

  bool influxEnabled = false;
  std::string influxUrl;
  std::string influxToken;
  std::string influxOrg;
  std::string influxBucket;

  bool wifiEnabled = false;
  std::vector<WifiNetwork> wifiNetworks;

  bool shellyEnabled = false;
  std::string shellyHost;
  int shellySwitchId = 0;

  std::string timezone;  // IANA name (e.g. "Europe/Berlin") or POSIX TZ string

  std::string otaPassword;
};

struct WifiConfig {
  bool enabled = false;
  std::vector<WifiNetwork> networks;
};

struct InfluxConfig {
  bool enabled = false;
  std::string url;
  std::string token;
  std::string org;
  std::string bucket;
};

// The Shelly Plug that switches the fan over WiFi (HTTP RPC). `host` is the
// plug's IP address or hostname; `switchId` is the relay channel (0 on a
// single-relay plug).
struct ShellyConfig {
  bool enabled = false;
  std::string host;
  int switchId = 0;
};

// Validated configuration. On invalid input, `ok` is false and `error` holds a
// message (we do not throw — exceptions are disabled on the ESP32 build).
struct Config {
  Sensors sensors;
  FanConfig fanConfig;
  InfluxConfig influx;
  WifiConfig wifi;
  ShellyConfig shelly;
  int brightness = 100;
  int dimLevel = 10;
  int dimTimeout = 0;
  std::string timezone;
  std::string otaPassword;
  bool ok = false;
  std::string error;
};

// Validate `raw` and build a Config. Range checks mirror the MicroPython
// `_check_range` calls.
Config parseConfig(const RawConfig& raw);

}  // namespace dpf
