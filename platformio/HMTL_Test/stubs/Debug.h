// Debug stub for native (desktop) unit tests.
//
// Instead of writing to Arduino Serial, all debug output is captured in an
// in-memory line buffer and optionally written to a file.  Tests inspect the
// buffer with the debug_log_* API declared below.
//
// The file path defaults to DEBUG_LOG_PATH (/tmp/hmtl_test_debug.log) and is
// opened lazily on the first write.  Call debug_log_open() to redirect it.
//
// Typical test setUp():
//   void setUp() { debug_log_reset(); }
//
// Typical assertion:
//   TEST_ASSERT_TRUE(debug_log_contains("sequence init"));
//
// NOTE: This header is NOT force-included into C files (only Arduino.h is).
// Debug.h is only pulled in by C++ firmware headers, so C++ templates in the
// macros are safe.
#pragma once
#include <stdio.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Level constants (match real Debug.h)
// ---------------------------------------------------------------------------
#define DEBUG_NONE       0
#define DEBUG_ERROR      1
#define DEBUG_LOW        2
#define DEBUG_MID        3
#define DEBUG_HIGH       4
#define DEBUG_TRACE      5
#define DEBUG_ERROR_ONLY (-1)

// Error codes
#define DEBUG_ERR_MALLOC     0x10
#define DEBUG_ERR_UNINIT     0x11
#define DEBUG_ERR_INVALID    0x12
#define DEBUG_ERR_REINIT     0x13
#define DEBUG_ERR_BADPINS    0x14
#define DEBUG_ERR_MEMCORRUPT 0x15

#ifndef DEBUG_LEVEL
#  define DEBUG_LEVEL 0
#endif

// ---------------------------------------------------------------------------
// Log file path — override at compile time with -DDEBUG_LOG_PATH=...
// ---------------------------------------------------------------------------
#ifndef DEBUG_LOG_PATH
#  define DEBUG_LOG_PATH "/tmp/hmtl_test_debug.log"
#endif

// ---------------------------------------------------------------------------
// Test API (C-compatible declarations)
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

// Write a section-header separator to the log file and clear the in-memory
// buffer.  Call in test setUp() with Unity.CurrentTestName so every test's
// output is labelled in the file.
void debug_log_begin_test(const char *name);

// Mid-test reset: clears the in-memory buffer so later assertions only see
// output produced after this call.  Writes a "--- reset ---" marker to the
// file so the context is preserved.
void debug_log_reset(void);

// Redirect the log file (default: DEBUG_LOG_PATH, opened lazily).
void debug_log_open(const char *path);

// Close the log file (it is reopened lazily on next write).
void debug_log_close(void);

// Number of completed (newline-terminated) lines captured so far.
int debug_log_count(void);

// Return completed line n (0-based), or NULL if out of range.
const char *debug_log_line(int n);

// Return 1 if any captured line or the current partial line contains substr.
int debug_log_contains(const char *substr);

// Internal: append s to the current line; flush to a completed line if newline != 0.
void _debug_emit(const char *s, int newline);

// Stubs for symbols declared in real Debug.h
void debug_err_state(int code);
void debug_print_memory(void);
void print_hex_buffer(const char *buff, int length);

#ifdef __cplusplus
} // extern "C"
#endif

// ---------------------------------------------------------------------------
// C++ helpers — format key+value then call _debug_emit (template, header-only)
// ---------------------------------------------------------------------------
#ifdef __cplusplus
#include <sstream>
#include <iomanip>

template<typename T>
inline void _debug_value_emit(const char *key, T val, int newline) {
    std::ostringstream oss;
    // Unary + promotes char/uint8_t to int so it prints as a number, not a raw byte.
    oss << key << +val;
    _debug_emit(oss.str().c_str(), newline);
}

template<typename T>
inline void _debug_hex_emit(const char *key, T val, int newline) {
    std::ostringstream oss;
    oss << key << "0x" << std::hex << static_cast<unsigned long>(val);
    _debug_emit(oss.str().c_str(), newline);
}
#endif /* __cplusplus */

