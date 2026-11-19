#pragma once
// An IMU - a three-axis accelerometer and gyroscope - as its datasheet describes it (ImuSpec, in the datasheet's own
// units; the numbers of a given model come from its row of data/sensors/imu.csv, scene/SensorSpec.h). The probe of
// the physics gives it the true specific force [m/s^2] and angular rate [rad/s] in its own axes; it is two channels of
// the signal chain (Signal.h) on one clock - the gyroscope's and the accelerometer's: at the output data rate, through
// the low-pass of the stated bandwidth, the noise of the stated densities, the offsets and the sensitivity error drawn
// once (the stated initial tolerances as 3 sigma), the full scale and the counts of its word, range / 2^(bits - 1).
// Between samples the reading is the last sample (its time is sampleNs). Cross-axis sensitivity, nonlinearity,
// temperature drift, a FIFO and a bus are not modelled.
#include "devices/Signal.h"

namespace ke {

struct ImuSpec {
    double rateHz = 1000, bandwidthHz = 260;
    double accelRangeG = 2, gyroRangeDps = 250;
    int bits = 16;
    double accelNoiseUg = 400, gyroNoiseDps = 0.005;       // noise density, per sqrt(Hz)
    double accelOffsetMg = 50, gyroOffsetDps = 20;         // initial offsets, +- (3 sigma)
    double scaleErrorPct = 3;                              // sensitivity, +- % (3 sigma)
};

class Imu {
public:
    explicit Imu(const ImuSpec& spec = ImuSpec(), uint32_t seed = 1, bool noise = false);

    // The clock on to `untilNs` with these true values holding since it last ran.
    void run(int64_t untilNs, const Vector3& force, const Vector3& rate);
    void skip(int64_t untilNs) { clock_.skip(untilNs); }

    const Vector3& accel() const { return accel_; } // m/s^2, the last sample: its counts times the step
    const Vector3& gyro() const { return gyro_; }   // rad/s
    double accelStep() const { return accelCh_.spec().step; } // m/s^2 per count
    double gyroStep() const { return gyroCh_.spec().step; }   // rad/s per count
    int64_t sampleNs() const { return sampleNs_; }  // when it was taken, the devices' clock (-1: none yet)
    long long samples() const { return samples_; }
    bool saturated() const { return accelCh_.saturated() || gyroCh_.saturated(); }
    const ImuSpec& spec() const { return spec_; }
    const Channel3& accelChannel() const { return accelCh_; } // its offsets and sensitivities
    const Channel3& gyroChannel() const { return gyroCh_; }

private:
    ImuSpec spec_;
    Random random_;
    bool noise_;
    SampleClock clock_;
    Channel3 accelCh_, gyroCh_;
    int64_t sampleNs_ = -1;
    Vector3 accel_{0.0_r}, gyro_{0.0_r};
    long long samples_ = 0;
};

} // namespace ke
