/*
 * Cross-ABI layout check: compile the real wire headers with every toolchain
 * that shares the bus, and let their static_asserts do the work.
 *
 * This exists because nothing else covers all of them. HMTL_Test's native suite
 * runs the host only (and shadows TimeSync.h with a stub that never declared
 * msg_time_sync_t), and the HMTL_Module firmware envs resolve their libraries
 * from a machine-local Arduino directory rather than this repo, so they check
 * whatever happens to be installed there rather than these sources.
 *
 * There is no test body: every assertion is a static_assert inside the headers,
 * next to the struct it constrains, so compiling is the test. See the Makefile
 * in this directory for the toolchain sweep and the negative control.
 */

#include "HMTLWireFormat.h"
#include "HMTLPrograms.h"
#include "TimeSync.h"

// Frame lengths, which are what a peer length-checks against and the reason a
// trailing-padding-only difference still breaks interoperation.
static_assert(HMTL_MSG_VALUE_LEN == 12, "VALUE frame length changed");
static_assert(HMTL_MSG_RGB_LEN == 13, "RGB frame length changed");
static_assert(HMTL_MSG_PROGRAM_LEN == 43, "PROGRAM frame length changed");
static_assert(HMTL_MSG_POLL_MIN_LEN == 23, "POLL frame length changed");
static_assert(HMTL_MSG_SET_ADDR_LEN == 12, "SET_ADDR frame length changed");
static_assert(HMTL_MSG_TIMESYNC_LEN == 13, "TIMESYNC frame length changed");
