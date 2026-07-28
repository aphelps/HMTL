/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2014
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
typedef struct {
  uint8_t     magic;
  uint8_t     version;
  uint8_t     address;
  uint8_t     num_outputs;
  uint8_t     flags;
} config_hdr_v1_t;

typedef struct {
  uint8_t     magic;
  uint8_t     protocol_version;
  uint8_t     hardware_version;
  uint16_t    address;
  uint8_t     reserved;

  uint8_t     num_outputs;
  uint8_t     flags;
} config_hdr_v2_t;

typedef struct {
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
typedef struct {
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

typedef struct {
  output_hdr_t hdr;
  uint16_t value : 13; // 13 bits provide values up to 8192
  uint16_t flags :  3;
} msg_value_t;
#define HMTL_MSG_VALUE_LEN (sizeof (msg_hdr_t) + sizeof (msg_value_t))

typedef struct {
  output_hdr_t hdr;
  uint8_t values[3];
} msg_rgb_t;
#define HMTL_MSG_RGB_LEN (sizeof (msg_hdr_t) + sizeof (msg_rgb_t))

#define MAX_PROGRAM_VAL 32
typedef struct {
  output_hdr_t hdr;
  uint8_t type;
  uint8_t values[MAX_PROGRAM_VAL];
} msg_program_t;
#define HMTL_MSG_PROGRAM_LEN (sizeof (msg_hdr_t) + sizeof (msg_program_t))

/*******************************************************************************
 * Message format for MSG_TYPE_POLL
 */

typedef struct {
  config_hdr_t config;
  uint16_t object_type;
  uint16_t recv_buffer_size;
  uint8_t msg_version;
  uint8_t data[0];
} msg_poll_response_t;
#define HMTL_MSG_POLL_MIN_LEN (sizeof (msg_hdr_t) + sizeof (msg_poll_response_t))

/*******************************************************************************
 * Message format for MSG_TYPE_DUMP_CONFIG
 */

typedef struct {
  uint8_t data[0];
} msg_dumpconfig_response_t;
#define HMTL_MSG_DUMPCONFIG_MIN_LEN (sizeof (msg_hdr_t) + sizeof (msg_dumpconfig_response_t))


/*******************************************************************************
 * Message format for MSG_TYPE_SET_ADDR
 */

typedef struct {
  uint16_t device_id;
  socket_addr_t address;
} msg_set_addr_t;
#define HMTL_MSG_SET_ADDR_LEN (sizeof (msg_hdr_t) + sizeof (msg_set_addr_t))

/*******************************************************************************
 * Message format for MSG_TYPE_SENSOR
 */
typedef struct {
  uint8_t data[];
} msg_sensor_response_t;
#define HMTL_MSG_SENSOR_MIN_LEN (sizeof (msg_hdr_t) + sizeof (msg_sensor_response_t))

typedef struct {
  uint8_t sensor_type;
  uint8_t data_len;
  uint8_t data[];
} msg_sensor_data_t;

// Sensor types
#define HMTL_SENSOR_SOUND 0x1
#define HMTL_SENSOR_LIGHT 0x2
#define HMTL_SENSOR_POT   0x3

/* This should be the largest individual message object ***********************/
typedef msg_program_t msg_max_t;

/*******************************************************************************
 * Message format for MSG_TYPE_TIMESYNC in TimeSync.h
 */

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
