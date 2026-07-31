#include "control.h"

namespace dpf {

void computeResults(const SensorData& inside, const SensorData& outside,
                    ResultData& result, const FanConfig& config,
                    int remoteOverride, double now) {
  if (remoteOverride > 0) {
    // manual override via touch UI / REST api
    if (remoteOverride == 1) {
      result.shouldBeOn = true;
      result.reason = Reason::SoftOverrideOn;
    } else {
      result.shouldBeOn = false;
      result.reason = Reason::SoftOverrideOff;
    }
    return;
  }

  if (!inside.hasReading || !outside.hasReading) {
    result.shouldBeOn = false;
    result.reason = Reason::NoData;
    return;
  }

  const double staleBefore = now - kFreshnessSeconds;
  if (inside.scanned < staleBefore || outside.scanned < staleBefore) {
    result.shouldBeOn = false;
    result.reason = Reason::NotEnoughData;
    return;
  }

  result.sensorsReady = true;

  if (inside.temperature < config.minTempInside) {
    result.shouldBeOn = false;
    result.reason = Reason::InsideTempTooLow;
    return;
  }

  if (outside.temperature < config.minTempOutside) {
    result.shouldBeOn = false;
    result.reason = Reason::OutsideTempTooLow;
    return;
  }

  if (inside.humidity < config.minHumidityInside) {
    result.shouldBeOn = false;
    result.reason = Reason::InsideHumidityTooLow;
    return;
  }

  const double deltaDp = inside.dewPoint - outside.dewPoint;
  if (deltaDp < config.minDiff) {
    result.shouldBeOn = false;
    result.reason = Reason::DewpointUnderHyst;
    return;
  }

  if (deltaDp >= config.minDiff + config.hysteresis) {
    result.shouldBeOn = true;
    result.reason = Reason::DewpointOverHyst;
    return;
  }

  if (deltaDp >= config.minDiff && deltaDp < config.minDiff + config.hysteresis) {
    // we don't change the fan state since we don't know if the dew point is
    // rising or falling
    result.reason = Reason::DewpointInBetween;
    return;
  }

  result.shouldBeOn = false;
  result.reason = Reason::Unknown;
}

}  // namespace dpf
