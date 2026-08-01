/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2014-2026
 *
 * The HMTL wire format: every constant and structure that appears on a wire or
 * in EEPROM, and nothing else.
 *
 * This header is deliberately transport- and platform-agnostic so that any
 * consumer can speak the protocol without dragging in the HMTL module runtime.
 * It must NEVER include <Arduino.h>, RS485Utils.h, PixelUtil.h, FastLED.h,
 * EEPromUtils.h or any other transport/hardware header: those dependencies are
 * what previously forced non-Arduino consumers (host tools, unit tests, the
 * WLED RS485 bridge usermod) to copy-and-paste these declarations instead of
 * importing them.
 *
 * INVARIANT: every struct below is __attribute__((__packed__)).
 * ------------------------------------------------------------
 * An ATMega328 module and an ESP32 module talk to each other over the same
 * RS485 bus, and they read each other's EEPROM config blobs, so a struct in
 * this header must have the SAME size and the SAME field offsets under
 * avr-gcc and under a 32-bit compiler. Unpacked, that is not true: avr-gcc
 * aligns every type to 1 byte, while xtensa/arm/x86 align uint16_t to 2, which
 * inserts padding an AVR peer does not expect. Two structs here drifted for
 * real before the attribute was applied:
 *
 *   config_hdr_v2_t     8 B on AVR (address at offset 3)
 *                      10 B on 32-bit (address at offset 4) - INTERIOR padding
 *   msg_poll_response_t 15 B on AVR, 16 B on 32-bit - trailing padding, which
 *                      made HMTL_MSG_POLL_MIN_LEN 23 or 24 depending on target
 *
 * Packing is layout-neutral on AVR (alignment is already 1, so deployed
 * modules see no change) and corrective everywhere else: it makes the 32-bit
 * layout equal the AVR layout that is already on the wire. Do not add a struct
 * to this header without the attribute, and do not remove it from one.
 * platformio/HMTL_Test/test/test_wire_format/ pins every size and offset so a
 * regression fails a build rather than a bus.
 *
 * Consequence to be aware of: a packed struct has alignment 1, so taking the
 * address of a multi-byte member yields a possibly-unaligned pointer and
 * -Waddress-of-packed-member will (rightly) complain. Copy the member out by
 * value, or memcpy, rather than pointing at it.
 *
 * On Arduino builds, include <Arduino.h> before this header - see the byte /
 * boolean note below. Off-Arduino this header is order-independent.
 *
 * The declarations here were moved verbatim out of:
 *   HMTLTypes/HMTLTypes.h        - config_hdr_*, output_hdr_t, HMTL_OUTPUT_*
 *   HMTLMessaging/HMTLMessaging.h - msg_hdr_t and the msg_* payload structs
 *   HMTLMessaging/HMTLPrograms.h  - the HMTL_PROGRAM_* / PROGRAM_* codes
 * and each of those headers now includes this one, so existing code that
 * includes them is unaffected.
 ******************************************************************************/

#ifndef HMTLWIREFORMAT_H
#define HMTLWIREFORMAT_H

#include <stdint.h>

/*
 * The Arduino core's `byte` / `boolean` aliases. HMTLTypes.h and Socket.h are
 * written against them without including <Arduino.h> themselves, so supply
 * them when this header is used off-Arduino (host tools, native unit tests).
 * Re-declaring a typedef with the same type is legal C++, so this stays a
 * no-op wherever Arduino.h - or the HMTL desktop stub in
 * platformio/HMTL_Test/stubs/Arduino.h - has already been seen.
 *
 * Note the asymmetry this creates, deliberately: off-Arduino the header is
 * fully self-sufficient and include-order-independent, but ON Arduino the
 * guard skips the aliases and Socket.h's `byte` / `boolean` usage then needs
 * <Arduino.h> to have been included EARLIER in the translation unit. That is
 * the pre-existing contract of every HMTL header (HMTLTypes.h used `byte`
 * under the same rule) and it is not fixed by including <Arduino.h> here: the
 * whole point of this file is that a host consumer can include it without an
 * Arduino toolchain anywhere in sight. Arduino sketches get <Arduino.h>
 * injected before the first line of the sketch, so in practice the ordering is
 * automatic; library .cpp files must include it themselves, as they already do.
 */
