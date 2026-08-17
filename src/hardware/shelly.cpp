#include "shelly.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <cerrno>
#include <cstring>

#include "logging.h"
#include "shelly_rpc.h"
#include "ble_scanner.h"

namespace dpf {

namespace {
// Per-attempt request timeout.  Long enough for the TCP handshake to complete
// even when NimBLE 2.x BLE scanning shares the radio; the initial ARP resolution
// for the plug's MAC may also need a full round-trip.
constexpr uint16_t kHttpTimeoutMs = 5000;
// Attempts within a single set() call. A fresh boot switches the plug the
// instant WiFi is up, before ARP/routing settle; retrying here (instead of
// waiting for the caller's 15 s backoff) closes that window.
constexpr int kAttempts = 4;
// Short pause between attempts to let ARP resolution / the stack settle.
constexpr uint32_t kRetryDelayMs = 1000;

// Persistent HTTPClient: the underlying WiFiClient socket is kept open between
// requests (HTTPClient defaults to keep-alive), avoiding a fresh connect plus
// the 1436-byte RX buffer alloc on every call.
HTTPClient g_http;

// One HTTP GET attempt. Returns the HTTP status code (200 = success), or a
// negative HTTPClient error code; `outErrno` receives errno on failure.
int getOnce(const char* url, int& outErrno) {
  g_http.end();  // release any stale WiFiClient from a previous failed call
  g_http.setConnectTimeout(kHttpTimeoutMs);
  g_http.setTimeout(kHttpTimeoutMs);
  if (!g_http.begin(url)) {
    outErrno = 0;
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  const int code = g_http.GET();
  // Capture errno now: connect()/select() set it on failure, and g_http.end()
  // (-> _client->stop()) may reset it before we read it.
  outErrno = (code <= 0) ? errno : 0;
  g_http.end();
  return code;
}
}  // namespace

void Shelly::begin(const std::string& host, int switchId) {
  host_ = host;
  switchId_ = switchId;
}

bool Shelly::set(bool on) {
  // Build the URL on the stack: avoids 2 temp std::string allocations per call.
  char url[128];
  snprintf(url, sizeof(url), "http://%s/rpc/Switch.Set?id=%d&on=%s",
           host_.c_str(), switchId_, on ? "true" : "false");
  // Log the attempt up front so a connection failure still shows the target.
  logf("[shelly] %s -> %s\n", on ? "ON " : "OFF", url);

  int code = 0;
  int sockErr = 0;
  // Stop BLE scanning while we talk to the Shelly plug.  NimBLE 2.x radio
  // coexistence starves lwIP of air time, preventing TCP handshakes even at
  // 50 % scan duty-cycle.
  bleScannerPause();
  for (int attempt = 1; attempt <= kAttempts; ++attempt) {
    code = getOnce(url, sockErr);
    if (code == 200) {
      bleScannerResume();
      on_ = on;
      if (attempt > 1) {
        logf("[shelly] %s ok (attempt %d)\n", on ? "ON" : "OFF", attempt);
      } else {
        logf("[shelly] %s ok\n", on ? "ON" : "OFF");
      }
      return true;
    }
    if (attempt < kAttempts) delay(kRetryDelayMs);
  }
  bleScannerResume();

  IPAddress selfIp = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress gw = WiFi.gatewayIP();
  char ipSelf[16], ipMask[16], ipGw[16];
  snprintf(ipSelf, sizeof(ipSelf), "%u.%u.%u.%u", selfIp[0], selfIp[1], selfIp[2], selfIp[3]);
  snprintf(ipMask, sizeof(ipMask), "%u.%u.%u.%u", mask[0], mask[1], mask[2], mask[3]);
  snprintf(ipGw, sizeof(ipGw), "%u.%u.%u.%u", gw[0], gw[1], gw[2], gw[3]);
  logf("[shelly] %s failed: HTTP %d (%s) errno=%d (%s) wifi=%d self=%s/%s gw=%s\n",
       on ? "ON" : "OFF", code, HTTPClient::errorToString(code).c_str(),
       sockErr, sockErr ? strerror(sockErr) : "ok",
       WiFi.status(), ipSelf, ipMask, ipGw);
  return false;
}

}  // namespace dpf
