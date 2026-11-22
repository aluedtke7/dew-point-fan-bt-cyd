// Host tests for the TouchGesture classifier: genuine taps / long presses are
// recognised, the phantom patterns seen in the field log are rejected.
#include <unity.h>

#include <vector>

#include "touch_gesture.h"

using namespace dpf;

void setUp() {}
void tearDown() {}

namespace {
constexpr double kPoll = 0.05;  // 50 ms loop poll

TouchSample at(int x, int y, int z = 900) { return {true, x, y, z}; }
const TouchSample kUp{};

// Poll clock: tick n is taken at n * kPoll seconds.
struct Clock {
  int tick = 0;
  double now() const { return tick * kPoll; }
};

TouchEvent step(TouchGesture& g, const TouchSample& s, Clock& c) {
  TouchEvent e = g.update(s, c.now());
  ++c.tick;
  return e;
}

// Feeds `n` polls of `s`; returns every non-None event.
std::vector<TouchEvent> feed(TouchGesture& g, const TouchSample& s, int n,
                             Clock& c) {
  std::vector<TouchEvent> ev;
  for (int i = 0; i < n; ++i) {
    TouchEvent e = step(g, s, c);
    if (e != TouchEvent::None) ev.push_back(e);
  }
  return ev;
}

// Release takes kMaxDropouts + 1 implausible polls; returns the last event.
TouchEvent release(TouchGesture& g, Clock& c) {
  TouchEvent e = TouchEvent::None;
  for (int i = 0; i <= kMaxDropouts; ++i) e = step(g, kUp, c);
  return e;
}
}  // namespace

void test_plausible_sample() {
  TEST_ASSERT_TRUE(isPlausible(at(3000, 3500)));
  TEST_ASSERT_FALSE(isPlausible(kUp));
  TEST_ASSERT_FALSE(isPlausible(at(3000, 3500, 599)));  // too soft
}

void test_saturated_samples_are_not_plausible() {
  // Phantoms from the log: raw=(4049,4062) z=1111, raw=(3886,3960) z=684
  TEST_ASSERT_FALSE(isPlausible(at(4049, 4062, 1111)));
  TEST_ASSERT_FALSE(isPlausible(at(3886, 3960, 684)));
  TEST_ASSERT_FALSE(isPlausible(at(100, 2000)));
}

void test_short_tap_then_cancel_on_release() {
  TouchGesture g;
  Clock c;
  auto ev = feed(g, at(3000, 3500), 4, c);  // 200 ms
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(TouchEvent::Tap, ev[0]);
  TEST_ASSERT_EQUAL(TouchEvent::Cancel, release(g, c));
  TEST_ASSERT_EQUAL(3000, g.rawX());
  TEST_ASSERT_EQUAL(3500, g.rawY());
}

void test_tap_needs_three_polls() {
  TouchGesture g;
  Clock c;
  TEST_ASSERT_EQUAL(0, feed(g, at(3000, 3500), 2, c).size());
  TEST_ASSERT_EQUAL(TouchEvent::None, release(g, c));  // never tapped
}

void test_long_press_fires_once() {
  TouchGesture g;
  Clock c;
  auto ev = feed(g, at(3000, 3500), 30, c);  // 1.5 s held
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(TouchEvent::Tap, ev[0]);
  TEST_ASSERT_EQUAL(TouchEvent::LongPress, ev[1]);
  TEST_ASSERT_EQUAL(TouchEvent::None, release(g, c));  // no cancel after
}

void test_long_press_timing() {
  TouchGesture g;
  Clock c;
  feed(g, at(3000, 3500), 16, c);  // samples at 0..750 ms
  TEST_ASSERT_EQUAL(TouchEvent::LongPress, step(g, at(3000, 3500), c));  // 800
}

void test_small_jitter_is_tolerated() {
  TouchGesture g;
  Clock c;
  std::vector<TouchEvent> ev;
  for (int i = 0; i < 20; ++i) {
    const int d = (i % 2) ? kMaxDrift : 0;  // within tolerance of the start
    TouchEvent e = step(g, at(3000 + d, 3500 - d), c);
    if (e != TouchEvent::None) ev.push_back(e);
  }
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(TouchEvent::LongPress, ev[1]);
}

