// PixelUtil stub for native (desktop) unit tests.
//
// Stores actual CRGB values in a heap-allocated array and emits debug lines
// on every mutation so tests can assert LED state via the debug log:
//
//   setPixelRGB(3, 0xff, 0x00, 0x00) → "pixel[3]=0xff0000"
//   setAllRGB(0, 0, 0)               → "setAllRGB 0x000000"
//   setRangeRGB([2,4], crgb)         → "setRangeRGB [2+4] 0xRRGGBB"
//   update()                         → "pixels: 0xRRGGBB 0xRRGGBB ..."
//
#pragma once
#include "Arduino.h"
#include "FastLED.h"
#include "Debug.h"

// <stdio.h>/<string.h> rather than <cstdio>/<cstring>: avr-libc ships the C
// headers but not the C++ ones, and tests/layout/ compiles these stubs with
// avr-g++ to check the wire layouts against the AVR ABI. Nothing here uses the
// std:: names.
#include <stdio.h>
#include <string.h>

// Mirror the real PixelUtil.h exactly. This stub previously hard-coded
// uint16_t, which is what the real header gives only under -DBIG_PIXELS; the
// default is uint8_t.
//
// pixel_range_t no longer goes on the wire — hmtl_program_color_t carries a
// fixed-width wire_pixel_range_t, precisely so that no wire layout depends on
// a flag the two ends set independently. Keeping the mirror still matters for
// two reasons: this is the type program_color() narrows INTO before calling
// setRangeRGB, so a stub of the wrong width would hide a truncation the real
// build has; and tests/layout/ compiles these stubs with avr-g++ and asserts
// the width, which is the guard that caught the hard-coded uint16_t.
#ifdef BIG_PIXELS
  #define PIXEL_ADDR_TYPE uint16_t
#else
  #define PIXEL_ADDR_TYPE uint8_t
#endif

typedef struct {
    PIXEL_ADDR_TYPE start;
    PIXEL_ADDR_TYPE length;
} pixel_range_t;

struct PRGB {
    uint16_t pixel;
    uint8_t  red, green, blue;
    CRGB color() const { return CRGB(red, green, blue); }
};

// Inline color component extractors (mirror PixelUtil.cpp)
inline byte     pixel_red(uint32_t c)            { return (c >> 16) & 0xFF; }
inline byte     pixel_green(uint32_t c)          { return (c >> 8)  & 0xFF; }
inline byte     pixel_blue(uint32_t c)           { return  c        & 0xFF; }
inline uint32_t pixel_color(byte r, byte g, byte b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// ---------------------------------------------------------------------------
// CRGB specialisation for _debug_value_emit — prints as 0xRRGGBB
// Placed here because PixelUtil.h is the first header that sees both
// the CRGB definition (FastLED.h) and the template declaration (Debug.h).
// ---------------------------------------------------------------------------
template<>
inline void _debug_value_emit<CRGB>(const char *key, CRGB val, int newline) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s0x%02x%02x%02x", key, val.r, val.g, val.b);
    _debug_emit(buf, newline);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline void _pixel_emit_one(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
    char buf[32];
    snprintf(buf, sizeof(buf), "pixel[%u]=0x%02x%02x%02x", idx, r, g, b);
    _debug_emit(buf, 1);
}

static inline void _pixel_emit_color(const char *label, uint8_t r, uint8_t g, uint8_t b) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s 0x%02x%02x%02x", label, r, g, b);
    _debug_emit(buf, 1);
}

// ---------------------------------------------------------------------------
// PixelUtil stub
// ---------------------------------------------------------------------------

class PixelUtil {
public:
    PixelUtil() : _num(0), _leds(nullptr) {}

    // _num is PIXEL_ADDR_TYPE, matching ArduinoLibs' PixelUtil: the real init()
    // rejects more than 255 pixels unless -DBIG_PIXELS is set, so a wider _num
    // here would let tests assert behaviour in a strip the firmware cannot have.
    //
    // HMTLTypes.cpp's pixel setup path calls init() on a default-constructed
    // instance, so init() must narrow and complain exactly as the constructor
    // does; a bare `_num = n; new CRGB[n]()` would allocate n while _num held
    // the truncated value.
    void init(uint16_t n, uint8_t /*data*/, uint8_t /*clock*/, uint8_t /*order*/ = 0) {
        delete[] _leds;
        _num  = (PIXEL_ADDR_TYPE)n;
        _leds = new CRGB[(PIXEL_ADDR_TYPE)n]();
        if (n > (uint16_t)(PIXEL_ADDR_TYPE)~(PIXEL_ADDR_TYPE)0) {
            fprintf(stderr,
                    "PixelUtil stub: init(%u) exceeds PIXEL_ADDR_TYPE max %u -- "
                    "truncated to %u. The real init() rejects this.\n",
                    (unsigned)n,
                    (unsigned)(uint16_t)(PIXEL_ADDR_TYPE)~(PIXEL_ADDR_TYPE)0,
                    (unsigned)_num);
        }
    }