// ---------------------------------------------------------------------------
// Level-gated macros — same structure as real Debug.h
// ---------------------------------------------------------------------------

// ERR fires at level >= 1 or when ERROR_ONLY is set
#if DEBUG_LEVEL >= 1 || DEBUG_LEVEL == DEBUG_ERROR_ONLY
#  define DEBUG_ERR(s)       { _debug_emit("[ERR] " s, 1); }
#  define DEBUG_ERR_STATE(x) { debug_err_state(x); }
#else
#  define DEBUG_ERR(s)
#  define DEBUG_ERR_STATE(x)
#endif

// Active macro bodies are wrapped in { } to match the real Debug.h block style,
// so they remain valid statements whether or not the caller writes a trailing ;
#if DEBUG_LEVEL >= 1
#  define DEBUG1_PRINT(s)        { _debug_emit(s, 0); }
#  define DEBUG1_PRINTLN(s)      { _debug_emit(s, 1); }
#  define DEBUG1_HEX(v)          { _debug_hex_emit("", v, 0); }
#  define DEBUG1_HEXLN(v)        { _debug_hex_emit("", v, 1); }
#  define DEBUG1_VALUE(k, v)     { _debug_value_emit(k, v, 0); }
#  define DEBUG1_VALUELN(k, v)   { _debug_value_emit(k, v, 1); }
#  define DEBUG1_HEXVAL(k, v)    { _debug_hex_emit(k, v, 0); }
#  define DEBUG1_HEXVALLN(k, v)  { _debug_hex_emit(k, v, 1); }
#  define DEBUG1_COMMAND(x)      { x; }
#  define DEBUG_ENDLN()          { _debug_emit("", 1); }
#else
#  define DEBUG1_PRINT(s)
#  define DEBUG1_PRINTLN(s)
#  define DEBUG1_HEX(v)
#  define DEBUG1_HEXLN(v)
#  define DEBUG1_VALUE(k, v)
#  define DEBUG1_VALUELN(k, v)
#  define DEBUG1_HEXVAL(k, v)
#  define DEBUG1_HEXVALLN(k, v)
#  define DEBUG1_COMMAND(x)
#  define DEBUG_ENDLN()
#endif

#if DEBUG_LEVEL >= 2
#  define DEBUG2_PRINT(s)        DEBUG1_PRINT(s)
#  define DEBUG2_PRINTLN(s)      DEBUG1_PRINTLN(s)
#  define DEBUG2_HEX(v)          DEBUG1_HEX(v)
#  define DEBUG2_HEXLN(v)        DEBUG1_HEXLN(v)
#  define DEBUG2_VALUE(k, v)     DEBUG1_VALUE(k, v)
#  define DEBUG2_VALUELN(k, v)   DEBUG1_VALUELN(k, v)
#  define DEBUG2_HEXVAL(k, v)    DEBUG1_HEXVAL(k, v)
#  define DEBUG2_HEXVALLN(k, v)  DEBUG1_HEXVALLN(k, v)
#  define DEBUG2_COMMAND(x)      { x; }
#else
#  define DEBUG2_PRINT(s)
#  define DEBUG2_PRINTLN(s)
#  define DEBUG2_HEX(v)
#  define DEBUG2_HEXLN(v)
#  define DEBUG2_VALUE(k, v)
#  define DEBUG2_VALUELN(k, v)
#  define DEBUG2_HEXVAL(k, v)
#  define DEBUG2_HEXVALLN(k, v)
#  define DEBUG2_COMMAND(x)
#endif

