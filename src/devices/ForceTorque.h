#pragma once
// A six-axis force-torque transducer as a robot's controller reads it: the tool flange sensor of a Universal Robots
// e-Series arm (UR5e datasheet, "Force/torque sensor, tool flange": force range 50 N, precision 3.5 N, accuracy
// 4.0 N; torque range 10 N m, precision 0.2 N m, accuracy 0.3 N m), sampled on the e-Series controller's 500 Hz
// cycle. The scene gives it the true load the flange passes to the tool, in the sensor's axes; it samples it on its
// own clock:
//   - a sample at every whole 2 ms of the devices' clock it passes, of the true values holding since it last ran;
//     between samples the reading is the last sample (its time is sampleNs), so a reading is up to 2 ms old;
//   - the unit's imperfections, when `noise`: white noise drawn at every sample on each axis, and an offset on
//     each axis drawn once from the seed. The datasheet's tolerances are taken as 3 sigma, as Mpu6050.h does
//     (precision: the noise, sigma = 3.5 / 3 N and 0.2 / 3 N m; accuracy: the offset, 4.0 / 3 N and 0.3 / 3 N m).
//     UR states neither the statistic of "precision" nor the band it is measured in: these sigmas are a
//     reading of the datasheet, not a measurement of a unit;
//   - every axis saturates at its range (the reading is held at +-50 N, +-10 N m and `saturated` says so).
// Not modelled: quantisation (not published), temperature drift, cross-talk between axes, the transducer's own
// bandwidth and any transport delay past the sample hold. A controller cancels the offset and a slow drift as UR's
// zero_ftsensor() does: it takes a reading at rest as its zero before it relies on a change of force.
#include "core/Random.h"
#include "math/Vector3.h"

#include <cstdint>

namespace ke {

struct ForceTorqueProfile {
    int64_t periodNs = 2000000;           // the sample period: 500 Hz
    double forceRange = 50, torqueRange = 10; // N, N m (each axis)
    double forceNoise = 3.5 / 3, torqueNoise = 0.2 / 3;   // sigma of a sample, N, N m
    double forceOffset = 4.0 / 3, torqueOffset = 0.3 / 3; // sigma of the unit's offset, N, N m
};

class ForceTorque {
public:
    explicit ForceTorque(uint32_t seed = 1, bool noise = false, const ForceTorqueProfile& profile = ForceTorqueProfile());

    // The sensor's clock on to `untilNs` with this true load holding since it last ran: a sample at every whole
    // period from time 0 that it passes.
    void run(int64_t untilNs, const Vector3& force, const Vector3& torque);
    void skip(int64_t untilNs) { lastNs_ = untilNs; } // on without sampling: it saw nothing meanwhile

    const Vector3& force() const { return force_; }   // the last sample, N, in the sensor's axes
    const Vector3& torque() const { return torque_; } // N m
    int64_t sampleNs() const { return sampleNs_; }    // when it was taken, the devices' clock (-1: none yet)
    long long samples() const { return samples_; }
    bool saturated() const { return saturated_; }      // an axis of the last sample at its range
    const ForceTorqueProfile& profile() const { return profile_; }

    Vector3 forceOffset{0.0_r}, torqueOffset{0.0_r}; // the unit's offsets, N, N m

private:
    void sample(int64_t t, const Vector3& force, const Vector3& torque);

    ForceTorqueProfile profile_;
    Random random_;
    bool noise_;
    int64_t lastNs_ = 0, sampleNs_ = -1;
    long long samples_ = 0;
    Vector3 force_{0.0_r}, torque_{0.0_r};
    bool saturated_ = false;
};

} // namespace ke
