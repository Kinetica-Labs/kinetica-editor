#pragma once
// The signal chain of a datasheet, the part every measuring device shares: a sensor is composed of a probe of the
// physics (what true quantity it takes from the world, src/sim), this chain (what its unit does to that quantity) and
// an interface (how a host reads it: a number, counts, pulses, edges). A device of src/devices is a recipe of these -
// an IMU two channels of three axes, a force-torque sensor two - and its datasheet the numbers of the stages.
//
// A channel of three axes takes the true value at each sample of its clock through the stages, each on when its number
// is: the unit's sensitivity and offset (drawn once from the seed, the datasheet's tolerance as 3 sigma), white noise
// (a density per sqrt(Hz) - sigma = density sqrt(f / 2), the band up to Nyquist - or a sigma per sample), a first-order
// low-pass of the bandwidth (y += a (x - y), a = 1 - e^{-2 pi B / f}), the full scale +-range (the reading held there,
// `saturated` says so), the counts of a step (rounded, the word one count short of +range).
#include "core/Random.h"
#include "math/Vector3.h"

#include <cstdint>

namespace ke {

// When a device samples: at every whole period from time 0 its clock passes.
class SampleClock {
public:
    explicit SampleClock(double rateHz);
    template <class Sample> void run(int64_t untilNs, Sample sample) {
        for (int64_t t = (lastNs_ / periodNs_ + 1) * periodNs_; t <= untilNs; t += periodNs_) sample(t);
        lastNs_ = untilNs;
    }
    void skip(int64_t untilNs) { lastNs_ = untilNs; } // on without sampling: it saw nothing meanwhile
    double rateHz() const { return 1e9 / double(periodNs_); }

private:
    int64_t periodNs_, lastNs_ = 0;
};

struct ChannelSpec {
    double rateHz = 1000;       // the clock's: the noise's band, the filter's step
    double scaleSigma = 0;      // of the unit's sensitivity, a fraction
    double offsetSigma = 0;     // of the unit's offset
    double noiseDensity = 0;    // per sqrt(Hz); or
    double noiseSigma = 0;      // per sample
    double bandwidthHz = 0;     // 0: no filter
    double range = 0;           // +-, 0: none
    double step = 0;            // a count, 0: not counted
};

class Channel3 {
public:
    explicit Channel3(const ChannelSpec& spec) : spec_(spec) {}
    void drawOffset(Random& r); // the unit's own, once
    void drawScale(Random& r);
    // One sample of the true value; `noise` null: none drawn.
    Vector3 sample(const Vector3& truth, Random* noise);
    bool saturated() const { return saturated_; }
    const ChannelSpec& spec() const { return spec_; }

    Vector3 offset{0.0_r}, scale{1.0_r};

private:
    ChannelSpec spec_;
    double state_[3] = {};
    bool saturated_ = false;
};

} // namespace ke
