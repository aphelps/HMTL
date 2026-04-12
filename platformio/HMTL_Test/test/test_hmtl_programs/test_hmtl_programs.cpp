/*
 * Unit tests for HMTLPrograms — message formatting and sequence program logic.
 */

#include <unity.h>
#include <string.h>
#include "HMTLTypes.h"
#include "HMTLMessaging.h"
#include "HMTLPrograms.h"
#include "ProgramManager.h"

extern unsigned long _mock_millis;

void setUp()    { _mock_millis = 0; debug_log_begin_test(Unity.CurrentTestName); }
void tearDown() {}

// ---------------------------------------------------------------------------
// program_sequence_fmt / program_sequence_add
// ---------------------------------------------------------------------------

void test_sequence_fmt_sets_no_output() {
    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    uint16_t len = program_sequence_fmt(buf, sizeof(buf), 1);

    TEST_ASSERT_EQUAL(HMTL_MSG_PROGRAM_LEN, len);

    // msg_hdr_t at offset 0, msg_program_t after
    msg_hdr_t     *hdr = (msg_hdr_t *)buf;
    msg_program_t *prg = (msg_program_t *)(hdr + 1);

    TEST_ASSERT_EQUAL(HMTL_MSG_START, hdr->startcode);
    TEST_ASSERT_EQUAL(HMTL_NO_OUTPUT, prg->hdr.output);
    TEST_ASSERT_EQUAL(HMTL_PROGRAM_SEQUENCE, prg->type);
}

void test_sequence_add_first_slot() {
    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sequence_fmt(buf, sizeof(buf), 1);

    int next = program_sequence_add(buf, /*output=*/2, /*duration=*/500, /*value=*/128, /*offset=*/-1);
    TEST_ASSERT_EQUAL(1, next);  // consumed slot 0, returns 1

    hmtl_program_sequence_t *seq =
        (hmtl_program_sequence_t *)((msg_program_t *)(buf + sizeof(msg_hdr_t)))->values;
    TEST_ASSERT_EQUAL(2,   seq->outputs[0]);
    TEST_ASSERT_EQUAL(500, seq->durations[0]);
    TEST_ASSERT_EQUAL(128, seq->values[0]);
    TEST_ASSERT_EQUAL(HMTL_NO_OUTPUT, seq->outputs[1]);  // next slot sentinel
}

void test_sequence_add_second_slot() {
    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sequence_fmt(buf, sizeof(buf), 1);

    int next = program_sequence_add(buf, 0, 300, 200, -1);
    next      = program_sequence_add(buf, 1, 400, 100, next);
    TEST_ASSERT_EQUAL(2, next);

    hmtl_program_sequence_t *seq =
        (hmtl_program_sequence_t *)((msg_program_t *)(buf + sizeof(msg_hdr_t)))->values;
    TEST_ASSERT_EQUAL(0,   seq->outputs[0]);
    TEST_ASSERT_EQUAL(300, seq->durations[0]);
    TEST_ASSERT_EQUAL(200, seq->values[0]);
    TEST_ASSERT_EQUAL(1,   seq->outputs[1]);
    TEST_ASSERT_EQUAL(400, seq->durations[1]);
    TEST_ASSERT_EQUAL(100, seq->values[1]);
    TEST_ASSERT_EQUAL(HMTL_NO_OUTPUT, seq->outputs[2]);
}

void test_sequence_add_overflow_returns_minus_one() {
    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sequence_fmt(buf, sizeof(buf), 1);

    int offset = -1;
    for (int i = 0; i < HMTL_SEQUENCE_MAX; i++) {
        offset = program_sequence_add(buf, (uint8_t)i, 100, 255, offset);
    }
    // All slots consumed — next add should return -1
    int result = program_sequence_add(buf, 0, 100, 255, offset);
    TEST_ASSERT_EQUAL(-1, result);
}

// ---------------------------------------------------------------------------
// hmtl_program_blink_fmt
// ---------------------------------------------------------------------------

void test_blink_fmt_packs_correctly() {
    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    uint16_t len = hmtl_program_blink_fmt(buf, sizeof(buf),
                                          /*address=*/42, /*output=*/1,
                                          /*on_period=*/500, /*on_color=*/0xFF0000,
                                          /*off_period=*/250, /*off_color=*/0x000000);
    TEST_ASSERT_EQUAL(HMTL_MSG_PROGRAM_LEN, len);

    msg_hdr_t     *hdr = (msg_hdr_t *)buf;
    msg_program_t *prg = (msg_program_t *)(hdr + 1);

    TEST_ASSERT_EQUAL(42, hdr->address);
    TEST_ASSERT_EQUAL(1,  prg->hdr.output);
    TEST_ASSERT_EQUAL(HMTL_PROGRAM_BLINK, prg->type);

    hmtl_program_blink_t *blink = (hmtl_program_blink_t *)prg->values;
    TEST_ASSERT_EQUAL(500, blink->on_period);
    TEST_ASSERT_EQUAL(250, blink->off_period);
}

