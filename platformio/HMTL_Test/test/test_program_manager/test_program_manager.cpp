/*
 * Unit tests for ProgramManager — message dispatch, tracker lifecycle,
 * NO_OUTPUT programs, and program tick scheduling.
 */

#include <unity.h>
#include "HMTLTypes.h"
#include "HMTLMessaging.h"
#include "HMTLPrograms.h"
#include "ProgramManager.h"

// Mocks needed by ProgramManager
extern unsigned long _mock_millis;

// ---------------------------------------------------------------------------
// Minimal test program
// ---------------------------------------------------------------------------

static boolean dummy_program(output_hdr_t *, void *, program_tracker_t *) {
    return false;
}

static boolean dummy_setup(msg_program_t *, program_tracker_t *tracker,
                            output_hdr_t *, void *, ProgramManager *) {
    // Allocate a trivial state so the tracker is "running"
    if (tracker) {
        tracker->state  = nullptr;
        tracker->flags  = 0;
    }
    return true;
}

static hmtl_program_t test_programs[] = {
    { HMTL_PROGRAM_BLINK, dummy_program, dummy_setup },
    { HMTL_PROGRAM_SEQUENCE, dummy_program, dummy_setup },
};
static const byte NUM_PROGRAMS = sizeof(test_programs) / sizeof(test_programs[0]);

// ---------------------------------------------------------------------------
// Test fixtures
// ---------------------------------------------------------------------------

static config_rgb_t rgb0, rgb1;
static output_hdr_t *outputs[2];
static program_tracker_t *trackers[2];
static void *objects[2];

static void make_program_msg(msg_program_t *msg, uint8_t output_idx,
                              uint8_t program_type) {
    memset(msg, 0, sizeof(*msg));
    msg->hdr.type   = HMTL_OUTPUT_PROGRAM;
    msg->hdr.output = output_idx;
    msg->type       = program_type;
}

void setUp() {
    _mock_millis = 0;
    debug_log_begin_test(Unity.CurrentTestName);

    memset(&rgb0, 0, sizeof(rgb0));
    memset(&rgb1, 0, sizeof(rgb1));

    rgb0.hdr.type = HMTL_OUTPUT_RGB; rgb0.hdr.output = 0;
    rgb1.hdr.type = HMTL_OUTPUT_RGB; rgb1.hdr.output = 1;

    outputs[0]  = (output_hdr_t *)&rgb0;
    outputs[1]  = (output_hdr_t *)&rgb1;
    trackers[0] = trackers[1] = nullptr;
    objects[0]  = objects[1]  = nullptr;
}

void tearDown() {}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_handle_msg_valid_output_creates_tracker() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    msg_program_t msg;
    make_program_msg(&msg, 0, HMTL_PROGRAM_BLINK);

    TEST_ASSERT_TRUE(mgr.handle_msg(&msg));
    TEST_ASSERT_NOT_NULL(trackers[0]);
    TEST_ASSERT_EQUAL(0, mgr.program_from_tracker(trackers[0]) == HMTL_PROGRAM_BLINK ? 0 : 1);
}

void test_handle_msg_invalid_output_rejected() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    msg_program_t msg;
    // output index 5 is beyond num_outputs=2
    make_program_msg(&msg, 5, HMTL_PROGRAM_BLINK);

    TEST_ASSERT_FALSE(mgr.handle_msg(&msg));
    TEST_ASSERT_NULL(trackers[0]);
    TEST_ASSERT_NULL(trackers[1]);
}

void test_handle_msg_no_output_uses_no_output_tracker() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    msg_program_t msg;
    make_program_msg(&msg, HMTL_NO_OUTPUT, HMTL_PROGRAM_SEQUENCE);

    TEST_ASSERT_TRUE(mgr.handle_msg(&msg));
    // Per-output trackers must NOT be set
    TEST_ASSERT_NULL(trackers[0]);
    TEST_ASSERT_NULL(trackers[1]);
}

void test_handle_msg_all_outputs_sets_all_trackers() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    msg_program_t msg;
    make_program_msg(&msg, HMTL_ALL_OUTPUTS, HMTL_PROGRAM_BLINK);

    TEST_ASSERT_TRUE(mgr.handle_msg(&msg));
    TEST_ASSERT_NOT_NULL(trackers[0]);
    TEST_ASSERT_NOT_NULL(trackers[1]);
}

void test_handle_msg_cancel_frees_tracker() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    // Install a program first
    msg_program_t msg;
    make_program_msg(&msg, 0, HMTL_PROGRAM_BLINK);
    mgr.handle_msg(&msg);
    TEST_ASSERT_NOT_NULL(trackers[0]);

    // Now cancel it
    make_program_msg(&msg, 0, HMTL_PROGRAM_NONE);
    TEST_ASSERT_TRUE(mgr.handle_msg(&msg));
    // Tracker should be freed (program_index set to NO_PROGRAM)
    TEST_ASSERT_TRUE(trackers[0] == nullptr ||
                     trackers[0]->program_index == ProgramManager::NO_PROGRAM);
}

void test_lookup_output_by_type_rgb() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    byte idx = mgr.lookup_output_by_type(HMTL_OUTPUT_RGB);
    TEST_ASSERT_EQUAL(0, idx);
}

void test_lookup_output_by_type_second_rgb() {
    ProgramManager mgr(outputs, trackers, objects, 2,
                       test_programs, NUM_PROGRAMS);

    byte idx = mgr.lookup_output_by_type(HMTL_OUTPUT_RGB, 1);
    TEST_ASSERT_EQUAL(1, idx);
}

// ---------------------------------------------------------------------------
// Unity runner
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_handle_msg_valid_output_creates_tracker);
    RUN_TEST(test_handle_msg_invalid_output_rejected);
    RUN_TEST(test_handle_msg_no_output_uses_no_output_tracker);
    RUN_TEST(test_handle_msg_all_outputs_sets_all_trackers);
    RUN_TEST(test_handle_msg_cancel_frees_tracker);
    RUN_TEST(test_lookup_output_by_type_rgb);
    RUN_TEST(test_lookup_output_by_type_second_rgb);

    return UNITY_END();
}