void test_landing_finger_settles() {
  // First contact is off by 180 raw, then the finger settles: still a press.
  TouchGesture g;
  Clock c;
  step(g, at(3000, 3500), c);
  auto ev = feed(g, at(3000, 3680), 20, c);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(TouchEvent::Tap, ev[0]);
  TEST_ASSERT_EQUAL(TouchEvent::LongPress, ev[1]);
  TEST_ASSERT_EQUAL(3680, g.rawY());  // reports the settled position
}

void test_drift_cancels_and_blocks_long_press() {
  TouchGesture g;
  Clock c;
  feed(g, at(3000, 3500), 4, c);  // Tap sent
  TEST_ASSERT_EQUAL(TouchEvent::Cancel, step(g, at(2500, 3500), c));
  TEST_ASSERT_EQUAL(0, feed(g, at(2500, 3500), 30, c).size());
  TEST_ASSERT_EQUAL(TouchEvent::None, release(g, c));  // cancel only once
}

void test_single_dropout_keeps_press() {
  // One low-pressure sample mid-hold must not restart the long press.
  TouchGesture g;
  Clock c;
  feed(g, at(3000, 3500), 10, c);
  TEST_ASSERT_EQUAL(TouchEvent::None, step(g, at(3000, 3500, 580), c));
  auto ev = feed(g, at(3000, 3500), 7, c);  // 900 ms since start
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(TouchEvent::LongPress, ev[0]);
}

void test_invalid_samples_end_press() {
  TouchGesture g;
  Clock c;
  feed(g, at(3000, 3500), 10, c);
  step(g, at(4080, 4070, 1400), c);
  TEST_ASSERT_EQUAL(TouchEvent::Cancel, step(g, at(4080, 4070, 1400), c));
  // A new press restarts the clock: no LongPress before another 800 ms.
  auto ev = feed(g, at(3000, 3500), 10, c);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(TouchEvent::Tap, ev[0]);
}

void test_stalled_loop_needs_samples() {
  // Two samples 1.5 s apart (blocked loop) must not count as a long press.
  TouchGesture g;
  TEST_ASSERT_EQUAL(TouchEvent::None, g.update(at(3433, 3582), 10.0));
  TEST_ASSERT_EQUAL(TouchEvent::None, g.update(at(3433, 3582), 11.5));
  TEST_ASSERT_EQUAL(TouchEvent::Tap, g.update(at(3433, 3582), 11.55));
  for (int i = 1; i <= 12; ++i) {
    TEST_ASSERT_EQUAL(TouchEvent::None, g.update(at(3433, 3582), 11.55 + i * kPoll));
  }
  TEST_ASSERT_EQUAL(TouchEvent::LongPress, g.update(at(3433, 3582), 12.25));
}

void test_saturated_burst_never_fires() {
  // 07:52:13 pattern: a sustained burst of saturated, firm readings.
  TouchGesture g;
  Clock c;
  const TouchSample burst[] = {at(4075, 4076, 1453), at(4071, 4076, 1482),
                               at(4091, 4074, 1602), at(4056, 3991, 1109)};
  for (int i = 0; i < 40; ++i) {
    TEST_ASSERT_EQUAL(TouchEvent::None, step(g, burst[i % 4], c));
  }
}

void test_jumping_noise_never_fires() {
  // In-range noise that wanders (e.g. raw (3433,3582) then (2398,2552)).
  TouchGesture g;
  Clock c;
  const TouchSample noise[] = {at(3433, 3582, 655), at(2398, 2552, 781),
                               at(2432, 2542, 881), at(2168, 2489, 760)};
  for (int i = 0; i < 40; ++i) {
    TEST_ASSERT_EQUAL(TouchEvent::None, step(g, noise[i % 4], c));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_plausible_sample);
  RUN_TEST(test_saturated_samples_are_not_plausible);
  RUN_TEST(test_short_tap_then_cancel_on_release);
  RUN_TEST(test_tap_needs_three_polls);
  RUN_TEST(test_long_press_fires_once);
  RUN_TEST(test_long_press_timing);
  RUN_TEST(test_small_jitter_is_tolerated);
  RUN_TEST(test_landing_finger_settles);
  RUN_TEST(test_drift_cancels_and_blocks_long_press);
  RUN_TEST(test_single_dropout_keeps_press);
  RUN_TEST(test_invalid_samples_end_press);
  RUN_TEST(test_stalled_loop_needs_samples);
  RUN_TEST(test_saturated_burst_never_fires);
  RUN_TEST(test_jumping_noise_never_fires);
  return UNITY_END();
}
