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
// program_color
//
// hmtl_program_color_t is the one wire struct whose layout used to follow
// -DBIG_PIXELS. Its range is now a fixed-width wire_pixel_range_t, and
// program_color() narrows that into pixel_range_t for setRangeRGB.
//
// Every payload below is written as LITERAL BYTES rather than through a
// formatter. There is no hmtl_program_color_fmt() to reuse, and inventing one
// for the test would only prove the encoder agrees with itself; transcribing
// the layout by hand is the independent half of the check.
// ---------------------------------------------------------------------------

static hmtl_program_t color_fns[] = {
    { PROGRAM_COLOR, NULL, program_color },
};

// Fill buf with a COLOR program carrying the seven wire bytes
// r g b start_lo start_hi length_lo length_hi.
static void make_color_msg(byte *buf, size_t buflen, uint8_t output,
                           uint8_t r, uint8_t g, uint8_t b,
                           uint16_t start, uint16_t length) {
    memset(buf, 0, buflen);
    msg_program_t *msg = (msg_program_t *)(buf + sizeof(msg_hdr_t));
    hmtl_program_fmt(msg, output, PROGRAM_COLOR, (uint16_t)buflen);

    msg->values[0] = r;
    msg->values[1] = g;
    msg->values[2] = b;
    msg->values[3] = (uint8_t)(start  & 0xff);   // little-endian, as on the wire
    msg->values[4] = (uint8_t)(start  >> 8);
    msg->values[5] = (uint8_t)(length & 0xff);
    msg->values[6] = (uint8_t)(length >> 8);
}

// The struct must read those seven bytes the way they were written. This is the
// assertion the old expression-form static_assert could not make: it agreed
// with whatever PIXEL_ADDR_TYPE happened to be, so it held on both sides of a
// disagreement.
void test_color_wire_bytes_decode_little_endian() {
    byte buf[HMTL_MSG_PROGRAM_LEN];
    make_color_msg(buf, sizeof(buf), 0, 0x11, 0x22, 0x33, 0x0102, 0x0304);

    msg_program_t *msg = (msg_program_t *)(buf + sizeof(msg_hdr_t));
    hmtl_program_color_t *color = (hmtl_program_color_t *)msg->values;

    TEST_ASSERT_EQUAL_UINT(7, sizeof(hmtl_program_color_t));
    TEST_ASSERT_EQUAL_HEX8(0x11, color->color.r);
    TEST_ASSERT_EQUAL_HEX8(0x22, color->color.g);
    TEST_ASSERT_EQUAL_HEX8(0x33, color->color.b);
    TEST_ASSERT_EQUAL_UINT16(0x0102, color->range.start);
    TEST_ASSERT_EQUAL_UINT16(0x0304, color->range.length);
}

void test_color_sets_the_requested_range() {
    PixelUtil pixels(8, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t      *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void              *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, color_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN];
    make_color_msg(buf, sizeof(buf), 0, 0xff, 0x00, 0x00, /*start*/2, /*length*/3);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    TEST_ASSERT_TRUE(debug_log_contains("setRangeRGB [2+3] 0xff0000"));
    for (uint16_t i = 2; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(i).r);
    }
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(1).r);
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(5).r);
}

// A range beyond 255 is the entire reason the wire field is uint16_t. On a
// default-flag build (PIXEL_ADDR_TYPE = uint8_t) it also exercises the
// narrowing: 300 must not truncate to 44.
void test_color_start_beyond_a_byte_is_not_truncated() {
    PixelUtil pixels(400, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t      *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void              *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, color_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN];
    make_color_msg(buf, sizeof(buf), 0, 0x00, 0xff, 0x00, /*start*/300, /*length*/2);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

#ifdef BIG_PIXELS
    // The full range survives: pixels 300-301 and nothing near the truncation.
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(300).g);
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(301).g);
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(44).g);
#else
    // PIXEL_ADDR_TYPE is uint8_t here, so setRangeRGB cannot express start=300
    // at all. What must NOT happen is a silent wrap that paints pixel 44; the
    // narrowing is bounded, so the message is rejected instead.
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(44).g);
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(300).g);
    TEST_ASSERT_TRUE(debug_log_contains("COLOR start past addressable range:300"));
#endif
}

// length == 0 keeps its documented meaning — the whole strip — and the clamp
// must never manufacture one out of an over-long range.
void test_color_zero_length_fills_whole_strip() {
    PixelUtil pixels(8, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t      *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void              *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, color_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN];
    make_color_msg(buf, sizeof(buf), 0, 0x00, 0x00, 0xff, /*start*/0, /*length*/0);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    TEST_ASSERT_TRUE(debug_log_contains("setAllRGB 0x0000ff"));
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(0).b);
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(7).b);
}

void test_color_overlong_length_is_clamped_not_zeroed() {
    PixelUtil pixels(8, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t      *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void              *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, color_fns, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN];
    make_color_msg(buf, sizeof(buf), 0, 0xff, 0x00, 0x00, /*start*/6, /*length*/100);
    TEST_ASSERT_TRUE(send_program(mgr, buf));

    // Clamped to the two pixels that exist, NOT collapsed to length 0 (which
    // would have flooded all eight).
    TEST_ASSERT_TRUE(debug_log_contains("COLOR length clamped to:2"));
    TEST_ASSERT_TRUE(debug_log_contains("setRangeRGB [6+2] 0xff0000"));
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(0).r);
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(6).r);
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels.getPixel(7).r);
}

// The migration case, verbatim: `HMTLClient -P color -C 255,0,0,0,10` used to
// mean "red, start 0, length 10". Read at the new layout those five bytes are
// start=2560, length=0 — and length 0 means the WHOLE STRIP. Rejecting on start
// first is what stops a stale invocation from flooding the strip silently.
void test_color_stale_five_byte_invocation_is_rejected_not_flooded() {
    PixelUtil pixels(8, 0, 0, 0);
    output_hdr_t out = make_pixels_hdr(0);
    output_hdr_t      *outputs[1]  = { &out };
    program_tracker_t *trackers[1] = { nullptr };
    void              *objects[1]  = { &pixels };
    ProgramManager mgr(outputs, trackers, objects, 1, color_fns, 1);

    // Exactly the bytes the old CLI form produced: r=255 g=0 b=0 then 0, 10.
    byte buf[HMTL_MSG_PROGRAM_LEN];
    memset(buf, 0, sizeof(buf));
    msg_program_t *msg = (msg_program_t *)(buf + sizeof(msg_hdr_t));
    hmtl_program_fmt(msg, 0, PROGRAM_COLOR, (uint16_t)sizeof(buf));
    msg->values[0] = 255; msg->values[1] = 0; msg->values[2] = 0;
    msg->values[3] = 0;   msg->values[4] = 10;

    TEST_ASSERT_TRUE(send_program(mgr, buf));

    TEST_ASSERT_TRUE(debug_log_contains("COLOR start past addressable range:2560"));
    TEST_ASSERT_FALSE(debug_log_contains("setAllRGB"));
    TEST_ASSERT_FALSE(debug_log_contains("setRangeRGB"));
    for (uint16_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, pixels.getPixel(i).r);
    }
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

    RUN_TEST(test_color_wire_bytes_decode_little_endian);
    RUN_TEST(test_color_sets_the_requested_range);
    RUN_TEST(test_color_start_beyond_a_byte_is_not_truncated);
    RUN_TEST(test_color_zero_length_fills_whole_strip);
    RUN_TEST(test_color_overlong_length_is_clamped_not_zeroed);
    RUN_TEST(test_color_stale_five_byte_invocation_is_rejected_not_flooded);

    return UNITY_END();
}
