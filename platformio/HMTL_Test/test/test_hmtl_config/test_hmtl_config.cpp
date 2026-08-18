/*
 * Regression tests for silent EEPROM corruption in hmtl_write_config().
 *
 * The bug: hmtl_output_size() returns -1 for any output type compiled out of
 * the build (this native build sets DISABLE_MPR121/RS485/XBEE), and the write
 * path passed that straight to EEPROM_safe_write() as a length -- emitting a
 * malformed record (START, length byte 0xFF, no data) while reporting
 * success; the corruption only surfaced as a CRC failure on the next read.
 *
 * These tests pin the fix: a config containing an unsizeable output must be
 * refused loudly (-3) before anything is written, and a fully sizeable config
 * must still round-trip byte-for-byte.
 */

#include <unity.h>
#include "Arduino.h"
#include "Debug.h"
#include "EEPromUtils.h"
#include "HMTLTypes.h"

static config_hdr_t hdr;
static config_max_t readback[HMTL_MAX_OUTPUTS];

void setUp() {
  debug_log_begin_test(Unity.CurrentTestName);
  eeprom_stub_reset();
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic = HMTL_CONFIG_MAGIC;
  hdr.protocol_version = HMTL_CONFIG_VERSION;
  hdr.address = 129;
  hdr.device_id = 129;
}

void tearDown() {}

/* A sizeable config (VALUE + RGB are always compiled in) round-trips. */
void test_write_then_read_roundtrip() {
  config_value_t val;
  memset(&val, 0, sizeof(val));
  val.hdr.type = HMTL_OUTPUT_VALUE;
  val.hdr.output = 0;
  val.pin = 9;
  val.value = 0;

  config_rgb_t rgb;
  memset(&rgb, 0, sizeof(rgb));
  rgb.hdr.type = HMTL_OUTPUT_RGB;
  rgb.hdr.output = 1;
  rgb.pins[0] = 11; rgb.pins[1] = 10; rgb.pins[2] = 13;

  output_hdr_t *outputs[2] = { &val.hdr, &rgb.hdr };
  hdr.num_outputs = 2;

  int end = hmtl_write_config(&hdr, outputs);
  TEST_ASSERT_GREATER_THAN(0, end);

  config_hdr_t rhdr;
  int rend = hmtl_read_config(&rhdr, readback, HMTL_MAX_OUTPUTS);
  TEST_ASSERT_EQUAL(end, rend);
  TEST_ASSERT_EQUAL(2, rhdr.num_outputs);
  TEST_ASSERT_EQUAL(129, rhdr.address);
  /* config_max_t is a typedef of the largest record (config_mpr121_t), not a
   * union -- reinterpret by the type each record declares. */
  TEST_ASSERT_EQUAL(HMTL_OUTPUT_VALUE, readback[0].hdr.type);
  TEST_ASSERT_EQUAL(9, ((config_value_t *)&readback[0])->pin);
  TEST_ASSERT_EQUAL(HMTL_OUTPUT_RGB, readback[1].hdr.type);
  TEST_ASSERT_EQUAL(10, ((config_rgb_t *)&readback[1])->pins[1]);
}

/* THE regression: an output whose type is compiled out (mpr121 here, exactly
 * as on the ESP32 config sketch) must make the write fail loudly... */
void test_unsizeable_output_refused() {
  config_value_t val;
  memset(&val, 0, sizeof(val));
  val.hdr.type = HMTL_OUTPUT_VALUE;
  val.pin = 9;

  output_hdr_t mpr;             /* header alone is enough: size lookup is by type */
  memset(&mpr, 0, sizeof(mpr));
  mpr.type = HMTL_OUTPUT_MPR121;

  output_hdr_t *outputs[2] = { &val.hdr, &mpr };
  hdr.num_outputs = 2;

  TEST_ASSERT_EQUAL(-3, hmtl_write_config(&hdr, outputs));
}

/* ...and, now that the refusal pre-scans, EEPROM must be COMPLETELY untouched:
 * the original config still reads back whole, and no poison byte was written. */
void test_refused_write_leaves_prior_config_readable() {
  config_value_t val;
  memset(&val, 0, sizeof(val));
  val.hdr.type = HMTL_OUTPUT_VALUE;
  val.pin = 7;
  output_hdr_t *good[1] = { &val.hdr };
  hdr.num_outputs = 1;
  int good_end = hmtl_write_config(&hdr, good);
  TEST_ASSERT_GREATER_THAN(0, good_end);

  /* Snapshot the byte just past the good config: the old bug's poison record
   * (START, 0xFF, crc) would land exactly here. */
  uint8_t *raw = eeprom_stub_raw();
  TEST_ASSERT_EQUAL_HEX8(0x00, raw[good_end]);

  /* Attempt a write containing an unsizeable output */
  output_hdr_t mpr;
  memset(&mpr, 0, sizeof(mpr));
  mpr.type = HMTL_OUTPUT_MPR121;
  output_hdr_t *bad[2] = { &val.hdr, &mpr };
  hdr.num_outputs = 2;
  TEST_ASSERT_EQUAL(-3, hmtl_write_config(&hdr, bad));

  /* Atomic refusal: the ORIGINAL one-output config reads back whole.  Under
   * the pre-fix code this fails two ways -- the header would have been
   * rewritten with num_outputs=2, and a poison record would sit at good_end. */
  config_hdr_t rhdr;
  int rend = hmtl_read_config(&rhdr, readback, HMTL_MAX_OUTPUTS);
  TEST_ASSERT_EQUAL(good_end, rend);
  TEST_ASSERT_EQUAL(1, rhdr.num_outputs);
  TEST_ASSERT_EQUAL(HMTL_OUTPUT_VALUE, readback[0].hdr.type);
  TEST_ASSERT_EQUAL(7, ((config_value_t *)&readback[0])->pin);
  TEST_ASSERT_EQUAL_HEX8(0x00, raw[good_end]);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_write_then_read_roundtrip);
  RUN_TEST(test_unsizeable_output_refused);
  RUN_TEST(test_refused_write_leaves_prior_config_readable);
  return UNITY_END();
}
