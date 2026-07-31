#include "config_fs.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace dpf {

Config loadConfigFromFs(const char* path) {
  Config failed;  // ok == false by default

  if (!LittleFS.begin(false)) {
    failed.error = "LittleFS mount failed";
    return failed;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    failed.error = "config file not found";
    return failed;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    failed.error = std::string("JSON parse error: ") + err.c_str();
    return failed;
  }

  // Extract raw values with the same defaults as the MicroPython parse_config.
  RawConfig raw;
  raw.insideMac = doc["inside"]["mac"] | "";
  raw.outsideMac = doc["outside"]["mac"] | "";
  raw.insideTempCal = doc["inside"]["temperature-calibration"] | 0.0;
  raw.insideHumCal = doc["inside"]["humidity-calibration"] | 0.0;
  raw.outsideTempCal = doc["outside"]["temperature-calibration"] | 0.0;
  raw.outsideHumCal = doc["outside"]["humidity-calibration"] | 0.0;

  raw.minDiff = doc["fan"]["minDiff"] | 0.0;
  raw.hysteresis = doc["fan"]["hysteresis"] | 0.0;
  raw.minHumidityInside = doc["fan"]["minHumidityInside"] | 0.0;
  raw.minTempInside = doc["fan"]["minTempInside"] | 0.0;
  raw.minTempOutside = doc["fan"]["minTempOutside"] | 0.0;

  raw.brightness = doc["ui"]["brightness"] | 100;
  raw.dimLevel = doc["ui"]["dimLevel"] | 10;
  raw.dimTimeout = doc["ui"]["dimTimeout"] | 0;

  raw.influxEnabled = doc["influx"]["enabled"] | false;
  raw.influxUrl = doc["influx"]["url"] | "";
  raw.influxToken = doc["influx"]["token"] | "";
  raw.influxOrg = doc["influx"]["org"] | "";
  raw.influxBucket = doc["influx"]["bucket"] | "";

  raw.wifiEnabled = doc["wifi"]["enabled"] | false;
  std::string ssid1 = doc["wifi"]["ssid"] | "";
  if (!ssid1.empty()) {
    raw.wifiNetworks.push_back({ssid1, doc["wifi"]["password"] | ""});
  }
  JsonArray networks = doc["wifi"]["networks"];
  for (JsonObject net : networks) {
    std::string ssid = net["ssid"] | "";
    if (!ssid.empty()) {
      raw.wifiNetworks.push_back({ssid, net["password"] | ""});
    }
  }

  raw.shellyEnabled = doc["shelly"]["enabled"] | false;
  raw.shellyHost = doc["shelly"]["host"] | "";
  raw.shellySwitchId = doc["shelly"]["switchId"] | 0;

  raw.timezone = doc["timezone"] | "";

  raw.otaPassword = doc["ota"]["password"] | "";

  return parseConfig(raw);
}

}  // namespace dpf
