#include "sensordata.h"

#include "dp_math.h"

namespace dpf {

const char* reasonName(Reason reason) {
  switch (reason) {
    case Reason::None: return "None";
    case Reason::NoData: return "No data";
    case Reason::NotEnoughData: return "Not enough data";
    case Reason::DewpointOverHyst: return "DP > hysteresis";
    case Reason::DewpointUnderHyst: return "DP < hysteresis";
    case Reason::DewpointInBetween: return "DP in between";
    case Reason::InsideTempTooLow: return "Inside temp too low";
    case Reason::OutsideTempTooLow: return "Outside temp too low";
    case Reason::InsideHumidityTooLow: return "Inside hum too low";
    case Reason::SoftOverrideOn: return "Soft override on";
    case Reason::SoftOverrideOff: return "Soft override off";
    case Reason::Unknown: return "Unknown reason";
  }
  return "Unknown reason";
}

const char* overrideName(int mode) {
  switch (mode) {
    case 0: return "auto";
    case 1: return "force on";
    case 2: return "force off";
  }
  return "unknown";
}

SensorDataList::SensorDataList(size_t maxData) : max_(maxData < 5 ? 5 : maxData) {
  data_.reserve(max_);
}

void SensorDataList::add(const SensorData& sensorData) {
  if (data_.size() >= max_) {
    data_.erase(data_.begin());
  }
  data_.push_back(sensorData);
}

size_t SensorDataList::size() const { return data_.size(); }

double SensorDataList::averageTemperature() const {
  if (data_.empty()) return 0.0;
  double total = 0.0;
  for (const auto& s : data_) total += s.temperature;
  return roundDouble(total / data_.size(), 1);
}

double SensorDataList::averageHumidity() const {
  if (data_.empty()) return 0.0;
  double total = 0.0;
  for (const auto& s : data_) total += s.humidity;
  return roundDouble(total / data_.size(), 1);
}

double SensorDataList::averageDewPoint() const {
  if (data_.empty()) return 0.0;
  double total = 0.0;
  for (const auto& s : data_) total += s.dewPoint;
  return roundDouble(total / data_.size(), 1);
}

}  // namespace dpf
