/*
 * Unit tests for HMTLTypes — struct layouts, sentinel values, and header
 * validation logic.
 */

#include <unity.h>
#include "HMTLTypes.h"
#include "HMTLMessaging.h"
#include "HMTLPrograms.h"
#include "Debug.h"

extern unsigned long _mock_millis;

void setUp()    { _mock_millis = 0; debug_log_begin_test(Unity.CurrentTestName); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Sentinel values
// ---------------------------------------------------------------------------

void test_no_output_sentinel() {
    TEST_ASSERT_EQUAL(0xFF, HMTL_NO_OUTPUT);
}

void test_all_outputs_sentinel() {
    TEST_ASSERT_EQUAL(0xFE, HMTL_ALL_OUTPUTS);
}

void test_max_outputs() {
    TEST_ASSERT_EQUAL(8, HMTL_MAX_OUTPUTS);
}

// ---------------------------------------------------------------------------
// Struct sizes — must match Python constants.py / C++ message encoding
// ---------------------------------------------------------------------------

void test_output_hdr_size() {
    // output_hdr_t: type (1B) + output (1B) = 2B
    TEST_ASSERT_EQUAL(2, sizeof(output_hdr_t));
}

void test_msg_hdr_size() {
    // msg_hdr_t: 8 bytes per HMTLMessaging.h comment
    TEST_ASSERT_EQUAL(8, sizeof(msg_hdr_t));
}

void test_config_hdr_v3_size() {
    // config_hdr_v3_t: magic(1) proto(1) hdw(1) baud(1) num(1) flags(1) dev_id(2) addr(2) = 10B
    TEST_ASSERT_EQUAL(10, sizeof(config_hdr_v3_t));
}

void test_sequence_program_size() {
    // hmtl_program_sequence_t: 8+16+8 = 32 bytes; must fit in MAX_PROGRAM_VAL
    TEST_ASSERT_EQUAL(32, sizeof(hmtl_program_sequence_t));
    TEST_ASSERT_TRUE(sizeof(hmtl_program_sequence_t) <= MAX_PROGRAM_VAL);
}

void test_sequence_max_steps() {
    TEST_ASSERT_EQUAL(8, HMTL_SEQUENCE_MAX);
}

// ---------------------------------------------------------------------------
// hmtl_validate_header
// ---------------------------------------------------------------------------

void test_validate_header_valid() {
    config_hdr_v3_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic            = HMTL_CONFIG_MAGIC;
    hdr.protocol_version = 3;
    hdr.num_outputs      = 4;

    TEST_ASSERT_TRUE(hmtl_validate_header((config_hdr_t *)&hdr));
}

void test_validate_header_bad_magic() {
    config_hdr_v3_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic            = 0x00;  // wrong
    hdr.protocol_version = 3;
    hdr.num_outputs      = 4;

    TEST_ASSERT_FALSE(hmtl_validate_header((config_hdr_t *)&hdr));
}

void test_validate_header_too_many_outputs() {
    config_hdr_v3_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic            = HMTL_CONFIG_MAGIC;
    hdr.protocol_version = 3;
    hdr.num_outputs      = HMTL_MAX_OUTPUTS + 1;

    TEST_ASSERT_FALSE(hmtl_validate_header((config_hdr_t *)&hdr));
}

// ---------------------------------------------------------------------------
// Output type codes match Python constants.py
// ---------------------------------------------------------------------------

void test_output_type_codes() {
    TEST_ASSERT_EQUAL(0x1, HMTL_OUTPUT_VALUE);
    TEST_ASSERT_EQUAL(0x2, HMTL_OUTPUT_RGB);
    TEST_ASSERT_EQUAL(0x3, HMTL_OUTPUT_PROGRAM);
    TEST_ASSERT_EQUAL(0x4, HMTL_OUTPUT_PIXELS);
    TEST_ASSERT_EQUAL(0x5, HMTL_OUTPUT_MPR121);
    TEST_ASSERT_EQUAL(0x6, HMTL_OUTPUT_RS485);
    TEST_ASSERT_EQUAL(0x7, HMTL_OUTPUT_XBEE);
}

// ---------------------------------------------------------------------------
// Unity runner
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_no_output_sentinel);
    RUN_TEST(test_all_outputs_sentinel);
    RUN_TEST(test_max_outputs);
    RUN_TEST(test_output_hdr_size);
    RUN_TEST(test_msg_hdr_size);
    RUN_TEST(test_config_hdr_v3_size);
    RUN_TEST(test_sequence_program_size);
    RUN_TEST(test_sequence_max_steps);
    RUN_TEST(test_validate_header_valid);
    RUN_TEST(test_validate_header_bad_magic);
    RUN_TEST(test_validate_header_too_many_outputs);
    RUN_TEST(test_output_type_codes);

    return UNITY_END();
}
