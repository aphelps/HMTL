// Minimal EEPROM stub (required by EEPromUtils.h include chain).
#pragma once
#include <stdint.h>

class EEPROMClass {
public:
    uint8_t  read(int)                  { return 0; }
    void     write(int, uint8_t)        {}
    void     update(int, uint8_t)       {}
    uint8_t& operator[](int)            { static uint8_t dummy = 0; return dummy; }
};
extern EEPROMClass EEPROM;
