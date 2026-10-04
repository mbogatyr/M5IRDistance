#include <unity.h>

#include "IrSweep.h"

void setUp(void) {}
void tearDown(void) {}

// A sweep where levels `from` and up were heard in all 8 bursts.
static void step(uint8_t hits[IrSweep::kLevels], int from) {
    for (int i = 0; i < IrSweep::kLevels; ++i) {
        hits[i] = i >= from ? 8 : 0;
    }
}

void test_duty_rises_from_the_weakest_to_half(void) {
    TEST_ASSERT_EQUAL_UINT32(1, IrSweep::duty(0));
    TEST_ASSERT_EQUAL_UINT32(IrSweep::kFullScale / 2, IrSweep::duty(IrSweep::kLevels - 1));
    for (int i = 1; i < IrSweep::kLevels; ++i) {
        TEST_ASSERT_GREATER_THAN_UINT32(IrSweep::duty(i - 1), IrSweep::duty(i));
    }
}

void test_duty_clamps_out_of_range_levels(void) {
    TEST_ASSERT_EQUAL_UINT32(IrSweep::duty(0), IrSweep::duty(-3));
    TEST_ASSERT_EQUAL_UINT32(IrSweep::duty(IrSweep::kLevels - 1), IrSweep::duty(IrSweep::kLevels));
}

// Full power is d = 1/2, sin(pi/2) = 1: 0 dB. The weakest is d = 1/1024:
// 20 lg sin(pi/1024) = 20 lg 0.0030680 = -50.26 dB.
void test_level_db_spans_50_db(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, IrSweep::levelDb(IrSweep::kLevels - 1));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -50.26f, IrSweep::levelDb(0));
}

// Levels 12..23 heard: the point is half-way between level 11 (duty 20,
// 20 lg sin(pi 20/1024) = -24.25 dB) and level 12 (duty 26, -21.97 dB):
// -23.11 dB, a margin of 23.11 dB.
void test_a_clean_step_lands_between_two_levels(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, 12);
    const IrSweep::Result r = IrSweep::estimate(hits, 8);
    TEST_ASSERT_TRUE(r.state == IrSweep::State::Ok);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 23.11f, r.marginDb);
}

// Level 12 heard in 4 of 8: sum 11.5, x = 24 - 11.5 - 0.5 = 12, exactly
// level 12: 21.97 dB.
void test_a_half_heard_level_is_the_point(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, 12);
    hits[12] = 4;
    const IrSweep::Result r = IrSweep::estimate(hits, 8);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 21.97f, r.marginDb);
}

// One stray hit at level 3 adds 1/8 to the sum: x = 11.375, so the point
// moves 3/8 of the way from level 11 to 12: -24.25 + 2.28 * 0.375 = -23.39.
// The margin moves by 0.28 dB, not to level 3's 38 dB.
void test_a_stray_hit_moves_the_point_only_a_little(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, 12);
    hits[3] = 1;
    const IrSweep::Result r = IrSweep::estimate(hits, 8);
    TEST_ASSERT_TRUE(r.state == IrSweep::State::Ok);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 23.39f, r.marginDb);
}

void test_nothing_heard_at_full_power_is_no_echo(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, IrSweep::kLevels); // nothing heard
    hits[IrSweep::kLevels - 1] = 3;
    const IrSweep::Result r = IrSweep::estimate(hits, 8);
    TEST_ASSERT_TRUE(r.state == IrSweep::State::NoEcho);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.marginDb);
}

void test_heard_at_the_weakest_power_is_saturated(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, 0);
    const IrSweep::Result r = IrSweep::estimate(hits, 8);
    TEST_ASSERT_TRUE(r.state == IrSweep::State::Saturated);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.26f, r.marginDb);
}

// Heard only at full power: x = 24 - 1 - 0.5 = 22.5, half-way between
// level 22 (-0.62 dB) and 23 (0 dB).
void test_heard_only_at_full_power_is_a_small_margin(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, IrSweep::kLevels - 1);
    const IrSweep::Result r = IrSweep::estimate(hits, 8);
    TEST_ASSERT_TRUE(r.state == IrSweep::State::Ok);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.31f, r.marginDb);
}

void test_hits_above_bursts_count_as_all(void) {
    static uint8_t a[IrSweep::kLevels];
    static uint8_t b[IrSweep::kLevels];
    step(a, 12);
    step(b, 12);
    b[20] = 200;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, IrSweep::estimate(a, 8).marginDb,
                             IrSweep::estimate(b, 8).marginDb);
}

void test_no_bursts_is_no_echo(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, 0);
    TEST_ASSERT_TRUE(IrSweep::estimate(hits, 0).state == IrSweep::State::NoEcho);
}

// The hits of a clean step, but a blank burst was heard as well.
void test_a_false_hit_makes_the_sweep_noisy(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, 12);
    const IrSweep::Result r = IrSweep::estimate(hits, 8, 1);
    TEST_ASSERT_TRUE(r.state == IrSweep::State::Noisy);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 23.11f, r.marginDb);
}

void test_noise_overrides_no_echo_too(void) {
    static uint8_t hits[IrSweep::kLevels];
    step(hits, IrSweep::kLevels);
    TEST_ASSERT_TRUE(IrSweep::estimate(hits, 8, 2).state == IrSweep::State::Noisy);
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_duty_rises_from_the_weakest_to_half);
    RUN_TEST(test_duty_clamps_out_of_range_levels);
    RUN_TEST(test_level_db_spans_50_db);
    RUN_TEST(test_a_clean_step_lands_between_two_levels);
    RUN_TEST(test_a_half_heard_level_is_the_point);
    RUN_TEST(test_a_stray_hit_moves_the_point_only_a_little);
    RUN_TEST(test_nothing_heard_at_full_power_is_no_echo);
    RUN_TEST(test_heard_at_the_weakest_power_is_saturated);
    RUN_TEST(test_heard_only_at_full_power_is_a_small_margin);
    RUN_TEST(test_hits_above_bursts_count_as_all);
    RUN_TEST(test_no_bursts_is_no_echo);
    RUN_TEST(test_a_false_hit_makes_the_sweep_noisy);
    RUN_TEST(test_noise_overrides_no_echo_too);

    return UNITY_END();
}
