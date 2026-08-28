#include "net_task.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>

#include "ble_scanner.h" // bleScannerPause / bleScannerResume
#include "logging.h"
#include "net.h"        // connectWifi, influxPush
#include "shelly_rpc.h" // shouldSendRpc, TriState

namespace dpf {

namespace {
App* g_app = nullptr;
Shelly* g_shelly = nullptr;
volatile bool* g_wifiUp = nullptr;
volatile bool* g_wifiReinitNeeded = nullptr;
bool g_wifiEnabled = false;

// Task cadence and back-off, matching the old loop() scheduler.
constexpr uint32_t kTickMs = 1000;
constexpr double kShellyRetrySeconds = 15.0;
constexpr uint32_t kShellyMaxAttempts = 3;
// After we've given up on a target, keep probing it slowly so a transiently
// unreachable plug recovers without needing a dew-point state change to reset
// the failure count.
constexpr double kShellyReprobeSeconds = kShellyRetrySeconds * 4;
constexpr double kInfluxIntervalSec = 60.0;
// After a cold boot (power cycle / OTA) wait before the first Shelly/Influx
// attempt so the plug's HTTP server has time to start and lwIP's ARP/PCB state
// is clean.
constexpr double kStartupGraceSec = 15.0;

// WiFi supervision. `WiFi.reconnect()` is the cheap first move; after
// kWifiSoftAttempts we fall back to a full re-association (which re-runs the
// best-RSSI scan and, crucially, re-applies the DHCP hostname). If the link is
// still down after kWifiRebootSeconds we restart: a reset clears wedged
// lwIP/WiFi driver state that WiFi.begin() alone cannot, and the next boot's
// "[boot] reset reason: software" line records why.
constexpr double kWifiRetrySeconds = 30.0;
constexpr uint32_t kWifiSoftAttempts = 3;
constexpr double kWifiRebootSeconds = 900.0;

// Shelly command state. Owned solely by this task, so no lock is needed for these.
TriState g_confirmed = TriState::Unknown;
TriState g_target = TriState::Unknown;
double g_lastAttempt = 0.0;
double g_lastInflux = 0.0;
// Consecutive failed attempts at the current target; resets on success or on a
// genuine desired-state change (which starts a fresh retry sequence).
uint32_t g_failCount = 0;
// True once we've logged the terminal "gave up" line for the current target, so
// it is emitted exactly once rather than on every tick after the limit.
bool g_gaveUpLogged = false;

// WiFi supervision state. Owned solely by this task.
double g_lastWifiAttempt = 0.0;
uint32_t g_wifiFailCount = 0;
double g_offlineSince = 0.0;  // monotonic seconds since the link went down
// The offline restart only arms once the link has worked at least once this
// boot. Otherwise an AP that is simply switched off would reboot the device
// every 15 minutes, and the fan/display would restart with it.
bool g_wasOnline = false;

// Monotonic seconds clock, same timebase as main.cpp's nowSeconds().
double nowSeconds() { return esp_timer_get_time() / 1000000.0; }

// True when the station is associated *and* holds a DHCP lease. The ESP32
// keeps reporting WL_CONNECTED after a lease lapses, leaving a 0.0.0.0 address
// that is associated but unreachable, so the address is checked too.
bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}

void logWifiUp(const char* what) {
  IPAddress ip = WiFi.localIP();
  char ipBuf[16];
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  logf("[wifi] %s, IP: %s\n", what, ipBuf);
}

// Monitor the link and drive recovery. Runs every tick; the actual attempts are
// spaced by kWifiRetrySeconds so an absent AP is not hammered.
void superviseWifi(double now) {
  if (wifiLinkUp()) {
    if (!*g_wifiUp) {
      *g_wifiUp = true;
      if (g_wifiReinitNeeded) *g_wifiReinitNeeded = true;
      logWifiUp("reconnected");
    }
    g_wifiFailCount = 0;
    g_offlineSince = 0.0;
    g_wasOnline = true;
    return;
  }

  if (*g_wifiUp || g_offlineSince == 0.0) {
    *g_wifiUp = false;
    g_offlineSince = now;
    g_lastWifiAttempt = now - kWifiRetrySeconds;  // attempt on this tick
    logf("[wifi] link down, starting recovery\n");
  }

  if (g_wasOnline && now - g_offlineSince >= kWifiRebootSeconds) {
    logf("[wifi] offline for %.0fs, restarting\n", now - g_offlineSince);
    delay(50);  // flush serial + log buffer before restart
    ESP.restart();
  }

  if (now - g_lastWifiAttempt < kWifiRetrySeconds) return;
  g_lastWifiAttempt = now;
  ++g_wifiFailCount;

  if (g_wifiFailCount <= kWifiSoftAttempts) {
    logf("[wifi] reconnect attempt %u\n", g_wifiFailCount);
    WiFi.reconnect();
    // Give the association up to 5 s; the next tick re-checks either way.
    for (int i = 0; i < 50 && !wifiLinkUp(); ++i) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  } else {
    // Soft reconnects only ever return to the AP chosen at boot and do not
    // re-apply the DHCP hostname. A full re-association re-scans the configured
    // networks and re-registers "dpf-cyd" with the router's DNS.
    logf("[wifi] full re-association (attempt %u)\n", g_wifiFailCount);
    WifiConfig cfg;
    {
      LockGuard lock(*g_app);
      cfg = g_app->config.wifi;
    }
    // connectWifi() does a blocking scanNetworks(); a running NimBLE scan
    // starves lwIP during it (see shelly.cpp).
    bleScannerPause();
    WiFi.disconnect(true);
    connectWifi(cfg);
    bleScannerResume();
  }

  if (wifiLinkUp()) {
    *g_wifiUp = true;
    if (g_wifiReinitNeeded) *g_wifiReinitNeeded = true;
    g_wifiFailCount = 0;
    g_offlineSince = 0.0;
    g_wasOnline = true;
    logWifiUp("reconnected");
  } else {
    logf("[wifi] attempt %u failed, retrying in %.0fs\n", g_wifiFailCount,
         kWifiRetrySeconds);
  }
}

void netTask(void*) {
  const double bootTime = nowSeconds();
  g_lastInflux = bootTime;
  for (;;) {
    const double now = nowSeconds();

    // WiFi supervision. Note this is gated on g_wifiEnabled, never on
    // *g_wifiUp: the flag is cleared the moment the link drops, so gating on it
    // would make the very first failed attempt permanent.
    if (g_wifiEnabled) superviseWifi(now);

    if (*g_wifiUp && (now - bootTime >= kStartupGraceSec)) {
      bool shouldBeOn = false;
      bool shellyEnabled = false;
      bool sensorsReady = false;
      Reason reason = Reason::None;
      {
        LockGuard lock(*g_app);
        shouldBeOn = g_app->result.shouldBeOn;
        shellyEnabled = g_app->config.shelly.enabled;
        sensorsReady = g_app->result.sensorsReady;
        reason = g_app->result.reason;
      }

      if (shellyEnabled) {
        bool defer = !sensorsReady &&
                     reason != Reason::SoftOverrideOn &&
                     reason != Reason::SoftOverrideOff;
        if (!defer) {
          const TriState newTarget = shouldBeOn ? TriState::On : TriState::Off;
          const bool gaveUp = g_failCount >= kShellyMaxAttempts;
          // Normal path: edge/backoff decision from the pure helper. Once we've
          // given up on a target, shouldSendRpc returns false, so fall through to
          // the slow re-probe below instead of hammering.
          if (shouldSendRpc(shouldBeOn, g_confirmed, g_target, now, g_lastAttempt,
                            kShellyRetrySeconds, g_failCount, kShellyMaxAttempts)) {
            g_lastAttempt = now;
            // A genuine target change starts a fresh retry sequence.
            if (newTarget != g_target) {
              g_target = newTarget;
              g_failCount = 0;
              g_gaveUpLogged = false;
            }
            if (g_shelly->set(shouldBeOn)) {
              g_confirmed = g_target;
              g_failCount = 0;
              g_gaveUpLogged = false;
            } else {
              ++g_failCount;
              if (g_failCount >= kShellyMaxAttempts && !g_gaveUpLogged) {
                g_gaveUpLogged = true;
                logf("[shelly] gave up after %u attempts to switch %s\n",
                     kShellyMaxAttempts, shouldBeOn ? "ON" : "OFF");
              }
            }
          } else if (gaveUp && newTarget == g_target &&
                     now - g_lastAttempt >= kShellyReprobeSeconds) {
            // We gave up on this target but it's still desired: probe slowly so a
            // transiently-unreachable plug recovers without a state change.
            g_lastAttempt = now;
            if (g_shelly->set(shouldBeOn)) {
              g_confirmed = g_target;
              g_failCount = 0;
              g_gaveUpLogged = false;
              logf("[shelly] recovered after give-up, switched %s\n",
                   shouldBeOn ? "ON" : "OFF");
            }
          }
        }
      }

      // Last confirmed plug state, for the banner / web API.
      {
        LockGuard lock(*g_app);
        g_app->result.isOn = g_shelly->isOn();
      }

      if (now - g_lastInflux >= kInfluxIntervalSec) {
        g_lastInflux = now;
        influxPush(*g_app);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
  }
}
}  // namespace

void netTaskBegin(App& app, Shelly& shelly, volatile bool* wifiUp,
                  volatile bool* wifiReinitNeeded) {
  g_app = &app;
  g_shelly = &shelly;
  g_wifiUp = wifiUp;
  g_wifiReinitNeeded = wifiReinitNeeded;
  g_wifiEnabled = app.config.wifi.enabled;
  // Pin to core 0 so the blocking HTTP calls never contend with loop()/touch
  // on core 1. Stack 8192 is sufficient: the same calls ran inside loop()'s
  // 8192-byte task stack before the move.
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace dpf
