// The devices of mechatronics (src/devices) on their own, against exact answers. A quadrature
// encoder: the count is floor(theta 4N / 2 pi) exactly at any speed; turning forward A leads B,
// turning back B leads A; every edge is within a nanosecond of the moment its line is crossed; the
// index comes once a turn. An IMU (MPU-6050): its registers as the datasheet has them, its noise
// against the statistics it is drawn from. A sonar (HC-SR04): its jitter. The same devices mounted
// on a robot in a running world are tested with the world (src/sim).
#include "TestRunner.h"

#include "devices/Encoder.h"
#include "devices/Hcsr04.h"
#include "devices/Mpu6050.h"

#include <cmath>
#include <cstdio>

namespace {

const double kPiD = 3.14159265358979323846;

int word(const Mpu6050& chip, int reg) { return int(int16_t(uint16_t(chip.read(uint8_t(reg)) << 8 | chip.read(uint8_t(reg + 1))))); }

} // namespace

void testEncoderQuadrature() {
    const int N = 100;
    const double omega = 10, h = 1.0 / 600, pi = kPiD;
    Encoder e(N);
    e.start(0);
    std::vector<PinEdge> edges;
    for (int k = 0; k < 600; ++k)
        e.advance(omega * k * h, omega * (k + 1) * h, int64_t(std::llround(k * h * 1e9)), int64_t(std::llround((k + 1) * h * 1e9)), edges);
    const int64_t expected = int64_t(std::floor(omega * 1.0 * 4 * N / (2 * pi)));
    int ab = 0, z = 0;
    double worstGap = 0; // each edge against the exact moment the shaft crosses its line, k / (4N / 2 pi) / omega
    for (const PinEdge& x : edges) {
        if (x.pin == 'Z') {
            z += x.level;
            continue;
        }
        ++ab;
        worstGap = std::max(worstGap, std::fabs(double(x.timeNs) - double(ab) * 2 * pi / (4 * N * omega) * 1e9));
    }
    Encoder back(N);
    back.start(0);
    std::vector<PinEdge> reverse;
    back.advance(0, -0.05, 0, 1000000, reverse);
    std::printf("  encoder %d lines at %.0f rad/s for 1 s: count %lld (floor(theta 4N / 2 pi) = %lld), %d A/B edges, %d index pulses; "
                "every edge within %.2f ns of the moment it crosses its line; forward first edge %c, backward first edge %c, backward count %lld\n", N, omega,
                (long long)e.count(), (long long)expected, ab, z, worstGap, edges.front().pin, reverse.front().pin, (long long)back.count());
    CHECK(e.count() == expected && ab == expected, "the count %lld or the edges %d are not floor(theta 4N / 2 pi) = %lld", (long long)e.count(), ab,
          (long long)expected);
    CHECK(worstGap <= 1.0, "an edge is %g ns off the moment its line is crossed (whole nanoseconds: at most 1)", worstGap);
    CHECK(edges.front().pin == 'A' && reverse.front().pin == 'B', "the quadrature order does not give the direction");
    CHECK(z == 1 && back.count() == int64_t(std::floor(-0.05 * 4 * N / (2 * pi))), "the index (%d) or the backward count is wrong", z);
}

