// The force-torque transducer (ForceTorque.h): its datasheet as two channels of the signal chain on one clock.
#include "devices/ForceTorque.h"

namespace ke {

namespace {

ChannelSpec channel(double rate, double range, double precision, double accuracy) {
    ChannelSpec c;
    c.rateHz = rate;
    c.offsetSigma = accuracy / 3;
    c.noiseSigma = precision / 3;
    c.range = range;
    return c;
}

} // namespace

// The unit's draws: the force's offsets, then the torque's.
ForceTorque::ForceTorque(const ForceTorqueSpec& spec, uint32_t seed, bool noise)
    : spec_(spec), random_(seed), noise_(noise), clock_(spec.rateHz),
      forceCh_(channel(spec.rateHz, spec.forceRangeN, spec.forcePrecisionN, spec.forceAccuracyN)),
      torqueCh_(channel(spec.rateHz, spec.torqueRangeNm, spec.torquePrecisionNm, spec.torqueAccuracyNm)) {
    if (!noise) return;
    forceCh_.drawOffset(random_);
    torqueCh_.drawOffset(random_);
}

// A sample: force x, y, z, then torque x, y, z.
void ForceTorque::run(int64_t untilNs, const Vector3& force, const Vector3& torque) {
    Random* noise = noise_ ? &random_ : nullptr;
    clock_.run(untilNs, [&](int64_t t) {
        force_ = forceCh_.sample(force, noise);
        torque_ = torqueCh_.sample(torque, noise);
        sampleNs_ = t;
        ++samples_;
    });
}

} // namespace ke
