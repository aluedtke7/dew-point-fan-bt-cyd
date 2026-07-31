// Ported from MicroPython tests/test_config.py (Go `readConfig` checks).
// parseConfig works on a RawConfig; on invalid input it sets ok=false + error
// instead of raising (exceptions are off on the ESP32 build).
#include <unity.h>

#include <cmath>

#include "config.h"

using namespace dpf;

void setUp() {}
void tearDown() {}

static RawConfig validRaw() {
  RawConfig r;
  r.insideMac = "9D:8B:00:00:18:BD";
  r.outsideMac = "9D:F2:00:00:14:B5";
  r.insideTempCal = -1.5;
  r.insideHumCal = -1.2;
  r.outsideTempCal = -0.74;
  r.outsideHumCal = -0.38;
  r.minDiff = 3.0;
  r.hysteresis = 1.0;
  r.minHumidityInside = 30.0;
  r.minTempInside = 10.0;
  r.minTempOutside = -10.0;
  r.brightness = 80;
  r.dimLevel = 10;
  r.dimTimeout = 30;
  r.influxEnabled = false;
  r.influxUrl = "http://x:8086";
  r.influxToken = "t";
  r.influxOrg = "o";
  r.influxBucket = "b";
  r.wifiNetworks = {{"net", "pw"}};
  r.otaPassword = "otapw";
  return r;
}

static void assertNear(double expected, double actual) {
  TEST_ASSERT_TRUE(std::fabs(actual - expected) <= 1e-9);
}

void test_valid_config() {
  Config cfg = parseConfig(validRaw());
  TEST_ASSERT_TRUE(cfg.ok);
  TEST_ASSERT_EQUAL_STRING("9D:8B:00:00:18:BD",
                           cfg.sensors.insideData.macAddress);
  assertNear(-1.5, cfg.sensors.insideCalibration.temperature);
  assertNear(3.0, cfg.fanConfig.minDiff);
  assertNear(1.0, cfg.fanConfig.hysteresis);
  TEST_ASSERT_EQUAL_INT(80, cfg.brightness);
  TEST_ASSERT_EQUAL_INT(10, cfg.dimLevel);
  TEST_ASSERT_EQUAL_INT(30, cfg.dimTimeout);
  TEST_ASSERT_EQUAL(1, cfg.wifi.networks.size());
  TEST_ASSERT_EQUAL_STRING("net", cfg.wifi.networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("otapw", cfg.otaPassword.c_str());
  TEST_ASSERT_FALSE(cfg.influx.enabled);
}

void test_timezone_propagated() {
  RawConfig r = validRaw();
  r.timezone = "Europe/Berlin";
  Config cfg = parseConfig(r);
  TEST_ASSERT_TRUE(cfg.ok);
  TEST_ASSERT_EQUAL_STRING("Europe/Berlin", cfg.timezone.c_str());
}

void test_timezone_default_empty() {
  Config cfg = parseConfig(validRaw());  // timezone left default (empty)
  TEST_ASSERT_TRUE(cfg.ok);
  TEST_ASSERT_EQUAL_STRING("", cfg.timezone.c_str());
}

void test_brightness_out_of_range() {
  RawConfig r = validRaw();
  r.brightness = 150;  // above 100
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_dim_timeout_out_of_range() {
  RawConfig r = validRaw();
  r.dimTimeout = 99999;  // above 3600
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_invalid_mac_length() {
  RawConfig r = validRaw();
  r.insideMac = "TOO_SHORT";
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_min_diff_out_of_range() {
  RawConfig r = validRaw();
  r.minDiff = 0.5;  // below 1
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_hysteresis_out_of_range() {
  RawConfig r = validRaw();
  r.hysteresis = 6.0;  // above 5
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_min_temp_outside_boundaries() {
  RawConfig r = validRaw();
  r.minTempOutside = -20;  // inclusive lower bound is valid
  TEST_ASSERT_TRUE(parseConfig(r).ok);
  r.minTempOutside = -21;  // just outside
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_shelly_disabled_allows_empty_host() {
  RawConfig r = validRaw();  // shellyEnabled defaults to false, host empty
  Config cfg = parseConfig(r);
  TEST_ASSERT_TRUE(cfg.ok);
  TEST_ASSERT_FALSE(cfg.shelly.enabled);
}

void test_shelly_enabled_valid() {
  RawConfig r = validRaw();
  r.shellyEnabled = true;
  r.shellyHost = "10.7.7.30";
  r.shellySwitchId = 0;
  Config cfg = parseConfig(r);
  TEST_ASSERT_TRUE(cfg.ok);
  TEST_ASSERT_TRUE(cfg.shelly.enabled);
  TEST_ASSERT_EQUAL_STRING("10.7.7.30", cfg.shelly.host.c_str());
  TEST_ASSERT_EQUAL_INT(0, cfg.shelly.switchId);
}

void test_shelly_enabled_needs_host() {
  RawConfig r = validRaw();
  r.shellyEnabled = true;
  r.shellyHost = "";  // enabled but no host
  TEST_ASSERT_FALSE(parseConfig(r).ok);
}

void test_multiple_wifi_config() {
  RawConfig r = validRaw();
  r.wifiNetworks.push_back({"net2", "pw2"});
  Config cfg = parseConfig(r);
  TEST_ASSERT_TRUE(cfg.ok);
  TEST_ASSERT_EQUAL(2, cfg.wifi.networks.size());
  TEST_ASSERT_EQUAL_STRING("net", cfg.wifi.networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("net2", cfg.wifi.networks[1].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("pw2", cfg.wifi.networks[1].password.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_config);
  RUN_TEST(test_invalid_mac_length);
  RUN_TEST(test_min_diff_out_of_range);
  RUN_TEST(test_hysteresis_out_of_range);
  RUN_TEST(test_brightness_out_of_range);
  RUN_TEST(test_dim_timeout_out_of_range);
  RUN_TEST(test_min_temp_outside_boundaries);
  RUN_TEST(test_shelly_disabled_allows_empty_host);
  RUN_TEST(test_shelly_enabled_valid);
  RUN_TEST(test_shelly_enabled_needs_host);
  RUN_TEST(test_multiple_wifi_config);
  RUN_TEST(test_timezone_propagated);
  RUN_TEST(test_timezone_default_empty);
  return UNITY_END();
}
