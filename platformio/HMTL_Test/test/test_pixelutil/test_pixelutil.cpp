/*
 * Unit tests for the PixelUtil stub — verifies that LED mutations are
 * reflected in both the internal state (via getPixel) and the debug log
 * (via debug_log_contains / debug_log_line).
 */

#include <unity.h>
#include "PixelUtil.h"
#include "Debug.h"

void setUp()    { debug_log_begin_test(Unity.CurrentTestName); }
void tearDown() {}

// ---------------------------------------------------------------------------
// setPixelRGB — individual pixel, three variants
// ---------------------------------------------------------------------------

void test_set_pixel_rgb_components_stored() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)2, (uint8_t)0xff, (uint8_t)0x80, (uint8_t)0x00);

    CRGB c = pixels.getPixel(2);
    TEST_ASSERT_EQUAL_HEX8(0xff, c.r);
    TEST_ASSERT_EQUAL_HEX8(0x80, c.g);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.b);
}

void test_set_pixel_rgb_components_log() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)2, (uint8_t)0xff, (uint8_t)0x80, (uint8_t)0x00);

    TEST_ASSERT_TRUE(debug_log_contains("pixel[2]=0xff8000"));
}

void test_set_pixel_rgb_uint32_stored() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)0, (uint32_t)0x0000ff);

    CRGB c = pixels.getPixel(0);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.r);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.g);
    TEST_ASSERT_EQUAL_HEX8(0xff, c.b);
}

void test_set_pixel_rgb_uint32_log() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)0, (uint32_t)0x0000ff);

    TEST_ASSERT_TRUE(debug_log_contains("pixel[0]=0x0000ff"));
}

void test_set_pixel_rgb_crgb_stored() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)3, CRGB(0x12, 0x34, 0x56));

    CRGB c = pixels.getPixel(3);
    TEST_ASSERT_EQUAL_HEX8(0x12, c.r);
    TEST_ASSERT_EQUAL_HEX8(0x34, c.g);
    TEST_ASSERT_EQUAL_HEX8(0x56, c.b);
}

void test_set_pixel_rgb_crgb_log() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)3, CRGB(0x12, 0x34, 0x56));

    TEST_ASSERT_TRUE(debug_log_contains("pixel[3]=0x123456"));
}

// ---------------------------------------------------------------------------
// setAllRGB
// ---------------------------------------------------------------------------

void test_set_all_rgb_stored() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setAllRGB((uint8_t)0x00, (uint8_t)0xff, (uint8_t)0x00);

    for (uint16_t i = 0; i < 4; i++) {
        CRGB c = pixels.getPixel(i);
        TEST_ASSERT_EQUAL_HEX8(0x00, c.r);
        TEST_ASSERT_EQUAL_HEX8(0xff, c.g);
        TEST_ASSERT_EQUAL_HEX8(0x00, c.b);
    }
}

void test_set_all_rgb_log() {
    PixelUtil pixels(4, 0, 0, 0);
    pixels.setAllRGB((uint8_t)0x00, (uint8_t)0xff, (uint8_t)0x00);

    TEST_ASSERT_TRUE(debug_log_contains("setAllRGB 0x00ff00"));
}

// ---------------------------------------------------------------------------
// setRangeRGB
// ---------------------------------------------------------------------------

void test_set_range_rgb_stored() {
    PixelUtil pixels(8, 0, 0, 0);
    pixel_range_t range = {2, 3};  // pixels 2, 3, 4
    pixels.setRangeRGB(range, CRGB(0xff, 0x00, 0x00));

    // pixels inside the range are set
    for (uint16_t i = 2; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(i).r);
        TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(i).g);
    }
    // pixels outside the range are untouched (still 0)
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(0).r);
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(5).r);
}

void test_set_range_rgb_log() {
    PixelUtil pixels(8, 0, 0, 0);
    pixel_range_t range = {2, 3};
    pixels.setRangeRGB(range, CRGB(0xff, 0x00, 0x00));

    TEST_ASSERT_TRUE(debug_log_contains("setRangeRGB [2+3] 0xff0000"));
}

// ---------------------------------------------------------------------------
// update — dumps all pixels as a hex array on one line
// ---------------------------------------------------------------------------

void test_update_emits_pixels_line() {
    PixelUtil pixels(3, 0, 0, 0);
    pixels.setAllRGB((uint8_t)0x10, (uint8_t)0x20, (uint8_t)0x30);
    debug_log_reset();  // clear the setAllRGB entry; only test update output

    pixels.update();

    TEST_ASSERT_TRUE(debug_log_contains("pixels:"));
}

void test_update_hex_values_present() {
    PixelUtil pixels(3, 0, 0, 0);
    pixels.setAllRGB((uint8_t)0x10, (uint8_t)0x20, (uint8_t)0x30);
    debug_log_reset();

    pixels.update();

    // Each of the 3 pixels should appear as 0x102030
    const char *line = debug_log_line(0);
    TEST_ASSERT_NOT_NULL(line);

    // Count occurrences of the colour — should appear exactly 3 times
    const char *p = line;
    int count = 0;
    while ((p = strstr(p, "0x102030")) != NULL) { count++; p++; }
    TEST_ASSERT_EQUAL(3, count);
}

void test_update_mixed_pixels() {
    PixelUtil pixels(3, 0, 0, 0);
    pixels.setPixelRGB((uint16_t)0, (uint8_t)0xff, (uint8_t)0x00, (uint8_t)0x00);
    pixels.setPixelRGB((uint16_t)1, (uint8_t)0x00, (uint8_t)0xff, (uint8_t)0x00);
    pixels.setPixelRGB((uint16_t)2, (uint8_t)0x00, (uint8_t)0x00, (uint8_t)0xff);
    debug_log_reset();

    pixels.update();

    TEST_ASSERT_TRUE(debug_log_contains("0xff0000"));
    TEST_ASSERT_TRUE(debug_log_contains("0x00ff00"));
    TEST_ASSERT_TRUE(debug_log_contains("0x0000ff"));
}

// ---------------------------------------------------------------------------
// Unity runner
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_set_pixel_rgb_components_stored);
    RUN_TEST(test_set_pixel_rgb_components_log);
    RUN_TEST(test_set_pixel_rgb_uint32_stored);
    RUN_TEST(test_set_pixel_rgb_uint32_log);
    RUN_TEST(test_set_pixel_rgb_crgb_stored);
    RUN_TEST(test_set_pixel_rgb_crgb_log);

    RUN_TEST(test_set_all_rgb_stored);
    RUN_TEST(test_set_all_rgb_log);

    RUN_TEST(test_set_range_rgb_stored);
    RUN_TEST(test_set_range_rgb_log);

    RUN_TEST(test_update_emits_pixels_line);
    RUN_TEST(test_update_hex_values_present);
    RUN_TEST(test_update_mixed_pixels);

    return UNITY_END();
}
