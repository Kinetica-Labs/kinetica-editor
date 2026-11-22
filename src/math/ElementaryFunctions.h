#pragma once
// The elementary functions of the core: ke::sin, cos, asin, acos, atan2, exp, log, hypot - the same
// bits on every compiler, C runtime and CPU.
//
// The C++ standard does not say how exact std::sin must be, so MinGW, glibc, MSVC and Apple's libm
// round its last bit differently, and a pile of boxes amplifies one bit into a different pile
// after a few seconds. These are computed in ElementaryFunctions.cpp only from
// + - * / and sqrt, which IEEE 754 rounds the same way everywhere, plus the exact floor, frexp and
// ldexp: Cody-Waite argument reduction and Taylor series, accurate to about one unit in the last
// place in double. The float overloads round the double result once. The core calls these, never
// std::sin and the like.

namespace ke {

double sin(double x);
double cos(double x);
double asin(double x);
double acos(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);

inline float sin(float x) { return float(sin(double(x))); }
inline float cos(float x) { return float(cos(double(x))); }
inline float asin(float x) { return float(asin(double(x))); }
inline float acos(float x) { return float(acos(double(x))); }
inline float atan2(float y, float x) { return float(atan2(double(y), double(x))); }
inline float exp(float x) { return float(exp(double(x))); }
inline float log(float x) { return float(log(double(x))); }
// In double the squares of two floats neither overflow nor lose a bit: one rounding at the end.
float hypot(float x, float y);

} // namespace ke
