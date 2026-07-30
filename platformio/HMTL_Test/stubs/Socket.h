// Desktop stub for ArduinoLibs' Socket.h.
//
// The real header lives in the sibling repository aphelps/ArduinoLibs
// (Socket/Socket.h). Before this file existed, [env:native] resolved it through
// a hardcoded absolute -I into one developer's Arduino libraries directory, so
// `pio test -e native` could not run on a fresh clone — which matters now that
// the native suite is the acceptance check for HMTLprotocol/HMTLWireFormat.h,
// and HMTLWireFormat.h needs socket_addr_t.
//
// Mirrors the real declaration; it is only a stub in the sense that it is
// vendored, like stubs/Arduino.h and stubs/RS485Utils.h next to it. The two must
// agree on socket_addr_t's width, because it is msg_hdr_t.address on the wire —
// test/test_wire_format asserts sizeof(msg_hdr_t) == 8 with address at offset 6,
// so a divergence fails the suite rather than the bus.
#pragma once
#include <stdint.h>
#include "Arduino.h"   // byte / boolean, which the real header assumes are in scope

typedef uint16_t socket_addr_t;
#define SOCKET_ADDR_ANY (socket_addr_t)-1
#define SOCKET_ADDR_INVALID (socket_addr_t)-2

class Socket {
 public:

  virtual void setup();

  virtual boolean initialized();

  virtual byte * initBuffer(byte * data, uint16_t data_size);

  virtual void sendMsgTo(uint16_t address, const byte * data, const byte length);
  virtual const byte *getMsg(unsigned int *retlen);
  virtual const byte *getMsg(uint16_t address, unsigned int *retlen);

  virtual byte getLength();
  virtual void *headerFromData(const void *data);
  virtual socket_addr_t sourceFromData(void *data);
  virtual socket_addr_t destFromData(void *data);

  byte recvLimit;
  uint16_t sourceAddress;
  byte *send_buffer;
  uint16_t send_data_size;
};

/*
 * Determine if two addresses are considered "matching" for purpose of
 * retrieving messages.  If they match or either is the broadcast address
 * then they "match".
 */
#define SOCKET_ADDRESS_MATCH(x, y) (                                    \
                                    (x == SOCKET_ADDR_ANY) ||           \
                                    (y == SOCKET_ADDR_ANY) ||           \
                                    (x == y)                            \
                                   )
