// The force-torque transducer (ForceTorque.h): its offsets drawn once from the seed, a sample at every whole period
// of the devices' clock with white noise on each axis, held at the range.
#include "devices/ForceTorque.h"

#include <algorithm>

namespace ke {

namespace {

// Three normal draws, x then y then z, scaled by sigma.
Vector3 normal3(Random& random, double sigma) {
    const double x = random.normal() * sigma;
    const double y = random.normal() * sigma;
    const double z = random.normal() * sigma;
    return {Real(x), Real(y), Real(z)};
}

// Each axis held at +-range; `held` set when one is.
Vector3 clamped(const Vector3& v, double range, bool& held) {
    Vector3 out = v;
    for (int k = 0; k < 3; ++k) {
        const Real r = Real(range);
        if (out[k] > r || out[k] < -r) held = true;
        out[k] = std::clamp(out[k], -r, r);
    }
    return out;
}

} // namespace

ForceTorque::ForceTorque(uint32_t seed, bool noise, const ForceTorqueProfile& profile)
    : profile_(profile), random_(seed), noise_(noise) {
    if (!noise) return;
    forceOffset = normal3(random_, profile_.forceOffset);
    torqueOffset = normal3(random_, profile_.torqueOffset);
}

void ForceTorque::run(int64_t untilNs, const Vector3& force, const Vector3& torque) {
    const int64_t period = profile_.periodNs;
    for (int64_t t = (lastNs_ / period + 1) * period; t <= untilNs; t += period) sample(t, force, torque);
    lastNs_ = untilNs;
}

// One sample at time t: the true load, the offsets and this sample's noise (force x, y, z, then torque x, y, z).
void ForceTorque::sample(int64_t t, const Vector3& force, const Vector3& torque) {
    Vector3 f = force + forceOffset, m = torque + torqueOffset;
    if (noise_) {
        f += normal3(random_, profile_.forceNoise);
        m += normal3(random_, profile_.torqueNoise);
    }
    saturated_ = false;
    force_ = clamped(f, profile_.forceRange, saturated_);
    torque_ = clamped(m, profile_.torqueRange, saturated_);
    sampleNs_ = t;
    ++samples_;
}

} // namespace ke
