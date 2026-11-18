#pragma once
// An InvenSense MPU-6050 (product specification PS-MPU-6000A-00 rev. 3.4, register map
// RM-MPU-6000A-00 rev. 4.2): a three-axis accelerometer and gyroscope behind 128 registers - what a
// firmware reads and writes over I2C. The scene gives it the true specific force [m/s^2] and angular
// rate [rad/s] in its own axes; it samples them on its own clock as the chip does:
//   - power-on: every register 0 but PWR_MGMT_1 = 0x40 (asleep) and WHO_AM_I = 0x68; asleep it does
//     not sample, and the data registers keep what they held;
//   - the gyroscope samples at 8 kHz with DLPF_CFG 0 or 7, at 1 kHz otherwise, the accelerometer at
//     1 kHz; the data registers latch every 1 + SMPLRT_DIV gyroscope samples (the sample rate);
//   - the digital low-pass filter (DLPF_CFG 0..6: 256, 188, 98, 42, 20, 10, 5 Hz for the gyroscope,
//     260, 184, 94, 44, 21, 10, 5 Hz for the accelerometer) as one first-order stage of that bandwidth,
//     y += a (x - y) with a = 1 - e^{-2 pi B / f} - the datasheet's delays are not matched (a first
//     order lags 1 / (2 pi B), the chip somewhat less);
//   - full scale +-2/4/8/16 g at 16384/8192/4096/2048 LSB/g (AFS_SEL) and +-250/500/1000/2000 deg/s at
//     131/65.5/32.8/16.4 LSB/(deg/s) (FS_SEL), g = 9.80665 m/s^2; counts rounded and held to a 16-bit
//     word, high byte first; temperature = TEMP_OUT / 340 + 36.53 deg C;
//   - the unit's imperfections, when `noise`: white noise of the datasheet's density (gyroscope
//     0.005 deg/s/sqrt(Hz), accelerometer 400 ug/sqrt(Hz)) drawn at each sample, sigma = density
//     sqrt(f / 2), and passed through the filter; offsets and scale errors drawn once from the seed,
//     the datasheet's tolerance taken as 3 sigma (zero-rate output +-20 deg/s; zero-g +-50 mg on x
//     and y, +-80 mg on z; sensitivity +-3 %). Cross-axis sensitivity, nonlinearity, temperature drift,
//     the FIFO, interrupts and self-test are not modelled.
#include "core/Random.h"
#include "math/Vector3.h"

#include <cstdint>
#include <array>

namespace ke {

class Mpu6050 {
public:
    enum Register : uint8_t {
        SmplrtDiv = 0x19, Config = 0x1A, GyroConfig = 0x1B, AccelConfig = 0x1C, AccelXoutH = 0x3B,
        TempOutH = 0x41, GyroXoutH = 0x43, GyroZoutL = 0x48, PwrMgmt1 = 0x6B, WhoAmI = 0x75
    };
    explicit Mpu6050(uint32_t seed = 1, bool noise = false);

    uint8_t read(uint8_t reg) const { return reg < 128 ? regs_[reg] : 0; }
    // A host's write: a read-only register ignores it; DEVICE_RESET (PWR_MGMT_1 bit 7) resets them all.
    void write(uint8_t reg, uint8_t value);
    // The chip's clock on to `untilNs` with these true values holding since it last ran: it samples
    // at every whole multiple of its sample period from time 0 that it passes.
    void run(int64_t untilNs, const Vector3& force, const Vector3& rate, double celsius);
    void skip(int64_t untilNs) { lastNs_ = untilNs; } // on without sampling: it saw nothing meanwhile

    // The data registers as a host decodes them at the full scale set now.
    void accel(double out[3]) const;  // m/s^2
    void gyro(double out[3]) const;   // rad/s
    double temperature() const;       // deg C
    bool asleep() const { return (regs_[PwrMgmt1] & 0x40) != 0; }
    long long samples() const { return samples_; } // latched since power-on
    // Host-side integration of decoded output registers at each latch, not extra ideal sensing.
    struct AccelIntegral { std::array<double,3> delta{}; double duration=0; uint64_t clipped=0; };
    const AccelIntegral& accelIntegral() const { return accelIntegral_; }

    double accelBias[3], gyroBias[3];   // the unit's offsets [m/s^2], [rad/s]
    double accelScale[3], gyroScale[3]; // and its sensitivities against the nominal

private:
    void reset();
    void tick(int64_t t, int64_t period, const double force[3], const double rate[3], double celsius);
    void latch(double celsius);
    int16_t word(uint8_t high) const { return int16_t(uint16_t(regs_[high] << 8 | regs_[high + 1])); }

    uint8_t regs_[128];
    Random random_;
    bool noise_;
    int64_t lastNs_ = 0;
    double gyroState_[3], accelState_[3]; // the filters' outputs
    long long samples_ = 0;
    AccelIntegral accelIntegral_;
};

} // namespace ke
