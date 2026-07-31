// Ported from Go `bluetooth/scanner_test.go` / MicroPython tests/test_beacon.py.
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "beacon.h"
#include "sensordata.h"

using namespace dpf;

static const char* INSIDE_MAC = "9D:8B:00:00:18:BD";
static const char* OUTSIDE_MAC = "9D:F2:00:00:14:B5";

// 18-byte payload: 2 header, 6 MAC (LE/reversed), 2 battery, 2 temp,
// 2 humidity, 4 uptime. MAC reversed spells INSIDE_MAC. temp=400/16=25.0,
// humidity=1280/16=80.0, battery=3088, uptime=90060 (=1d 1h 1m).
static const uint8_t INSIDE_PAYLOAD[18] = {
    0x00, 0x00,                          // header
    0xBD, 0x18, 0x00, 0x00, 0x8B, 0x9D,  // MAC -> 9D:8B:00:00:18:BD
    0x10, 0x0C,                          // battery 3088
    0x90, 0x01,                          // temp 400 -> 25.0
    0x00, 0x05,                          // humidity 1280 -> 80.0
    0xCC, 0x5F, 0x01, 0x00,              // uptime 90060
};

void setUp() {}
void tearDown() {}

static Sensors makeSensors(SensorCalibration insideCal = {},
                           SensorCalibration outsideCal = {}) {
  Sensors s;
  std::strncpy(s.insideData.macAddress, INSIDE_MAC, 18);
  std::strncpy(s.outsideData.macAddress, OUTSIDE_MAC, 18);
  s.insideCalibration = insideCal;
  s.outsideCalibration = outsideCal;
  return s;
}

static void assertNear(double expected, double actual) {
  TEST_ASSERT_TRUE(std::fabs(actual - expected) <= 1e-6);
}

// Build an inside-sensor payload with the given raw 16-bit little-endian temp
// and humidity counts, leaving MAC/battery/uptime as in INSIDE_PAYLOAD. Lets
// each conversion test state the raw ADC counts explicitly and read back the
// converted physical value.
static std::vector<uint8_t> payloadWith(uint16_t tempRaw, uint16_t humRaw) {
  std::vector<uint8_t> p(INSIDE_PAYLOAD, INSIDE_PAYLOAD + 18);
  p[10] = static_cast<uint8_t>(tempRaw & 0xFF);
  p[11] = static_cast<uint8_t>(tempRaw >> 8);
  p[12] = static_cast<uint8_t>(humRaw & 0xFF);
  p[13] = static_cast<uint8_t>(humRaw >> 8);
  return p;
}

// Convert a raw temperature count via parseWS02 (inside sensor, no calibration).
static double convTemp(uint16_t tempRaw) {
  auto d = parseWS02(payloadWith(tempRaw, 1280).data(), 18, -55, makeSensors(),
                     0.0);
  TEST_ASSERT_TRUE(d.has_value());
  return d->temperature;
}

// Convert a raw humidity count via parseWS02 (inside sensor, no calibration).
static double convHum(uint16_t humRaw) {
  auto d = parseWS02(payloadWith(400, humRaw).data(), 18, -55, makeSensors(),
                     0.0);
  TEST_ASSERT_TRUE(d.has_value());
  return d->humidity;
}

void test_known_inside_sensor() {
  Sensors sensors = makeSensors();
  auto data = parseWS02(INSIDE_PAYLOAD, 18, -55, sensors, 123.0);
  TEST_ASSERT_TRUE(data.has_value());
  TEST_ASSERT_EQUAL_STRING(INSIDE_MAC, data->macAddress);
  TEST_ASSERT_EQUAL_STRING("Inside", data->name);
  TEST_ASSERT_EQUAL_UINT16(3088, data->batLevel);
  TEST_ASSERT_EQUAL_INT16(-55, data->rssi);
  TEST_ASSERT_EQUAL_UINT32(90060, data->uptime);
  assertNear(25.0, data->temperature);
  assertNear(80.0, data->humidity);
  assertNear(21.3, data->dewPoint);  // calcDewPoint(25, 80)
  assertNear(123.0, data->scanned);
  TEST_ASSERT_TRUE(data->hasReading);
}