// ---------------------------------------------------------------------------
// Sequence program runtime — install and tick
// ---------------------------------------------------------------------------

static boolean seq_dummy_program(output_hdr_t *, void *, program_tracker_t *) {
    return false;
}
static hmtl_program_t seq_functions[] = {
    { HMTL_PROGRAM_SEQUENCE, program_sequence, program_sequence_init },
};

void test_sequence_program_activates_first_output() {
    config_rgb_t rgb0, rgb1;
    memset(&rgb0, 0, sizeof(rgb0)); rgb0.hdr.type = HMTL_OUTPUT_RGB; rgb0.hdr.output = 0;
    memset(&rgb1, 0, sizeof(rgb1)); rgb1.hdr.type = HMTL_OUTPUT_RGB; rgb1.hdr.output = 1;

    output_hdr_t    *outputs[2]  = {(output_hdr_t*)&rgb0, (output_hdr_t*)&rgb1};
    program_tracker_t *trackers[2] = {nullptr, nullptr};
    void             *objects[2]  = {nullptr, nullptr};

    ProgramManager mgr(outputs, trackers, objects, 2, seq_functions, 1);

    // Build and send a 2-step sequence message
    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sequence_fmt(buf, sizeof(buf), 1);
    program_sequence_add(buf, 0, 500, 200, -1);
    program_sequence_add(buf, 1, 300, 100,  1);

    msg_program_t *msg = (msg_program_t *)(buf + sizeof(msg_hdr_t));
    TEST_ASSERT_TRUE(mgr.handle_msg(msg));

    // Tick at t=0: first tick initializes step 0, turns on output 0
    _mock_millis = 0;
    mgr.run();

    // Output 0 should have value 200 (set_rgb called with 200,200,200 → value[0]=200)
    TEST_ASSERT_EQUAL(200, rgb0.values[0]);
    TEST_ASSERT_EQUAL(200, rgb0.values[1]);
    TEST_ASSERT_EQUAL(200, rgb0.values[2]);
    // Output 1 still zero
    TEST_ASSERT_EQUAL(0, rgb1.values[0]);
}

void test_sequence_program_advances_after_duration() {
    config_rgb_t rgb0, rgb1;
    memset(&rgb0, 0, sizeof(rgb0)); rgb0.hdr.type = HMTL_OUTPUT_RGB; rgb0.hdr.output = 0;
    memset(&rgb1, 0, sizeof(rgb1)); rgb1.hdr.type = HMTL_OUTPUT_RGB; rgb1.hdr.output = 1;

    output_hdr_t    *outputs[2]  = {(output_hdr_t*)&rgb0, (output_hdr_t*)&rgb1};
    program_tracker_t *trackers[2] = {nullptr, nullptr};
    void             *objects[2]  = {nullptr, nullptr};

    ProgramManager mgr(outputs, trackers, objects, 2, seq_functions, 1);

    byte buf[HMTL_MSG_PROGRAM_LEN] = {};
    program_sequence_fmt(buf, sizeof(buf), 1);
    program_sequence_add(buf, 0, 500, 200, -1);
    program_sequence_add(buf, 1, 300, 100,  1);

    msg_program_t *msg = (msg_program_t *)(buf + sizeof(msg_hdr_t));
    mgr.handle_msg(msg);

    _mock_millis = 0;   mgr.run();  // init: output 0 on
    _mock_millis = 600; mgr.run();  // past 500ms: output 1 on

    TEST_ASSERT_EQUAL(0,   rgb0.values[0]);   // output 0 off
    TEST_ASSERT_EQUAL(100, rgb1.values[0]);   // output 1 on
}

// ---------------------------------------------------------------------------
// Unity runner
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_sequence_fmt_sets_no_output);
    RUN_TEST(test_sequence_add_first_slot);
    RUN_TEST(test_sequence_add_second_slot);
    RUN_TEST(test_sequence_add_overflow_returns_minus_one);
    RUN_TEST(test_blink_fmt_packs_correctly);
    RUN_TEST(test_sequence_program_activates_first_output);
    RUN_TEST(test_sequence_program_advances_after_duration);

    return UNITY_END();
}