    // Delegates to init(), mirroring the real library (ArduinoLibs
    // PixelUtil.cpp:27-32). Every test builds its strip through the
    // constructor, so this delegation is what puts init() under test: without
    // it, a mutation inside init() leaves the whole suite green.
    PixelUtil(uint16_t n, uint8_t data, uint8_t clock, uint8_t order = 0)
        : _num(0), _leds(nullptr)
    {
        init(n, data, clock, order);
    }

    ~PixelUtil() { delete[] _leds; }

    uint16_t numPixels() { return _num; }

    void setPixelRGB(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
        if (idx < _num) _leds[idx] = CRGB(r, g, b);
        _pixel_emit_one(idx, r, g, b);
    }

    void setPixelRGB(uint16_t idx, uint32_t color) {
        setPixelRGB(idx, pixel_red(color), pixel_green(color), pixel_blue(color));
    }

    void setPixelRGB(uint16_t idx, CRGB c) {
        setPixelRGB(idx, c.r, c.g, c.b);
    }

    void setPixelRGB(PRGB *rgb) {
        if (rgb->pixel < _num) setPixelRGB(rgb->pixel, rgb->red, rgb->green, rgb->blue);
    }

    void setAllRGB(uint8_t r, uint8_t g, uint8_t b) {
        for (uint16_t i = 0; i < _num; i++) _leds[i] = CRGB(r, g, b);
        _pixel_emit_color("setAllRGB", r, g, b);
    }

    void setAllRGB(uint32_t color) {
        setAllRGB(pixel_red(color), pixel_green(color), pixel_blue(color));
    }

    // Mirrors ArduinoLibs' PixelUtil::setRangeRGB semantics, which this stub
    // previously diverged from in exactly the two cases a range test exercises:
    //
    //   length == 0  -> the WHOLE strip (0.._num), not start.._num. The real
    //                   implementation routes this to setAllRGB, and the
    //                   meaning is load-bearing: HMTLprotocol.py zero-fills
    //                   unspecified program bytes, so a colour program carrying
    //                   only an RGB triple arrives as {0, 0}.
    //   start >= _num -> draws nothing.
    //
    // The old form filled start.._num for length == 0 and clamped start-past-end
    // into a full-strip fill, so a test written against it would have encoded
    // the stub's behaviour as the expectation. program_color() bounds both
    // fields before calling this, so within the domain it permits the two agree
    // — but the stub should not be the thing that makes that true.
    void setRangeRGB(pixel_range_t range, CRGB crgb) {
        if (range.length == 0) {
            setAllRGB(crgb.r, crgb.g, crgb.b);
        } else if (range.start >= _num) {
            // Mirror the real implementation's guard diagnostic so tests can
            // observe that this branch ran (not merely that no pixels changed).
            DEBUG1_VALUELN("setRangeRGB: start past end:", range.start);
        } else {
            uint16_t avail = _num - range.start;
            uint16_t len   = (range.length < avail) ? range.length : avail;
            for (uint16_t i = 0; i < len; i++) _leds[range.start + i] = crgb;
        }

        char label[32];
        snprintf(label, sizeof(label), "setRangeRGB [%u+%u]",
                 (unsigned)range.start, (unsigned)range.length);
        _pixel_emit_color(label, crgb.r, crgb.g, crgb.b);
    }

    // Emit all pixel values as a single "pixels: 0xRRGGBB ..." line.
    void update() {
        _debug_emit("pixels:", 0);
        for (uint16_t i = 0; i < _num; i++) {
            char buf[12];
            snprintf(buf, sizeof(buf), " 0x%02x%02x%02x", _leds[i].r, _leds[i].g, _leds[i].b);
            _debug_emit(buf, 0);
        }
        _debug_emit("", 1);  // newline to complete the line
    }

    // Direct access for test assertions without going through the debug log.
    CRGB getPixel(uint16_t idx) const {
        if (idx < _num) return _leds[idx];
        return CRGB();
    }

private:
    PIXEL_ADDR_TYPE _num;
    CRGB    *_leds;
};
