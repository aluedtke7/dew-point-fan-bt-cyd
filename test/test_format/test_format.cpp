// Host tests for the pure screen-string formatters. Byte-exact: the degree
// symbol is the backtick byte 0x60 (rendered as `°` by TFT_eSPI Font2).
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "format.h"

using namespace dpf;

void setUp() {}
void tearDown() {}

void test_measure_temperature_with_degree() {
  char buf[16];
  formatMeasure(buf, sizeof(buf), 26.0, "`C", true);
  TEST_ASSERT_EQUAL_STRING("26.0`C", buf);
}

void test_measure_dew_point_with_degree() {
  char buf[16];
  formatMeasure(buf, sizeof(buf), 12.5, "`C", true);
  TEST_ASSERT_EQUAL_STRING("12.5`C", buf);
}

void test_measure_humidity_percent() {
  char buf[16];
  formatMeasure(buf, sizeof(buf), 55.0, "%", true);
  TEST_ASSERT_EQUAL_STRING("55.0%", buf);
}

void test_measure_invalid_is_dashes() {
  char buf[16];
  formatMeasure(buf, sizeof(buf), 26.0, "`C", false);
  TEST_ASSERT_EQUAL_STRING("--", buf);
}

void test_measure_negative_rounding() {
  // %.1f rounds to one decimal; -0.04 -> "-0.0", -0.06 -> "-0.1".
  char buf[16];
  formatMeasure(buf, sizeof(buf), -0.04, "`C", true);
  TEST_ASSERT_EQUAL_STRING("-0.0`C", buf);
  formatMeasure(buf, sizeof(buf), -0.06, "`C", true);
  TEST_ASSERT_EQUAL_STRING("-0.1`C", buf);
}

void test_diff_valid() {
  char buf[48];
  formatDiff(buf, sizeof(buf), 3.0, 4.0, true);
  TEST_ASSERT_EQUAL_STRING("Diff 3.0`C  (thr 4.0)", buf);
}

void test_diff_invalid() {
  char buf[48];
  formatDiff(buf, sizeof(buf), 0.0, 4.0, false);
  TEST_ASSERT_EQUAL_STRING("Diff --  (thr 4.0)", buf);
}

void test_log_timestamp_unsynced_is_zeros() {
  // Before NTP sync (epoch below the ~2001 threshold) -> all zeros.
  char buf[24];
  formatLogTimestamp(buf, sizeof(buf), 0);
  TEST_ASSERT_EQUAL_STRING("00.00.00 00:00:00", buf);
  formatLogTimestamp(buf, sizeof(buf), 999999999);
  TEST_ASSERT_EQUAL_STRING("00.00.00 00:00:00", buf);
}

void test_log_timestamp_synced_boundary_not_zeros() {
  char buf[24];
  formatLogTimestamp(buf, sizeof(buf), 1000000000);
  TEST_ASSERT_TRUE(strcmp(buf, "00.00.00 00:00:00") != 0);
}

void test_log_timestamp_format() {
  // 1700000000 == 2023-11-14 22:13:20 UTC.
  setenv("TZ", "UTC", 1);
  tzset();
  char buf[24];
  formatLogTimestamp(buf, sizeof(buf), 1700000000);
  TEST_ASSERT_EQUAL_STRING("14.11.23 22:13:20", buf);
}

void test_heap_suffix() {
  char buf[48];
  formatHeapSuffix(buf, sizeof(buf), 152340, 101232, 65520);
  TEST_ASSERT_EQUAL_STRING("heap=152340 min=101232 blk=65520", buf);
}

void test_heap_suffix_zeros() {
  char buf[48];
  formatHeapSuffix(buf, sizeof(buf), 0, 0, 0);
  TEST_ASSERT_EQUAL_STRING("heap=0 min=0 blk=0", buf);
}

void test_net_line_online() {
  char buf[64];
  formatNetLine(buf, sizeof(buf), "192.168.178.42", "FRITZ!Box-Gast", true);
  TEST_ASSERT_EQUAL_STRING("IP 192.168.178.42   FRITZ!Box-Gast", buf);
}

void test_net_line_offline_drops_stale_address() {
  char buf[64];
  formatNetLine(buf, sizeof(buf), "192.168.178.42", "FRITZ!Box-Gast", false);
  TEST_ASSERT_EQUAL_STRING("IP -- (offline)", buf);
}

void test_net_line_online_without_ssid() {
  char buf[64];
  formatNetLine(buf, sizeof(buf), "10.0.0.7", "", true);
  TEST_ASSERT_EQUAL_STRING("IP 10.0.0.7", buf);
}

void test_net_line_online_without_ip_is_offline() {
  char buf[64];
  formatNetLine(buf, sizeof(buf), "", "FRITZ!Box-Gast", true);
  TEST_ASSERT_EQUAL_STRING("IP -- (offline)", buf);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_measure_temperature_with_degree);
  RUN_TEST(test_measure_dew_point_with_degree);
  RUN_TEST(test_measure_humidity_percent);
  RUN_TEST(test_measure_invalid_is_dashes);
  RUN_TEST(test_measure_negative_rounding);
  RUN_TEST(test_diff_valid);
  RUN_TEST(test_diff_invalid);
  RUN_TEST(test_log_timestamp_unsynced_is_zeros);
  RUN_TEST(test_log_timestamp_synced_boundary_not_zeros);
  RUN_TEST(test_log_timestamp_format);
  RUN_TEST(test_heap_suffix);
  RUN_TEST(test_heap_suffix_zeros);
  RUN_TEST(test_net_line_online);
  RUN_TEST(test_net_line_offline_drops_stale_address);
  RUN_TEST(test_net_line_online_without_ssid);
  RUN_TEST(test_net_line_online_without_ip_is_offline);
  return UNITY_END();
}
