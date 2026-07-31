// Sensor data structures and the rolling-average store.
// Ported from the Go `sensor` package (`structs.go`, `sensor_store.go`) and the
// MicroPython `dewpoint/sensordata.py`. Pure C++, no Arduino dependencies.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace dpf {

using ScreenLines = std::array<std::string, 4>;

constexpr const char* kVersion = "DewPointFan CYD v1";

inline ScreenLines startScreen(const std::string& buildTime) {
  return {kVersion, "Build time:", buildTime, "Connecting to WiFi..."};
}

// Reason codes for a fan decision. Values mirror the Go iota order so any
// persisted/transmitted integers stay compatible.
enum class Reason : uint8_t {
  None = 0,
  NoData = 1,
  NotEnoughData = 2,
  DewpointOverHyst = 3,
  DewpointUnderHyst = 4,
  DewpointInBetween = 5,
  InsideTempTooLow = 6,
  OutsideTempTooLow = 7,
  InsideHumidityTooLow = 8,
  SoftOverrideOn = 9,
  SoftOverrideOff = 10,
  Unknown = 11,
};

// Human-readable name for a reason (matches the MicroPython REASON_NAME table).
const char* reasonName(Reason reason);

// Human-readable name for an override mode (0 = auto, 1 = force on, 2 = force off).
const char* overrideName(int mode);

// A single reading from a sensor plus its metadata.
// `scanned` is the UTC epoch-seconds timestamp of the reading (set from the
// NTP-synced wall clock); it is only valid when `hasReading` is true.
struct SensorData {
  char macAddress[18] = "";     // "AA:BB:CC:DD:EE:FF" + null
  const char* name = "";        // "Inside" / "Outside" / "" (string literals only)
  uint16_t batLevel = 0;
  int16_t rssi = 0;
  uint32_t uptime = 0;
  double temperature = 0.0;
  double humidity = 0.0;
  double dewPoint = 0.0;
  int64_t scanned = 0;
  bool hasReading = false;
};

// Per-sensor calibration offsets added to the raw reading.
struct SensorCalibration {
  double temperature = 0.0;
  double humidity = 0.0;
};

// Identity (MAC) and calibration for the inside and outside sensors.
struct Sensors {
  SensorData insideData;
  SensorData outsideData;
  SensorCalibration insideCalibration;
  SensorCalibration outsideCalibration;
};

// Thresholds controlling fan behaviour.
struct FanConfig {
  double minDiff = 0.0;
  double hysteresis = 0.0;
  double minHumidityInside = 0.0;
  double minTempInside = 0.0;
  double minTempOutside = 0.0;
};

// Output of a fan decision.
struct ResultData {
  double dpDiff = 0.0;
  bool shouldBeOn = false;
  bool isOn = false;
  bool sensorsReady = false;  // both sensors have reported valid fresh data
  Reason reason = Reason::None;
};

// Fixed-capacity ring buffer of recent readings, with averages. Capacity below 5 is clamped to 5.
class SensorDataList {
 public:
  explicit SensorDataList(size_t maxData);

  void add(const SensorData& sensorData);
  size_t size() const;
  double averageTemperature() const;
  double averageHumidity() const;
  double averageDewPoint() const;

 private:
  std::vector<SensorData> data_;
  size_t max_;
};

}  // namespace dpf
