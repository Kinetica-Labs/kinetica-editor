// The elementary functions of ElementaryFunctions.h, built only from + - * /, sqrt and the exact
// floor, fmod, frexp and ldexp, so IEEE 754 gives the same bits on every machine. Each function
// reduces its argument to a small interval and sums a Taylor series there, keeping
// the rounding error of the reduction in a second double where it matters. This file is compiled
// with -ffp-contract=off in every build: a fused multiply-add would change the bits and break the
// exact products below. The constants are hex literals, which every compiler reads to
// the same double.

#include "math/ElementaryFunctions.h"

#include <cmath>
#include <limits>

namespace ke {
namespace {

constexpr double kPio2[4] = {
    0x1.921fb54400000p+0, 0x1.0b4611a600000p-34, 0x1.3198a2e000000p-69, 0x1.b839a252049c1p-104
};
constexpr double kTwoOverPi = 0x1.45f306dc9c883p-1;
constexpr double kPiHi = 0x1.921fb54442d18p+1, kPiLo = 0x1.1a62633145c07p-53;
constexpr double kHalfPiHi = 0x1.921fb54442d18p+0, kHalfPiLo = 0x1.1a62633145c07p-54;
constexpr double kLn2Hi = 0x1.62e42fefa2000p-1, kLn2Lo = 0x1.9ef35793c7673p-41;
constexpr double kInvLn2 = 0x1.71547652b82fep+0;
constexpr double kAtanTable[8][2] = {
    {0x1.fd5ba9aac2f6ep-4, -0x1.cd37686760c17p-59}, {0x1.f5b75f92c80ddp-3, 0x1.8ab6e3cf7afbdp-57},
    {0x1.6f61941e4def1p-2, -0x1.c63aae6f6e918p-56}, {0x1.dac670561bb4fp-2, 0x1.a2b7f222f65e2p-56},
    {0x1.1e00babdefeb4p-1, -0x1.928df287a668fp-58}, {0x1.4978fa3269ee1p-1, 0x1.2419a87f2a458p-56},
    {0x1.700a7c5784634p-1, -0x1.8c34d25aadef6p-56}, {0x1.921fb54442d18p-1, 0x1.1a62633145c07p-55}
};
constexpr double kSin[9] = {
    -0x1.5555555555555p-3, 0x1.1111111111111p-7, -0x1.a01a01a01a01ap-13, 0x1.71de3a556c734p-19,
    -0x1.ae64567f544e4p-26, 0x1.6124613a86d09p-33, -0x1.ae7f3e733b81fp-41, 0x1.952c77030ad4ap-49,
    -0x1.2f49b46814157p-57
};
constexpr double kCos[8] = {
    0x1.5555555555555p-5, -0x1.6c16c16c16c17p-10, 0x1.a01a01a01a01ap-16, -0x1.27e4fb7789f5cp-22,
    0x1.1eed8eff8d898p-29, -0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-45, -0x1.6827863b97d97p-53
};
constexpr double kExp[14] = {
    0x1.0000000000000p-1, 0x1.5555555555555p-3, 0x1.5555555555555p-5, 0x1.1111111111111p-7,
    0x1.6c16c16c16c17p-10, 0x1.a01a01a01a01ap-13, 0x1.a01a01a01a01ap-16, 0x1.71de3a556c734p-19,
    0x1.27e4fb7789f5cp-22, 0x1.ae64567f544e4p-26, 0x1.1eed8eff8d898p-29, 0x1.6124613a86d09p-33,
    0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-41
};
constexpr double kAtan[7] = {
    -0x1.5555555555555p-2, 0x1.999999999999ap-3, -0x1.2492492492492p-3, 0x1.c71c71c71c71cp-4,
    -0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4, -0x1.1111111111111p-4
};
constexpr double kLog[11] = {
    0x1.5555555555555p-1, 0x1.999999999999ap-2, 0x1.2492492492492p-2, 0x1.c71c71c71c71cp-3,
    0x1.745d1745d1746p-3, 0x1.3b13b13b13b14p-3, 0x1.1111111111111p-3, 0x1.e1e1e1e1e1e1ep-4,
    0x1.af286bca1af28p-4, 0x1.8618618618618p-4, 0x1.642c8590b2164p-4
};
constexpr double kSqrtHalf = 0x1.6a09e667f3bcdp-1;
constexpr double kExpMax = 0x1.62e42fefa39efp+9;  // ln(DBL_MAX): above it exp overflows
constexpr double kExpMin = -0x1.74910d52d3051p+9; // ln(smallest subnormal / 2): below it exp is 0
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfD = std::numeric_limits<double>::infinity();

// c[0] + z (c[1] + z (c[2] + ...)): Horner's rule, n - 1 multiplications.
double horner(const double* c, int n, double z) {
    double p = c[n - 1];
    for (int i = n - 2; i >= 0; --i) p = c[i] + z * p;
    return p;
}

// s + e = a + b exactly (Knuth's two-sum): no condition on |a|, |b|.
void twoSum(double a, double b, double& s, double& e) {
    s = a + b;
    const double bb = s - a;
    e = (a - (s - bb)) + (b - bb);
}

// p + e = a b exactly (Dekker's product): Veltkamp's split in two 26-bit halves whose products are
// exact. Needs no fused multiply-add, hence -ffp-contract=off. |a|, |b| < 2^995.
void twoProduct(double a, double b, double& p, double& e) {
    constexpr double kSplit = 134217729.0; // 2^27 + 1
    const double ca = kSplit * a, ah = ca - (ca - a), al = a - ah;
    const double cb = kSplit * b, bh = cb - (cb - b), bl = b - bh;
    p = a * b;
    e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
}

// x = n pi/2 + (y0 + y1), |y0 + y1| <= pi/4 (a hair more), returns n mod 4. Cody-Waite
// reduction: pi/2 = P0 + P1 + P2 + P3 with P0..P2 of 33 bits, so n Pk is exact for n < 2^20
// and x - n P0 is exact (Sterbenz); twoSum keeps what the next two subtractions round off. Past
// 2^20 pi/2 (1.6e6 rad, never met in a simulation) x is first cut by the exact fmod with the
// double nearest 2 pi: the same bits everywhere, the accuracy of a few digits less.
int reduceHalfPi(double x, double& y0, double& y1) {
    if (std::fabs(x) <= 0.25 * kPiHi) { y0 = x; y1 = 0; return 0; }
    if (std::fabs(x) >= 0x1p20 * kHalfPiHi) x = std::fmod(x, 2.0 * kPiHi);
    const double n = std::floor(x * kTwoOverPi + 0.5);
    double a, ae, b, be;
    twoSum(x - n * kPio2[0], -n * kPio2[1], a, ae);
    twoSum(a, -n * kPio2[2], b, be);
    const double tail = (ae + be) - n * kPio2[3];
    y0 = b + tail;
    y1 = tail - (y0 - b);
    const int q = int(std::fmod(n, 4.0));
    return q < 0 ? q + 4 : q;
}

// sin(x + y) for |x| <= pi/4, |y| << |x|: x + x^3 (S1 + x^2 S2 + ...) + y cos x (A&S 4.3.65),
// the small terms summed first so that only the last addition rounds at the scale of x.
double sinKernel(double x, double y) {
    const double z = x * x;
    return x + (z * x * horner(kSin, 9, z) + y * (1.0 - 0.5 * z));
}

// cos(x + y) = 1 - x^2/2 + x^4 (C2 + ...) - x y (A&S 4.3.66). w = 1 - x^2/2 rounds; (1 - w) - x^2/2
// is that rounding error (1 - w is exact by Sterbenz) and goes back in.
double cosKernel(double x, double y) {
    const double z = x * x, hz = 0.5 * z, w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * z * horner(kCos, 8, z) - x * y));
}