void test_calibration_offsets_applied() {
  Sensors sensors = makeSensors({-1.5, -1.2});
  auto data = parseWS02(INSIDE_PAYLOAD, 18, -55, sensors, 0.0);
  TEST_ASSERT_TRUE(data.has_value());
  assertNear(23.5, data->temperature);  // 25.0 - 1.5
  assertNear(78.8, data->humidity);     // 80.0 - 1.2
}

void test_unknown_mac_has_empty_name() {
  Sensors sensors;
  std::strncpy(sensors.insideData.macAddress, "AA:BB:CC:DD:EE:FF", 18);
  std::strncpy(sensors.outsideData.macAddress, "11:22:33:44:55:66", 18);
  auto data = parseWS02(INSIDE_PAYLOAD, 18, -55, sensors, 0.0);
  TEST_ASSERT_TRUE(data.has_value());
  TEST_ASSERT_EQUAL_STRING("", data->name);
}

void test_negative_temperature_wraparound() {
  // temp_int 65136 -> 4071.0 > 4000 -> 4071 - 4096 = -25.0
  std::vector<uint8_t> payload(INSIDE_PAYLOAD, INSIDE_PAYLOAD + 18);
  payload[10] = 0x70;
  payload[11] = 0xFE;  // 0xFE70 = 65136
  auto data = parseWS02(payload.data(), payload.size(), -55, makeSensors(), 0.0);
  TEST_ASSERT_TRUE(data.has_value());
  assertNear(-25.0, data->temperature);
}

void test_wrong_length_returns_none() {
  TEST_ASSERT_FALSE(parseWS02(INSIDE_PAYLOAD, 17, -55, makeSensors(), 0.0).has_value());
  uint8_t longer[19] = {0};
  TEST_ASSERT_FALSE(parseWS02(longer, 19, -55, makeSensors(), 0.0).has_value());
}

void test_format_uptime() {
  TEST_ASSERT_EQUAL_STRING("1d 1h 1m", formatUptime(90060).c_str());
  TEST_ASSERT_EQUAL_STRING("0d 0h 0m", formatUptime(0).c_str());
  TEST_ASSERT_EQUAL_STRING("0d 1h 1m", formatUptime(3661).c_str());
}

// --- Raw ADC count -> physical value conversion ---------------------------
// The raw 16-bit little-endian count is divided by 16; if the *quotient*
// exceeds 4000.0 it wraps by subtracting 4096.0 (two's-complement-style sign
// applied after the /16), the per-sensor calibration offset is added, and the
// result is rounded to 1 decimal place (half away from zero). These tests pin
// that exact pipeline for both temperature and humidity.

void test_temp_conversion_zero() {
  assertNear(0.0, convTemp(0));  // 0/16 = 0.0
}

void test_temp_conversion_typical_positive() {
  assertNear(25.0, convTemp(400));  // 400/16 = 25.0
}

void test_temp_conversion_fractional_resolution() {
  assertNear(25.1, convTemp(401));  // 401/16 = 25.0625 -> 25.1
}

void test_temp_conversion_exact_half_step() {
  assertNear(25.5, convTemp(408));  // 408/16 = 25.5
}

void test_temp_conversion_no_wrap_boundary() {
  // 64000/16 = 4000.0 exactly; the wrap is `> 4000.0`, so this stays positive.
  assertNear(4000.0, convTemp(64000));
}

void test_temp_conversion_wrap_just_over_threshold() {
  // 64001/16 = 4000.0625 > 4000 -> 4000.0625 - 4096 = -95.9375 -> -95.9
  assertNear(-95.9, convTemp(64001));
}

void test_temp_conversion_wrap_typical_negative() {
  // 0xFE70 = 65136; 65136/16 = 4071.0 -> 4071 - 4096 = -25.0
  assertNear(-25.0, convTemp(0xFE70));
}

void test_temp_conversion_wrap_max_raw() {
  // 0xFFFF = 65535; 65535/16 = 4095.9375 -> -0.0625 -> -0.1
  assertNear(-0.1, convTemp(0xFFFF));
}

void test_humidity_conversion_zero() {
  assertNear(0.0, convHum(0));  // 0/16 = 0.0
}

void test_humidity_conversion_typical() {
  assertNear(80.0, convHum(1280));  // 1280/16 = 80.0
}

void test_humidity_conversion_full_scale() {
  assertNear(100.0, convHum(1600));  // 1600/16 = 100.0
}

