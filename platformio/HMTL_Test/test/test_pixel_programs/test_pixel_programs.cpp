/*
 * Integration tests for pixel-driving programs (fade, sparkle, circular).
 *
 * Each test creates a PixelUtil stub, installs a program via ProgramManager,
 * advances _mock_millis, and asserts both pixel state (getPixel) and the
 * debug log lines emitted by the PixelUtil stub.
 *
 * Notes on the CHSV stub:
 *   CRGB(CHSV(h, s, v)) = CRGB(v, v, v)  — simplified grey conversion.
 *   So any sparkle/circular color with val=255 becomes 0xffffff.
 */

#include <unity.h>
#include <cstring>
#include "HMTLTypes.h"
#include "HMTLPrograms.h"
#include "ProgramManager.h"
#include "PixelUtil.h"
#include "Debug.h"

extern unsigned long _mock_millis;

void setUp()    { _mock_millis = 0; debug_log_begin_test(Unity.CurrentTestName); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Minimal output header for a pixel output at position idx.
static output_hdr_t make_pixels_hdr(uint8_t idx) {
    output_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.type   = HMTL_OUTPUT_PIXELS;
    h.output = idx;
    return h;
}

// Send a formatted program buffer to the manager.
static bool send_program(ProgramManager &mgr, byte *buf) {
    msg_program_t *msg = (msg_program_t *)(buf + sizeof(msg_hdr_t));
    return mgr.handle_msg(msg);
}

// ---------------------------------------------------------------------------
// program_fade
// ---------------------------------------------------------------------------

static hmtl_program_t fade_fns[] = {
    { HMTL_PROGRAM_FADE, program_fade, program_fade_init },
};

// Note: program_fade uses start_time==0 as "uninitialised" sentinel.
// Starting the first run at t=1 (not 0) ensures start_time is set to a
// non-zero value so subsequent ticks enter the blend branch correctly.

void test_fade_sets_start_color_on_first_tick() {
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, fade_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    hmtl_program_fade_fmt(buf, sizeof(buf), /*addr*/1, /*output*/0,
                          /*period*/1000,
                          CRGB(0xff, 0x00, 0x00),   // start: red
                          CRGB(0x00, 0x00, 0xff),   // stop:  blue
                          /*flags*/0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 1;   // t=1: init tick, latches start_time=1 and sets start color
    mgr.run();

    TEST_ASSERT_TRUE(debug_log_contains("setAllRGB 0xff0000"));
    CRGB c = pixels.getPixel(0);
    TEST_ASSERT_EQUAL_HEX8(0xff, c.r);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.g);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.b);
}

