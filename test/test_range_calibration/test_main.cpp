#include <unity.h>

#include "RangeCalibration.h"

void setUp(void) {}
void tearDown(void) {}

// White paper on the board, 2026-10-04.
static void paper(RangeCalibration &cal) {
    cal.add(5, 38.4f);
    cal.add(10, 22.9f);
    cal.add(20, 13.9f);
    cal.add(40, 0.7f);
}

void test_fewer_than_two_points_give_nothing(void) {
    RangeCalibration cal;
    TEST_ASSERT_FALSE(cal.estimate(20).valid);
    cal.add(10, 22.9f);
    TEST_ASSERT_FALSE(cal.estimate(22.9f).valid);
}

void test_a_measured_point_comes_back(void) {
    RangeCalibration cal;
    paper(cal);
    const RangeCalibration::Estimate e = cal.estimate(22.9f);
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_FALSE(e.extrapolated);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, e.cm);
}

// 18.4 dB is half-way from 22.9 (10 cm) to 13.9 (20 cm); half-way in
// log(cm) is sqrt(10 * 20) = 14.14 cm, not 15.
void test_between_points_is_half_way_in_log_distance(void) {
    RangeCalibration cal;
    paper(cal);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.14f, cal.estimate(18.4f).cm);
}

// 7.3 dB is half-way from 13.9 (20 cm) to 0.7 (40 cm): sqrt(800) = 28.28.
void test_the_far_segment(void) {
    RangeCalibration cal;
    paper(cal);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.28f, cal.estimate(7.3f).cm);
}

// Beyond 5 cm along the 5..10 cm segment:
// lg cm = lg 5 + (45 - 38.4) * (lg 10 - lg 5) / (22.9 - 38.4) = 0.5708,
// 3.72 cm.
void test_nearer_than_the_first_point_is_extrapolated(void) {
    RangeCalibration cal;
    paper(cal);
    const RangeCalibration::Estimate e = cal.estimate(45);
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_TRUE(e.extrapolated);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.72f, e.cm);
}

// Beyond 40 cm along the 20..40 cm segment: 53.96 cm.
void test_farther_than_the_last_point_is_extrapolated(void) {
    RangeCalibration cal;
    paper(cal);
    const RangeCalibration::Estimate e = cal.estimate(-5);
    TEST_ASSERT_TRUE(e.extrapolated);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 53.96f, e.cm);
}

// Far outside, the estimate stops at half the nearest point and twice the
// farthest.
void test_extrapolation_is_clamped(void) {
    RangeCalibration cal;
    paper(cal);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, cal.estimate(200).cm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 80.0f, cal.estimate(-200).cm);
}

void test_points_are_kept_sorted_by_distance(void) {
    RangeCalibration cal;
    cal.add(20, 13.9f);
    cal.add(5, 38.4f);
    cal.add(10, 22.9f);
    TEST_ASSERT_EQUAL_INT(3, cal.count());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, cal.point(0).cm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, cal.point(1).cm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, cal.point(2).cm);
}

void test_the_same_distance_replaces_its_point(void) {
    RangeCalibration cal;
    paper(cal);
    TEST_ASSERT_TRUE(cal.add(10.1f, 25.0f));
    TEST_ASSERT_EQUAL_INT(4, cal.count());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.1f, cal.estimate(25.0f).cm);
}

void test_bad_distances_and_a_full_table_are_refused(void) {
    RangeCalibration cal;
    TEST_ASSERT_FALSE(cal.add(0, 10));
    TEST_ASSERT_FALSE(cal.add(-3, 10));
    for (int i = 0; i < RangeCalibration::kMaxPoints; ++i) {
        TEST_ASSERT_TRUE(cal.add(5.0f * (i + 1), 40.0f - 5 * i));
    }
    TEST_ASSERT_FALSE(cal.add(100, 0));
    TEST_ASSERT_EQUAL_INT(RangeCalibration::kMaxPoints, cal.count());
}

// A margin that grows with distance (a wrongly placed target) still
// interpolates, but cannot be extrapolated.
void test_a_rising_segment_is_not_extrapolated(void) {
    RangeCalibration cal;
    cal.add(5, 10);
    cal.add(10, 20);
    TEST_ASSERT_TRUE(cal.estimate(15).valid);
    TEST_ASSERT_FALSE(cal.estimate(25).valid);
}

void test_clear_forgets_the_points(void) {
    RangeCalibration cal;
    paper(cal);
    cal.clear();
    TEST_ASSERT_EQUAL_INT(0, cal.count());
    TEST_ASSERT_FALSE(cal.estimate(20).valid);
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_fewer_than_two_points_give_nothing);
    RUN_TEST(test_a_measured_point_comes_back);
    RUN_TEST(test_between_points_is_half_way_in_log_distance);
    RUN_TEST(test_the_far_segment);
    RUN_TEST(test_nearer_than_the_first_point_is_extrapolated);
    RUN_TEST(test_farther_than_the_last_point_is_extrapolated);
    RUN_TEST(test_extrapolation_is_clamped);
    RUN_TEST(test_points_are_kept_sorted_by_distance);
    RUN_TEST(test_the_same_distance_replaces_its_point);
    RUN_TEST(test_bad_distances_and_a_full_table_are_refused);
    RUN_TEST(test_a_rising_segment_is_not_extrapolated);
    RUN_TEST(test_clear_forgets_the_points);

    return UNITY_END();
}