// The MPU-6050's registers as its datasheet has them: WHO_AM_I 0x68; asleep at power-on, latching
// nothing until PWR_MGMT_1 is cleared; 1 g at +-2 g is 16384 counts, at +-4 g 8192, and 3 g at +-2 g
// holds at 32767; 100 deg/s at +-250 deg/s is 13100; 25 deg C is -3920 (TEMP_OUT / 340 + 36.53); the
// data registers latch 8000 times a second with DLPF_CFG 0 and SMPLRT_DIV 0, 1000 with SMPLRT_DIV 7;
// with DLPF_CFG 6 (5 Hz) a step of the rate is 1 - e^{-2 pi 5 t} of itself at t = 32 ms - the one
// first-order stage sampled at 1 kHz; a write to WHO_AM_I is ignored and DEVICE_RESET restores all.
void testMpu6050Registers() {
    const double g = 9.80665, deg = kPiD / 180;
    Mpu6050 chip(1, false);
    const bool powerOn = chip.read(Mpu6050::WhoAmI) == 0x68 && chip.read(Mpu6050::PwrMgmt1) == 0x40 && chip.asleep();
    const Vector3 oneG(0, Real(g), 0), spin(0, 0, Real(100 * deg));
    chip.run(10000000, oneG, spin, 25);
    const long long asleep = chip.samples();
    chip.write(Mpu6050::PwrMgmt1, 0);
    chip.run(20000000, oneG, spin, 25);
    const long long fast = chip.samples() - asleep;
    const int ay = word(chip, Mpu6050::AccelXoutH + 2), gz = word(chip, Mpu6050::GyroXoutH + 4), temp = word(chip, Mpu6050::TempOutH);
    chip.write(Mpu6050::AccelConfig, 0x08); // AFS_SEL 1: +-4 g
    chip.write(Mpu6050::SmplrtDiv, 7);
    chip.run(30000000, oneG, spin, 25);
    const int ay4 = word(chip, Mpu6050::AccelXoutH + 2);
    const long long slow = chip.samples() - asleep - fast;
    chip.write(Mpu6050::AccelConfig, 0);
    chip.run(40000000, Vector3(0, Real(3 * g), 0), spin, 25);
    const int held = word(chip, Mpu6050::AccelXoutH + 2);
    chip.write(Mpu6050::Config, 6);
    chip.write(Mpu6050::SmplrtDiv, 0);
    chip.run(1040000000, oneG, Vector3(0.0_r), 25); // a second at rest: the 5 Hz filter settles
    chip.run(1072000000, oneG, spin, 25);
    double w[3];
    chip.gyro(w);
    const double stepped = w[2] / deg, expected = 100 * (1 - std::exp(-2 * kPiD * 5 * 0.032));
    chip.write(Mpu6050::WhoAmI, 0x12);
    const bool readOnly = chip.read(Mpu6050::WhoAmI) == 0x68;
    chip.write(Mpu6050::PwrMgmt1, 0x80);
    const bool reset = chip.asleep() && chip.read(Mpu6050::Config) == 0 && chip.read(Mpu6050::SmplrtDiv) == 0;
    std::printf("  MPU-6050: power-on WHO_AM_I 0x68, PWR_MGMT_1 0x40 asleep: %s, %lld samples asleep; awake 10 ms: %lld samples; 1 g = %d, "
                "at +-4 g %d (%lld samples in 10 ms at SMPLRT_DIV 7), 3 g at +-2 g %d; 100 deg/s = %d; 25 deg C = %d (%.4f deg C); 5 Hz filter "
                "at 32 ms after a step: %.3f deg/s (1 - e^{-2 pi 5 t}: %.3f); WHO_AM_I read-only: %s; DEVICE_RESET: %s\n", powerOn ? "yes" : "no",
                asleep, fast, ay, ay4, slow, held, gz, temp, double(temp) / 340 + 36.53, stepped, expected, readOnly ? "yes" : "no", reset ? "yes" : "no");
    CHECK(powerOn && asleep == 0, "the chip does not power on asleep with WHO_AM_I 0x68");
    CHECK(fast == 80 && slow == 10, "the sample rate is not 8 kHz / (1 + SMPLRT_DIV): %lld, %lld in 10 ms", fast, slow);
    CHECK(ay == 16384 && ay4 == 8192 && held == 32767 && gz == 13100 && temp == -3920, "the counts %d %d %d %d %d are not the datasheet's", ay, ay4, held, gz, temp);
    CHECK(std::fabs(stepped - expected) <= 1.0 / 131, "the 5 Hz filter's step %f is not %f", stepped, expected);
    CHECK(readOnly && reset, "WHO_AM_I took a write, or DEVICE_RESET did not reset");
}

