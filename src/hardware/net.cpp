#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstring>

#include "logging.h"

namespace dpf {

namespace {
App* g_app = nullptr;
WebServer g_server(80);

// ESP32 (newlib) has no IANA zoneinfo database, so map the common IANA names
// we support to POSIX TZ strings. DST is encoded by the Mm.n.d rules. The
// classic EU rule (Berlin) is: DST starts last Sun Mar 02:00 local (-> CEST,
// UTC+2), ends last Sun Oct 03:00 local (-> CET, UTC+1).
// Unknown names are passed through unchanged, so a raw POSIX TZ string is also
// accepted. Empty -> UTC.
std::string resolveTimezone(const std::string& tz) {
  if (tz.empty()) return "UTC";
  static const std::pair<const char*, const char*> kKnown[] = {
      {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
      {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
      {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
      {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
      {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
      {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
      {"UTC", "UTC"},
  };
  for (const auto& e : kKnown) {
    if (tz == e.first) return e.second;
  }
  return tz;  // assume a POSIX TZ string
}

void handleRoot() {
  SensorData i, o;
  ResultData r;
  {
    LockGuard lock(*g_app);
    i = g_app->sensors.insideData;
    o = g_app->sensors.outsideData;
    r = g_app->result;
  }

  char buf[256];
  snprintf(buf, sizeof(buf),
           "%s\n"
           "inside  dp=%.1f temp=%.1f hum=%.1f\n"
           "outside dp=%.1f temp=%.1f hum=%.1f\n"
           "dp diff=%.1f  fan shouldBeOn=%s isOn=%s\n",
           kVersion,
           i.dewPoint, i.temperature, i.humidity, o.dewPoint, o.temperature,
           o.humidity, i.dewPoint - o.dewPoint, r.shouldBeOn ? "true" : "false",
           r.isOn ? "true" : "false");
  g_server.send(200, "text/plain", buf);
}

void handleInfo() {
  SensorData i, o;
  ResultData r;
  int ovr;
  FanConfig fanCfg;
  {
    LockGuard lock(*g_app);
    i = g_app->sensors.insideData;
    o = g_app->sensors.outsideData;
    r = g_app->result;
    ovr = g_app->overrideMode;
    fanCfg = g_app->config.fanConfig;
  }

  // Format the wall-clock time of the most recent BLE scan. SensorData.scanned
  // is the UTC epoch-seconds timestamp of the reading (set from the NTP-synced
  // clock); localtime() applies the configured timezone (DST included).
  char timeBuf[20];
  {
    time_t ts = static_cast<time_t>(i.scanned > 0 ? i.scanned : o.scanned);
    struct tm* tmInfo = localtime(&ts);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmInfo);
  }

  JsonDocument doc;
  doc["update"] = timeBuf;

  JsonArray sensors = doc["sensors"].to<JsonArray>();
  auto addSensor = [&](const SensorData& s) {
    JsonObject obj = sensors.add<JsonObject>();
    obj["name"] = s.name;
    obj["temperature"] = s.temperature;
    obj["humidity"] = s.humidity;
    obj["dew_point"] = s.dewPoint;
    obj["bat_level"] = s.batLevel / 1000.0;
    obj["rssi"] = s.rssi;
    obj["up_time_in_sec"] = s.uptime;
    char scanBuf[20] = {0};
    if (s.scanned > 0) {
      time_t ts = static_cast<time_t>(s.scanned);
      struct tm* tmInfo = localtime(&ts);
      strftime(scanBuf, sizeof(scanBuf), "%Y-%m-%d %H:%M:%S", tmInfo);
    }
    obj["scanned"] = scanBuf;
  };
  addSensor(i);
  addSensor(o);

  doc["reason"] = static_cast<int>(r.reason);
  doc["venting"] = r.isOn;
  doc["override"] = ovr != 0;
  doc["remote_override"] = ovr;
  doc["diff_min"] = fanCfg.minDiff;
  doc["hysteresis"] = fanCfg.hysteresis;

  // serializeJson only appends the NUL terminator when the output fits strictly
  // inside the buffer, so a completely full buffer must be treated as an error
  // rather than handed to send() as a C string.
  char out[768];
  const size_t n = serializeJson(doc, out, sizeof(out));
  if (n >= sizeof(out)) {
    logf("[http] /info truncated (%u bytes)\n", static_cast<unsigned>(n));
    g_server.send(500, "text/plain", "info too large");
    return;
  }
  g_server.send(200, "application/json", out);
}

void handleOverride() {
  int value = 0;
  if (g_server.hasArg("plain")) {
    JsonDocument doc;
    if (!deserializeJson(doc, g_server.arg("plain"))) {
      value = doc["override"] | 0;
    }
  }
  int prev, current;
  {
    LockGuard lock(*g_app);
    prev = g_app->overrideMode;
    if (value == 0 || value == 1 || value == 2) g_app->overrideMode = value;
    current = g_app->overrideMode;
  }
  if (current != prev) {
    logf("[override] http: %s -> %s\n", overrideName(prev),
         overrideName(current));
  }
  char out[64];
  snprintf(out, sizeof(out), "{\"override\":%d}", current);
  g_server.send(200, "application/json", out);
}

void handleLogs() { streamLogs(g_server); }
}  // namespace

WifiResult connectWifi(const WifiConfig& cfg, uint32_t timeoutMs) {
  if (!cfg.enabled || cfg.networks.empty()) return {"", ""};

  WiFi.mode(WIFI_STA);
  // DHCP/station hostname shown in the router; matches the OTA mDNS name
  // (kHostname in ota.cpp). Must be set after WIFI_STA mode and before begin().
  WiFi.setHostname("dpf-cyd");

  int bestNetIdx = 0;

  if (cfg.networks.size() > 1) {
    logf("[wifi] scanning for best signal...\n");
    int n = WiFi.scanNetworks();
    int bestRssi = -1000;

    for (int i = 0; i < n; ++i) {
      for (size_t j = 0; j < cfg.networks.size(); ++j) {
        if (WiFi.SSID(i) == cfg.networks[j].ssid.c_str()) {
          int rssi = WiFi.RSSI(i);
          logf("[wifi] examined: %s (RSSI %d)\n", WiFi.SSID(i).c_str(), rssi);
          if (rssi > bestRssi) {
            bestRssi = rssi;
            bestNetIdx = j;
          }
        }
      }
    }
    logf("[wifi] best network: %s (RSSI %d)\n",
         cfg.networks[bestNetIdx].ssid.c_str(), bestRssi);
    WiFi.scanDelete();
  } else {
    // Only one network configured, still good to log what we're trying.
    logf("[wifi] connecting to %s...\n", cfg.networks[0].ssid.c_str());
  }

  WiFi.begin(cfg.networks[bestNetIdx].ssid.c_str(),
             cfg.networks[bestNetIdx].password.c_str());
  const uint32_t deadline = millis() + timeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) return {"", ""};
  IPAddress ip = WiFi.localIP();
  char ipBuf[16];
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return {ipBuf, cfg.networks[bestNetIdx].ssid};
}

void ntpSync(uint32_t timeoutMs, const std::string& timezone) {
  // pool.ntp.org with UTC offset 0; the configured timezone (DST included) is
  // applied below via the TZ env var so localtime()/strftime() report local
  // time. configTime's numeric offsets are left at 0 because we drive DST from
  // TZ instead (avoids configTime overwriting our TZ with a fixed offset).
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  logf("[ntp] syncing...\n");
  const uint32_t deadline = millis() + timeoutMs;
  while (time(nullptr) < 1000000000L && millis() < deadline) {
    delay(100);
  }
  // Apply the timezone now that the UTC clock is (hopefully) set. TZ only
  // affects localtime()/mktime() conversions, not the stored UTC epoch, so the
  // exact moment we set it does not shift the clock.
  setenv("TZ", resolveTimezone(timezone).c_str(), 1);
  tzset();
  if (time(nullptr) >= 1000000000L) {
    char buf[32];
    time_t now = time(nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    logf("[ntp] synced: %s (TZ=%s)\n", buf, timezone.c_str());
  } else {
    logf("[ntp] sync timed out\n");
  }
}

void httpBegin(App& app, uint16_t /*port*/) {
  g_app = &app;
  // g_server is constructed on port 80 (the default); reconfiguring the port
  // would require reconstructing it, which WebServer does not support cleanly.
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/info", HTTP_GET, handleInfo);
  g_server.on("/override", HTTP_POST, handleOverride);
  g_server.on("/logs", HTTP_GET, handleLogs);
  g_server.begin();
}

void httpHandle() { g_server.handleClient(); }

void influxPush(App& app) {
  const InfluxConfig& cfg = app.config.influx;
  if (!cfg.enabled) return;

  double tempI, tempO, dpI, dpO, humI, humO;
  size_t nIn, nOut;
  bool isOn;
  int ovr;
  {
    LockGuard lock(app);
    nIn = app.store.inside.size();
    nOut = app.store.outside.size();
    tempI = app.store.inside.averageTemperature();
    tempO = app.store.outside.averageTemperature();
    dpI = app.store.inside.averageDewPoint();
    dpO = app.store.outside.averageDewPoint();
    humI = app.store.inside.averageHumidity();
    humO = app.store.outside.averageHumidity();
    isOn = app.result.isOn;
    ovr = app.overrideMode;
  }

  if (nIn < kInfluxMinSamples || nOut < kInfluxMinSamples) return;

  char line[256];
  snprintf(line, sizeof(line),
           "dp temp_i=%.1f,temp_o=%.1f,dewpoint_i=%.1f,dewpoint_o=%.1f,"
           "hum_i=%.1f,hum_o=%.1f,vent_val=%d,vent_override=%d",
           tempI, tempO, dpI, dpO, humI, humO, isOn ? 1 : 0, ovr);

  // Build URL and auth header on the stack — avoids heap fragmentation from
  // repeated std::string concatenation (every 60 s).
  char url[256];
  snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=s",
           cfg.url.c_str(), cfg.org.c_str(), cfg.bucket.c_str());
  char auth[512];
  snprintf(auth, sizeof(auth), "Token %s", cfg.token.c_str());
  // Persistent HTTPClient: the underlying WiFiClient socket is reused across
  // pushes, avoiding heap alloc/free of the socket state every 60 s.
  static HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", auth);
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(reinterpret_cast<uint8_t*>(line), strlen(line));
  if (code <= 0) {
    logf("[influx] push failed: HTTP %d\n", code);
  }
  http.end();
}

}  // namespace dpf
