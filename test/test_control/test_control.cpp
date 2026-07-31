// Ported from Go `dpf-main_test.go` / MicroPython tests/test_control.py.
// A fixed NOW is injected so freshness is deterministic and clock-independent.
#include <unity.h>

#include "control.h"
#include "sensordata.h"

using namespace dpf;

static constexpr double NOW = 1000000.0;
static constexpr double OLD = NOW - 10 * 60;  // 10 minutes old -> stale

void setUp() {}
void tearDown() {}

// Build a reading. hasReading defaults to true; pass false for "no data".
static SensorData sd(double temperature = 0.0, double humidity = 0.0,
                     double dewPoint = 0.0, double scanned = NOW,
                     bool hasReading = true) {
  SensorData s;
  s.temperature = temperature;
  s.humidity = humidity;
  s.dewPoint = dewPoint;
  s.scanned = scanned;
  s.hasReading = hasReading;
  return s;
}

static ResultData run(const SensorData& inside, const SensorData& outside,
                      int remoteOverride, const FanConfig& config,
                      bool lastShouldBeOn) {
  ResultData result;
  result.shouldBeOn = lastShouldBeOn;
  result.reason = Reason::None;
  computeResults(inside, outside, result, config, remoteOverride, NOW);
  return result;
}

void test_remote_override_on() {
  ResultData r = run(sd(), sd(), 1, FanConfig(), false);
  TEST_ASSERT_TRUE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::SoftOverrideOn, r.reason);
  TEST_ASSERT_FALSE(r.sensorsReady);
}

void test_remote_override_off() {
  ResultData r = run(sd(), sd(), 2, FanConfig(), true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::SoftOverrideOff, r.reason);
  TEST_ASSERT_FALSE(r.sensorsReady);
}

void test_no_data_inside() {
  ResultData r = run(sd(0, 0, 0, NOW, false), sd(0, 0, 0, NOW, true), 0,
                     FanConfig(), true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::NoData, r.reason);
  TEST_ASSERT_FALSE(r.sensorsReady);
}

void test_no_data_outside() {
  ResultData r = run(sd(0, 0, 0, NOW, true), sd(0, 0, 0, NOW, false), 0,
                     FanConfig(), true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::NoData, r.reason);
  TEST_ASSERT_FALSE(r.sensorsReady);
}

void test_data_too_old() {
  ResultData r = run(sd(0, 0, 0, OLD), sd(0, 0, 0, NOW), 0, FanConfig(), true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::NotEnoughData, r.reason);
  TEST_ASSERT_FALSE(r.sensorsReady);
}

void test_low_inside_temperature() {
  FanConfig c;
  c.minTempInside = 20.0;
  ResultData r = run(sd(18.0), sd(), 0, c, true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::InsideTempTooLow, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

void test_low_outside_temperature() {
  FanConfig c;
  c.minTempOutside = 10.0;
  ResultData r = run(sd(21.0), sd(7.0), 0, c, true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::OutsideTempTooLow, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

void test_low_inside_humidity() {
  FanConfig c;
  c.minHumidityInside = 50.0;
  ResultData r = run(sd(0, 45.0), sd(), 0, c, true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::InsideHumidityTooLow, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

void test_dew_point_below_hysteresis() {
  FanConfig c;
  c.minDiff = 4.0;
  ResultData r = run(sd(0, 0, 10.0), sd(0, 0, 7.0), 0, c, true);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::DewpointUnderHyst, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

void test_dew_point_above_hysteresis() {
  FanConfig c;
  c.minDiff = 4.0;
  c.hysteresis = 2.0;
  ResultData r = run(sd(0, 0, 15.0), sd(0, 0, 9.0), 0, c, false);
  TEST_ASSERT_TRUE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::DewpointOverHyst, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

void test_dew_point_in_between_from_low() {
  // delta = 4, band is [4, 6): state is kept (was off -> stays off).
  FanConfig c;
  c.minDiff = 4.0;
  c.hysteresis = 2.0;
  ResultData r = run(sd(0, 0, 13.0), sd(0, 0, 9.0), 0, c, false);
  TEST_ASSERT_FALSE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::DewpointInBetween, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

void test_dew_point_in_between_from_high() {
  // delta = 4, band is [4, 6): state is kept (was on -> stays on).
  FanConfig c;
  c.minDiff = 4.0;
  c.hysteresis = 2.0;
  ResultData r = run(sd(0, 0, 13.0), sd(0, 0, 9.0), 0, c, true);
  TEST_ASSERT_TRUE(r.shouldBeOn);
  TEST_ASSERT_EQUAL(Reason::DewpointInBetween, r.reason);
  TEST_ASSERT_TRUE(r.sensorsReady);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_remote_override_on);
  RUN_TEST(test_remote_override_off);
  RUN_TEST(test_no_data_inside);
  RUN_TEST(test_no_data_outside);
  RUN_TEST(test_data_too_old);
  RUN_TEST(test_low_inside_temperature);
  RUN_TEST(test_low_outside_temperature);
  RUN_TEST(test_low_inside_humidity);
  RUN_TEST(test_dew_point_below_hysteresis);
  RUN_TEST(test_dew_point_above_hysteresis);
  RUN_TEST(test_dew_point_in_between_from_low);
  RUN_TEST(test_dew_point_in_between_from_high);
  return UNITY_END();
}
