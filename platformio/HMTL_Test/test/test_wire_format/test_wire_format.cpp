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

#define WF_SIZE(t, n)   static_assert(sizeof(t) == (n), #t " changed size")
#define WF_OFF(t, f, n) static_assert(offsetof(t, f) == (n), #t "." #f " moved")

WF_SIZE(config_hdr_v1_t, 5);
WF_OFF(config_hdr_v1_t, magic, 0);
WF_OFF(config_hdr_v1_t, version, 1);
WF_OFF(config_hdr_v1_t, address, 2);
WF_OFF(config_hdr_v1_t, num_outputs, 3);
WF_OFF(config_hdr_v1_t, flags, 4);

// 8 B with address at 3. Unpacked this was 10 B with address at 4 on 32-bit
// targets — interior padding, the worst of the drift the packing fixed.
WF_SIZE(config_hdr_v2_t, 8);
WF_OFF(config_hdr_v2_t, magic, 0);
WF_OFF(config_hdr_v2_t, protocol_version, 1);
WF_OFF(config_hdr_v2_t, hardware_version, 2);
WF_OFF(config_hdr_v2_t, address, 3);
WF_OFF(config_hdr_v2_t, reserved, 5);
WF_OFF(config_hdr_v2_t, num_outputs, 6);
WF_OFF(config_hdr_v2_t, flags, 7);

WF_SIZE(config_hdr_v3_t, 10);
WF_OFF(config_hdr_v3_t, magic, 0);
WF_OFF(config_hdr_v3_t, protocol_version, 1);
WF_OFF(config_hdr_v3_t, hardware_version, 2);
WF_OFF(config_hdr_v3_t, baud, 3);
WF_OFF(config_hdr_v3_t, num_outputs, 4);
WF_OFF(config_hdr_v3_t, flags, 5);
WF_OFF(config_hdr_v3_t, device_id, 6);
WF_OFF(config_hdr_v3_t, address, 8);

WF_SIZE(output_hdr_t, 2);
WF_OFF(output_hdr_t, type, 0);
WF_OFF(output_hdr_t, output, 1);

WF_SIZE(msg_hdr_t, 8);
WF_OFF(msg_hdr_t, startcode, 0);
WF_OFF(msg_hdr_t, crc, 1);
WF_OFF(msg_hdr_t, version, 2);
WF_OFF(msg_hdr_t, length, 3);
WF_OFF(msg_hdr_t, type, 4);
WF_OFF(msg_hdr_t, flags, 5);
WF_OFF(msg_hdr_t, address, 6);   // catches a socket_addr_t width change too

WF_SIZE(msg_value_t, 4);
WF_OFF(msg_value_t, hdr, 0);     // bitfields have no offsetof; see the runtime test

WF_SIZE(msg_rgb_t, 5);
WF_OFF(msg_rgb_t, hdr, 0);
WF_OFF(msg_rgb_t, values, 2);

WF_SIZE(msg_program_t, 35);
WF_OFF(msg_program_t, hdr, 0);
WF_OFF(msg_program_t, type, 2);
WF_OFF(msg_program_t, values, 3);

// 15 B on every target. Unpacked this was 15 on AVR and 16 on 32-bit, which
// made HMTL_MSG_POLL_MIN_LEN 23 or 24 and let a length-checking peer reject a
// structurally valid poll response.
WF_SIZE(msg_poll_response_t, 15);
WF_OFF(msg_poll_response_t, config, 0);
WF_OFF(msg_poll_response_t, object_type, 10);
WF_OFF(msg_poll_response_t, recv_buffer_size, 12);
WF_OFF(msg_poll_response_t, msg_version, 14);
WF_OFF(msg_poll_response_t, data, 15);

WF_SIZE(msg_dumpconfig_response_t, 0);

WF_SIZE(msg_set_addr_t, 4);
WF_OFF(msg_set_addr_t, device_id, 0);
WF_OFF(msg_set_addr_t, address, 2);

WF_SIZE(msg_sensor_response_t, 0);

WF_SIZE(msg_sensor_data_t, 2);
WF_OFF(msg_sensor_data_t, sensor_type, 0);
WF_OFF(msg_sensor_data_t, data_len, 1);
WF_OFF(msg_sensor_data_t, data, 2);

// 5 B with timestamp at 1. This one lived in TimeSync.h until it moved here with
// the rest of the wire format; unpacked and with `unsigned long timestamp` it
// was 8 B with timestamp at 4 on 32-bit targets, so an ESP32 and an AVR read
// each other's TIMESYNC payloads three bytes out of step. Two changes were
// needed, not one: packing fixes AVR-vs-32-bit, and uint32_t fixes the fact that
// `unsigned long` is 8 bytes on this LP64 host — which is also why a host
// -fpack-struct=1 measurement of the OLD struct (9 B) matched neither target.
WF_SIZE(msg_time_sync_t, 5);
WF_OFF(msg_time_sync_t, sync_phase, 0);
WF_OFF(msg_time_sync_t, timestamp, 1);

// The frame lengths that follow from the above. These are the numbers that go
// out on the wire, so they are also the numbers a peer length-checks against.
static_assert(HMTL_MSG_VALUE_LEN == 12, "VALUE frame length changed");
static_assert(HMTL_MSG_RGB_LEN == 13, "RGB frame length changed");
static_assert(HMTL_MSG_PROGRAM_LEN == 43, "PROGRAM frame length changed");
static_assert(HMTL_MSG_POLL_MIN_LEN == 23, "POLL frame length changed");
static_assert(HMTL_MSG_SET_ADDR_LEN == 12, "SET_ADDR frame length changed");
static_assert(HMTL_MSG_SENSOR_MIN_LEN == 8, "SENSOR frame length changed");
static_assert(HMTL_MSG_DUMPCONFIG_MIN_LEN == 8, "DUMPCONFIG frame length changed");
static_assert(HMTL_MSG_TIMESYNC_LEN == 13, "TIMESYNC frame length changed");

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
