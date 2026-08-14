/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2014
 *
 * Program modules that can be executed on HTML Modules
 ******************************************************************************/

#ifndef HMTLPROGRAMS_H
#define HMTLPROGRAMS_H

#ifndef DISABLE_PIXELUTIL
#include "FastLED.h"
#include "PixelUtil.h"
#endif
#include "ProgramManager.h"

/* The HMTL_PROGRAM_* / PROGRAM_* codes travel in msg_program_t.type, so they
 * live with the rest of the wire format in HMTLWireFormat.h. */
#include "HMTLWireFormat.h"

#include <stddef.h> // offsetof, for the layout guard at the end of this header

/*******************************************************************************
 * INVARIANT: every hmtl_program_*_t below is __attribute__((__packed__)).
 * -----------------------------------------------------------------------
 * These are PROGRAM payloads: they are memcpy'd straight out of a received
 * frame and cast in place (HMTLPrograms.cpp), so an ATMega328 module and an
 * ESP32 module must agree on their size and field offsets byte for byte. The
 * structs in HMTLWireFormat.h were packed in HMTL#6; these were deliberately
 * left out to keep that change reviewable, and they have the same defect.
 *
 * Two of them moved a field, which is a silent misparse rather than a length
 * mismatch:
 *
 *   hmtl_program_blink_t  10 B on AVR (off_period at 5)
 *                         12 B on 32-bit (off_period at 6) - INTERIOR padding
 *   hmtl_program_color_t   7 B on AVR under -DBIG_PIXELS (range at 3)
 *                          8 B on 32-bit (range at 4)      - INTERIOR padding
 *
 * hmtl_program_color_t is now 7 B with range at 3 EVERYWHERE, on both settings
 * of -DBIG_PIXELS, because its range no longer borrows the flag-dependent
 * pixel_range_t; see wire_pixel_range_t below. The two rows above are what it
 * was, kept because the -DBIG_PIXELS row is the reason tests/layout/ sweeps
 * that flag as a second axis.
 *
 * and four more differed in size only, which still matters because sizeof feeds
 * the length a peer checks: timed_change 10/12, fade 11/12, sparkle 13/14,
 * circular 9/10. sequence (32) and brightness (1) never diverged; they are
 * packed and asserted anyway so the invariant is uniform.
 *
 * As in HMTLWireFormat.h, packing is layout-neutral on AVR - verified, not
 * assumed: every AVR size and offset above is byte-identical packed and
 * unpacked, so the deployed fleet sees no change - and corrective everywhere
 * else. An independent implementation already agrees the AVR layout is the wire
 * truth: python/hmtl/HMTLprotocol.py encodes the packed forms
 * (MSG_PROGRAM_BLINK_FMT = '<HBBBHBBB', fade 11 B, sparkle 13 B, circular 9 B).
 *
 * The state_* structs are NOT packed and must not be: they are runtime state,
 * never on the wire, and packing them would cost unaligned access for nothing.
 * HMTLPrograms.cpp only ever memcpy's the embedded `msg` member.
 *
 * EXPECTED WARNING, do not "fix" it by deleting the attribute: both avr-g++ and
 * xtensa-esp32-elf-g++ emit, under -Wall,
 *   warning: ignoring packed attribute because of unpacked non-POD field
 *            'CRGB ...::start_value'
 * once for each CRGB member (fade x2, sparkle, color, circular). GCC declines to
 * pack the CRGB *field*; the struct still comes out packed only because CRGB is
 * itself sizeof 3 / alignof 1. That is a FastLED property nothing else checks,
 * so it is pinned by static_assert below alongside the layouts.
 ******************************************************************************/

/* Intialize the program header */
void hmtl_program_fmt(msg_program_t *msg_program, uint8_t output,
                      uint8_t program, uint16_t buffsize);


/*
 * Program to blink between two colors
 */
typedef struct __attribute__((__packed__)) {
  uint16_t on_period;
  uint8_t on_value[3];
  uint16_t off_period;
  uint8_t off_value[3];
} hmtl_program_blink_t;
uint16_t hmtl_program_blink_fmt(byte *buffer, uint16_t buffsize,
				uint16_t address, uint8_t output,
				uint16_t on_period,
				uint32_t on_color,
				uint16_t off_period,
				uint32_t off_color);
void hmtl_send_blink(Socket *socket, byte *buff, byte buff_len,
		     uint16_t address, uint8_t output,
		     uint16_t on_period, uint32_t on_color,
		     uint16_t off_period, uint32_t off_color);

