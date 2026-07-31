// Ported from Go `utility/math_test.go` / MicroPython tests/test_math.py.
#include <unity.h>

#include <cmath>

#include "dp_math.h"

using dpf::calcDewPoint;
using dpf::roundDouble;

void setUp() {}
void tearDown() {}

static void assertNear(double expected, double actual) {
  TEST_ASSERT_TRUE(std::fabs(actual - expected) <= 1e-6);
}

void test_calc_dew_point_cases() {
  assertNear(21.3, calcDewPoint(25.0, 80.0));
  assertNear(-6.4, calcDewPoint(-5.0, 90.0));
  assertNear(-2.2, calcDewPoint(0.0, 85.0));
  assertNear(10.5, calcDewPoint(30.0, 30.0));
  assertNear(-28.7, calcDewPoint(-10.0, 20.0));
  assertNear(36.7, calcDewPoint(50.0, 50.0));
  assertNear(-46.4, calcDewPoint(-40.0, 50.0));
  assertNear(20.0, calcDewPoint(20.0, 100.0));
}

void test_zero_humidity_is_negative_infinity() {
  double a = calcDewPoint(0.0, 0.0);
  double b = calcDewPoint(-10.0, 0.0);
  TEST_ASSERT_TRUE(std::isinf(a) && a < 0);
  TEST_ASSERT_TRUE(std::isinf(b) && b < 0);
}

void test_round_double_cases() {
  assertNear(123.0, roundDouble(123.456, 0));
  assertNear(123.5, roundDouble(123.456, 1));
  assertNear(123.46, roundDouble(123.456, 2));
  assertNear(100.0, roundDouble(100.0, 2));
  assertNear(-123.46, roundDouble(-123.456, 2));
  assertNear(-124.0, roundDouble(-123.756, 0));
  assertNear(0.0, roundDouble(0.0049, 2));
  assertNear(0.01, roundDouble(0.0051, 2));
  assertNear(98765432.123, roundDouble(98765432.123456, 3));
}

void test_round_half_away_from_zero() {
  assertNear(1.0, roundDouble(0.5, 0));
  assertNear(2.0, roundDouble(1.5, 0));
  assertNear(3.0, roundDouble(2.5, 0));
  assertNear(-1.0, roundDouble(-0.5, 0));
  assertNear(-2.0, roundDouble(-1.5, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_calc_dew_point_cases);
  RUN_TEST(test_zero_humidity_is_negative_infinity);
  RUN_TEST(test_round_double_cases);
  RUN_TEST(test_round_half_away_from_zero);
  return UNITY_END();
}
