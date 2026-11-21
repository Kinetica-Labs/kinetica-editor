#pragma once
// The core's number type and the scalar constants and helpers shared by the math types.
//
// ke::Real is float, or double when the core is built with KE_DOUBLE (ke/Config.h): one switch for
// every solver. Literals are written 1.5_r, a Real in both
// builds: 1.5f would stay float in the double build and drop digits, 1.5 would turn float
// arithmetic into double. Code that must stay float whatever the build (pixels, GPU buffers) says
// float and is marked ke:float.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "math/ElementaryFunctions.h"
#include "ke/Config.h"

namespace ke {

#if KE_DOUBLE
using Real = double;
#else
using Real = float; // ke:float
#endif

// 1.5_r, 2_r: the literal rounded once to Real (the decimal is read as long double first).
constexpr Real operator""_r(long double v) { return Real(v); }
constexpr Real operator""_r(unsigned long long v) { return Real(v); }

constexpr Real kPi = 3.14159265358979323846_r;
constexpr Real kInf = std::numeric_limits<Real>::infinity();

template <class T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Real sqr(Real v) { return v * v; }
inline Real lerp(Real a, Real b, Real t) { return a + (b - a) * t; }
inline Real degToRad(Real d) { return d * (kPi / 180.0_r); }
inline Real radToDeg(Real r) { return r * (180.0_r / kPi); }

} // namespace ke
