#pragma once
// The core's one source of pseudo-random numbers, the same sequence on every compiler, standard
// library and CPU: a 32-bit linear congruential generator (Press et al., "Numerical Recipes in C",
// 2nd ed., 1992, 7.1, "ranqd1"; multiplier 1664525, increment 1013904223) and a float built from its
// top 24 bits. std::uniform_real_distribution is not used: the standard fixes mt19937's bits but
// not the distribution's algorithm, so libstdc++, MSVC and libc++ give different numbers.
//
// The vector draws take x, then y, then z. Never write Vector3(r.uniform(), r.uniform(), ...):
// C++ leaves the order of a function's arguments open, GCC evaluates them right to left and Clang
// left to right, and the same seed gives different bodies (a braced list {a(), b()} is ordered,
// but the habit is the trap).

#include "math/ElementaryFunctions.h"
#include "math/Vector3.h"

#include <cmath>
#include <cstdint>

namespace ke {

class Random {
public:
    explicit Random(uint32_t seed) : state_(seed) {}

    uint32_t nextBits() {
        state_ = state_ * 1664525u + 1013904223u; // modulo 2^32 by unsigned overflow
        return state_;
    }
    // [0, 1) in steps of 2^-24: every value is a float, and the scale by 2^-24 is exact.
    Real uniform() { return Real(nextBits() >> 8) * (1.0_r / 16777216.0_r); }
    Real uniform(Real lo, Real hi) { return lo + (hi - lo) * uniform(); }
    // [-1, 1).
    Real symmetric() { return uniform() * 2.0_r - 1.0_r; }

    Vector3 uniform3() {
        const Real x = uniform(), y = uniform(), z = uniform();
        return {x, y, z};
    }
    Vector3 uniform3(const Vector3& lo, const Vector3& hi) {
        const Real x = uniform(lo.x, hi.x), y = uniform(lo.y, hi.y), z = uniform(lo.z, hi.z);
        return {x, y, z};
    }
    Vector3 symmetric3() {
        const Real x = symmetric(), y = symmetric(), z = symmetric();
        return {x, y, z};
    }

    // A standard normal number, by Box and Muller (Ann. Math. Statist. 29, 1958): sqrt(-2 ln u1)
    // cos(2 pi u2) with u1 in (0, 1] and u2 in [0, 1) of 24 bits each (tails to 5.8 sigma), in double
    // through ke::log and ke::cos - the same bits everywhere. Two draws a call; the second of the pair
    // is not kept.
    double normal() {
        const double u1 = double((nextBits() >> 8) + 1) * (1.0 / 16777216.0);
        const double u2 = double(nextBits() >> 8) * (1.0 / 16777216.0);
        return std::sqrt(-2.0 * ke::log(u1)) * ke::cos(6.283185307179586 * u2);
    }

    uint32_t state() const { return state_; }

private:
    uint32_t state_;
};

} // namespace ke
