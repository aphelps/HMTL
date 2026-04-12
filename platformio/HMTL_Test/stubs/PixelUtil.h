// No-op PixelUtil stub — satisfies #includes in HMTLPrograms.cpp without FastLED hardware.
#pragma once
#include "Arduino.h"
#include "FastLED.h"

typedef uint16_t PIXEL_ADDR_TYPE;

struct pixel_range_t {
    uint16_t start;
    uint16_t length;
};

struct PRGB {
    uint16_t index;
    uint8_t r, g, b;
};

// Inline color component extractors (mirror PixelUtil.cpp implementations)
inline byte pixel_red(uint32_t color)   { return (color >> 16) & 0xFF; }
inline byte pixel_green(uint32_t color) { return (color >> 8)  & 0xFF; }
inline byte pixel_blue(uint32_t color)  { return  color        & 0xFF; }
inline uint32_t pixel_value(byte r, byte g, byte b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

class PixelUtil {
public:
    PixelUtil() : _num(0) {}
    PixelUtil(uint16_t n, uint8_t, uint8_t, uint8_t) : _num(n) {}

    uint16_t numPixels()                             { return _num; }
    void setPixelRGB(uint16_t, uint8_t, uint8_t, uint8_t) {}
    void setPixelRGB(uint16_t, uint32_t)             {}
    void setPixelRGB(uint16_t, CRGB)                 {}
    void setPixelRGB(PRGB*)                          {}
    void setAllRGB(uint8_t, uint8_t, uint8_t)        {}
    void setAllRGB(uint32_t)                         {}
    void setRangeRGB(pixel_range_t, CRGB)            {}
    void update()                                    {}

private:
    uint16_t _num;
};
