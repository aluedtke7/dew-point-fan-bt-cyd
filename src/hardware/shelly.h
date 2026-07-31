// Fan actuator: a Shelly Plug (Gen3) switched over WiFi via its HTTP RPC
// endpoint, replacing the old GPIO27 relay. A Switch.Set request is a single
// short HTTP GET (`http://<host>/rpc/Switch.Set?id=<ch>&on=<bool>`), bounded by
// a timeout — no BLE, no pairing. The request path is the pure, host-tested
// `shelly_rpc.h`; this module is just the HTTP transport (mirrors the InfluxDB
// push in net.cpp). The control loop edge-triggers and retries with a backoff,
// so a brief per-attempt block is acceptable.
#pragma once

#include <string>

namespace dpf {

class Shelly {
 public:
  // Store the plug's host (IP or hostname) and relay channel. Call once from
  // setup(); the actual switching needs WiFi to be up.
  void begin(const std::string& host, int switchId);

  // Switch the relay on/off via HTTP RPC. Blocks for the request (bounded by the
  // HTTP timeout). Returns true when the plug acknowledged (HTTP 200).
  bool set(bool on);

  // Last relay state the plug acknowledged.
  bool isOn() const { return on_; }

 private:
  std::string host_;
  int switchId_ = 0;
  bool on_ = false;
};

}  // namespace dpf