boolean program_blink_init(msg_program_t *msg, program_tracker_t *tracker,
                           output_hdr_t *output, void *object,
                           ProgramManager *manager);
boolean program_blink(output_hdr_t *output, void *object,
                      program_tracker_t *tracker);

typedef struct {
  hmtl_program_blink_t msg;
  boolean on;
  unsigned long next_change;
} state_blink_t;


/*
 * Program which sets a color, waits, and sets another color
 */
typedef struct __attribute__((__packed__)) {
  uint32_t change_period;
  uint8_t start_value[3];
  uint8_t stop_value[3];
} hmtl_program_timed_change_t;
uint16_t hmtl_program_timed_change_fmt(byte *buffer, uint16_t buffsize,
				       uint16_t address, uint8_t output,
				       uint32_t change_period,
				       uint32_t start_color,
				       uint32_t stop_color);
void hmtl_send_timed_change(Socket *socket, byte *buff, byte buff_len,
			    uint16_t address, uint8_t output,
			    uint32_t change_period,
			    uint32_t start_color,
			    uint32_t stop_color);

typedef struct {
  hmtl_program_timed_change_t msg;
  unsigned long change_time;
} state_timed_change_t;

boolean program_timed_change_init(msg_program_t *msg,
                                  program_tracker_t *tracker,
                                  output_hdr_t *output, void *object,
                                  ProgramManager *manager);
boolean program_timed_change(output_hdr_t *output, void *object,
                             program_tracker_t *tracker);

#ifndef DISABLE_PIXELUTIL
/*
 * Program which sets a color and fades to another over a set period
 */
typedef struct __attribute__((__packed__)) {
  uint32_t period;         //  4B
  CRGB start_value;        //  3B
  CRGB stop_value;         //  3B
  uint8_t flags;           //  1B
} hmtl_program_fade_t;     // 11B
#define HMTL_FADE_FLAG_CYCLE 0x1 // Fade reverses when completed
uint16_t hmtl_program_fade_fmt(byte *buffer, uint16_t buffsize,
                               uint16_t address, uint8_t output,
                               uint32_t period,
                               CRGB start_color,
                               CRGB stop_color,
                               uint8_t flags);
boolean program_fade_init(msg_program_t *msg, program_tracker_t *tracker,
                          output_hdr_t *output, void *object,
                          ProgramManager *manager);
boolean program_fade(output_hdr_t *output, void *object,
                     program_tracker_t *tracker);

typedef struct {
  hmtl_program_fade_t msg;
  unsigned long start_time;
} state_fade_t;


/*
 * Program which generates a randomized sparkle pattern
 */
typedef struct __attribute__((__packed__)) {
  uint16_t period;        //  2B
  CRGB bgColor;           //  3B
  byte sparkle_threshold; //  1B Percentage of pixels to change each iteration
  byte bg_threshold;      //  1B Percentage of pixels to leave as background
  byte hue_min;           //  1B
  byte hue_max;           //  1B
  byte sat_min;           //  1B
  byte sat_max;           //  1B
  byte val_min;           //  1B
  byte val_max;           //  1B

                          // 13B Total
} hmtl_program_sparkle_t;

typedef struct {
  hmtl_program_sparkle_t msg;
  unsigned long last_change_ms;
} state_sparkle_t;

uint16_t program_sparkle_fmt(byte *buffer, uint16_t buffsize,
                             uint16_t address, uint8_t output,
                             uint32_t period,
                             CRGB bgColor,
                             uint8_t sparkle_threshold,
                             uint8_t bg_threshold,
                             uint8_t hue_min,
                             uint8_t hue_max,
                             uint8_t sat_min,
                             uint8_t sat_max,
                             uint8_t val_min,
                             uint8_t val_max);

boolean program_sparkle_init(msg_program_t *msg, program_tracker_t *tracker,
                          output_hdr_t *output, void *object,
                             ProgramManager *manager);
boolean program_sparkle(output_hdr_t *output, void *object,
                     program_tracker_t *tracker);

/*
 * Program to set the brightness of a pixel output
 */
typedef struct __attribute__((__packed__)) {
  uint8_t value;
} hmtl_program_brightness_t;
uint16_t program_brightness_fmt(byte *buffer, uint16_t buffsize,
                             uint16_t address, uint8_t output,
                             uint8_t value);
boolean program_brightness(msg_program_t *msg, program_tracker_t *tracker,
                           output_hdr_t *output, void *object,
                           ProgramManager *manager);

