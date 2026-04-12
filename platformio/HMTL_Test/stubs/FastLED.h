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
    // Match real FastLED memory layout: raw[0]=r, raw[1]=g, raw[2]=b.
    // hmtl_set_output_rgb passes .raw directly to setAllRGB(r,g,b), so the
    // order must be RGB, not BGR.
    union {
        struct { uint8_t r, g, b; };
        uint8_t raw[3];
    };
    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    // Simplified HSV→RGB: map value to grey
    CRGB(const CHSV& hsv) : r(hsv.v), g(hsv.v), b(hsv.v) {}

    uint8_t& operator[](uint8_t idx)       { return raw[idx]; }
    uint8_t  operator[](uint8_t idx) const { return raw[idx]; }

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