// The MPU-6050's imperfections against the statistics they are drawn from. At rest, 10 s of samples
// read at 8 kHz (gyroscope) and 1 kHz (accelerometer) with the filters at 256 / 260 Hz: the standard
// deviation is density sqrt(f / 2) sqrt(a / (2 - a)) - white noise through y += a (x - y) keeps the
// share a / (2 - a) of its variance (std:: here as the independent reference); the mean is the unit's
// own offset (the accelerometer's with its sensitivity times g); one seed gives the same bits twice,
// another seed another unit; the offsets lie within the datasheet's tolerance.
void testMpu6050Noise() {
    const double g = 9.80665, deg = kPiD / 180;
    Mpu6050 chip(7, true), twin(7, true), other(8, true);
    for (Mpu6050* c : {&chip, &twin, &other}) c->write(Mpu6050::PwrMgmt1, 0);
    double gs = 0, gss = 0, as = 0, ass = 0;
    int ng = 0, na = 0;
    bool same = true;
    for (int64_t t = 125000; t <= 10000000000LL; t += 125000) {
        chip.run(t, Vector3(0, Real(g), 0), Vector3(0.0_r), 25);
        twin.run(t, Vector3(0, Real(g), 0), Vector3(0.0_r), 25);
        const double x = word(chip, Mpu6050::GyroXoutH);
        same = same && x == word(twin, Mpu6050::GyroXoutH);
        if (t < 1000000000LL) continue; // the first second: the filters from zero to the offset
        gs += x, gss += x * x, ++ng;
        if (t % 1000000 != 0) continue;
        const double y = word(chip, Mpu6050::AccelXoutH + 2);
        as += y, ass += y * y, ++na;
    }
    const double gMean = gs / ng, gSd = std::sqrt(gss / ng - gMean * gMean), aMean = as / na, aSd = std::sqrt(ass / na - aMean * aMean);
    const double ag = 1 - std::exp(-2 * kPiD * 256 / 8000), aa = 1 - std::exp(-2 * kPiD * 260 / 1000);
    const double gExpected = 0.005 * std::sqrt(4000.0) * std::sqrt(ag / (2 - ag)) * 131, aExpected = 400e-6 * std::sqrt(500.0) * std::sqrt(aa / (2 - aa)) * 16384;
    const double gOffset = chip.gyroBias[0] / deg * 131, aOffset = (chip.accelScale[1] * g + chip.accelBias[1]) / g * 16384;
    bool within = true;
    for (int k = 0; k < 3; ++k)
        within = within && std::fabs(chip.gyroBias[k]) <= 20 * deg && std::fabs(chip.accelBias[k]) <= (k == 2 ? 0.080 : 0.050) * g;
    std::printf("  MPU-6050 noise, 9 s at rest: gyroscope x sd %.2f counts (density sqrt(f/2) sqrt(a/(2-a)) = %.2f), mean %.1f (its offset %.1f); "
                "accelerometer y sd %.2f counts (%.2f), mean %.1f (its scale g + offset %.1f); seed 7 twice the same bits: %s; seed 8 offset %.3f deg/s "
                "vs %.3f; offsets within the datasheet's tolerance: %s\n", gSd, gExpected, gMean, gOffset, aSd, aExpected, aMean, aOffset,
                same ? "yes" : "no", other.gyroBias[0] / deg, chip.gyroBias[0] / deg, within ? "yes" : "no");
    CHECK(std::fabs(gSd / gExpected - 1) < 0.03 && std::fabs(aSd / aExpected - 1) < 0.03, "the noise %f, %f is not the datasheet's %f, %f", gSd, aSd, gExpected, aExpected);
    CHECK(std::fabs(gMean - gOffset) < 1.0 && std::fabs(aMean - aOffset) < 5.0, "the means %f %f are not the offsets %f %f", gMean, aMean, gOffset, aOffset);
    CHECK(same && other.gyroBias[0] != chip.gyroBias[0], "a seed does not give its own unit, the same each time");
    CHECK(within, "an offset lies outside the datasheet's tolerance");
}

// The HC-SR04's jitter against the normal it is drawn from: 4000 measurements of an echo at 1 m read
// 1 m on average, with a standard deviation of the datasheet's 3 mm accuracy as 3 sigma, 1 mm; without
// noise every one reads 1 m to the nanosecond of the pulse; one seed gives the same pulses twice.
void testSonarNoise() {
    Hcsr04 noisy(5, true), twin(5, true), exact(5, false);
    std::vector<PinEdge> a, b, e;
    double sum = 0, sum2 = 0, worst = 0;
    bool same = true;
    for (int k = 0; k < 4000; ++k) {
        const double x = noisy.measure(int64_t(k) * Hcsr04::kCycleNs, 1.0, 20, a), y = twin.measure(int64_t(k) * Hcsr04::kCycleNs, 1.0, 20, b);
        same = same && x == y;
        sum += x, sum2 += x * x;
        worst = std::max(worst, std::fabs(exact.measure(int64_t(k) * Hcsr04::kCycleNs, 1.0, 20, e) - 1.0));
    }
    const double mean = sum / 4000, sd = std::sqrt(sum2 / 4000 - mean * mean);
    std::printf("  sonar jitter: 4000 echoes at 1 m read %.5f m on average, sd %.4f mm (1 mm); without noise off by at most %.2g m; seed 5 twice "
                "the same: %s\n", mean, sd * 1e3, worst, same ? "yes" : "no");
    CHECK(std::fabs(mean - 1) < 1e-4 && std::fabs(sd / 1e-3 - 1) < 0.05, "the jitter: mean %f, sd %f", mean, sd);
    CHECK(worst < 2e-7 && same, "the exact sonar is off by %g, or a seed is not its own", worst);
}