/*
 * Program that sets the color for a range of pixels
 *
 * The range is wire_pixel_range_t, NOT pixel_range_t, and that distinction is
 * the whole point of this struct's layout. pixel_range_t is two
 * PIXEL_ADDR_TYPE, which PixelUtil.h makes uint8_t by default and uint16_t
 * under -DBIG_PIXELS, so embedding it gave this one struct a wire layout that
 * followed a BUILD FLAG: 5 bytes in one half of the fleet, 7 in the other, and
 * two ATMega328 modules built with different flags already disagreed about it.
 * A wire layout may depend on nothing the two ends configure independently -
 * not the target, not the ABI, and not a -D.
 *
 * uint16_t rather than uint8_t because the builds that set -DBIG_PIXELS are
 * exactly the ones whose strips exceed 255 pixels; narrowing to the default
 * width would have capped range addressing there forever. The cost is that the
 * only in-tree producer, `HMTLClient -P color`, naturally emitted the 5-byte
 * form - see ProgramColor and the -P color warning in python/, which is how
 * that migration is handled.
 *
 * VALIDITY RULE, part of the contract and not just an implementation detail:
 * length == 0 means the WHOLE strip, which makes start meaningless, so a
 * nonzero start with a zero length is REJECTED. Only start == 0 may carry
 * length == 0 - which is what a sender's zero-fill produces for a colour
 * program given nothing but an RGB triple.
 *
 * That rule is what makes a stale 5-byte `-P color -C r,g,b,start,length`
 * refusable at all: those five bytes leave wire bytes 5-6 zero, so the wire
 * length is ALWAYS 0 and the old start lands in the high half of the new one.
 * Rejecting on the pairing catches every such message on every strip length
 * and both -DBIG_PIXELS settings; rejecting on the magnitude of start instead
 * would let start=2560 through on a 3000-pixel module and flood it.
 * python/'s ProgramColor raises on the same pairing, so a sender finds out at
 * the CLI rather than by watching a strip do nothing.
 *
 * program_color() converts to pixel_range_t at the setRangeRGB call, bounding
 * both fields on the way; see HMTLPrograms.cpp.
 *
 * There is deliberately no hmtl_program_color_fmt(), unlike every other
 * program here. Nothing in C originates a COLOR message - HMTL_Command_CLI has
 * no program command at all - so a formatter would be an encoder with no
 * caller and no test, free to drift from the struct exactly as the layout did.
 * The two independent statements of this layout are python/'s ProgramColor and
 * the hand-transcribed bytes in HMTL_Test's test_pixel_programs; add a fmt
 * helper when something needs to send one, and give it a test then.
 */
typedef struct __attribute__((__packed__)) {
  uint16_t start;
  uint16_t length;
} wire_pixel_range_t;

typedef struct __attribute__((__packed__)) {
  CRGB color;
  wire_pixel_range_t range;
} hmtl_program_color_t;
boolean program_color(msg_program_t *msg, program_tracker_t *tracker,
                      output_hdr_t *output, void *object,
                      ProgramManager *manager);

/*
 * Program that sends a pattern on a circular loop of the available LEDs
 */
typedef struct __attribute__((__packed__)) {
  uint16_t period;        // 2B
  uint16_t length;        // 2B
  CRGB bgColor;           // 3B
  uint8_t pattern;        // 1B
  uint8_t flags;          // 1B
} hmtl_program_circular_t;
typedef struct {
  hmtl_program_circular_t msg;
  unsigned long last_change_ms;
  uint16_t current;
  byte color_position;
} state_circular_t;

uint16_t program_circular_fmt(byte *buffer, uint16_t buffsize,
                             uint16_t address, uint8_t output,
                             uint16_t period, uint16_t length, CRGB bgColor,
                             uint8_t pattern, uint8_t flags);
boolean program_circular_init(msg_program_t *msg, program_tracker_t *tracker,
                             output_hdr_t *output, void *object,
                              ProgramManager *manager);
boolean program_circular(output_hdr_t *output, void *object,
                        program_tracker_t *tracker);
#endif /* DISABLE_PIXELUTIL */

/*
 * Program that triggers multiple value-type outputs in sequence
 */
