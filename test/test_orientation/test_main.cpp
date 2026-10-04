#include <unity.h>

#include "Orientation.h"

// Acceleration in g, in board axes. Held upright in portrait, gravity runs
// along Y; in landscape, along X.
static const float kUpright[] = {0.0f, 1.0f, 0.0f};
static const float kUpsideDown[] = {0.0f, -1.0f, 0.0f};
static const float kFlat[] = {0.0f, 0.0f, 1.0f};
static const float kOnItsSide[] = {1.0f, 0.0f, 0.0f};

static const float kLandscapeUpright[] = {1.0f, 0.0f, 0.0f};
static const float kLandscapeUpsideDown[] = {-1.0f, 0.0f, 0.0f};

static constexpr uint32_t kSettle = Orientation::kSettleMs;

static bool feed(Orientation &o, uint32_t now, const float *a) {
    return o.update(now, a[0], a[1], a[2]);
}

// Already settled: held the normal way up, in portrait.
static Orientation settledUpright() {
    Orientation o;
    feed(o, 0, kUpright);
    return o;
}

void setUp(void) {}
void tearDown(void) {}

void test_is_not_flipped_before_any_reading(void) {
    Orientation o;
    TEST_ASSERT_FALSE(o.flipped());
}

void test_first_clear_reading_applies_at_once(void) {
    Orientation o;
    TEST_ASSERT_TRUE(feed(o, 0, kUpsideDown));
}

void test_flat_reading_does_not_decide_anything(void) {
    Orientation o;
    TEST_ASSERT_FALSE(feed(o, 0, kFlat));
    TEST_ASSERT_TRUE(feed(o, 20, kUpsideDown)); // first clear reading
}

void test_turning_over_flips_after_it_settles(void) {
    Orientation o = settledUpright();
    TEST_ASSERT_FALSE(feed(o, 1000, kUpsideDown));
    TEST_ASSERT_FALSE(feed(o, 1000 + kSettle - 1, kUpsideDown));
    TEST_ASSERT_TRUE(feed(o, 1000 + kSettle, kUpsideDown));
}

void test_turning_back_unflips(void) {
    Orientation o;
    feed(o, 0, kUpsideDown);
    feed(o, 1000, kUpright);
    TEST_ASSERT_FALSE(feed(o, 1000 + kSettle, kUpright));
}

void test_short_bump_does_not_flip(void) {
    Orientation o = settledUpright();
    feed(o, 1000, kUpsideDown);
    feed(o, 1200, kUpright); // turned back before the reading settled
    TEST_ASSERT_FALSE(feed(o, 1000 + kSettle, kUpsideDown));
    TEST_ASSERT_FALSE(feed(o, 1000 + kSettle + 100, kUpsideDown));
}

void test_lying_flat_keeps_the_orientation(void) {
    Orientation o;
    feed(o, 0, kUpsideDown);
    feed(o, 1000, kFlat);
    TEST_ASSERT_TRUE(feed(o, 1000 + kSettle * 10, kFlat));
}

void test_lying_on_its_side_keeps_the_orientation(void) {
    Orientation o;
    feed(o, 0, kUpsideDown);
    feed(o, 1000, kOnItsSide);
    TEST_ASSERT_TRUE(feed(o, 1000 + kSettle * 10, kOnItsSide));
}

void test_weak_tilt_is_not_enough(void) {
    Orientation o = settledUpright();
    const float tilted[] = {0.0f, -0.5f, 0.866f}; // 60° off vertical
    feed(o, 1000, tilted);
    TEST_ASSERT_FALSE(feed(o, 1000 + kSettle, tilted));
}

void test_shaking_is_ignored_and_restarts_the_wait(void) {
    Orientation o = settledUpright();
    const float shake[] = {0.0f, -2.0f, 0.0f};
    feed(o, 1000, kUpsideDown);
    feed(o, 1200, shake);
    TEST_ASSERT_FALSE(feed(o, 1000 + kSettle, kUpsideDown));
    TEST_ASSERT_TRUE(feed(o, 1000 + kSettle + kSettle, kUpsideDown));
}

void test_reset_lets_the_next_clear_reading_apply_at_once(void) {
    Orientation o = settledUpright();
    o.reset();
    TEST_ASSERT_FALSE(o.flipped()); // unchanged until a new reading
    TEST_ASSERT_TRUE(feed(o, 5000, kUpsideDown));
}

void test_settling_survives_the_millis_rollover(void) {
    Orientation o = settledUpright();
    const uint32_t start = 0xFFFFFF00u; // 256 ms before overflow
    feed(o, start, kUpsideDown);
    TEST_ASSERT_FALSE(feed(o, start + kSettle - 1, kUpsideDown));
    TEST_ASSERT_TRUE(feed(o, start + kSettle, kUpsideDown));
}

void test_landscape_reads_the_x_axis(void) {
    Orientation o(Orientation::Layout::Landscape);
    TEST_ASSERT_FALSE(feed(o, 0, kLandscapeUpright));
    feed(o, 1000, kLandscapeUpsideDown);
    TEST_ASSERT_TRUE(feed(o, 1000 + kSettle, kLandscapeUpsideDown));
}

void test_landscape_ignores_gravity_along_y(void) {
    Orientation o(Orientation::Layout::Landscape);
    feed(o, 0, kLandscapeUpsideDown);
    feed(o, 1000, kUpright);
    TEST_ASSERT_TRUE(feed(o, 1000 + kSettle * 10, kUpright));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_is_not_flipped_before_any_reading);
    RUN_TEST(test_first_clear_reading_applies_at_once);
    RUN_TEST(test_flat_reading_does_not_decide_anything);
    RUN_TEST(test_turning_over_flips_after_it_settles);
    RUN_TEST(test_turning_back_unflips);
    RUN_TEST(test_short_bump_does_not_flip);
    RUN_TEST(test_lying_flat_keeps_the_orientation);
    RUN_TEST(test_lying_on_its_side_keeps_the_orientation);
    RUN_TEST(test_weak_tilt_is_not_enough);
    RUN_TEST(test_shaking_is_ignored_and_restarts_the_wait);
    RUN_TEST(test_reset_lets_the_next_clear_reading_apply_at_once);
    RUN_TEST(test_settling_survives_the_millis_rollover);
    RUN_TEST(test_landscape_reads_the_x_axis);
    RUN_TEST(test_landscape_ignores_gravity_along_y);

    return UNITY_END();
}
