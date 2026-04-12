// Desktop shim for Arduino built-in types and functions.
// Provides just enough for HMTLMessaging / HMTLPrograms to compile natively.
//
// NOTE: This header is force-included (-include Arduino.h) into every
// translation unit, including Unity's C files.  Guard all C++-specific
// content with #ifdef __cplusplus so C compilations see only the safe parts.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus

typedef uint8_t  byte;
typedef bool     boolean;

// Controllable mock clock — set _mock_millis to drive time in tests
extern unsigned long _mock_millis;
inline unsigned long millis() { return _mock_millis; }
inline void          delay(unsigned long ms) { _mock_millis += ms; }

// Arduino's random() — rename to avoid conflict with macOS stdlib random(void)
inline long random(long maxval) {
    return maxval > 0 ? (::rand() % maxval) : 0;
}
inline long random(long minval, long maxval) {
    long range = maxval - minval;
    return minval + (range > 0 ? ::rand() % range : 0);
}
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define F(s)             (s)
#define PSTR(s)          (s)

// Serial stub — suppress any Serial.print calls compiled in
struct FakeSerial {
    void begin(unsigned long) {}
    template<typename T> void print(T)   {}
    template<typename T> void println(T) {}
    void flush() {}
};
extern FakeSerial Serial;

#endif /* __cplusplus */