#if DEBUG_LEVEL >= 3
#  define DEBUG3_PRINT(s)        DEBUG1_PRINT(s)
#  define DEBUG3_PRINTLN(s)      DEBUG1_PRINTLN(s)
#  define DEBUG3_HEX(v)          DEBUG1_HEX(v)
#  define DEBUG3_HEXLN(v)        DEBUG1_HEXLN(v)
#  define DEBUG3_VALUE(k, v)     DEBUG1_VALUE(k, v)
#  define DEBUG3_VALUELN(k, v)   DEBUG1_VALUELN(k, v)
#  define DEBUG3_HEXVAL(k, v)    DEBUG1_HEXVAL(k, v)
#  define DEBUG3_HEXVALLN(k, v)  DEBUG1_HEXVALLN(k, v)
#  define DEBUG3_COMMAND(x)      { x; }
#else
#  define DEBUG3_PRINT(s)
#  define DEBUG3_PRINTLN(s)
#  define DEBUG3_HEX(v)
#  define DEBUG3_HEXLN(v)
#  define DEBUG3_VALUE(k, v)
#  define DEBUG3_VALUELN(k, v)
#  define DEBUG3_HEXVAL(k, v)
#  define DEBUG3_HEXVALLN(k, v)
#  define DEBUG3_COMMAND(x)
#endif

#if DEBUG_LEVEL >= 4
#  define DEBUG4_PRINT(s)        DEBUG1_PRINT(s)
#  define DEBUG4_PRINTLN(s)      DEBUG1_PRINTLN(s)
#  define DEBUG4_HEX(v)          DEBUG1_HEX(v)
#  define DEBUG4_HEXLN(v)        DEBUG1_HEXLN(v)
#  define DEBUG4_VALUE(k, v)     DEBUG1_VALUE(k, v)
#  define DEBUG4_VALUELN(k, v)   DEBUG1_VALUELN(k, v)
#  define DEBUG4_HEXVAL(k, v)    DEBUG1_HEXVAL(k, v)
#  define DEBUG4_HEXVALLN(k, v)  DEBUG1_HEXVALLN(k, v)
#  define DEBUG4_COMMAND(x)      { x; }
#else
#  define DEBUG4_PRINT(s)
#  define DEBUG4_PRINTLN(s)
#  define DEBUG4_HEX(v)
#  define DEBUG4_HEXLN(v)
#  define DEBUG4_VALUE(k, v)
#  define DEBUG4_VALUELN(k, v)
#  define DEBUG4_HEXVAL(k, v)
#  define DEBUG4_HEXVALLN(k, v)
#  define DEBUG4_COMMAND(x)
#endif

#if DEBUG_LEVEL >= 5
#  define DEBUG5_PRINT(s)        DEBUG1_PRINT(s)
#  define DEBUG5_PRINTLN(s)      DEBUG1_PRINTLN(s)
#  define DEBUG5_HEX(v)          DEBUG1_HEX(v)
#  define DEBUG5_HEXLN(v)        DEBUG1_HEXLN(v)
#  define DEBUG5_VALUE(k, v)     DEBUG1_VALUE(k, v)
#  define DEBUG5_VALUELN(k, v)   DEBUG1_VALUELN(k, v)
#  define DEBUG5_HEXVAL(k, v)    DEBUG1_HEXVAL(k, v)
#  define DEBUG5_HEXVALLN(k, v)  DEBUG1_HEXVALLN(k, v)
#  define DEBUG5_COMMAND(x)      { x; }
#else
#  define DEBUG5_PRINT(s)
#  define DEBUG5_PRINTLN(s)
#  define DEBUG5_HEX(v)
#  define DEBUG5_HEXLN(v)
#  define DEBUG5_VALUE(k, v)
#  define DEBUG5_VALUELN(k, v)
#  define DEBUG5_HEXVAL(k, v)
#  define DEBUG5_HEXVALLN(k, v)
#  define DEBUG5_COMMAND(x)
#endif

// ---------------------------------------------------------------------------
// Convenience aliases used by some firmware files
// ---------------------------------------------------------------------------
#define DEBUG_PRINT(k, v)    DEBUG1_VALUE(k, v)
#define DEBUG_PRINTLN(k, v)  DEBUG1_VALUELN(k, v)
#define DEBUG_VALUE(k, v)    DEBUG1_VALUE(k, v)
#define DEBUG_VALUELN(k, v)  DEBUG1_VALUELN(k, v)
#define DEBUG_PRINT_END()    // no-op: partial lines flushed by *LN macros
#define DEBUG_COMMAND(v, x)  // legacy two-arg form, unused in modern firmware
#define DEBUG_MEMORY(v)
