/*
 * Smoke tests for HMTL firmware compiled for ATmega328 and run under simavr.
 *
 * Verifies that HMTL sentinel values, struct sizes, and output type codes are
 * correct when compiled by avr-gcc — catching any AVR-specific packing or
 * alignment surprises that the native (x86/arm64) build would not catch.
 */

#include <Arduino.h>
#include <unity.h>
#include "HMTLTypes.h"
#include "HMTLMessaging.h"

void setUp()    {}
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
// Struct sizes — must match Python constants.py and the native build
// ---------------------------------------------------------------------------

void test_output_hdr_size() {
    TEST_ASSERT_EQUAL(2, sizeof(output_hdr_t));
}

void test_msg_hdr_size() {
    TEST_ASSERT_EQUAL(8, sizeof(msg_hdr_t));
}

void test_config_hdr_v3_size() {
    TEST_ASSERT_EQUAL(10, sizeof(config_hdr_v3_t));
}

// ---------------------------------------------------------------------------
// Output type codes
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
// Arduino entry points required by the Unity + Arduino test runner
// ---------------------------------------------------------------------------

void setup() {
    delay(2000);  // give simavr time to attach the pseudo-terminal
    UNITY_BEGIN();

    RUN_TEST(test_no_output_sentinel);
    RUN_TEST(test_all_outputs_sentinel);
    RUN_TEST(test_max_outputs);
    RUN_TEST(test_output_hdr_size);
    RUN_TEST(test_msg_hdr_size);
    RUN_TEST(test_config_hdr_v3_size);
    RUN_TEST(test_output_type_codes);

    UNITY_END();
}

void loop() {}
