#include <unity.h>

#include "BatteryFilter.h"

void setUp(void) {}
void tearDown(void) {}

void test_first_reading_is_shown_as_is(void) {
    BatteryFilter f;
    TEST_ASSERT_EQUAL_UINT8(37, f.update(37));
}

// Seen on the board: 37 % before Wi-Fi, 31 % right after it came on.
// One low reading moves the average by a quarter of the gap: 37 - 1.5.
void test_a_single_sag_moves_the_value_only_a_little(void) {
    BatteryFilter f;
    f.update(37);
    TEST_ASSERT_EQUAL_UINT8(36, f.update(31)); // average 35.5 -> shown 36
    TEST_ASSERT_EQUAL_UINT8(36, f.update(37)); // average 35.9: within 0.75 of 36
}

void test_the_value_does_not_flicker_between_neighbours(void) {
    BatteryFilter f;
    f.update(50);
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_EQUAL_UINT8(50, f.update(i % 2 ? 51 : 50));
    }
}

void test_a_real_change_comes_through(void) {
    BatteryFilter f;
    f.update(80);
    uint8_t shown = 0;
    for (int i = 0; i < 20; ++i) {
        shown = f.update(70);
    }
    TEST_ASSERT_EQUAL_UINT8(70, shown);
}

void test_readings_above_100_are_clamped(void) {
    BatteryFilter f;
    TEST_ASSERT_EQUAL_UINT8(100, f.update(255));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_first_reading_is_shown_as_is);
    RUN_TEST(test_a_single_sag_moves_the_value_only_a_little);
    RUN_TEST(test_the_value_does_not_flicker_between_neighbours);
    RUN_TEST(test_a_real_change_comes_through);
    RUN_TEST(test_readings_above_100_are_clamped);

    return UNITY_END();
}
