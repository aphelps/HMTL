// Declarations for GeneralUtils — implementations are in test_support.cpp.
#pragma once
#include "Arduino.h"

void    blink_value(int pin, int value, int period_ms, int idle_periods);
boolean pin_is_PWM(int pin);
void    print_hex_string(const byte *bytes, int len);