#if !defined(ARDUINO) && !defined(Arduino_h)
typedef uint8_t byte;
typedef bool boolean;
#endif

/* socket_addr_t - the address carried in msg_hdr_t. Socket.h is include-free
 * and declares an abstract base class only; it pulls in no transport. */
#include "Socket.h"

/******************************************************************************
 * Module configuration header (also the payload of a POLL response)
 */

#define HMTL_CONFIG_MAGIC 0x5C
#define HMTL_CONFIG_VERSION 3
typedef struct __attribute__((__packed__)) {
  uint8_t     magic;
  uint8_t     version;
  uint8_t     address;
  uint8_t     num_outputs;
  uint8_t     flags;
} config_hdr_v1_t;

typedef struct __attribute__((__packed__)) {
  uint8_t     magic;
  uint8_t     protocol_version;
  uint8_t     hardware_version;
  uint16_t    address;
  uint8_t     reserved;

  uint8_t     num_outputs;
  uint8_t     flags;
} config_hdr_v2_t;

typedef struct __attribute__((__packed__)) {
  // Fixed portion, must not change between versions
  uint8_t     magic;
  uint8_t     protocol_version;
  // End of fixed portion
  
  uint8_t     hardware_version;
  uint8_t     baud;

  uint8_t     num_outputs;
  uint8_t     flags;

  uint16_t    device_id;
  uint16_t    address;
} config_hdr_v3_t; // 10B

#if HMTL_CONFIG_VERSION == 3
  typedef config_hdr_v3_t config_hdr_t;
#elif HMTL_CONFIG_VERSION == 2
  typedef config_hdr_v2_t config_hdr_t;
#elif HMTL_CONFIG_VERSION == 1
  typedef config_hdr_v1_t config_hdr_t;
#endif

#define HMTL_NO_ADDRESS (uint16_t)-1

// Convert a 8bit baud value to actual baud
#define BYTE_TO_BAUD(val) ((uint32_t)val * 1200)
#define BAUD_TO_BYTE(val) (val / 1200)

/******************************************************************************
 * Output types, config-header flags, and the output header outputs are
 * addressed with
 */

#define HMTL_OUTPUT_NONE    (uint8_t)-1
#define HMTL_OUTPUT_VALUE   0x1
#define HMTL_OUTPUT_RGB     0x2
#define HMTL_OUTPUT_PROGRAM 0x3
#define HMTL_OUTPUT_PIXELS  0x4
#define HMTL_OUTPUT_MPR121  0x5
#define HMTL_OUTPUT_RS485   0x6
#define HMTL_OUTPUT_XBEE    0x7

#define IS_HMTL_RGB_OUTPUT(out) \
  ((out == HMTL_OUTPUT_VALUE) || \
   (out == HMTL_OUTPUT_RGB) || \
   (out == HMTL_OUTPUT_PIXELS))

#define IS_HMTL_PIXEL_OUTPUT(out) \
  ((out == HMTL_OUTPUT_PIXELS))

#define HMTL_FLAG_MASTER 0x1
#define HMTL_FLAG_SERIAL 0x2

#define HMTL_NO_OUTPUT (uint8_t)-1
#define HMTL_ALL_OUTPUTS (uint8_t)-2

typedef struct __attribute__((__packed__)) {
  byte type;
  byte output;
} output_hdr_t;

/******************************************************************************
 * Transport-agnostic message types
 */

