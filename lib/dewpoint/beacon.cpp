#include "beacon.h"

#include <cstdio>
#include <cstring>

#include "dp_math.h"

namespace dpf {

namespace {
// Field offsets within the 18-byte manufacturer-data payload.
constexpr size_t kMacOffset = 2;
constexpr size_t kBatOffset = 8;
constexpr size_t kTempOffset = 10;
constexpr size_t kHumidityOffset = 12;
constexpr size_t kUptimeOffset = 14;

uint16_t readU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

std::optional<SensorData> parseWS02(const uint8_t* payload, size_t len,
                                    int16_t rssi, const Sensors& sensors,
                                    double now) {
  if (len != kPayloadLen) {
    return std::nullopt;
  }

  // MAC is stored little-endian; render reversed as "AA:BB:CC:DD:EE:FF".
  char mac[18];
  std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                payload[kMacOffset + 5], payload[kMacOffset + 4],
                payload[kMacOffset + 3], payload[kMacOffset + 2],
                payload[kMacOffset + 1], payload[kMacOffset + 0]);

  const char* name = "";
  double tempCal = 0.0;
  double humCal = 0.0;
  if (std::strncmp(mac, sensors.insideData.macAddress, 18) == 0) {
    name = "Inside";
    tempCal = sensors.insideCalibration.temperature;
    humCal = sensors.insideCalibration.humidity;
  } else if (std::strncmp(mac, sensors.outsideData.macAddress, 18) == 0) {
    name = "Outside";
    tempCal = sensors.outsideCalibration.temperature;
    humCal = sensors.outsideCalibration.humidity;
  }

  const uint16_t batLevel = readU16LE(payload + kBatOffset);
  const uint32_t uptime = readU32LE(payload + kUptimeOffset);

  double temperatureRaw = readU16LE(payload + kTempOffset) / 16.0;
  if (temperatureRaw > 4000.0) temperatureRaw -= 4096.0;
  const double temperature = temperatureRaw + tempCal;

  double humidityRaw = readU16LE(payload + kHumidityOffset) / 16.0;
  if (humidityRaw > 4000.0) humidityRaw -= 4096.0;
  const double humidity = humidityRaw + humCal;

  const double roundedTemperature = roundDouble(temperature, 1);
  const double roundedHumidity = roundDouble(humidity, 1);

  SensorData data;
  std::strncpy(data.macAddress, mac, sizeof(data.macAddress));
  data.macAddress[17] = '\0';
  data.name = name;
  data.batLevel = batLevel;
  data.rssi = rssi;
  data.uptime = uptime;
  data.temperature = roundedTemperature;
  data.humidity = roundedHumidity;
  data.dewPoint = calcDewPoint(roundedTemperature, roundedHumidity);
  data.scanned = now;
  data.hasReading = true;
  return data;
}

std::string formatUptime(uint32_t seconds) {
  const uint32_t days = seconds / (24 * 3600);
  const uint32_t hours = (seconds % (24 * 3600)) / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%ud %uh %um",
                static_cast<unsigned>(days), static_cast<unsigned>(hours),
                static_cast<unsigned>(minutes));
  return std::string(buf);
}

}  // namespace dpf