#define HMTL_SEQUENCE_MAX 8
typedef struct __attribute__((__packed__)) {
  uint8_t outputs[HMTL_SEQUENCE_MAX];    //  8B
  uint16_t durations[HMTL_SEQUENCE_MAX]; // 16B
  uint8_t values[HMTL_SEQUENCE_MAX];     //  8B
} hmtl_program_sequence_t;               // 32B
typedef struct {
  hmtl_program_sequence_t msg;  // Copy of message config
  uint8_t current;              // Index of the current active step
  unsigned long next_change;    // ms timestamp when to advance to next step
  output_hdr_t **outputs;       // Pointer to manager's outputs array
  void **objects;               // Pointer to manager's objects array
  uint8_t num_outputs;          // Length of the outputs/objects arrays
} state_sequence_t;

uint16_t program_sequence_fmt(byte *buffer, uint16_t buffsize,
                              uint16_t address);
int program_sequence_add(byte *buffer, uint8_t output, uint16_t duration, uint8_t value,
                         int offset);
boolean program_sequence_init(msg_program_t *msg, program_tracker_t *tracker,
                              output_hdr_t *output, void *object,
                              ProgramManager *manager);
boolean program_sequence(output_hdr_t *output, void *object,
                         program_tracker_t *tracker);

/*******************************************************************************
 * Additional helper messages
 */

// Format a cancel message
uint16_t hmtl_program_cancel_fmt(byte *buffer, uint16_t buffsize,
                                 uint16_t address, uint8_t output);

// Send a request to cancel any program running on an output
void hmtl_send_cancel(Socket *socket, byte *buff, byte buff_len,
                      uint16_t address, uint8_t output);


/*******************************************************************************
 * Cross-ABI layout guard
 *
 * Sizes AND every field offset, because the three interior-padding cases are
 * exactly the ones a size-only assert would miss if a future field were
 * reordered to compensate. These fire wherever the header is compiled, so the
 * sweep is "build this header with each toolchain that matters", which is what
 * tests/layout/ does: host, host -fpack-struct=1, avr-g++ and
 * xtensa-esp32-elf-g++, all four reached by `make test`, with
 * `make test-layout-negative` proving each assert fails the build when broken.
 *
 * Note that the HMTL_Module firmware envs are NOT that check, however much they
 * look like it: their platformio.ini resolves libraries from a machine-local
 * Arduino directory rather than this repo, so `make test-simavr` stays green
 * with a deliberately impossible assert in this file. Verified, not assumed.
 *
 * The numbers below are the AVR layout, which is what is already on the wire.
 ******************************************************************************/
#define HMTL_LAYOUT_SIZE(t, n)   static_assert(sizeof(t) == (n), #t " changed size")
#define HMTL_LAYOUT_OFF(t, f, n) static_assert(offsetof(t, f) == (n), #t "." #f " moved")

// 10 B with off_period at 5. Unpacked this was 12 B with off_period at 6 on
// 32-bit targets: an ESP32 reading an AVR blob took off_period from AVR bytes
// 6-7, i.e. the high byte of off_period plus off_value[0].
HMTL_LAYOUT_SIZE(hmtl_program_blink_t, 10);
HMTL_LAYOUT_OFF(hmtl_program_blink_t, on_period, 0);
HMTL_LAYOUT_OFF(hmtl_program_blink_t, on_value, 2);
HMTL_LAYOUT_OFF(hmtl_program_blink_t, off_period, 5);
HMTL_LAYOUT_OFF(hmtl_program_blink_t, off_value, 7);

// Trailing padding only (was 12 on 32-bit): fields were already right, the
// declared frame length was not.
HMTL_LAYOUT_SIZE(hmtl_program_timed_change_t, 10);
HMTL_LAYOUT_OFF(hmtl_program_timed_change_t, change_period, 0);
HMTL_LAYOUT_OFF(hmtl_program_timed_change_t, start_value, 4);
HMTL_LAYOUT_OFF(hmtl_program_timed_change_t, stop_value, 7);

HMTL_LAYOUT_SIZE(hmtl_program_sequence_t, 32);
HMTL_LAYOUT_OFF(hmtl_program_sequence_t, outputs, 0);
HMTL_LAYOUT_OFF(hmtl_program_sequence_t, durations, 8);
HMTL_LAYOUT_OFF(hmtl_program_sequence_t, values, 24);

#ifndef DISABLE_PIXELUTIL
// The packed layouts above hold only because CRGB is byte-sized and
// byte-aligned - GCC refuses to pack the CRGB members themselves and says so
// (see the EXPECTED WARNING note at the top of this header). Pin the property
// the silence depends on.
static_assert(sizeof(CRGB) == 3, "CRGB must be 3 bytes for the packed wire layout");
static_assert(alignof(CRGB) == 1, "CRGB must be alignment 1; packing is ignored for it");

