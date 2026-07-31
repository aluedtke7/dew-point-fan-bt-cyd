#include "lastalive.h"

#include <esp_attr.h>

namespace dpf {

namespace {
constexpr uint32_t kMagic = 0xD9FA1AFE;

struct RtcRecord {
  uint32_t magic;
  uint32_t bootCount;
  int64_t bootEpoch;
  int64_t lastAliveEpoch;
};

// Survives panic/WDT/brownout/software resets; random garbage after power-on
// (caught by kMagic) and cleared only by a full RTC power loss.
RTC_NOINIT_ATTR RtcRecord g_rtc;
}  // namespace

LastAlivePrev lastAliveBegin(int64_t bootEpoch) {
  LastAlivePrev prev;
  if (g_rtc.magic == kMagic) {
    prev.valid = true;
    prev.bootCount = g_rtc.bootCount;
    prev.bootEpoch = g_rtc.bootEpoch;
    prev.lastAliveEpoch = g_rtc.lastAliveEpoch;
    g_rtc.bootCount = prev.bootCount + 1;
  } else {
    g_rtc.magic = kMagic;
    g_rtc.bootCount = 1;
  }
  g_rtc.bootEpoch = bootEpoch;
  g_rtc.lastAliveEpoch = bootEpoch;
  return prev;
}

void lastAliveHeartbeat(int64_t nowEpoch) { g_rtc.lastAliveEpoch = nowEpoch; }

}  // namespace dpf
