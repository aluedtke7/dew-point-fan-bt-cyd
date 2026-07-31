// Shelly Gen3 HTTP-RPC helpers: the request path that switches the relay, and
// the edge/backoff decision for when to (re)send a command.
//
// Pure and unit-testable on the host; the HTTP transport that fetches the path
// lives in `src/hardware/shelly.*`.
#pragma once

#include <cstdint>
#include <string>

namespace dpf {

// Build the RPC path that switches the plug's relay:
//   /rpc/Switch.Set?id=<switchId>&on=<true|false>
// switchId is the relay channel (0 on a single-relay plug).
std::string switchSetPath(int switchId, bool on);

// A relay state that may also be unknown. `confirmed` starts Unknown after boot
// (the plug may be in any state); `lastTarget` is Unknown before the first send.
enum class TriState : uint8_t { Unknown, Off, On };

// Decide whether the control loop should send a Switch.Set this tick.
//   confirmed        - the relay state the plug last acknowledged
//   lastTarget       - the desired state of the previous send attempt
//   now              - monotonic seconds
//   lastAttempt      - monotonic seconds of the previous send attempt
//   retryInterval    - minimum seconds between re-attempts of the SAME target
//   consecutiveFails - how many times the current target has failed in a row
//   maxAttempts      - give up (return false) once consecutiveFails >= maxAttempts
// Returns false when the plug is already at `desired`. Otherwise sends
// immediately when `desired` differs from `lastTarget` (a genuine state change),
// and only rate-limits with `retryInterval` when re-attempting the SAME target
// (i.e. a previous attempt to reach it did not confirm) — so a button press or a
// dew-point flip acts at once, while an unreachable plug isn't hammered. Once the
// same target has failed `maxAttempts` times in a row it stops retrying entirely,
// so a plug that is (and stays) unreachable doesn't log forever; a genuine state
// change resets the failure count and retries fresh.
bool shouldSendRpc(bool desired, TriState confirmed, TriState lastTarget,
                   double now, double lastAttempt, double retryInterval,
                   uint32_t consecutiveFails, uint32_t maxAttempts);

}  // namespace dpf
