// No-op debug macro stubs.
// Real Debug.h uses Serial and PROGMEM; replace everything with empty expansions.
#pragma once

#define DEBUG_NONE       0
#define DEBUG_ERROR      1
#define DEBUG_LOW        2
#define DEBUG_MID        3
#define DEBUG_HIGH       4
#define DEBUG_TRACE      5
#define DEBUG_ERROR_ONLY (-1)

#ifndef DEBUG_LEVEL
#  define DEBUG_LEVEL 0
#endif

// Core macros — all no-ops
#define DEBUG_ERR(s)
#define DEBUG_ERR_STATE(x)
#define DEBUG_PRINT(s)
#define DEBUG_PRINTLN(s)
#define DEBUG_VALUE(k, v)
#define DEBUG_VALUELN(k, v)
#define DEBUG_COMMAND(level, code)
#define DEBUG_PRINT_END()

// Level-specific macros used by library code
#define DEBUG1_PRINT(s)
#define DEBUG1_PRINTLN(s)
#define DEBUG1_VALUE(k, v)
#define DEBUG1_VALUELN(k, v)
#define DEBUG2_PRINT(s)
#define DEBUG2_PRINTLN(s)
#define DEBUG2_VALUE(k, v)
#define DEBUG2_VALUELN(k, v)
#define DEBUG3_PRINT(s)
#define DEBUG3_PRINTLN(s)
#define DEBUG3_VALUE(k, v)
#define DEBUG3_VALUELN(k, v)
#define DEBUG4_PRINT(s)
#define DEBUG4_PRINTLN(s)
#define DEBUG4_VALUE(k, v)
#define DEBUG4_VALUELN(k, v)
#define DEBUG5_PRINT(s)
#define DEBUG5_PRINTLN(s)
#define DEBUG5_VALUE(k, v)
#define DEBUG5_VALUELN(k, v)

// Hex-value variants used by some library code
#define DEBUG1_HEXVAL(k, v)
#define DEBUG1_HEXVALLN(k, v)
#define DEBUG2_HEXVAL(k, v)
#define DEBUG2_HEXVALLN(k, v)
#define DEBUG3_HEXVAL(k, v)
#define DEBUG3_HEXVALLN(k, v)
#define DEBUG4_HEXVAL(k, v)
#define DEBUG4_HEXVALLN(k, v)
#define DEBUG5_HEXVAL(k, v)
#define DEBUG5_HEXVALLN(k, v)
