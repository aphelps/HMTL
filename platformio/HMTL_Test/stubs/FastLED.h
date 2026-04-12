// Minimal FastLED stub — provides CRGB, CHSV, blend(), fract8, CFastLED.
// Only what HMTLPrograms.cpp actually uses.
#pragma once
#include <stdint.h>

typedef uint8_t fract8;

struct CHSV {
    uint8_t h, s, v;
    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t h, uint8_t s, uint8_t v) : h(h), s(s), v(v) {}
};

struct CRGB {
    union {
        struct { uint8_t b, g, r; };
        uint8_t raw[3];
    };
    CRGB() : b(0), g(0), r(0) {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : b(b), g(g), r(r) {}
    // Simplified HSV→RGB: just grey
    CRGB(const CHSV& hsv) : b(hsv.v), g(hsv.v), r(hsv.v) {}

    CRGB& nscale8(uint8_t scale) {
        r = (uint8_t)((uint16_t)r * scale / 255);
        g = (uint8_t)((uint16_t)g * scale / 255);
        b = (uint8_t)((uint16_t)b * scale / 255);
        return *this;
    }
};

inline CRGB blend(const CRGB& a, const CRGB& b, fract8 frac) {
    CRGB result;
    result.r = (uint8_t)((b.r * frac + a.r * (255 - frac)) / 255);
    result.g = (uint8_t)((b.g * frac + a.g * (255 - frac)) / 255);
    result.b = (uint8_t)((b.b * frac + a.b * (255 - frac)) / 255);
    return result;
}

class CFastLED {
public:
    void setBrightness(uint8_t) {}
    void show() {}
};
extern CFastLED FastLED;
