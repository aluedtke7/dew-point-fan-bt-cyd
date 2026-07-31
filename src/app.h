// Shared application state. Equivalent to the MicroPython `App`/`Store`.
// The BLE callback (NimBLE task) writes `sensors`/`store`; the control loop,
// display and HTTP handlers (main task) read them. Both run as FreeRTOS tasks
// (not ISRs), so a plain mutex is the right primitive — and unlike a portMUX
// critical section it lets us safely copy the std::string-bearing structs.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "sensordata.h"

namespace dpf {

constexpr size_t kMaxSensorData = 20;

struct Store {
  SensorDataList inside{kMaxSensorData};
  SensorDataList outside{kMaxSensorData};
};

struct App {
  Config config;
  Sensors sensors;              // configured MACs + latest reading per sensor
  Store store;                  // rolling window for averages / InfluxDB
  ResultData result;
  int overrideMode = 0;         // 0 = auto, 1 = force on, 2 = force off
  SemaphoreHandle_t mutex = nullptr;

  // Create the mutex. Call once from setup() before starting the BLE scan.
  void begin() {
    if (mutex == nullptr) mutex = xSemaphoreCreateMutex();
  }
};

// RAII lock over App::mutex. Keep the locked scope short.
class LockGuard {
 public:
  explicit LockGuard(App& app) : app_(app) {
    xSemaphoreTake(app_.mutex, portMAX_DELAY);
  }
  ~LockGuard() { xSemaphoreGive(app_.mutex); }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  App& app_;
};

}  // namespace dpf