/*
 * A message is composed of a msg_hdr_t followed by additional structures
 * depending on the header's type and flag fields.
 *
 * Message header:
 * 8B:  |startcode |   crc    | version  | length   |
 *      |  type    |  flags   |       address       |
 *
 * Output message adds output_hdr_t + output-type specific data
 * 2B:  |   type   |  output  | ...
 */

#define HMTL_MAX_MSG_LEN 128

#define HMTL_MSG_START 0xFC

#define HMTL_MSG_VERSION 2
typedef struct __attribute__((__packed__)) {
  uint8_t startcode;
  uint8_t crc;
  uint8_t version;
  uint8_t length; // Length includes

  uint8_t type;
  uint8_t flags;

  // This address is redundant with the address in the RS485 socket header,
  // however it is necessary for messages received from other sources (such as
  // serial).
  socket_addr_t address;

} msg_hdr_t;

/* Message type codes */
#define MSG_TYPE_OUTPUT      0x01
#define MSG_TYPE_POLL        0x02
#define MSG_TYPE_SET_ADDR    0x03
#define MSG_TYPE_SENSOR      0x04
#define MSG_TYPE_TIMESYNC    0x05

#define MSG_TYPE_DONT_FORWARD 0xE0 // Msg types past this should not be forwarded
#define MSG_TYPE_DUMP_CONFIG  0xE0

/* Message flags */
#define MSG_FLAG_ACK        (1 << 0) // This message is an acknowledgement
#define MSG_FLAG_RESPONSE   (1 << 1) // This message expects a response
#define MSG_FLAG_MORE_DATA  (1 << 2) // This message has followup messages
#define MSG_FLAG_ERROR      (1 << 3) // This message indicates an error

#define HMTL_MSG_SIZE(msgtype) (sizeof (msg_hdr_t) + sizeof (msgtype))
/*******************************************************************************
 * Message formats for messages of type MSG_TYPE_OUTPUT
 */

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  uint16_t value : 13; // 13 bits provide values up to 8192
  uint16_t flags :  3;
} msg_value_t;
#define HMTL_MSG_VALUE_LEN (sizeof (msg_hdr_t) + sizeof (msg_value_t))

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  uint8_t values[3];
} msg_rgb_t;
#define HMTL_MSG_RGB_LEN (sizeof (msg_hdr_t) + sizeof (msg_rgb_t))

#define MAX_PROGRAM_VAL 32
typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  uint8_t type;
  uint8_t values[MAX_PROGRAM_VAL];
} msg_program_t;
#define HMTL_MSG_PROGRAM_LEN (sizeof (msg_hdr_t) + sizeof (msg_program_t))

/*******************************************************************************
 * Message format for MSG_TYPE_POLL
 */

typedef struct __attribute__((__packed__)) {
  config_hdr_t config;
  uint16_t object_type;
  uint16_t recv_buffer_size;
  uint8_t msg_version;
  uint8_t data[0];
} msg_poll_response_t;
// 23 on every target, because msg_poll_response_t is packed: 8 B msg_hdr_t plus
// a 15 B response (10 B config_hdr_t + 2 + 2 + 1). Before the packing invariant
// above this macro was 23 on AVR and 24 on 32-bit targets, so a peer that
// length-checked a poll response against its own sizeof could reject the other
// side's; it is now safe to use as a cross-platform wire length.
#define HMTL_MSG_POLL_MIN_LEN (sizeof (msg_hdr_t) + sizeof (msg_poll_response_t))

/*******************************************************************************
 * Message format for MSG_TYPE_DUMP_CONFIG
 */

typedef struct __attribute__((__packed__)) {
  uint8_t data[0];
} msg_dumpconfig_response_t;
#define HMTL_MSG_DUMPCONFIG_MIN_LEN (sizeof (msg_hdr_t) + sizeof (msg_dumpconfig_response_t))


/*******************************************************************************
 * Message format for MSG_TYPE_SET_ADDR
 */

