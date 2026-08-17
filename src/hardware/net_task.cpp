#include "net_task.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>

#include "logging.h"
#include "net.h"        // influxPush
#include "shelly_rpc.h" // shouldSendRpc, TriState

namespace dpf {

namespace {
App* g_app = nullptr;
Shelly* g_shelly = nullptr;
bool* g_wifiUp = nullptr;
bool* g_wifiReinitNeeded = nullptr;

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

// Monotonic seconds clock, same timebase as main.cpp's nowSeconds().
double nowSeconds() { return esp_timer_get_time() / 1000000.0; }

void netTask(void*) {
  const double bootTime = nowSeconds();
  g_lastInflux = bootTime;
  for (;;) {
    const double now = nowSeconds();

    // WiFi reconnection: monitor the link and attempt recovery when it drops.
    if (g_wifiUp && *g_wifiUp && WiFi.status() != WL_CONNECTED) {
      *g_wifiUp = false;
      logf("[wifi] disconnected, reconnecting...\n");
      WiFi.reconnect();
      // Give the reconnection up to 5 s before giving up for this tick.
      for (int i = 0; i < 50 && WiFi.status() != WL_CONNECTED; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if (WiFi.status() == WL_CONNECTED) {
        *g_wifiUp = true;
        if (g_wifiReinitNeeded) *g_wifiReinitNeeded = true;
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        logf("[wifi] reconnected, IP: %s\n", ipBuf);
      } else {
        logf("[wifi] reconnect failed, will retry next tick\n");
      }
    }

    if (g_wifiUp && *g_wifiUp && (now - bootTime >= kStartupGraceSec)) {
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

void netTaskBegin(App& app, Shelly& shelly, bool* wifiUp, bool* wifiReinitNeeded) {
  g_app = &app;
  g_shelly = &shelly;
  g_wifiUp = wifiUp;
  g_wifiReinitNeeded = wifiReinitNeeded;
  // Pin to core 0 so the blocking HTTP calls never contend with loop()/touch
  // on core 1. Stack 8192 is sufficient: the same calls ran inside loop()'s
  // 8192-byte task stack before the move.
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace dpf
