/*
 * Compile-time size/offset assertions for HMTLWireFormat.h.
 *
 * These began life inside platformio/HMTL_Test/test/test_wire_format/, which
 * runs on the host only — so the very structs HMTL#6 packed to make an AVR and
 * an ESP32 agree were never checked under either of those compilers. Extracted
 * to a header so tests/layout/layout_check.cpp can put them in front of every
 * toolchain, while test_wire_format.cpp keeps including them alongside its
 * runtime byte-order tests. One copy of the numbers, two consumers.
 */

#ifndef HMTL_WIRE_LAYOUT_ASSERTS_H
#define HMTL_WIRE_LAYOUT_ASSERTS_H

#include <stddef.h>
#include "HMTLWireFormat.h"

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

#endif /* HMTL_WIRE_LAYOUT_ASSERTS_H */
