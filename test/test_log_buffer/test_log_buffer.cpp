// Host tests for the pure LogBuffer ring: order, capacity, and oldest-dropped
// behaviour that backs the /logs endpoint.
#include <unity.h>

#include "log_buffer.h"

using namespace dpf;

void setUp() {}
void tearDown() {}

void test_empty_has_no_lines() {
  LogBuffer buf;
  TEST_ASSERT_EQUAL_UINT(0, buf.size());
}

void test_under_capacity_keeps_order() {
  LogBuffer buf;
  buf.push("first");
  buf.push("second");
  buf.push("third");
  TEST_ASSERT_EQUAL_UINT(3, buf.size());
  TEST_ASSERT_EQUAL_STRING("first", buf.lineAt(0));
  TEST_ASSERT_EQUAL_STRING("second", buf.lineAt(1));
  TEST_ASSERT_EQUAL_STRING("third", buf.lineAt(2));
}

void test_exactly_at_capacity() {
  LogBuffer buf(15);
  for (int i = 0; i < 15; ++i) buf.push(std::to_string(i));
  TEST_ASSERT_EQUAL_UINT(15, buf.size());
  TEST_ASSERT_EQUAL_STRING("0", buf.lineAt(0));
  TEST_ASSERT_EQUAL_STRING("14", buf.lineAt(14));
}

void test_over_capacity_drops_oldest() {
  LogBuffer buf(15);
  for (int i = 0; i < 20; ++i) buf.push(std::to_string(i));
  // Only the last 15 (5..19) survive, oldest first.
  TEST_ASSERT_EQUAL_UINT(15, buf.size());
  TEST_ASSERT_EQUAL_STRING("5", buf.lineAt(0));
  TEST_ASSERT_EQUAL_STRING("19", buf.lineAt(14));
}

void test_small_custom_capacity() {
  LogBuffer buf(3);
  buf.push("a");
  buf.push("b");
  buf.push("c");
  buf.push("d");
  TEST_ASSERT_EQUAL_UINT(3, buf.size());
  TEST_ASSERT_EQUAL_STRING("b", buf.lineAt(0));
  TEST_ASSERT_EQUAL_STRING("c", buf.lineAt(1));
  TEST_ASSERT_EQUAL_STRING("d", buf.lineAt(2));
}

void test_lineAt_out_of_range_returns_null() {
  LogBuffer buf(3);
  buf.push("x");
  TEST_ASSERT_NULL(buf.lineAt(1));
  TEST_ASSERT_NULL(buf.lineAt(100));
}

void test_stdstring_overload() {
  LogBuffer buf(2);
  std::string s1 = "hello";
  std::string s2 = "world";
  buf.push(s1);
  buf.push(s2);
  TEST_ASSERT_EQUAL_UINT(2, buf.size());
  TEST_ASSERT_EQUAL_STRING("hello", buf.lineAt(0));
  TEST_ASSERT_EQUAL_STRING("world", buf.lineAt(1));
}

void test_wrap_around_full_circle() {
  LogBuffer buf(3);
  // Fill, then push enough to wrap the ring multiple times.
  for (int i = 0; i < 30; ++i) buf.push(std::to_string(i));
  TEST_ASSERT_EQUAL_UINT(3, buf.size());
  TEST_ASSERT_EQUAL_STRING("27", buf.lineAt(0));
  TEST_ASSERT_EQUAL_STRING("28", buf.lineAt(1));
  TEST_ASSERT_EQUAL_STRING("29", buf.lineAt(2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_has_no_lines);
  RUN_TEST(test_under_capacity_keeps_order);
  RUN_TEST(test_exactly_at_capacity);
  RUN_TEST(test_over_capacity_drops_oldest);
  RUN_TEST(test_small_custom_capacity);
  RUN_TEST(test_lineAt_out_of_range_returns_null);
  RUN_TEST(test_stdstring_overload);
  RUN_TEST(test_wrap_around_full_circle);
  return UNITY_END();
}
