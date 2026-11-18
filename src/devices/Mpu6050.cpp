// The MPU-6050 (Mpu6050.h): its registers, its sampling clock, filter and noise, and the latch of the
// data registers in the counts of the full scale set.
#include "devices/Mpu6050.h"

#include "math/ElementaryFunctions.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

const double kG = 9.80665, kDeg = 3.14159265358979323846 / 180;
const double kGyroBandwidth[8] = {256, 188, 98, 42, 20, 10, 5, 256}; // Hz, by DLPF_CFG (7 reserved: as 0)
const double kAccelBandwidth[8] = {260, 184, 94, 44, 21, 10, 5, 260};
const double kGyroLsb[4] = {131, 65.5, 32.8, 16.4};  // LSB/(deg/s), by FS_SEL
const double kAccelLsb[4] = {16384, 8192, 4096, 2048}; // LSB/g, by AFS_SEL
const double kGyroDensity = 0.005 * kDeg;            // rad/s/sqrt(Hz)
const double kAccelDensity = 400e-6 * kG;            // m/s^2/sqrt(Hz)

// One first-order low-pass step at bandwidth B sampled at f, with white noise of the density added
// at the input: y += a (x + n - y), n of sigma = density sqrt(f / 2) (the band up to Nyquist).
double filtered(double y, double x, double bandwidth, double f, double density, Random* noise) {
    const double a = 1 - ke::exp(-2 * 3.14159265358979323846 * bandwidth / f);
    const double n = noise ? noise->normal() * density * std::sqrt(f / 2) : 0;
    return y + a * (x + n - y);
}

uint8_t high(int v) { return uint8_t((v >> 8) & 0xFF); }
int counts(double v) { return int(std::clamp(std::floor(v + 0.5), -32768.0, 32767.0)); }

} // namespace

Mpu6050::Mpu6050(uint32_t seed, bool noise) : random_(seed), noise_(noise) {
    reset();
    for (int k = 0; k < 3; ++k) accelBias[k] = gyroBias[k] = 0, accelScale[k] = gyroScale[k] = 1;
    if (!noise) return;
    for (int k = 0; k < 3; ++k) gyroBias[k] = random_.normal() * (20.0 / 3) * kDeg;
    for (int k = 0; k < 3; ++k) accelBias[k] = random_.normal() * ((k == 2 ? 0.080 : 0.050) / 3) * kG;
    for (int k = 0; k < 3; ++k) gyroScale[k] = 1 + random_.normal() * 0.01;
    for (int k = 0; k < 3; ++k) accelScale[k] = 1 + random_.normal() * 0.01;
}

void Mpu6050::reset() {
    accelIntegral_={};
    std::fill(regs_, regs_ + 128, uint8_t(0));
    regs_[PwrMgmt1] = 0x40;
    regs_[WhoAmI] = 0x68;
    std::fill(gyroState_, gyroState_ + 3, 0.0);
    std::fill(accelState_, accelState_ + 3, 0.0);
}

void Mpu6050::write(uint8_t reg, uint8_t value) {
    if (reg >= 128 || reg == WhoAmI || (reg >= AccelXoutH && reg <= GyroZoutL)) return;
    if (reg == PwrMgmt1 && (value & 0x80)) return reset();
    regs_[reg] = value;
}

void Mpu6050::run(int64_t untilNs, const Vector3& force, const Vector3& rate, double celsius) {
    const double f[3] = {double(force.x), double(force.y), double(force.z)};
    const double w[3] = {double(rate.x), double(rate.y), double(rate.z)};
    const int dlpf = regs_[Config] & 7;
    const int64_t period = dlpf == 0 || dlpf == 7 ? 125000 : 1000000; // the gyroscope's 8 or 1 kHz [ns]
    if (!asleep())
        for (int64_t t = (lastNs_ / period + 1) * period; t <= untilNs; t += period) tick(t, period, f, w, celsius);
    lastNs_ = untilNs;
}

// One gyroscope sample at time t (the accelerometer's on each whole millisecond), latched into the data
// registers every 1 + SMPLRT_DIV of them. The draws go gyroscope x, y, z, then accelerometer x, y, z.
void Mpu6050::tick(int64_t t, int64_t period, const double force[3], const double rate[3], double celsius) {
    const int dlpf = regs_[Config] & 7;
    Random* noise = noise_ ? &random_ : nullptr;
    for (int k = 0; k < 3; ++k)
        gyroState_[k] = filtered(gyroState_[k], gyroScale[k] * rate[k] + gyroBias[k], kGyroBandwidth[dlpf], 1e9 / double(period), kGyroDensity, noise);
    if (t % 1000000 == 0)
        for (int k = 0; k < 3; ++k)
            accelState_[k] = filtered(accelState_[k], accelScale[k] * force[k] + accelBias[k], kAccelBandwidth[dlpf], 1000, kAccelDensity, noise);
    if ((t / period) % (1 + regs_[SmplrtDiv]) == 0) {
        latch(celsius);
        const double dt=double(period)*(1+regs_[SmplrtDiv])*1e-9;
        double measured[3]; accel(measured);
        for (int k=0;k<3;++k) {
            accelIntegral_.delta[size_t(k)]+=measured[k]*dt;
            if (std::abs(int(word(uint8_t(AccelXoutH+2*k))))>=32760) ++accelIntegral_.clipped;
        }
        accelIntegral_.duration+=dt;
    }
}

void Mpu6050::latch(double celsius) {
    const double perAccel = kAccelLsb[(regs_[AccelConfig] >> 3) & 3] / kG, perGyro = kGyroLsb[(regs_[GyroConfig] >> 3) & 3] / kDeg;
    auto put = [this](int reg, int v) { regs_[reg] = high(v), regs_[reg + 1] = uint8_t(v & 0xFF); };
    for (int k = 0; k < 3; ++k) put(AccelXoutH + 2 * k, counts(accelState_[k] * perAccel));
    if (!(regs_[PwrMgmt1] & 0x08)) put(TempOutH, counts((celsius - 36.53) * 340)); // TEMP_DIS
    for (int k = 0; k < 3; ++k) put(GyroXoutH + 2 * k, counts(gyroState_[k] * perGyro));
    ++samples_;
}

void Mpu6050::accel(double out[3]) const {
    const double perAccel = kAccelLsb[(regs_[AccelConfig] >> 3) & 3] / kG;
    for (int k = 0; k < 3; ++k) out[k] = double(word(uint8_t(AccelXoutH + 2 * k))) / perAccel;
}

void Mpu6050::gyro(double out[3]) const {
    const double perGyro = kGyroLsb[(regs_[GyroConfig] >> 3) & 3] / kDeg;
    for (int k = 0; k < 3; ++k) out[k] = double(word(uint8_t(GyroXoutH + 2 * k))) / perGyro;
}

double Mpu6050::temperature() const { return double(word(TempOutH)) / 340 + 36.53; }

} // namespace ke
