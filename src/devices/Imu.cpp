// The IMU (Imu.h): its datasheet as two channels of the signal chain on one clock.
#include "devices/Imu.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

const double kG = 9.80665, kDeg = 3.14159265358979323846 / 180;

ChannelSpec channel(const ImuSpec& s, double range, double density, double offset) {
    ChannelSpec c;
    c.rateHz = s.rateHz;
    c.scaleSigma = s.scaleErrorPct / 300;
    c.offsetSigma = offset / 3;
    c.noiseDensity = density;
    c.bandwidthHz = s.bandwidthHz;
    c.range = range;
    c.step = range / std::ldexp(1.0, std::clamp(s.bits, 2, 32) - 1);
    return c;
}

} // namespace

// The unit's draws: the gyroscope's offsets, the accelerometer's, then their sensitivities.
Imu::Imu(const ImuSpec& spec, uint32_t seed, bool noise)
    : spec_(spec), random_(seed), noise_(noise), clock_(spec.rateHz),
      accelCh_(channel(spec, spec.accelRangeG * kG, spec.accelNoiseUg * 1e-6 * kG, spec.accelOffsetMg * 1e-3 * kG)),
      gyroCh_(channel(spec, spec.gyroRangeDps * kDeg, spec.gyroNoiseDps * kDeg, spec.gyroOffsetDps * kDeg)) {
    if (!noise) return;
    gyroCh_.drawOffset(random_);
    accelCh_.drawOffset(random_);
    gyroCh_.drawScale(random_);
    accelCh_.drawScale(random_);
}

void Imu::run(int64_t untilNs, const Vector3& force, const Vector3& rate) {
    Random* noise = noise_ ? &random_ : nullptr;
    clock_.run(untilNs, [&](int64_t t) {
        gyro_ = gyroCh_.sample(rate, noise);
        accel_ = accelCh_.sample(force, noise);
        sampleNs_ = t;
        ++samples_;
    });
}

} // namespace ke
