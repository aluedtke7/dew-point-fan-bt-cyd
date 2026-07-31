// Ported from Go `sensor/sensor_store_test.go` / MicroPython tests/test_sensordata.py.
#include <unity.h>

#include <cmath>

#include "sensordata.h"

using namespace dpf;

void setUp() {}
void tearDown() {}

static SensorData reading(double t, double h, double dp) {
  SensorData s;
  s.temperature = t;
  s.humidity = h;
  s.dewPoint = dp;
  return s;
}

static void assertNear(double expected, double actual) {
  TEST_ASSERT_TRUE(std::fabs(actual - expected) <= 1e-6);
}

void test_capacity_clamped_to_minimum_five() {
  SensorDataList store(2);  // below 5 -> clamped to 5
  for (int i = 0; i < 6; ++i) store.add(reading(i, 0, 0));
  TEST_ASSERT_EQUAL_size_t(5, store.size());
}

void test_oldest_entry_evicted_when_full() {
  SensorDataList store(5);
  for (int i = 0; i < 7; ++i) store.add(reading(i, 0, 0));
  // 7 added, capacity 5 -> values 2..6 remain, average = 4.0
  TEST_ASSERT_EQUAL_size_t(5, store.size());
  assertNear(4.0, store.averageTemperature());
}

void test_empty_averages_are_zero() {
  SensorDataList store(5);
  assertNear(0.0, store.averageTemperature());
  assertNear(0.0, store.averageHumidity());
  assertNear(0.0, store.averageDewPoint());
}

void test_averages_rounded_to_one_decimal() {
  SensorDataList store(5);
  store.add(reading(20.0, 50.0, 9.0));
  store.add(reading(21.0, 51.0, 10.0));
  store.add(reading(22.0, 52.0, 11.0));
  assertNear(21.0, store.averageTemperature());
  assertNear(51.0, store.averageHumidity());
  assertNear(10.0, store.averageDewPoint());
}

void test_average_rounding() {
  SensorDataList store(5);
  store.add(reading(20.0, 0, 0));
  store.add(reading(20.0, 0, 0));
  store.add(reading(21.0, 0, 0));
  // mean = 20.333... -> rounded to 20.3
  assertNear(20.3, store.averageTemperature());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_capacity_clamped_to_minimum_five);
  RUN_TEST(test_oldest_entry_evicted_when_full);
  RUN_TEST(test_empty_averages_are_zero);
  RUN_TEST(test_averages_rounded_to_one_decimal);
  RUN_TEST(test_average_rounding);
  return UNITY_END();
}
