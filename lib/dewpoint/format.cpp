#include "format.h"

#include "sensordata.h"

#include <cstdio>
#include <ctime>

namespace dpf {

namespace {
// Epoch below which the clock is considered not yet NTP-synced (Sept 2001). Same
// threshold net.cpp/main.cpp use to gate wall-clock use.
constexpr time_t kClockSyncedEpoch = 1000000000;
// Upper bound for the age printed by formatSensorHealth (~27 h).
constexpr double kMaxHealthAgeSeconds = 99999;
}  // namespace

size_t formatMeasure(char* buf, size_t cap, double value, const char* unit,
                     bool valid) {
  if (!valid) {
    if (cap > 0) buf[0] = '\0';
    std::snprintf(buf, cap, "--");
    return 2;
  }
  return static_cast<size_t>(
      std::snprintf(buf, cap, "%.1f%s", value, unit));
}

size_t formatDiff(char* buf, size_t cap, double diff, double thr, bool valid) {
  if (valid) {
    return static_cast<size_t>(
        std::snprintf(buf, cap, "Diff %.1f`C  (thr %.1f)", diff, thr));
  }
  return static_cast<size_t>(
      std::snprintf(buf, cap, "Diff --  (thr %.1f)", thr));
}

size_t formatLogTimestamp(char* buf, size_t cap, time_t epoch) {
  if (epoch < kClockSyncedEpoch) {
    return static_cast<size_t>(std::snprintf(buf, cap, "00.00.00 00:00:00"));
  }
  struct tm tmv;
  localtime_r(&epoch, &tmv);  // reentrant: logf runs on several FreeRTOS tasks
  return static_cast<size_t>(
      std::strftime(buf, cap, "%d.%m.%y %H:%M:%S", &tmv));
}

size_t formatHeapSuffix(char* buf, size_t cap, uint32_t freeHeap,
                        uint32_t minFreeHeap, uint32_t largestBlock) {
  return static_cast<size_t>(std::snprintf(
      buf, cap, "heap=%lu min=%lu blk=%lu",
      static_cast<unsigned long>(freeHeap),
      static_cast<unsigned long>(minFreeHeap),
      static_cast<unsigned long>(largestBlock)));
}

size_t formatNetLine(char* buf, size_t cap, const char* ip, const char* ssid,
                     bool online) {
  if (!online || ip == nullptr || ip[0] == '\0') {
    return static_cast<size_t>(std::snprintf(buf, cap, "IP -- (offline)"));
  }
  if (ssid == nullptr || ssid[0] == '\0') {
    return static_cast<size_t>(std::snprintf(buf, cap, "IP %s", ip));
  }
  return static_cast<size_t>(std::snprintf(buf, cap, "IP %s   %s", ip, ssid));
}

size_t formatSensorHealth(char* buf, size_t cap, const char* label,
                          const SensorData& s, double now) {
  if (!s.hasReading) {
    return static_cast<size_t>(std::snprintf(buf, cap, "%s: no data", label));
  }
  // Clamp before the integer cast: the unsynced-clock sentinel "now" (1e18)
  // would overflow a 32-bit long.
  double age = now - static_cast<double>(s.scanned);
  if (age < 0) age = 0;
  if (age > kMaxHealthAgeSeconds) age = kMaxHealthAgeSeconds;
  return static_cast<size_t>(std::snprintf(
      buf, cap, "%s: age=%lds rssi=%d bat=%umV", label, static_cast<long>(age),
      s.rssi,
      static_cast<unsigned>(s.batLevel)));
}

}  // namespace dpf
