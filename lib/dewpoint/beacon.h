// ThermoBeacon (WS02) advertisement parsing.
// Ported from `parseWS02Data` in Go `bluetooth/scanner.go` (via the MicroPython
// `dewpoint/beacon.py`). Pure byte handling, fully unit-testable without a radio.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "sensordata.h"

namespace dpf {

// Expected length of the WS02 manufacturer-data payload.
constexpr size_t kPayloadLen = 18;

// Parse a WS02 payload into a SensorData.
// Returns std::nullopt if the payload is not exactly kPayloadLen bytes. The
// returned SensorData.name is "Inside"/"Outside" when the MAC matches a
// configured sensor, otherwise "" (caller should ignore unknown beacons).
//
// payload/len: raw manufacturer data (binary, may contain NUL bytes).
// rssi: received signal strength. sensors: configured MACs + calibrations.
// now: epoch-seconds timestamp to stamp onto the reading.
std::optional<SensorData> parseWS02(const uint8_t* payload, size_t len,
                                    int16_t rssi, const Sensors& sensors,
                                    double now);

// Format uptime seconds as "Xd Yh Zm". Ported from `formatUptime`.
std::string formatUptime(uint32_t seconds);

}  // namespace dpf
