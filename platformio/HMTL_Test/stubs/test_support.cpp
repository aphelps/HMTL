/*
 * Global instances and stub implementations needed by the firmware libraries
 * when compiled for native (desktop) testing.
 *
 * This file is pulled into every test binary via src/hmtl_sources.cpp.
 */

#include "Arduino.h"
#include "TimeSync.h"
#include "FastLED.h"
#include "PixelUtil.h"
#include "EEPROM.h"
#include "HMTLTypes.h"
#include "HMTLMessaging.h"
#include "Debug.h"

#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Debug log — in-memory line buffer + log file
// ---------------------------------------------------------------------------

static std::vector<std::string> s_log_lines;
static std::string               s_log_current;
static FILE                     *s_log_file   = NULL;
static std::string               s_log_path   = DEBUG_LOG_PATH;

static void ensure_log_file() {
    if (!s_log_file) {
        // Append so successive test runs accumulate in one file.
        s_log_file = fopen(s_log_path.c_str(), "a");
    }
}

static void write_separator(const char *label) {
    ensure_log_file();
    if (!s_log_file) return;
    fprintf(s_log_file,
            "\n"
            "================================================================\n"
            "  %s\n"
            "================================================================\n",
            label);
    fflush(s_log_file);
}

extern "C" {

// Called in setUp() with Unity.CurrentTestName — writes a section header to
// the file and clears the in-memory buffer for the new test.
void debug_log_begin_test(const char *name) {
    s_log_lines.clear();
    s_log_current.clear();
    write_separator(name ? name : "(unknown test)");
}

// Mid-test reset: clears in-memory buffer so later assertions only see output
// produced after this call.  Writes a brief marker so the file stays readable.
void debug_log_reset() {
    s_log_lines.clear();
    s_log_current.clear();
    ensure_log_file();
    if (s_log_file) {
        fputs("--- reset ---\n", s_log_file);
        fflush(s_log_file);
    }
}

void debug_log_open(const char *path) {
    if (s_log_file) fclose(s_log_file);
    s_log_path = path;
    // Append mode: keep existing content from previous runs.
    s_log_file = fopen(path, "a");
}

void debug_log_close() {
    if (s_log_file) { fclose(s_log_file); s_log_file = NULL; }
}

int debug_log_count() {
    return (int)s_log_lines.size();
}

const char *debug_log_line(int n) {
    if (n < 0 || n >= (int)s_log_lines.size()) return NULL;
    return s_log_lines[n].c_str();
}

int debug_log_contains(const char *substr) {
    for (const std::string &line : s_log_lines) {
        if (line.find(substr) != std::string::npos) return 1;
    }
    // Also check any partial line not yet terminated with a newline
    if (s_log_current.find(substr) != std::string::npos) return 1;
    return 0;
}

void _debug_emit(const char *s, int newline) {
    ensure_log_file();
    s_log_current += s;
    if (s_log_file) fputs(s, s_log_file);
    if (newline) {
        if (s_log_file) { fputc('\n', s_log_file); fflush(s_log_file); }
        s_log_lines.push_back(s_log_current);
        s_log_current.clear();
    }
}

void debug_err_state(int code) {
    char buf[32];
    snprintf(buf, sizeof(buf), "[ERR_STATE:0x%02x]", code);
    _debug_emit(buf, 1);
    // Real implementation loops forever; in tests just record it.
}

void debug_print_memory()                          {}
void print_hex_buffer(const char *, int)           {}

} // extern "C"

// ---------------------------------------------------------------------------
// Globals required by firmware code
// ---------------------------------------------------------------------------

unsigned long _mock_millis = 0;
TimeSync      timesync;
CFastLED      FastLED;
EEPROMClass   EEPROM;
FakeSerial    Serial;

// ---------------------------------------------------------------------------
// HMTLTypes.cpp — only the functions actually called by ProgramManager /
// HMTLPrograms in the tests.
// ---------------------------------------------------------------------------

