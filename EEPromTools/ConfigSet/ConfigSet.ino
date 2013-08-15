/*
 * Write out an HTML config if it doesn't already exist
 */
#include "EEPROM.h"
#include <RS485_non_blocking.h>
#include <SoftwareSerial.h>

#include "SPI.h"
#include "Adafruit_WS2801.h"

#define DEBUG_LEVEL DEBUG_HIGH
#include "Debug.h"

#include "GeneralUtils.h"
#include "EEPromUtils.h"
#include "HMTLTypes.h"
#include "PixelUtil.h"

boolean wrote_config = false;

#define PIN_DEBUG_LED 13

#define MAX_OUTPUTS 3

config_hdr_t config;
output_hdr_t *outputs[MAX_OUTPUTS];

config_value_t val_output;
config_rgb_t rgb_output, rgb_output2;
config_pixels_t pixel_output;
boolean force_write = false; // XXX - Should not be enabled except for debugging

// Initialize from a 2D array [type][pin pin pin val val val], etc

void config_init() 
{
  int out = 0;

  val_output.hdr.type = HMTL_OUTPUT_VALUE;
  val_output.hdr.output = 0;
  val_output.pin = 11;
  val_output.value = 0;
  outputs[out] = &val_output.hdr;   out++;

  rgb_output.hdr.type = HMTL_OUTPUT_RGB;
  rgb_output.hdr.output = 0;
  rgb_output.pins[0] = 6;
  rgb_output.pins[1] = 9;
  rgb_output.pins[2] = 10;
  rgb_output.values[0] = 128;
  rgb_output.values[1] = 0;
  rgb_output.values[2] = 0;
  outputs[out] = &rgb_output.hdr;   out++;

  rgb_output2.hdr.type = HMTL_OUTPUT_RGB;
  rgb_output2.hdr.output = 1;
  rgb_output2.pins[0] = 9;
  rgb_output2.pins[1] = 10;
  rgb_output2.pins[2] = 11;
  rgb_output2.values[0] = 0;
  rgb_output2.values[1] = 128;
  rgb_output2.values[2] = 0;
//  outputs[out] = &rgb_output2.hdr;  out++;

  pixel_output.hdr.type = HMTL_OUTPUT_PIXELS;
  pixel_output.hdr.output = 2;
  pixel_output.clockPin = 12;
  pixel_output.dataPin = 8;
  pixel_output.numPixels = 50;
//  outputs[out] = &pixel_output.hdr; out++;

  hmtl_default_config(&config);
  config.address = 0;
  config.num_outputs = out;
}


config_hdr_t readconfig;
config_max_t readoutputs[MAX_OUTPUTS];

void setup() 
{
  config_init();
  
  Serial.begin(9600);

  readconfig.address = -1;
  if ((hmtl_read_config(&readconfig, readoutputs, MAX_OUTPUTS) < 0) ||
      (readconfig.address != config.address) ||
      force_write) {
    if (hmtl_write_config(&config, outputs) < 0) {
      DEBUG_PRINTLN(0, "Failed to write config");
    } else {
      wrote_config = true;
    }
  } else {
    memcpy(&config, &readconfig, sizeof (config_hdr_t));
    for (int i = 0; i < config.num_outputs; i++) {

//      uint8_t *buff = (uint8_t *)&readoutputs[i];
//      DEBUG_VALUE(DEBUG_HIGH, " data=", (int)buff);
//      for (uint8_t j = 0; j < sizeof(readoutputs[i]); j++) {
//        DEBUG_HEXVAL(DEBUG_HIGH, " ", buff[j]);
//      }

      if (i >= MAX_OUTPUTS) {
        DEBUG_VALUELN(0, "Too many outputs:", config.num_outputs);
        return;
      }
      outputs[i] = (output_hdr_t *)&readoutputs[i];
    }
  }

  pinMode(PIN_DEBUG_LED, OUTPUT);
}

boolean output_data = false;
void loop() 
{
  if (!output_data) {
    DEBUG_VALUE(0, "My address:", config.address);
    DEBUG_VALUELN(0, " Was address written: ", wrote_config);
    hmtl_print_config(&config, outputs);
    output_data = true;
  }

  blink_value(PIN_DEBUG_LED, config.address, 250, 4);
  delay(10);
}