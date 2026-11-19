#pragma once
// A six-axis force-torque transducer as a robot's controller reads it, as its datasheet describes it (ForceTorqueSpec;
// the numbers of a given model come from its row of data/sensors/wrench.csv, scene/SensorSpec.h). The scene gives
// it the true load the flange passes to the tool, in the sensor's axes; it samples it on its own clock:
//   - a sample at every whole period 1 / rate of the devices' clock it passes, of the true values holding since it
//     last ran; between samples the reading is the last sample (its time is sampleNs), so a reading is up to a period
//     old;
//   - the unit's imperfections, when `noise`: white noise drawn at every sample on each axis (the stated precision as
//     3 sigma), and an offset on each axis drawn once from the seed (the stated accuracy as 3 sigma). Datasheets rarely
//     state the statistic of "precision" or the band it is measured in: these sigmas are a reading of the datasheet,
//     not a measurement of a unit;
//   - every axis saturates at its range (the reading is held there and `saturated` says so).
// Not modelled: quantisation, temperature drift, cross-talk between axes, the transducer's own bandwidth and any
// transport delay past the sample hold. A controller cancels the offset and a slow drift as UR's zero_ftsensor()
// does: it takes a reading at rest as its zero before it relies on a change of force.
#include "devices/Signal.h"

namespace ke {

struct ForceTorqueSpec {
    double rateHz = 500;
    double forceRangeN = 50, torqueRangeNm = 10;         // each axis
    double forcePrecisionN = 3.5, torquePrecisionNm = 0.2; // the noise of a sample (3 sigma)
    double forceAccuracyN = 4.0, torqueAccuracyNm = 0.3;   // the unit's offset (3 sigma)
};

// Two channels of the signal chain (Signal.h) on one clock: force and torque, each with its noise per sample, its
// offset drawn once and its range - no filter, no counts.
class ForceTorque {
public:
    explicit ForceTorque(const ForceTorqueSpec& spec = ForceTorqueSpec(), uint32_t seed = 1, bool noise = false);

    // The sensor's clock on to `untilNs` with this true load holding since it last ran.
    void run(int64_t untilNs, const Vector3& force, const Vector3& torque);
    void skip(int64_t untilNs) { clock_.skip(untilNs); }

    const Vector3& force() const { return force_; }   // the last sample, N, in the sensor's axes
    const Vector3& torque() const { return torque_; } // N m
    int64_t sampleNs() const { return sampleNs_; }    // when it was taken, the devices' clock (-1: none yet)
    long long samples() const { return samples_; }
    bool saturated() const { return forceCh_.saturated() || torqueCh_.saturated(); }
    const ForceTorqueSpec& spec() const { return spec_; }
    const Vector3& forceOffset() const { return forceCh_.offset; } // the unit's offsets, N, N m
    const Vector3& torqueOffset() const { return torqueCh_.offset; }

private:
    ForceTorqueSpec spec_;
    Random random_;
    bool noise_;
    SampleClock clock_;
    Channel3 forceCh_, torqueCh_;
    int64_t sampleNs_ = -1;
    long long samples_ = 0;
    Vector3 force_{0.0_r}, torque_{0.0_r};
};

} // namespace ke