// q + ql = n / d to twice the precision: q rounds, and the remainder n - q d is exact
// (twoProduct, then Sterbenz), so ql = remainder / d. |n|, |d| < 2^995.
void divide(double n, double d, double& q, double& ql) {
    q = n / d;
    double p, pe;
    twoProduct(q, d, p, pe);
    ql = ((n - p) - pe) / d;
}

// atan(a + al) for 0 <= a <= 1, al a low part: a = c + d with c = k/8 the nearest eighth, then
// atan(a) = atan(c) + atan(t), t = (a - c) / (1 + a c), |t| <= 1/16 (A&S 4.4.34), and the Taylor
// series of atan(t) (A&S 4.4.42) to t^15. a - c is exact (Sterbenz), 1 + a c is a head and a
// tail, the division keeps its remainder, and atan(k/8) is a head and a tail: the only rounding
// at the scale of the result is the last addition.
double atanUnit(double a, double al) {
    const int k = int(a * 8.0 + 0.5);
    if (k == 0) return a + (al + a * (a * a) * horner(kAtan, 7, a * a));
    const double c = k * 0.125;
    double ac, acl, dh, dl, t, tl;
    twoProduct(a, c, ac, acl);
    twoSum(1.0, ac, dh, dl);
    divide((a - c) + al, dh, t, tl);
    const double z = t * t;
    tl -= t * (dl + acl) / dh;
    return kAtanTable[k - 1][0] + (kAtanTable[k - 1][1] + (t + (tl + t * z * horner(kAtan, 7, z))));
}

// atan(n / d) for 0 <= n <= d, both finite and d > 0, the quotient to twice the precision. Both are
// scaled by a power of two first (exact), so that twoProduct inside cannot overflow.
double atanRatio(double n, double d) {
    if (d > 0x1p900) { n = std::ldexp(n, -200); d = std::ldexp(d, -200); }
    else if (d < 0x1p-900) { n = std::ldexp(n, 200); d = std::ldexp(d, 200); }
    double q, ql;
    divide(n, d, q, ql);
    return atanUnit(q, ql);
}

