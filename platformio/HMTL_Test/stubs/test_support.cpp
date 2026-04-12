/*
 * Global instances and stub implementations needed by the firmware libraries
 * when compiled for native (desktop) testing.
 *
 * This file is pulled into every test binary via src/hmtl_sources.cpp.
 */

#include "Arduino.h"
#include "TimeSync.h"
#include "FastLED.h"
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

void hmtl_set_output_rgb(output_hdr_t *output, void *object, uint8_t value[3]) {
    switch (output->type) {
        case HMTL_OUTPUT_VALUE: {
            config_value_t *val = (config_value_t *)output;
            val->value = value[0];
            break;
        }
        case HMTL_OUTPUT_RGB: {
            config_rgb_t *rgb = (config_rgb_t *)output;
            rgb->values[0] = value[0];
            rgb->values[1] = value[1];
            rgb->values[2] = value[2];
            break;
        }
        default:
            break;
    }
}

boolean hmtl_validate_header(config_hdr_t *hdr) {
    if (hdr->magic != HMTL_CONFIG_MAGIC) return false;
    config_hdr_v3_t *h = (config_hdr_v3_t *)hdr;
    return (hdr->protocol_version == 3) && (h->num_outputs <= HMTL_MAX_OUTPUTS);
}

int hmtl_output_size(output_hdr_t *output) {
    switch (output->type) {
        case HMTL_OUTPUT_VALUE: return sizeof(config_value_t);
        case HMTL_OUTPUT_RGB:   return sizeof(config_rgb_t);
        default:                return -1;
    }
}

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
