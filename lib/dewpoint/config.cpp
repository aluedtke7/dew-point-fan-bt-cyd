#include "config.h"

#include <cstring>

namespace dpf {

namespace {
// Returns false and sets err/message when value is outside [low, high].
bool checkRange(double value, double low, double high, const char* message,
                Config& cfg) {
  if (value < low || value > high) {
    cfg.ok = false;
    cfg.error = message;
    return false;
  }
  return true;
}
}  // namespace

Config parseConfig(const RawConfig& raw) {
  Config cfg;

  if (raw.insideMac.size() != 17 || raw.outsideMac.size() != 17) {
    cfg.error = "Invalid MAC address! Must be 17 characters long.";
    return cfg;  // ok stays false
  }

  std::strncpy(cfg.sensors.insideData.macAddress, raw.insideMac.c_str(),
               sizeof(cfg.sensors.insideData.macAddress) - 1);
  cfg.sensors.insideData.macAddress[17] = '\0';
  std::strncpy(cfg.sensors.outsideData.macAddress, raw.outsideMac.c_str(),
               sizeof(cfg.sensors.outsideData.macAddress) - 1);
  cfg.sensors.outsideData.macAddress[17] = '\0';
  cfg.sensors.insideCalibration = {raw.insideTempCal, raw.insideHumCal};
  cfg.sensors.outsideCalibration = {raw.outsideTempCal, raw.outsideHumCal};

  cfg.fanConfig.minDiff = raw.minDiff;
  cfg.fanConfig.hysteresis = raw.hysteresis;
  cfg.fanConfig.minHumidityInside = raw.minHumidityInside;
  cfg.fanConfig.minTempInside = raw.minTempInside;
  cfg.fanConfig.minTempOutside = raw.minTempOutside;

  if (!checkRange(cfg.fanConfig.minDiff, 1, 10,
                  "Invalid minimal difference! Must be between 1 and 10 degC.",
                  cfg))
    return cfg;
  if (!checkRange(cfg.fanConfig.hysteresis, 0.1, 5,
                  "Invalid hysteresis! Must be between 0.1 and 5 degC.", cfg))
    return cfg;
  if (!checkRange(cfg.fanConfig.minHumidityInside, 30, 70,
                  "Invalid minimal inside humidity! Must be between 30 and 70%.",
                  cfg))
    return cfg;
  if (!checkRange(
          cfg.fanConfig.minTempInside, 10, 40,
          "Invalid minimal inside temperature! Must be between 10 and 40 degC.",
          cfg))
    return cfg;
  if (!checkRange(cfg.fanConfig.minTempOutside, -20, 20,
                  "Invalid minimal outside temperature! Must be between -20 and "
                  "20 degC.",
                  cfg))
    return cfg;

  cfg.brightness = raw.brightness;
  if (!checkRange(cfg.brightness, 0, 100,
                  "Invalid brightness! Must be between 0 and 100%.", cfg))
    return cfg;
  cfg.dimLevel = raw.dimLevel;
  if (!checkRange(cfg.dimLevel, 0, 100,
                  "Invalid dim level! Must be between 0 and 100%.", cfg))
    return cfg;
  cfg.dimTimeout = raw.dimTimeout;
  if (!checkRange(
          cfg.dimTimeout, 0, 3600,
          "Invalid dim timeout! Must be between 0 and 3600 seconds.", cfg))
    return cfg;

  cfg.influx.enabled = raw.influxEnabled;
  cfg.influx.url = raw.influxUrl;
  cfg.influx.token = raw.influxToken;
  cfg.influx.org = raw.influxOrg;
  cfg.influx.bucket = raw.influxBucket;

  // WiFi is opt-in: on a no-PSRAM ESP32, BLE + WiFi are tight on RAM. Enable
  // `wifi.enabled` only after validating coexistence (see README phase 3).
  cfg.wifi.enabled = raw.wifiEnabled;
  cfg.wifi.networks = raw.wifiNetworks;

  // The Shelly plug switches the fan over WiFi (HTTP RPC). Require a host only
  // when the plug is enabled; a disabled Shelly may leave `host` empty.
  cfg.shelly.enabled = raw.shellyEnabled;
  cfg.shelly.host = raw.shellyHost;
  cfg.shelly.switchId = raw.shellySwitchId;
  if (cfg.shelly.enabled && cfg.shelly.host.empty()) {
    cfg.error = "Shelly is enabled but shelly.host is empty.";
    cfg.ok = false;
    return cfg;
  }

  cfg.otaPassword = raw.otaPassword;
  cfg.timezone = raw.timezone;

  cfg.ok = true;
  return cfg;
}

}  // namespace dpf