// exp(x + xl) for |x| <= kExpMax: x = k ln2 + r, |r| <= ln2/2 (Cody-Waite; ln 2 is a
// 40-bit head, so k head is exact and x - k head is exact by Sterbenz), e^r by its Taylor series to
// r^15 (A&S 4.2.1), then 2^k by the exact ldexp. xl carries the low half of pow's product.
double expParts(double x, double xl) {
    const double k = std::floor(x * kInvLn2 + 0.5);
    const double r = ((x - k * kLn2Hi) - k * kLn2Lo) + xl;
    const double er = 1.0 + (r + r * r * horner(kExp, 14, r));
    return std::ldexp(er, int(k));
}

// log x = e ln2 + log(1 + f) for x = 2^e (1 + f), sqrt(1/2) <= 1 + f < sqrt(2), f exact. With
// s = f / (2 + f), log(1 + f) = 2 atanh s = f - f^2/2 + s (f^2/2 + R), R = sum 2 s^2k / (2k + 1)
// (A&S 4.1.29). Returns
// log(1 + f) - f, which is small, and e and f, so callers add the big parts exactly.
double logParts(double x, int& e, double& f) {
    double m = std::frexp(x, &e);
    if (m < kSqrtHalf) { m *= 2.0; --e; }
    f = m - 1.0;
    const double s = f / (2.0 + f), z = s * s, hfsq = 0.5 * f * f;
    return s * (hfsq + z * horner(kLog, 11, z)) - hfsq;
}

} // namespace

// Below 2^-27 sin x = x to the last bit, and the sign of -0 is kept.
double sin(double x) {
    if (!std::isfinite(x)) return kNaN;
    if (std::fabs(x) < 0x1p-27) return x;
    double y0, y1;
    switch (reduceHalfPi(x, y0, y1)) {
    case 0: return sinKernel(y0, y1);
    case 1: return cosKernel(y0, y1);
    case 2: return -sinKernel(y0, y1);
    default: return -cosKernel(y0, y1);
    }
}

double cos(double x) {
    if (!std::isfinite(x)) return kNaN;
    double y0, y1;
    switch (reduceHalfPi(x, y0, y1)) {
    case 0: return cosKernel(y0, y1);
    case 1: return -sinKernel(y0, y1);
    case 2: return -cosKernel(y0, y1);
    default: return sinKernel(y0, y1);
    }
}

// atan2 by octants: atan(small / big) is atanUnit, the others are pi/2 - that and pi - that
// (A&S 4.4.5), with the signed zeros and infinities of C99 Annex F.9.1.4.
double atan2(double y, double x) {
    if (std::isnan(x) || std::isnan(y)) return x + y;
    if (std::isinf(x) && std::isinf(y)) return std::copysign(x > 0 ? 0.25 * kPiHi : 0.75 * kPiHi, y);
    double angle; // in [0, pi], the sign of y added at the end
    const double ax = std::fabs(x), ay = std::fabs(y);
    if (y == 0.0 || std::isinf(x)) angle = std::signbit(x) ? kPiHi : 0.0;
    else if (x == 0.0 || std::isinf(y)) angle = kHalfPiHi;
    else {
        angle = ay <= ax ? atanRatio(ay, ax) : kHalfPiHi - (atanRatio(ax, ay) - kHalfPiLo);
        if (x < 0) angle = kPiHi - (angle - kPiLo);
    }
    return std::copysign(angle, y);
}

// asin x = atan2(x, sqrt(1 - x^2)), acos x = atan2(sqrt(1 - x^2), x) (A&S 4.4.3, 4.4.4) with
// 1 - x^2 as (1 - x)(1 + x): each factor is exact where it is small, so no digits cancel at |x| -> 1.
double asin(double x) {
    if (!(std::fabs(x) <= 1.0)) return kNaN;
    return atan2(x, std::sqrt((1.0 - x) * (1.0 + x)));
}

double acos(double x) {
    if (!(std::fabs(x) <= 1.0)) return kNaN;
    return atan2(std::sqrt((1.0 - x) * (1.0 + x)), x);
}

double exp(double x) {
    if (std::isnan(x)) return x;
    if (x > kExpMax) return kInfD;
    if (x < kExpMin) return 0.0;
    return expParts(x, 0.0);
}

// log x = e ln2 + f + (log(1 + f) - f), summed from the small terms up; e ln2Hi is exact.
double log(double x) {
    if (std::isnan(x) || x == kInfD) return x;
    if (x == 0.0) return -kInfD;
    if (x < 0.0) return kNaN;
    int e;
    double f;
    const double c = logParts(x, e, f);
    return e * kLn2Hi + (f + (c + e * kLn2Lo));
}

float hypot(float x, float y) {
    const double a = x, b = y;
    if (std::isinf(a) || std::isinf(b)) return std::numeric_limits<float>::infinity();
    return float(std::sqrt(a * a + b * b));
}

} // namespace ke