void test_fade_reaches_stop_color_at_end_of_period() {
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, fade_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    hmtl_program_fade_fmt(buf, sizeof(buf), 1, 0, /*period*/1000,
                          CRGB(0x00, 0x00, 0x00),   // start: black
                          CRGB(0xff, 0xff, 0xff),   // stop:  white
                          0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 1;    mgr.run();   // init tick: latches start_time=1
    _mock_millis = 1001; mgr.run();   // elapsed == period → stop color

    TEST_ASSERT_TRUE(debug_log_contains("setAllRGB 0xffffff"));
    CRGB c = pixels.getPixel(0);
    TEST_ASSERT_EQUAL_HEX8(0xff, c.r);
    TEST_ASSERT_EQUAL_HEX8(0xff, c.g);
    TEST_ASSERT_EQUAL_HEX8(0xff, c.b);
}

void test_fade_is_mid_blend_at_half_period() {
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, fade_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    // Fade from black to g=200 over 1000 ms.
    // At elapsed=500 (fract8≈127), blend gives g≈99.
    hmtl_program_fade_fmt(buf, sizeof(buf), 1, 0, 1000,
                          CRGB(0x00, 0x00,  0x00),
                          CRGB(0x00, 200,   0x00),
                          0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 1;   mgr.run();   // init tick: latches start_time=1
    _mock_millis = 501; mgr.run();   // elapsed=500 → halfway

    // Green at halfway ≈ 99 (±10 for integer/fract8 rounding)
    CRGB c = pixels.getPixel(0);
    TEST_ASSERT_GREATER_THAN(85,  c.g);
    TEST_ASSERT_LESS_THAN(115, c.g);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.r);
    TEST_ASSERT_EQUAL_HEX8(0x00, c.b);
}

void test_fade_marks_done_after_period() {
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, fade_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    hmtl_program_fade_fmt(buf, sizeof(buf), 1, 0, 500,
                          CRGB(0xff, 0x00, 0x00),
                          CRGB(0x00, 0xff, 0x00),
                          0);   // no CYCLE flag → done after one pass
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 1;   mgr.run();   // init tick
    _mock_millis = 501; mgr.run();   // elapsed == period → done

    // "Fade done:" is emitted at DEBUG3 level (we build at DEBUG_LEVEL=5)
    TEST_ASSERT_TRUE(debug_log_contains("Fade done:"));
}

// ---------------------------------------------------------------------------
// program_sparkle
// ---------------------------------------------------------------------------

static hmtl_program_t sparkle_fns[] = {
    { HMTL_PROGRAM_SPARKLE, program_sparkle, program_sparkle_init },
};

void test_sparkle_no_change_before_period() {
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, sparkle_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sparkle_fmt(buf, sizeof(buf), 1, 0,
                        /*period*/100, /*bgColor*/CRGB(0,0,0),
                        /*sparkle_threshold*/100, /*bg_threshold*/0,
                        /*hue_min*/0,   /*hue_max*/255,
                        /*sat_min*/255, /*sat_max*/255,
                        /*val_min*/255, /*val_max*/255);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 0;
    mgr.run();   // t=0: last_change_ms=0, elapsed=0 < 100 → no pixels touched

    // No setPixelRGB calls should appear in the log
    TEST_ASSERT_FALSE(debug_log_contains("pixel["));
}

void test_sparkle_all_pixels_lit_after_period() {
    // With sparkle_threshold=100, random(100)∈[0,99] ≤ 100 always →
    // every pixel gets a sparkle colour.
    // With val_min=val_max=255 and our CHSV stub (r=g=b=v), colour=0xffffff.
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, sparkle_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sparkle_fmt(buf, sizeof(buf), 1, 0,
                        /*period*/100, CRGB(0,0,0),
                        /*sparkle_threshold*/100, /*bg_threshold*/0,
                        0, 255, 255, 255, 255, 255);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 0;   mgr.run();   // init tick, no change
    _mock_millis = 100; mgr.run();   // first sparkle tick

    // All 4 pixels should have been set to white
    for (uint16_t i = 0; i < 4; i++) {
        CRGB c = pixels.getPixel(i);
        TEST_ASSERT_EQUAL_HEX8(0xff, c.r);
        TEST_ASSERT_EQUAL_HEX8(0xff, c.g);
        TEST_ASSERT_EQUAL_HEX8(0xff, c.b);
    }
    TEST_ASSERT_TRUE(debug_log_contains("pixel[0]=0xffffff"));
    TEST_ASSERT_TRUE(debug_log_contains("pixel[3]=0xffffff"));
}

void test_sparkle_repeats_every_period() {
    PixelUtil pixels(4, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, sparkle_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sparkle_fmt(buf, sizeof(buf), 1, 0,
                        50, CRGB(0,0,0),
                        100, 0, 0, 255, 255, 255, 255, 255);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 0;   mgr.run();
    _mock_millis = 50;  mgr.run();   // first tick
    debug_log_reset();               // only inspect second tick's output
    _mock_millis = 100; mgr.run();   // second tick

    TEST_ASSERT_TRUE(debug_log_contains("pixel[0]=0xffffff"));
}

// ---------------------------------------------------------------------------
// program_circular
// ---------------------------------------------------------------------------

static hmtl_program_t circular_fns[] = {
    { HMTL_PROGRAM_CIRCULAR, program_circular, program_circular_init },
};

// With pattern=1, colour = CRGB(CHSV(position, 255, 255)).
// Our CHSV stub gives r=g=b=v=255, so every lit pixel is 0xffffff.
// With length=3, each tick lights 3 consecutive pixels and clears the old head.

void test_circular_no_change_before_period() {
    const int N = 8;
    PixelUtil pixels(N, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, circular_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_circular_fmt(buf, sizeof(buf), 1, 0,
                         /*period*/100, /*length*/3,
                         CRGB(0,0,0), /*pattern*/1, /*flags*/0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 0;
    mgr.run();   // t=0 == last_change_ms → no advance

    TEST_ASSERT_FALSE(debug_log_contains("pixel[0]=0x000000"));
}

void test_circular_first_tick_lights_segment() {
    const int N = 8;
    PixelUtil pixels(N, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, circular_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_circular_fmt(buf, sizeof(buf), 1, 0,
                         100, /*length*/3, CRGB(0,0,0), /*pattern*/1, 0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 100; mgr.run();   // first advance

    // Old head (pixel 0) is cleared, new segment starts at pixel 1
    TEST_ASSERT_TRUE(debug_log_contains("pixel[0]=0x000000"));
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(0).r);

    // Pixels 1, 2, 3 are lit white (CHSV→grey stub, val=255)
    for (int i = 1; i <= 3; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(i).r);
    }
}

void test_circular_segment_advances_each_period() {
    const int N = 8;
    PixelUtil pixels(N, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, circular_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_circular_fmt(buf, sizeof(buf), 1, 0,
                         100, 3, CRGB(0,0,0), 1, 0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    _mock_millis = 100; mgr.run();   // segment at 1,2,3
    _mock_millis = 200; mgr.run();   // segment at 2,3,4

    // Pixel 1 should have been cleared on the second tick
    TEST_ASSERT_TRUE(debug_log_contains("pixel[1]=0x000000"));
    // Pixels 2, 3, 4 should be lit
    for (int i = 2; i <= 4; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(i).r);
    }
}

void test_circular_segment_wraps_around() {
    const int N = 4;   // small strip so wrap happens quickly
    PixelUtil pixels(N, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t    *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void             *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, circular_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    // length=2 so wrap is visible sooner
    program_circular_fmt(buf, sizeof(buf), 1, 0,
                         100, 2, CRGB(0,0,0), 1, 0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    // Advance 4 ticks — current cycles through all 4 positions
    for (int t = 1; t <= 4; t++) {
        _mock_millis = t * 100;
        mgr.run();
    }

    // After 4 ticks on a 4-pixel strip, current == 0 again.
    // Pixel 0 should have been lit (log contains it as white at some point)
    TEST_ASSERT_TRUE(debug_log_contains("pixel[0]=0xffffff"));
    // And it should also have been cleared again on the following tick
    // (look for the clear entry appearing after the lit entry)
    TEST_ASSERT_TRUE(debug_log_contains("pixel[0]=0x000000"));
}

// ---------------------------------------------------------------------------
// Unity runner
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_fade_sets_start_color_on_first_tick);
    RUN_TEST(test_fade_reaches_stop_color_at_end_of_period);
    RUN_TEST(test_fade_is_mid_blend_at_half_period);
    RUN_TEST(test_fade_marks_done_after_period);

    RUN_TEST(test_sparkle_no_change_before_period);
    RUN_TEST(test_sparkle_all_pixels_lit_after_period);
    RUN_TEST(test_sparkle_repeats_every_period);

    RUN_TEST(test_circular_no_change_before_period);
    RUN_TEST(test_circular_first_tick_lights_segment);
    RUN_TEST(test_circular_segment_advances_each_period);
    RUN_TEST(test_circular_segment_wraps_around);

    return UNITY_END();
}
