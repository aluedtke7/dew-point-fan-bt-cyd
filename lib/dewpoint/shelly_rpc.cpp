#include "shelly_rpc.h"

namespace dpf {

std::string switchSetPath(int switchId, bool on) {
  std::string s = "/rpc/Switch.Set?id=";
  s += std::to_string(switchId);
  s += "&on=";
  s += on ? "true" : "false";
  return s;
}

namespace {
// True when a TriState equals a desired boolean (Unknown never matches).
bool matches(TriState s, bool desired) {
  return (s == TriState::On && desired) || (s == TriState::Off && !desired);
}
}  // namespace

bool shouldSendRpc(bool desired, TriState confirmed, TriState lastTarget,
                    double now, double lastAttempt, double retryInterval,
                    uint32_t consecutiveFails, uint32_t maxAttempts) {
  if (matches(confirmed, desired)) return false;  // already at the desired state
  if (!matches(lastTarget, desired)) return true;  // target changed -> act now
  if (consecutiveFails >= maxAttempts) return false;  // gave up on this target
  return now - lastAttempt >= retryInterval;        // retrying -> rate-limit
}

}  // namespace dpf
