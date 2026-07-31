// Host tests for the pure Shelly HTTP-RPC helpers: the Switch.Set request path
// and the edge/backoff send decision.
#include <unity.h>

#include "shelly_rpc.h"

using namespace dpf;

void setUp() {}
void tearDown() {}

void test_switch_set_path_on() {
  TEST_ASSERT_EQUAL_STRING("/rpc/Switch.Set?id=0&on=true",
                           switchSetPath(0, true).c_str());
}

void test_switch_set_path_off() {
  TEST_ASSERT_EQUAL_STRING("/rpc/Switch.Set?id=1&on=false",
                           switchSetPath(1, false).c_str());
}

void test_should_send_already_at_state() {
  // Plug already at the desired state -> never send, regardless of target/time.
  TEST_ASSERT_FALSE(
      shouldSendRpc(true, TriState::On, TriState::On, 100.0, 0.0, 10.0, 0, 10));
  TEST_ASSERT_FALSE(
      shouldSendRpc(false, TriState::Off, TriState::Off, 100.0, 0.0, 10.0, 0, 10));
}

void test_should_send_changed_target_acts_immediately() {
  // Desired just changed (differs from lastTarget) -> send now even though the
  // previous attempt was 1 s ago and the interval is 15 s. This is the button-
  // press / dew-point-flip case that must not wait out the backoff.
  TEST_ASSERT_TRUE(shouldSendRpc(false, TriState::On, TriState::On, 101.0, 100.0,
                                 15.0, 0, 10));
  TEST_ASSERT_TRUE(shouldSendRpc(true, TriState::Off, TriState::Off, 101.0, 100.0,
                                 15.0, 0, 10));
}

void test_should_send_retry_same_target_respects_backoff() {
  // Same target as last attempt but still unconfirmed (previous send failed):
  // within the interval -> wait; after it -> retry.
  TEST_ASSERT_FALSE(shouldSendRpc(true, TriState::Off, TriState::On, 105.0, 100.0,
                                  10.0, 1, 10));
  TEST_ASSERT_TRUE(shouldSendRpc(true, TriState::Off, TriState::On, 111.0, 100.0,
                                 10.0, 1, 10));
}

void test_should_send_first_time_unknown_target() {
  // No previous send (lastTarget Unknown) -> treated as a change -> send now.
  TEST_ASSERT_TRUE(shouldSendRpc(false, TriState::Unknown, TriState::Unknown, 1.0,
                                 0.0, 15.0, 0, 10));
}

void test_should_send_retries_until_max_then_gives_up() {
  // Same target, past the interval, but the failure count has not yet reached
  // maxAttempts -> keep retrying.
  TEST_ASSERT_TRUE(shouldSendRpc(false, TriState::Unknown, TriState::Off, 111.0,
                                 100.0, 10.0, 9, 10));
  // Once consecutiveFails >= maxAttempts, stop retrying the same target.
  TEST_ASSERT_FALSE(shouldSendRpc(false, TriState::Unknown, TriState::Off, 200.0,
                                  100.0, 10.0, 10, 10));
  TEST_ASSERT_FALSE(shouldSendRpc(false, TriState::Unknown, TriState::Off, 200.0,
                                  100.0, 10.0, 11, 10));
}

void test_should_send_state_change_resets_fail_count() {
  // Even if we had given up (failures >= max) on the OFF target, a flip to ON
  // (a genuine state change, lastTarget differs) must send immediately.
  TEST_ASSERT_TRUE(shouldSendRpc(true, TriState::Off, TriState::Off, 200.0, 100.0,
                                 10.0, 10, 10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_switch_set_path_on);
  RUN_TEST(test_switch_set_path_off);
  RUN_TEST(test_should_send_already_at_state);
  RUN_TEST(test_should_send_changed_target_acts_immediately);
  RUN_TEST(test_should_send_retry_same_target_respects_backoff);
  RUN_TEST(test_should_send_first_time_unknown_target);
  RUN_TEST(test_should_send_retries_until_max_then_gives_up);
  RUN_TEST(test_should_send_state_change_resets_fail_count);
  return UNITY_END();
}
