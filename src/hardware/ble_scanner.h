// BLE observer that scans for ThermoBeacon (WS02) advertisements and feeds the
// sensor store. Ported from the MicroPython `hardware/ble_scanner.py` (aioble)
// to NimBLE-Arduino. Passive/observer scanning only — no connections.
#pragma once

#include "app.h"

namespace dpf {

// Start continuous passive scanning. Advertisements are parsed on the NimBLE
// task and written into `app.sensors` / `app.store` under `app.mutex`.
// `nowFn` returns the current monotonic time in seconds (same clock the control
// loop uses for freshness). Call once from setup().
void bleScannerBegin(App& app, double (*nowFn)());

// Temporarily stop/resume BLE scanning.  Used by the Shelly HTTP client to
// give the WiFi radio exclusive access to the air interface during TCP
// handshakes — NimBLE 2.x coexistence starves lwIP otherwise.
void bleScannerPause();
void bleScannerResume();

}  // namespace dpf
