// Entry point for the dew-point fan controller on the CYD (ESP32-2432S028R).
//
// The original's four Go goroutines (and the MicroPython asyncio tasks) become
// one cooperative, non-blocking loop() driven by millis() timers, plus the
// NimBLE scan and the network control task, each on its own FreeRTOS task.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <WiFi.h>

#include <cstring>
#include <string>

#include "app.h"
#include "control.h"
#include "format.h"

#include "hardware/backlight.h"
#include "hardware/ble_scanner.h"
#include "hardware/config_fs.h"
#include "hardware/display_tft.h"
#include "hardware/lastalive.h"
#include "hardware/logging.h"
#include "hardware/net.h"
#include "hardware/ota.h"
#include "hardware/shelly.h"
#include "hardware/net_task.h"
#include "hardware/touch.h"

using namespace dpf;

namespace {
App app;
Shelly shelly;
DisplayTft display;
Touch touch;
Backlight backlight;

  // Converts the build string into a format with only numbers "dd.MM.yyyy hh:mm:ss"
std::string formatBuildTime() {
  static const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  int month = 1;
  for (const char* p = kMonths; *p; p += 3, ++month) {
    if (strncmp(p, mon, 3) == 0) break;
  }
  int day = (__DATE__[4] == ' ') ? (__DATE__[5] - '0')
                                 : ((__DATE__[4] - '0') * 10 + (__DATE__[5] - '0'));
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d.%02d.%.4s %s", day, month, __DATE__ + 7, __TIME__);
  return std::string(buf);
}

std::string g_ip;
std::string g_ssid;
bool g_configOk = false;
volatile bool g_wifiUp = false;
volatile bool g_wifiReinitNeeded = false;
bool g_clockSynced = false;  // true once NTP has set the wall clock

// Human-readable reset cause, logged at boot so an unexpected overnight
// restart explains itself (brownout / watchdog / panic vs. a clean reset).
const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic/exception";
    case ESP_RST_INT_WDT: return "interrupt wdt";
    case ESP_RST_TASK_WDT: return "task wdt";
    case ESP_RST_WDT: return "other wdt";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

// Sentinel "now" used for freshness/age while the wall clock is not yet valid:
// a huge value makes every reading look ancient, so the fan is safely off
// until the clock is NTP-synced.
constexpr double kUnsyncedNow = 1e18;

// Wall-clock epoch seconds (UTC) from the NTP-synced system clock. This is the
// clock that fills SensorData.scanned and the one freshness math compares
// against, so both share the same timebase.
double nowEpochSeconds() { return static_cast<double>(time(nullptr)); }

// Epoch "now" to feed freshness/age logic. Before the clock is NTP-synced we
// return a huge sentinel so every reading is treated as stale (fan safely off).
double freshnessNow() { return g_clockSynced ? nowEpochSeconds() : kUnsyncedNow; }

// Scheduler intervals (ms).
constexpr uint32_t kTouchIntervalMs = 50;
constexpr uint32_t kControlIntervalMs = 1000;
constexpr uint32_t kRenderIntervalMs = 1000;
constexpr uint32_t kDimFadeMs = 5000;  // ramp brightness -> dimLevel over 5 s
constexpr uint32_t kBootTouchGuardMs = 2000;  // ignore phantom touch at boot
constexpr uint32_t kAliveIntervalMs = 30000;  // RTC last-alive heartbeat
constexpr uint32_t kHeapReportIntervalMs = 3600000;  // 60 min heap report

// Heap floors enforced on the kAliveIntervalMs tick. Free heap alone misses a
// fragmentation death (plenty free, but no contiguous block left), so the
// largest allocatable block is checked too.
constexpr uint32_t kHeapFloorBytes = 30000;
constexpr uint32_t kBlockFloorBytes = 20000;

uint32_t tTouch = 0, tControl = 0, tScreen = 0, tBoot = 0, tAlive = 0, tHeapReport = 0;
uint32_t tLastActivity = 0;
bool dimmed = false;
Screen g_screen = Screen::Dashboard;

// Refresh the IP/SSID shown on the diagnostics screen from the WiFi driver.
// Called from the main task only, so `g_ip`/`g_ssid` stay single-threaded.
void refreshNetIdentity() {
  if (!g_wifiUp) {
    g_ip.clear();
    g_ssid.clear();
    return;
  }
  IPAddress ip = WiFi.localIP();
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  g_ip = buf;
  g_ssid = WiFi.SSID().c_str();
}

// Draw the active screen from a snapshot of the shared state taken under lock.
void renderScreen() {
  SensorData inside, outside;
  ResultData result;
  int ovr;
  {
    LockGuard lock(app);
    inside = app.sensors.insideData;
    outside = app.sensors.outsideData;
    result = app.result;
    ovr = app.overrideMode;
  }
  const double now = freshnessNow();
  if (g_screen == Screen::Diagnostics) {
    refreshNetIdentity();
    display.showDiagnostics(inside, outside, ovr, g_ip, g_ssid, g_wifiUp, now);
  } else {
    display.showDashboard(inside, outside, result, app.config.fanConfig, ovr, now);
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  loggingBegin();  // create the log mutex before the first logf()
  const esp_reset_reason_t resetReason = esp_reset_reason();
  logf("[boot] reset reason: %s (%d)\n", resetReasonName(resetReason),
       static_cast<int>(resetReason));
  // RTC RAM survives a panic reset (and its wall clock too), so the previous
  // run's record tells us when it died and how long it had been up. Nothing
  // is reported after a real power cycle (RTC content is garbage then).
  {
    const LastAlivePrev prev =
        lastAliveBegin(static_cast<int64_t>(nowEpochSeconds()));
    if (prev.valid && prev.lastAliveEpoch > 0 && resetReason != ESP_RST_POWERON) {
      const double uptimeH =
          static_cast<double>(prev.lastAliveEpoch - prev.bootEpoch) / 3600.0;
      char prevTs[24];
      formatLogTimestamp(prevTs, sizeof(prevTs),
                        static_cast<time_t>(prev.lastAliveEpoch));
      logf("[boot] previous run #%u died at %s, uptime %.1fh\n",
           prev.bootCount, prevTs, uptimeH);
    }
  }
  display.begin();
  display.showLines({kVersion, "booting...", "", ""});
  display.showLines(startScreen(formatBuildTime()));

  Config config = loadConfigFromFs();
  if (!config.ok) {
    logf("config error: %s\n", config.error.c_str());
    display.showLines({"CONFIG ERROR", config.error.c_str(), "", "fix config.json"});
    return;  // loop() will idle; nothing to control without a valid config
  }
  g_configOk = true;
  app.config = config;
  app.sensors = config.sensors;  // carries the configured MACs + calibration

  app.begin();  // create the state mutex before the BLE task can write
  touch.begin();
  backlight.begin(app.config.brightness);
  bleScannerBegin(app, nowEpochSeconds);
  // The fan is a Shelly Plug switched over WiFi (HTTP RPC).
  if (app.config.shelly.enabled) {
    shelly.begin(app.config.shelly.host, app.config.shelly.switchId);
  }

  // WiFi is opt-in. On a no-PSRAM CYD, watch the free heap and confirm BLE keeps
  // delivering advertisements while associated (see README phase 3).
  if (app.config.wifi.enabled) {
    logf("free heap before WiFi: %u\n", ESP.getFreeHeap());
    WifiResult res = connectWifi(app.config.wifi);
    g_ip = res.ip;
    g_ssid = res.ssid;
    logf("WiFi IP: %s  SSID: %s  free heap: %u\n", g_ip.c_str(),
         g_ssid.c_str(), ESP.getFreeHeap());
    if (!g_ip.empty()) {
      g_wifiUp = true;
      ntpSync(5000, app.config.timezone);
      g_clockSynced = (time(nullptr) >= 1000000000L);
      httpBegin(app);
      otaBegin(app.config.otaPassword.c_str());
    }
  }

  // Shelly RPC + InfluxDB push run on their own task so the touch UI never
  // blocks on the synchronous HTTP calls. That task also supervises the WiFi
  // link, so it is started only after the boot association has finished — two
  // tasks driving WiFi.begin() at once would race. If the boot association
  // failed, the supervisor picks it up from here and keeps retrying.
  netTaskBegin(app, shelly, &g_wifiUp, &g_wifiReinitNeeded);

  tBoot = millis();  // start the boot-touch guard window
}

void loop() {
  if (!g_configOk) {
    delay(100);
    return;
  }

  const uint32_t ms = millis();
  bool renderNow = false;

  if (ms - tTouch >= kTouchIntervalMs) {
    tTouch = ms;
    TouchPoint tp = touch.poll();
    const bool guarded = (ms - tBoot) < kBootTouchGuardMs;
    if (tp.pressed && !guarded) {
      tLastActivity = ms;
      if (dimmed) {
        // wake from dim: restore brightness and consume this tap
        backlight.set(app.config.brightness);
        dimmed = false;
      } else if (tp.y < ui::kBannerH + 15) {
        // top banner -> switch between Dashboard and Diagnostics
        // a little offset avoids touch screen calibration
        g_screen = (g_screen == Screen::Dashboard) ? Screen::Diagnostics
                                                   : Screen::Dashboard;
        renderNow = true;
      } else if (tp.y >= ui::kBtnY) {
        // bottom button row -> Auto (0) / force On (1) / force Off (2)
        int mode = tp.x / (ui::kScreenW / ui::kBtnCount);
        if (mode > 2) mode = 2;
        int prev;
        {
          LockGuard lock(app);
          prev = app.overrideMode;
          app.overrideMode = mode;
        }
        if (mode != prev) {
          logf("[override] touch: %s -> %s\n", overrideName(prev),
               overrideName(mode));
          renderNow = true;
        }
      }
    } else if (tp.pressed && guarded) {
      logf("[touch] ignored during boot guard (px=%d,%d)\n", tp.x, tp.y);
    }
  }

  // Auto-dim the backlight after a period of no touch (dimTimeout == 0 = off).
  // The dim is a slow 5 s fade; waking (touch) restores brightness instantly.
  if (app.config.dimTimeout > 0 && !dimmed &&
      ms - tLastActivity >= static_cast<uint32_t>(app.config.dimTimeout) * 1000) {
    backlight.fadeTo(app.config.dimLevel, kDimFadeMs, ms);
    dimmed = true;
  }
  backlight.update(ms);

  if (ms - tControl >= kControlIntervalMs) {
    tControl = ms;
    // Wall-clock epoch for the fan decision (freshness/age math compares
    // against SensorData.scanned, which is filled from the same clock).
    const double now = freshnessNow();
    SensorData inside, outside;
    int ovr;
    ResultData result;
    {
      LockGuard lock(app);
      inside = app.sensors.insideData;
      outside = app.sensors.outsideData;
      ovr = app.overrideMode;
      result = app.result;  // carries isOn through computeResults
    }

    computeResults(inside, outside, result, app.config.fanConfig, ovr, now);
    result.dpDiff = inside.dewPoint - outside.dewPoint;

    // The relay RPC (Shelly) is performed by the network task, which reads
    // result.shouldBeOn and writes back result.isOn; we only publish the
    // decision here.
    {
      LockGuard lock(app);
      app.result = result;
    }
  }

  if (renderNow || ms - tScreen >= kRenderIntervalMs) {
    tScreen = ms;
    renderScreen();
  }

  // Keep the RTC last-alive marker fresh so the next boot can time a crash.
  // Also enforce the heap floors: below them the device is one allocation away
  // from a panic. A controlled restart is far better — the next boot's [boot]
  // lines will tell us exactly when and why. Only the heartbeat needs a synced
  // clock; the heap check runs regardless.
  if (ms - tAlive >= kAliveIntervalMs) {
    tAlive = ms;
    if (g_clockSynced) {
      lastAliveHeartbeat(static_cast<int64_t>(nowEpochSeconds()));
    }
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (freeHeap < kHeapFloorBytes || blk < kBlockFloorBytes) {
      logf("[sys] heap critical: free=%u blk=%u, restarting\n", freeHeap, blk);
      delay(50);  // flush serial + log buffer before restart
      ESP.restart();
    }
  }

  // Use this log report in case of memory problems
  // if (ms - tHeapReport >= kHeapReportIntervalMs) {
  //   tHeapReport = ms;
  //   logf("[heap] periodic report\n");
  // }

  if (g_wifiUp) {
    otaHandle();
    httpHandle();
  }

  // After the net task reconnects a dropped WiFi link, re-bind listeners and
  // re-sync the wall clock so the OTA/HTTP/age-check services work again.
  if (g_wifiReinitNeeded) {
    g_wifiReinitNeeded = false;
    ntpSync(1500, app.config.timezone);
    g_clockSynced = (time(nullptr) >= 1000000000L);
    httpBegin(app);
    otaBegin(app.config.otaPassword.c_str());
  }

  delay(1);  // yield to the BLE / WiFi FreeRTOS tasks (not a blocking sleep)
}
