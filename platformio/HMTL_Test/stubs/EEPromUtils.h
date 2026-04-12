// Minimal EEPromUtils stub (satisfies indirect include from HMTLTypes.cpp path).
#pragma once
#include "Arduino.h"
#include "EEPROM.h"

#define EEPROM_START_BYTE    0xAF
#define EEPROM_WRAPPER_SIZE  3
#define EEPROM_ERROR_END_EXCEEDED  -1
#define EEPROM_ERROR_CRC_MISMATCH  -2

int eeprom_read_objects(int addr, byte *dest, int len);
int eeprom_write_objects(int addr, byte *src,  int len);
