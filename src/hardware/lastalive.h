// "Last alive" crash forensics in RTC RAM.
//
// On this Arduino core (2.0.17 / IDF 4.4) there is no esp_set_panic_handler()
// hook and core-dump-to-flash needs sdkconfig changes, so a backtrace cannot be
// captured. What RTC RAM gives us instead: it survives every reset except a
// real power cycle, so a small record written here lets the *next* boot report
// when the previous run died and how long it had been up. Combined with the
// reset reason (esp_reset_reason, logged at boot) this correlates crashes with
// activity (idle overnight vs. right after an HTTP override) without needing a
// serial monitor attached.
//
// Writes are two plain stores to RTC memory — safe to do anytime, no locks,
// no flash, no allocation. The `magic` field distinguishes a surviving record
// from random power-on garbage.
#pragma once

#include <cstdint>

namespace dpf {

// Snapshot of the previous run's record, as found at boot.
struct LastAlivePrev {
  bool valid = false;      // true only if RTC RAM held an intact record
  uint32_t bootCount = 0;  // which boot the previous run was (1-based)
  int64_t bootEpoch = 0;   // wall clock at that run's boot (0 if unsynced)
  int64_t lastAliveEpoch = 0;  // last heartbeat before it died
};

// Call once at the very top of setup(). `bootEpoch` is the current wall clock
// (may be stale-but-valid RTC time after a panic reset, or ~0 after power
// loss). Returns the previous run's record; valid==false means there is
// nothing to report (first boot after flash, or a real power cycle).
LastAlivePrev lastAliveBegin(int64_t bootEpoch);

// Refresh the alive timestamp. Call periodically from loop() once the clock
// is NTP-synced (every 30 s is plenty; the write itself is free).
void lastAliveHeartbeat(int64_t nowEpoch);

}  // namespace dpf