void test_humidity_conversion_fractional_resolution() {
  assertNear(100.1, convHum(1601));  // 1601/16 = 100.0625 -> 100.1
}

void test_humidity_conversion_shares_wraparound_logic() {
  // Humidity uses the identical `> 4000 -> -4096` branch as temperature.
  assertNear(-25.0, convHum(0xFE70));  // 4071.0 -> -25.0
}

void test_conversion_calibration_applied_before_rounding() {
  // 399/16 = 24.9375; + 0.7 = 25.6375 -> 25.6. If calibration were applied
  // after rounding (24.9 + 0.7 = 25.6) the result would coincidentally match,
  // so also cover the boundary case below where the order matters.
  Sensors sensors = makeSensors({0.7, 0.0});
  auto d = parseWS02(payloadWith(399, 1280).data(), 18, -55, sensors, 0.0);
  TEST_ASSERT_TRUE(d.has_value());
  assertNear(25.6, d->temperature);
}

void test_conversion_calibration_crosses_round_boundary() {
  // 400/16 = 25.0; + (-0.05) = 24.95 -> 25.0 (half away from zero). Proves the
  // offset is added to the unrounded value before the single 1-decimal round.
  Sensors sensors = makeSensors({-0.05, 0.0});
  auto d = parseWS02(payloadWith(400, 1280).data(), 18, -55, sensors, 0.0);
  TEST_ASSERT_TRUE(d.has_value());
  assertNear(25.0, d->temperature);
}

void test_conversion_humidity_calibration() {
  // 1280/16 = 80.0; + 0.55 = 80.55 -> 80.6
  Sensors sensors = makeSensors({0.0, 0.55});
  auto d = parseWS02(payloadWith(400, 1280).data(), 18, -55, sensors, 0.0);
  TEST_ASSERT_TRUE(d.has_value());
  assertNear(80.6, d->humidity);
}

void test_conversion_outside_sensor_uses_outside_calibration() {
  // Build a payload whose MAC matches the OUTSIDE sensor, and give inside and
  // outside distinct calibrations. The outside offsets must be the ones applied.
  std::vector<uint8_t> p = payloadWith(400, 1280);
  // MAC bytes 2..7 are little-endian/reversed of OUTSIDE_MAC 9D:F2:00:00:14:B5.
  p[2] = 0xB5; p[3] = 0x14; p[4] = 0x00; p[5] = 0x00; p[6] = 0xF2; p[7] = 0x9D;
  Sensors sensors = makeSensors(/*inside=*/{-1.5, -1.2}, /*outside=*/{2.0, 3.0});
  auto d = parseWS02(p.data(), 18, -55, sensors, 0.0);
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_EQUAL_STRING("Outside", d->name);
  assertNear(27.0, d->temperature);  // 25.0 + 2.0 (outside), not - 1.5
  assertNear(83.0, d->humidity);     // 80.0 + 3.0 (outside), not - 1.2
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_inside_sensor);
  RUN_TEST(test_calibration_offsets_applied);
  RUN_TEST(test_unknown_mac_has_empty_name);
  RUN_TEST(test_negative_temperature_wraparound);
  RUN_TEST(test_wrong_length_returns_none);
  RUN_TEST(test_format_uptime);
  RUN_TEST(test_temp_conversion_zero);
  RUN_TEST(test_temp_conversion_typical_positive);
  RUN_TEST(test_temp_conversion_fractional_resolution);
  RUN_TEST(test_temp_conversion_exact_half_step);
  RUN_TEST(test_temp_conversion_no_wrap_boundary);
  RUN_TEST(test_temp_conversion_wrap_just_over_threshold);
  RUN_TEST(test_temp_conversion_wrap_typical_negative);
  RUN_TEST(test_temp_conversion_wrap_max_raw);
  RUN_TEST(test_humidity_conversion_zero);
  RUN_TEST(test_humidity_conversion_typical);
  RUN_TEST(test_humidity_conversion_full_scale);
  RUN_TEST(test_humidity_conversion_fractional_resolution);
  RUN_TEST(test_humidity_conversion_shares_wraparound_logic);
  RUN_TEST(test_conversion_calibration_applied_before_rounding);
  RUN_TEST(test_conversion_calibration_crosses_round_boundary);
  RUN_TEST(test_conversion_humidity_calibration);
  RUN_TEST(test_conversion_outside_sensor_uses_outside_calibration);
  return UNITY_END();
}
