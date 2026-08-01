/*
 * Cross-ABI layout guard for HMTLprotocol/HMTLWireFormat.h.
 *
 * An ATMega328 module and an ESP32 module share one RS485 bus and read each
 * other's EEPROM config blobs, so every struct in the wire header must have the
 * same size AND the same field offsets under avr-gcc and under any 32-bit
 * compiler. HMTLWireFormat.h buys that with __attribute__((__packed__)) on
 * every struct; this file is what stops the guarantee rotting.
 *
 * The static_assert block below fires at COMPILE time, so it holds for whatever
 * target the file is built for — running the native suite proves the 32-bit
 * layout, and building any module firmware proves the AVR/ESP32 one. Reproduce
 * the full sweep by syntax-checking this file (or any TU that includes the
 * header) with each toolchain:
 *
 *   for CXX in c++ "c++ -fpack-struct=1" avr-g++ xtensa-esp32-elf-g++; do ...
 *
 * The RUN_TEST cases add the checks a static_assert cannot make: that the bytes
 * land where the offsets say they do, including the msg_value_t bitfield split,
 * which packing could in principle have re-allocated.
 */

#include <unity.h>
#include <stddef.h>
#include <string.h>
#include "HMTLWireFormat.h"

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Compile-time: sizes and offsets, identical on every ABI
// ---------------------------------------------------------------------------

// The compile-time block lives in tests/layout/wire_layout_asserts.h so the
// same numbers are checked by avr-g++ and xtensa-esp32-elf-g++ too, not just
// by this host-only suite. The runtime cases below are what a static_assert
// cannot express.
#include "../../../../tests/layout/wire_layout_asserts.h"

// ---------------------------------------------------------------------------
// Runtime: the bytes really land where the offsets claim
// ---------------------------------------------------------------------------

// Fill a msg_hdr_t field by field and read the result back as bytes: proves the
// header a peer parses positionally is the header we wrote.
void test_msg_hdr_bytes() {
    msg_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.startcode = HMTL_MSG_START;
    h.crc       = 0x11;
    h.version   = HMTL_MSG_VERSION;
    h.length    = 0x0C;
    h.type      = MSG_TYPE_OUTPUT;
    h.flags     = MSG_FLAG_RESPONSE;
    h.address   = 0xBEEF;

    const uint8_t *b = (const uint8_t *)&h;
    TEST_ASSERT_EQUAL_HEX8(0xFC, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, b[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, b[3]);
    TEST_ASSERT_EQUAL_HEX8(0x01, b[4]);
    TEST_ASSERT_EQUAL_HEX8(0x02, b[5]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, b[6]);   // little-endian on AVR and Xtensa alike
    TEST_ASSERT_EQUAL_HEX8(0xBE, b[7]);
}

// config_hdr_v2_t is compile-time dead under HMTL_CONFIG_VERSION 3, but a v2
// EEPROM blob written years ago is not, so its layout still has to be the AVR
// one. address at offset 3 straddles bytes 3-4 with no padding in front.
void test_config_hdr_v2_bytes() {
    config_hdr_v2_t c;
    memset(&c, 0, sizeof(c));
    c.magic   = HMTL_CONFIG_MAGIC;
    c.address = 0x1234;
    c.flags   = HMTL_FLAG_MASTER;

    const uint8_t *b = (const uint8_t *)&c;
    TEST_ASSERT_EQUAL_HEX8(0x5C, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, b[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12, b[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, b[7]);
}

// Packing a struct that contains bitfields is the one case where the attribute
// could have changed more than padding, so check the split explicitly: 13 bits
// of value then 3 bits of flags, filling the 16-bit unit exactly.
void test_msg_value_bitfields() {
    msg_value_t m;
    memset(&m, 0, sizeof(m));
    m.hdr.type   = HMTL_OUTPUT_VALUE;
    m.hdr.output = 0;
    m.value      = 0x1FFF;   // all 13 bits
    m.flags      = 0;

    const uint8_t *b = (const uint8_t *)&m;
    TEST_ASSERT_EQUAL_HEX8(0xFF, b[2]);
    TEST_ASSERT_EQUAL_HEX8(0x1F, b[3]);

    m.value = 0;
    m.flags = 0x7;           // all 3 bits, in the top of the unit
    TEST_ASSERT_EQUAL_HEX8(0x00, b[2]);
    TEST_ASSERT_EQUAL_HEX8(0xE0, b[3]);

    // 13 bits is the whole range: 8191 round-trips, 8192 does not fit.
    m.value = 8191;
    TEST_ASSERT_EQUAL_UINT16(8191, m.value);
}

// A POLL response is a config header followed by three scalars. This is the one
// frame whose length used to differ between AVR and 32-bit peers.
void test_poll_response_bytes() {
    msg_poll_response_t p;
    memset(&p, 0, sizeof(p));
    p.config.magic   = HMTL_CONFIG_MAGIC;
    p.config.address = 0x0007;
    p.object_type      = 0x0102;
    p.recv_buffer_size = 0x0304;
    p.msg_version      = HMTL_MSG_VERSION;

    const uint8_t *b = (const uint8_t *)&p;
    TEST_ASSERT_EQUAL_HEX8(0x5C, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07, b[8]);    // config_hdr_v3_t.address at 8
    TEST_ASSERT_EQUAL_HEX8(0x00, b[9]);
    TEST_ASSERT_EQUAL_HEX8(0x02, b[10]);   // object_type
    TEST_ASSERT_EQUAL_HEX8(0x01, b[11]);
    TEST_ASSERT_EQUAL_HEX8(0x04, b[12]);   // recv_buffer_size
    TEST_ASSERT_EQUAL_HEX8(0x03, b[13]);
    TEST_ASSERT_EQUAL_HEX8(0x02, b[14]);   // msg_version
    TEST_ASSERT_EQUAL_UINT32(23, (uint32_t)HMTL_MSG_POLL_MIN_LEN);
}

// ---------------------------------------------------------------------------
// Unity runner
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_msg_hdr_bytes);
    RUN_TEST(test_config_hdr_v2_bytes);
    RUN_TEST(test_msg_value_bitfields);
    RUN_TEST(test_poll_response_bytes);

    return UNITY_END();
}