HMTL_LAYOUT_SIZE(hmtl_program_fade_t, 11);        // trailing pad only (was 12)
HMTL_LAYOUT_OFF(hmtl_program_fade_t, period, 0);
HMTL_LAYOUT_OFF(hmtl_program_fade_t, start_value, 4);
HMTL_LAYOUT_OFF(hmtl_program_fade_t, stop_value, 7);
HMTL_LAYOUT_OFF(hmtl_program_fade_t, flags, 10);

HMTL_LAYOUT_SIZE(hmtl_program_sparkle_t, 13);     // trailing pad only (was 14)
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, period, 0);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, bgColor, 2);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, sparkle_threshold, 5);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, bg_threshold, 6);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, hue_min, 7);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, hue_max, 8);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, sat_min, 9);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, sat_max, 10);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, val_min, 11);
HMTL_LAYOUT_OFF(hmtl_program_sparkle_t, val_max, 12);

HMTL_LAYOUT_SIZE(hmtl_program_brightness_t, 1);   // never diverged
HMTL_LAYOUT_OFF(hmtl_program_brightness_t, value, 0);

HMTL_LAYOUT_SIZE(hmtl_program_circular_t, 9);     // trailing pad only (was 10)
HMTL_LAYOUT_OFF(hmtl_program_circular_t, period, 0);
HMTL_LAYOUT_OFF(hmtl_program_circular_t, length, 2);
HMTL_LAYOUT_OFF(hmtl_program_circular_t, bgColor, 4);
HMTL_LAYOUT_OFF(hmtl_program_circular_t, pattern, 7);
HMTL_LAYOUT_OFF(hmtl_program_circular_t, flags, 8);

/*
 * hmtl_program_color_t used to be asserted as an EXPRESSION,
 * `3 + 2 * sizeof(PIXEL_ADDR_TYPE)`, because its wire width followed
 * -DBIG_PIXELS rather than the target. That form is worth remembering as a
 * cautionary shape: an assert written in terms of a configurable agrees with
 * whatever that configurable happens to be, so it passed happily on both sides
 * of a disagreement it could not see. Now that the wire range is a fixed-width
 * wire_pixel_range_t, the number is a literal like every other struct here, and
 * "the same 7 holds under both flag settings" is the property being checked.
 */
HMTL_LAYOUT_SIZE(wire_pixel_range_t, 4);
HMTL_LAYOUT_OFF(wire_pixel_range_t, start, 0);
HMTL_LAYOUT_OFF(wire_pixel_range_t, length, 2);

HMTL_LAYOUT_SIZE(hmtl_program_color_t, 7);
HMTL_LAYOUT_OFF(hmtl_program_color_t, color, 0);
HMTL_LAYOUT_OFF(hmtl_program_color_t, range, 3);

/*
 * pixel_range_t is no longer on the wire, but it is still what setRangeRGB
 * takes, so program_color() narrows into it and the narrowing has to know how
 * wide the target is. Pinning it also keeps the sweep's -DBIG_PIXELS axis able
 * to fail: hmtl_program_color_t was the only flag-sensitive struct in the
 * guard, and with it pinned every other assert compiles identically under both
 * settings - a second axis that cannot fail differently from the first is the
 * same dead guard this subsystem has now produced three times.
 *
 * Scope, so it is not over-read: tests/layout/ puts -I$(STUBS) on the include
 * path, so within the sweep PixelUtil.h is HMTL_Test's stub, not ArduinoLibs'
 * shipped header (a repo HMTL does not vendor). Against the stub this pins the
 * stub's mirror of the flag, which is a real guard - it once hard-coded
 * uint16_t regardless - and any TU that compiles this header against the real
 * ArduinoLibs copy checks that one instead.
 */
static_assert(sizeof(pixel_range_t) == 2 * sizeof(PIXEL_ADDR_TYPE),
              "pixel_range_t must be two bare PIXEL_ADDR_TYPE with no padding");
#ifdef BIG_PIXELS
static_assert(sizeof(pixel_range_t) == 4, "pixel_range_t is 4 B under -DBIG_PIXELS");
#else
static_assert(sizeof(pixel_range_t) == 2, "pixel_range_t is 2 B by default");
#endif
#endif /* DISABLE_PIXELUTIL */

#undef HMTL_LAYOUT_SIZE
#undef HMTL_LAYOUT_OFF

#endif
