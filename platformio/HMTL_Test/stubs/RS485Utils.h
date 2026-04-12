// Minimal RS485Utils stub — declares RS485Socket so HMTLMessaging.h compiles.
// Actual RS485 functionality is disabled in tests via -DDISABLE_RS485.
#pragma once
#include "Arduino.h"
#include "Socket.h"

class RS485Socket : public Socket {
public:
    void    setup()                                              override {}
    boolean initialized()                                        override { return false; }
    byte*   initBuffer(byte*, uint16_t)                          override { return nullptr; }
    void    sendMsgTo(uint16_t, const byte*, const byte)         override {}
    const byte* getMsg(unsigned int *retlen)                     override { *retlen = 0; return nullptr; }
    const byte* getMsg(uint16_t, unsigned int *retlen)           override { *retlen = 0; return nullptr; }
    byte    getLength()                                          override { return 0; }
    void*   headerFromData(const void*)                          override { return nullptr; }
    socket_addr_t sourceFromData(void*)                          override { return 0; }
    socket_addr_t destFromData(void*)                            override { return 0; }
};
