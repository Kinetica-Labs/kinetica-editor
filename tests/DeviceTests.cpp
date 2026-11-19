// The devices of mechatronics (src/devices) on their own, against exact answers. A quadrature
// encoder: the count is floor(theta 4N / 2 pi) exactly at any speed; turning forward A leads B,
// turning back B leads A; every edge is within a nanosecond of the moment its line is crossed; the
// index comes once a turn. An IMU from its datasheet: its rate, full scale, counts and filter, its
// noise against the statistics it is drawn from. A sonar: its jitter. The same devices mounted
// on a robot in a running world are tested with the world (src/sim).
#include "TestRunner.h"

#include "devices/Encoder.h"
#include "devices/Imu.h"
#include "devices/Sonar.h"

#include <cmath>
#include <cstdio>

namespace {

const double kPiD = 3.14159265358979323846;

long counts(Real v, double step) { return std::lround(double(v) / step); }

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

// An IMU of the power-on MPU-6050's numbers (1 kHz, 260 Hz, +-2 g, +-250 deg/s, 16 bits) and of others: 10 samples
// in 10 ms at 1 kHz, 80 at 8 kHz; 1 g reads 32768 / 2 = 16384 counts at +-2 g and 8192 at +-4 g, 100 deg/s reads
// 100 * 32768 / 250 = 13107; 3 g at +-2 g holds at 32767 and says so; with a 5 Hz filter a step of the rate is
// 1 - e^{-2 pi 5 t} of itself at t = 32 ms - the one first-order stage sampled at 1 kHz; 12 bits: the step is the
// range over 2048.
void testImuSpec() {
    const double g = 9.80665, deg = kPiD / 180;
    const Vector3 oneG(0, Real(g), 0), spin(0, 0, Real(100 * deg));
    Imu imu, fast(ImuSpec{8000}), wide(ImuSpec{1000, 260, 4}), slow(ImuSpec{1000, 5}), coarse(ImuSpec{1000, 260, 2, 250, 12});
    for (Imu* u : {&imu, &fast, &wide, &coarse}) u->run(10000000, oneG, spin);
    const long ay = counts(imu.accel()[1], imu.accelStep()), gz = counts(imu.gyro()[2], imu.gyroStep());
    const long ay4 = counts(wide.accel()[1], wide.accelStep()), ay12 = counts(coarse.accel()[1], coarse.accelStep());
    const bool clean = !imu.saturated();
    imu.run(20000000, Vector3(0, Real(3 * g), 0), spin);
    const long held = counts(imu.accel()[1], imu.accelStep());
    slow.run(1000000000, oneG, Vector3(0.0_r)); // a second at rest: the 5 Hz filter settles
    slow.run(1032000000, oneG, spin);
    const double stepped = double(slow.gyro()[2]) / deg, expected = 100 * (1 - std::exp(-2 * kPiD * 5 * 0.032));
    std::printf("  IMU: %lld samples in 10 ms at 1 kHz, %lld at 8 kHz; 1 g = %ld counts (+-4 g: %ld, 12 bits: %ld), 100 deg/s = %ld; 3 g at +-2 g "
                "holds at %ld, saturated: %s; 5 Hz filter at 32 ms after a step: %.3f deg/s (1 - e^{-2 pi 5 t}: %.3f)\n", imu.samples() - 10,
                fast.samples(), ay, ay4, ay12, gz, held, imu.saturated() ? "yes" : "no", stepped, expected);
    CHECK(imu.samples() == 20 && fast.samples() == 80, "the rate: %lld, %lld samples", imu.samples(), fast.samples());
    CHECK(ay == 16384 && ay4 == 8192 && ay12 == 1024 && gz == 13107 && clean, "the counts %ld %ld %ld %ld are not the full scale's", ay, ay4, ay12, gz);
    CHECK(held == 32767 && imu.saturated(), "3 g at +-2 g holds at %ld", held);
    CHECK(std::fabs(stepped - expected) <= slow.gyroStep() / deg, "the 5 Hz filter's step %f is not %f", stepped, expected);
}

// The IMU's imperfections against the statistics they are drawn from. At rest, 9 s of samples at 1 kHz with the filter
// at 260 Hz: the standard deviation is density sqrt(f / 2) sqrt(a / (2 - a)) - white noise through y += a (x - y) keeps
// the share a / (2 - a) of its variance (std:: here as the independent reference); the mean is the unit's own offset
// (the accelerometer's with its sensitivity times g); one seed gives the same bits twice, another seed another unit;
// the offsets lie within the datasheet's tolerance.
void testImuNoise() {
    const double g = 9.80665, deg = kPiD / 180;
    const ImuSpec spec;
    Imu imu(spec, 7, true), twin(spec, 7, true), other(spec, 8, true);
    double gs = 0, gss = 0, as = 0, ass = 0;
    int n = 0;
    bool same = true;
    for (int64_t t = 1000000; t <= 10000000000LL; t += 1000000) {
        imu.run(t, Vector3(0, Real(g), 0), Vector3(0.0_r));
        twin.run(t, Vector3(0, Real(g), 0), Vector3(0.0_r));
        const double x = double(counts(imu.gyro()[0], imu.gyroStep())), y = double(counts(imu.accel()[1], imu.accelStep()));
        same = same && imu.gyro()[0] == twin.gyro()[0];
        if (t < 1000000000LL) continue; // the first second: the filter from zero to the offset
        gs += x, gss += x * x, as += y, ass += y * y, ++n;
    }
    const double gMean = gs / n, gSd = std::sqrt(gss / n - gMean * gMean), aMean = as / n, aSd = std::sqrt(ass / n - aMean * aMean);
    const double a = 1 - std::exp(-2 * kPiD * 260 / 1000), share = std::sqrt(a / (2 - a));
    const double gExpected = 0.005 * deg * std::sqrt(500.0) * share / imu.gyroStep(), aExpected = 400e-6 * g * std::sqrt(500.0) * share / imu.accelStep();
    const Channel3 &gyro = imu.gyroChannel(), &accel = imu.accelChannel();
    const double gOffset = gyro.offset[0] / imu.gyroStep(), aOffset = (accel.scale[1] * g + accel.offset[1]) / imu.accelStep();
    bool within = true;
    for (int k = 0; k < 3; ++k) within = within && std::fabs(gyro.offset[k]) <= 20 * deg && std::fabs(accel.offset[k]) <= 0.050 * g;
    std::printf("  IMU noise, 9 s at rest: gyroscope x sd %.2f counts (density sqrt(f/2) sqrt(a/(2-a)) = %.2f), mean %.1f (its offset %.1f); "
                "accelerometer y sd %.2f counts (%.2f), mean %.1f (its scale g + offset %.1f); seed 7 twice the same bits: %s; seed 8 offset %.3f deg/s "
                "vs %.3f; offsets within the datasheet's tolerance: %s\n", gSd, gExpected, gMean, gOffset, aSd, aExpected, aMean, aOffset,
                same ? "yes" : "no", other.gyroChannel().offset[0] / deg, gyro.offset[0] / deg, within ? "yes" : "no");
    CHECK(std::fabs(gSd / gExpected - 1) < 0.03 && std::fabs(aSd / aExpected - 1) < 0.03, "the noise %f, %f is not the datasheet's %f, %f", gSd, aSd, gExpected, aExpected);
    CHECK(std::fabs(gMean - gOffset) < 1.0 && std::fabs(aMean - aOffset) < 5.0, "the means %f %f are not the offsets %f %f", gMean, aMean, gOffset, aOffset);
    CHECK(same && other.gyroChannel().offset[0] != gyro.offset[0], "a seed does not give its own unit, the same each time");
    CHECK(within, "an offset lies outside the datasheet's tolerance");
}

// The sonar's jitter against the normal it is drawn from: 4000 measurements of an echo at 1 m read 1 m on average,
// with a standard deviation of the datasheet's 3 mm accuracy as 3 sigma, 1 mm; without noise every one reads 1 m to
// the nanosecond of the pulse; one seed gives the same pulses twice.
void testSonarNoise() {
    Sonar noisy(SonarSpec(), 5, true), twin(SonarSpec(), 5, true), exact(SonarSpec(), 5, false);
    std::vector<PinEdge> a, b, e;
    double sum = 0, sum2 = 0, worst = 0;
    bool same = true;
    for (int k = 0; k < 4000; ++k) {
        const int64_t t = int64_t(k) * noisy.cycleNs();
        const double x = noisy.measure(t, 1.0, 20, a), y = twin.measure(t, 1.0, 20, b);
        same = same && x == y;
        sum += x, sum2 += x * x;
        worst = std::max(worst, std::fabs(exact.measure(t, 1.0, 20, e) - 1.0));
    }
    const double mean = sum / 4000, sd = std::sqrt(sum2 / 4000 - mean * mean);
    std::printf("  sonar jitter: 4000 echoes at 1 m read %.5f m on average, sd %.4f mm (1 mm); without noise off by at most %.2g m; seed 5 twice "
                "the same: %s\n", mean, sd * 1e3, worst, same ? "yes" : "no");
    CHECK(std::fabs(mean - 1) < 1e-4 && std::fabs(sd / 1e-3 - 1) < 0.05, "the jitter: mean %f, sd %f", mean, sd);
    CHECK(worst < 2e-7 && same, "the exact sonar is off by %g, or a seed is not its own", worst);
}
