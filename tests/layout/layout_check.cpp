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

// Every struct in HMTLWireFormat.h, by size and field offset. HMTL#6 packed
// those and asserted them in HMTL_Test's host-only suite, so until now nothing
// checked the packing under the two compilers it exists to reconcile.
#include "wire_layout_asserts.h"
