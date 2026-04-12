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
