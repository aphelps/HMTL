/*******************************************************************************
 * This class performs time synchronization between modules, providing a
 * synchonized clock API.
 *
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2015
 ******************************************************************************/

#ifndef TIMESYNC_H
#define TIMESYNC_H

#include "Socket.h"
#include "HMTLMessaging.h"

class TimeSync {
 public:

  static const byte STATE_IDLE         = 0;
  static const byte STATE_AWAITING_ACK = 1;
  static const byte STATE_AWAITING_SET = 2;
  static const byte STATE_SYNCED       = 3;

  TimeSync();

  /*
   * Return the current time adjusted based on the derived time delta
   */
  unsigned long ms();
  unsigned long s();
  void set(unsigned long time);

  boolean synchronize(Socket *socket,
                      socket_addr_t target,
                      msg_hdr_t *msg_hdr);
  void resynchronize(Socket *socket,
                     socket_addr_t target);
  void check(Socket *socket, socket_addr_t target);

 private:
  unsigned long latency;
  long delta;
  byte state;
  unsigned long last_msg_time;

  void sendSyncMsg(Socket *socket, socket_addr_t target, byte phase, unsigned long adjustment);
};

/*******************************************************************************
 * Message format for MSG_TYPE_TIMESYNC
 *
 * msg_time_sync_t and the TIMESYNC_* phase codes now live in
 * HMTLprotocol/HMTLWireFormat.h with the rest of the wire format, reached from
 * here via HMTLMessaging.h. They are unchanged for every caller except that
 * timestamp is uint32_t rather than `unsigned long` (identical on AVR and
 * xtensa; see the note beside the struct).
 *
 * The layout guard is repeated here rather than living only in
 * platformio/HMTL_Test/test/test_wire_format/, because that suite substitutes
 * stubs/TimeSync.h — which never declared msg_time_sync_t at all, and is how
 * this struct stayed unpacked while the rest of the wire format was fixed.
 * Asserting in the real header is what puts it in front of the toolchains:
 * tests/layout/ compiles this file with host, host -fpack-struct=1, avr-g++ and
 * xtensa-esp32-elf-g++, and `make -C tests/layout negative` proves each assert
 * below fails the build when broken.
 */
#include <stddef.h>
static_assert(sizeof(msg_time_sync_t) == 5,
              "msg_time_sync_t changed size");
static_assert(offsetof(msg_time_sync_t, sync_phase) == 0,
              "msg_time_sync_t.sync_phase moved");
// Unpacked, and with `unsigned long`, this was offset 4 in an 8-byte struct on
// 32-bit targets: an ESP32 read an AVR peer's timestamp three bytes late and off
// the end of the payload, and an AVR read an ESP32's three bytes early.
static_assert(offsetof(msg_time_sync_t, timestamp) == 1,
              "msg_time_sync_t.timestamp moved");
static_assert(HMTL_MSG_TIMESYNC_LEN == 13,
              "TIMESYNC frame length changed");

#endif
