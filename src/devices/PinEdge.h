#pragma once
// A level change on a digital pin of a device - what a microcontroller's pin-change interrupt sees:
// when it happened (whole nanoseconds of the devices' clock), on which pin
// ('A', 'B', 'Z' of an encoder; 'T' trigger, 'E' echo of a sonar) and the level after it.
#include <cstdint>

namespace ke {

struct PinEdge {
    int64_t timeNs = 0;
    char pin = 'A';
    bool level = false;
};

} // namespace ke
