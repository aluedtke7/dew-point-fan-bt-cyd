// Network control task: the Shelly relay RPC and the InfluxDB push.
//
// These are synchronous HTTP calls (Shelly is bounded at 3 s, Influx at the
// HTTPClient default ~10 s). They used to run inline in loop(), which froze the
// touch UI for the duration of each request. Like the BLE scan, they now run on
// their own FreeRTOS task (pinned to core 0) so loop()'s touch poll and render
// stay responsive. The task reads the control decision from the shared state
// under lock and copies state in/out only around the lock — never across the
// blocking HTTP call, mirroring the BLE callback discipline.
#pragma once

#include "app.h"
#include "shelly.h"

namespace dpf {

// Start the network control task. `wifiUp` points at the shared "WiFi
// associated" flag, which the task owns: it clears the flag when the link
// drops and sets it again once the link (and a DHCP address) are back. The
// Shelly RPC and the InfluxDB push idle while it is false.  After a successful
// recovery the task sets *wifiReinitNeeded = true so the main loop can re-bind
// listeners and re-sync NTP.
void netTaskBegin(App& app, Shelly& shelly, volatile bool* wifiUp,
                  volatile bool* wifiReinitNeeded);

}  // namespace dpf
