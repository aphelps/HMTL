// Mock TimeSync — delegates to the controllable _mock_millis clock.
#pragma once
#include "Arduino.h"

class TimeSync {
public:
    unsigned long ms() { return millis(); }
    unsigned long s()  { return millis() / 1000; }
    void update() {}
    void setPeriod(unsigned long) {}
};

extern TimeSync timesync;
