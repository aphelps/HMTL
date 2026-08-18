// Minimal EEPromUtils stub (satisfies indirect include from HMTLTypes.cpp path).
#pragma once
#include "Arduino.h"
#include "EEPROM.h"

#define EEPROM_START_BYTE    0xAF
#define EEPROM_WRAPPER_SIZE  3

int eeprom_read_objects(int addr, byte *dest, int len);
int eeprom_write_objects(int addr, byte *src,  int len);

/*
 * In-memory EEPROM emulation (implemented in test_support.cpp) so that
 * HMTLTypes.cpp's config read/write path can run natively.  The wrapper
 * format matches the real library (START, datalen, data..., crc) but the
 * CRC only needs to be self-consistent for the tests.
 */
bool EEPROM_init();
bool EEPROM_commit();
void EEPROM_end();
int  EEPROM_safe_write(int location, uint8_t *data, int datalen);
int  EEPROM_safe_read(int location, uint8_t *buff, int bufflen);
void EEPROM_dump(int location);
void eeprom_stub_reset();          // test hook: zero the backing array
uint8_t *eeprom_stub_raw();        // test hook: inspect raw bytes