/* hmtl_set_output_rgb / hmtl_validate_header / hmtl_output_size are no longer
 * stubbed here: the real HMTLTypes.cpp is compiled into the native build (it
 * is what the config regression tests exercise). */

// ---------------------------------------------------------------------------
// HMTLMessaging.cpp — hmtl_msg_fmt is called by program_*_fmt functions
// ---------------------------------------------------------------------------

void hmtl_msg_fmt(msg_hdr_t *msg_hdr, socket_addr_t address,
                  uint8_t length, uint8_t type, uint8_t flags) {
    msg_hdr->startcode = HMTL_MSG_START;
    msg_hdr->crc       = 0;
    msg_hdr->version   = HMTL_MSG_VERSION;
    msg_hdr->length    = length;
    msg_hdr->type      = type;
    msg_hdr->flags     = flags;
    msg_hdr->address   = address;
}

// ---------------------------------------------------------------------------
// GeneralUtils stubs
// ---------------------------------------------------------------------------

void    blink_value(int, int, int, int)         {}
boolean pin_is_PWM(int)                         { return false; }
void    print_hex_string(const byte *, int)     {}

// ---------------------------------------------------------------------------
// EEPromUtils stubs (symbols referenced in some HMTLTypes paths)
// ---------------------------------------------------------------------------

int eeprom_read_objects(int, byte *, int)  { return -1; }
int eeprom_write_objects(int, byte *, int) { return -1; }

/* ---- In-memory EEPROM (see EEPromUtils.h stub) ---------------------------- */
#include "EEPromUtils.h"

#define EEPROM_STUB_SIZE 4096
static uint8_t eeprom_stub_mem[EEPROM_STUB_SIZE];

void eeprom_stub_reset() { memset(eeprom_stub_mem, 0, sizeof(eeprom_stub_mem)); }
uint8_t *eeprom_stub_raw() { return eeprom_stub_mem; }

bool EEPROM_init()   { return true; }
bool EEPROM_commit() { return true; }
void EEPROM_end()    {}
void EEPROM_dump(int location) { (void)location; }

static uint8_t eeprom_stub_crc(const uint8_t *data, int len) {
  uint8_t crc = 0x5A;
  for (int i = 0; i < len; i++) crc = (uint8_t)((crc << 1) ^ data[i] ^ (crc >> 7));
  return crc;
}

int EEPROM_safe_write(int location, uint8_t *data, int datalen) {
  /* Mirrors the real library's bounds behaviour, including that a negative
   * datalen historically slipped straight through -- the guard against that
   * lives (now) in hmtl_write_config, which is what the config tests pin. */
  if (datalen < 0) return -1;  /* stub refuses; real lib wrote a poisoned record */
  if (location + datalen + EEPROM_WRAPPER_SIZE > EEPROM_STUB_SIZE) return -1;
  eeprom_stub_mem[location++] = EEPROM_START_BYTE;
  eeprom_stub_mem[location++] = (uint8_t)datalen;
  for (int i = 0; i < datalen; i++) eeprom_stub_mem[location++] = data[i];
  eeprom_stub_mem[location++] = eeprom_stub_crc(data, datalen);
  return location;
}

int EEPROM_safe_read(int location, uint8_t *buff, int bufflen) {
  /* Error codes mirror the real EEPromUtils.cpp so tests cannot come to
   * depend on stub-only values: -1 not-START, -2 bad length/bounds, -3 CRC,
   * -4 exceeds device end. */
  if (location + EEPROM_WRAPPER_SIZE > EEPROM_STUB_SIZE) return -4;
  if (eeprom_stub_mem[location] != EEPROM_START_BYTE) return -1;
  location++;
  int datalen = eeprom_stub_mem[location++];
  if (datalen > bufflen) return -2;
  if (location + datalen + 1 > EEPROM_STUB_SIZE) return -4;
  for (int i = 0; i < datalen; i++) buff[i] = eeprom_stub_mem[location++];
  if (eeprom_stub_mem[location++] != eeprom_stub_crc(buff, datalen)) return -3;
  return location;
}