typedef struct __attribute__((__packed__)) {
  uint16_t device_id;
  socket_addr_t address;
} msg_set_addr_t;
#define HMTL_MSG_SET_ADDR_LEN (sizeof (msg_hdr_t) + sizeof (msg_set_addr_t))

/*******************************************************************************
 * Message format for MSG_TYPE_SENSOR
 */
// data[0], not data[]: a C99 flexible array member in an otherwise-empty struct
// is rejected by g++ >= 9 ("flexible array member in an otherwise empty
// struct"), which would stop host consumers of this header building on a
// typical Linux box. The GNU zero-length-array form below is accepted
// everywhere, and both spell sizeof == 0, so HMTL_MSG_SENSOR_MIN_LEN and the
// wire layout are unchanged.
typedef struct __attribute__((__packed__)) {
  uint8_t data[0];
} msg_sensor_response_t;
#define HMTL_MSG_SENSOR_MIN_LEN (sizeof (msg_hdr_t) + sizeof (msg_sensor_response_t))

typedef struct __attribute__((__packed__)) {
  uint8_t sensor_type;
  uint8_t data_len;
  uint8_t data[0]; // zero-length array for the same reason as above
} msg_sensor_data_t;

// Sensor types
#define HMTL_SENSOR_SOUND 0x1
#define HMTL_SENSOR_LIGHT 0x2
#define HMTL_SENSOR_POT   0x3

/* This should be the largest individual message object ***********************/
typedef msg_program_t msg_max_t;

/*******************************************************************************
 * Message format for MSG_TYPE_TIMESYNC
 *
 * Moved here from TimeSync.h, which is where HMTL#6 left it: it is wire format,
 * every module handles MSG_TYPE_TIMESYNC, and HMTL_Command_CLI can originate one
 * to any address on the bus. Leaving it in TimeSync.h also put it out of reach
 * of the layout guard, because the native suite substitutes a stub TimeSync.h
 * that never declared the struct at all.
 *
 * timestamp is uint32_t, not `unsigned long`, and the type is the part that
 * actually removes the hazard. `unsigned long` happens to be 4 bytes on both
 * AVR and xtensa, but it is 8 on any LP64 host, so the struct's size depended on
 * the ABI's `long` even after packing. Note the corollary for anyone measuring:
 * host -fpack-struct=1 was NOT a valid AVR proxy for this struct before the type
 * change (it reported 9 where AVR gives 5), and is valid after it.
 */
#define TIMESYNC_SYNC   0x1
#define TIMESYNC_ACK    0x2
#define TIMESYNC_SET    0x3
#define TIMESYNC_RESYNC 0x4
#define TIMESYNC_CHECK  0x5

typedef struct __attribute__((__packed__)) {
  uint8_t sync_phase;
  uint32_t timestamp;
} msg_time_sync_t;
#define HMTL_MSG_TIMESYNC_LEN (sizeof (msg_hdr_t) + sizeof (msg_time_sync_t))

/*******************************************************************************
 * HMTL Programs message formats
 */

// 1 byte value
#define HMTL_PROGRAM_NONE         0x00
#define HMTL_PROGRAM_BLINK        0x01
#define HMTL_PROGRAM_TIMED_CHANGE 0x02
#define HMTL_PROGRAM_LEVEL_VALUE  0x03
#define HMTL_PROGRAM_SOUND_VALUE  0x04
#define HMTL_PROGRAM_FADE         0x05
#define HMTL_PROGRAM_SPARKLE      0x06
#define HMTL_PROGRAM_SOUND_PIXELS 0x07
#define HMTL_PROGRAM_CIRCULAR     0x08
#define HMTL_PROGRAM_SEQUENCE     0x09

#define PROGRAM_SENSOR_DATA       0x10 // Special handler for sensor data messages

#define PROGRAM_BRIGHTNESS        0x30 // One-time only
#define PROGRAM_COLOR             0x31

#endif
