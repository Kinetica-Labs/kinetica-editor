#pragma once
// An ultrasonic range finder with a TRIG / ECHO pulse interface, as its datasheet describes it (SonarSpec; the numbers
// of a given model come from its row of data/sensors/sonar.csv, scene/SensorSpec.h): a 10 us pulse on TRIG starts a
// measurement; the module sends a burst and then holds ECHO high for the sound's round trip to the nearest echo, 2 D / c
// - so a host's distance is the pulse's width times c / 2. Range min to max over a beam of the stated full angle, the
// stated accuracy taken as 3 sigma of a normal jitter of the trip when `noise`, a cycle between triggers. With no echo
// in range ECHO falls after the timeout.
// Where the echo comes from is the physics' - the probe of the running world (sim/LiveSensors.cpp) casts the beam.
//
// The speed of sound in air, c = 331.3 sqrt(1 + T / 273.15) m/s (343.2 at 20 deg C).
#include "core/Random.h"
#include "devices/PinEdge.h"
#include "math/Real.h"

#include <vector>

namespace ke {

struct SonarSpec {
    double minM = 0.02, maxM = 4.0;
    double beamDeg = 15;             // the full angle of the cone
    double accuracyMm = 3;           // of the range (3 sigma)
    double cycleMs = 60, timeoutMs = 38;
};

class Sonar {
public:
    static constexpr int64_t kTriggerNs = 10000; // TRIG high
    static constexpr int64_t kBurstNs = 200000;  // the burst (8 cycles of 40 kHz), then ECHO rises

    explicit Sonar(const SonarSpec& spec = SonarSpec(), uint32_t seed = 1, bool noise = false) : spec_(spec), random_(seed), noise_(noise) {}

    // One measurement, the trigger rising at t, of an echo from `range` (< 0: none): the TRIG and ECHO edges into
    // `out`; returns the distance a host reads from the pulse at the speed of sound in air at `celsius` (-1: the echo
    // timed out).
    double measure(int64_t t, double range, double celsius, std::vector<PinEdge>& out);
    int64_t cycleNs() const { return int64_t(spec_.cycleMs * 1e6); }
    const SonarSpec& spec() const { return spec_; }
    static double soundSpeed(double celsius);

private:
    SonarSpec spec_;
    Random random_;
    bool noise_;
};

} // namespace ke
