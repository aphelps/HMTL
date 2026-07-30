/*
 * This header contains data types for HMTL modules
 */
#ifndef HMTLTYPES_H
#define HMTLTYPES_H

/* The on-the-wire / in-EEPROM declarations (config_hdr_*, output_hdr_t,
 * HMTL_OUTPUT_*, the baud macros) live in HMTLprotocol so that consumers which
 * only speak the protocol do not have to pull in the module runtime below. */
#include "HMTLWireFormat.h"

/******************************************************************************
 * Library options
 */

//#define DISABLE_PIXELUTIL
#ifndef DISABLE_PIXELUTIL
#define USE_PIXELUTIL
#endif

//#define DISABLE_RS485
#ifndef DISABLE_RS485
#define USE_RS485
#endif

//#define DISABLE_MPR121
#ifndef DISABLE_MPR121
#define USE_MPR121
#endif

//#define DISABLE_XBEE
#ifndef DISABLE_XBEE
#define USE_XBEE
#endif

/******************************************************************************
 * Module configuration
 */

#define HMTL_MAX_OUTPUTS 8 // The maximum number of outputs for a module

#define HMTL_CONFIG_ADDR  0x0E

/* HMTL_CONFIG_MAGIC / HMTL_CONFIG_VERSION, config_hdr_v1_t..v3_t, config_hdr_t,
 * HMTL_NO_ADDRESS, BYTE_TO_BAUD / BAUD_TO_BYTE, HMTL_OUTPUT_*, HMTL_FLAG_*,
 * HMTL_NO_OUTPUT / HMTL_ALL_OUTPUTS and output_hdr_t are in HMTLWireFormat.h,
 * included above. */

// Max pin value

// TODO: This should be contingent on board type
#define MAX_PIN_NUM 40

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  byte pin;
  uint16_t value : 13;  // 13 bits provide values up to 8192
  uint16_t flags :  3;
} config_value_t;

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  byte pins[3];
  byte values[3];
} config_rgb_t;

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  byte clockPin;
  byte dataPin;
  uint16_t numPixels;
  byte type;
} config_pixels_t;

// This should be MPR121::MAX_SENSORS, but we don't want to include that here
#define MAX_MPR121_PINS 12
typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  //  byte address; // TODO: Requires version update BS
  byte irqPin;
  boolean useInterrupt;
  byte thresholds[MAX_MPR121_PINS];
} config_mpr121_t;

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  byte recvPin;
  byte xmitPin;
  byte enablePin;
  //  byte bufferSize; // TODO: Need to handle this
} config_rs485_t;

typedef struct __attribute__((__packed__)) {
  output_hdr_t hdr;
  byte recvPin; // TODO: Use for SoftwareSerial connection
  byte xmitPin;
} config_xbee_t;

typedef config_mpr121_t config_max_t; // Set to the largest output structure

/* Dump the entire raw configuration to serial */
void hmtl_dump_config();

int hmtl_read_config(config_hdr_t *hdr, config_max_t outputs[],
                     int max_outputs);

int32_t hmtl_setup(config_hdr_t *config, 
                   config_max_t readoutputs[], output_hdr_t *outputs[], 
                   void *objects[], byte num_outputs, 
                   void *rs485, void *xbee, void *pixels, void *mpr121,
                   config_rgb_t *rgb_output, config_value_t *value_output,
                   int *configOffset);

int hmtl_write_config(config_hdr_t *hdr, output_hdr_t *outputs[]);
int hmtl_setup_output(config_hdr_t *config, output_hdr_t *hdr, void *data);
int hmtl_update_output(output_hdr_t *hdr, void *data);

// Set an output to a 3byte value
void hmtl_set_output_rgb(output_hdr_t *output, void *object, uint8_t value[3]);


/* Configuration validation */
boolean hmtl_validate_header(config_hdr_t *config_hdr);
boolean hmtl_validate_value(config_value_t *val);
boolean hmtl_validate_rgb(config_rgb_t *rgb);
boolean hmtl_validate_pixels(config_pixels_t *pixels);
boolean hmtl_validate_mpr121(config_mpr121_t *mpr121);
boolean hmtl_validate_rs485(config_rs485_t *rs485);
boolean hmtl_validate_xbee(config_xbee_t *xbee);
boolean hmtl_validate_config(config_hdr_t *config_hdr, output_hdr_t *outputs[],
                             int num_outputs);

/* Debug printing of configuration */
void hmtl_print_config(config_hdr_t *hdr, output_hdr_t *outputs[]);
void hmtl_print_header(config_hdr_t *hdr);
void hmtl_print_output(output_hdr_t *val);

#endif
