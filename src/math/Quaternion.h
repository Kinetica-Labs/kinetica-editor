#pragma once
// Unit quaternion (float) for orientations: q = (w, x, y, z) = (cos(a/2), axis * sin(a/2)).

#include "math/ElementaryFunctions.h"
#include "math/Matrix3x3.h"

namespace ke {

struct Quaternion {
    Real w = 1, x = 0, y = 0, z = 0;

    static Quaternion fromAxisAngle(const Vector3& axis, Real angle) {
        Vector3 a = normalize(axis);
        Real s = ke::sin(angle * 0.5_r);
        return {ke::cos(angle * 0.5_r), a.x * s, a.y * s, a.z * s};
    }
    // Intrinsic rotations: yaw about Y, pitch about Z, roll about X (radians).
    static Quaternion fromEuler(Real yaw, Real pitch, Real roll) {
        return fromAxisAngle({0, 1, 0}, yaw) * fromAxisAngle({0, 0, 1}, pitch) * fromAxisAngle({1, 0, 0}, roll);
    }
    static Quaternion fromMatrix3x3(const Matrix3x3& R) {
        const auto& m = R.m;
        Real tr = m[0][0] + m[1][1] + m[2][2];
        Quaternion q;
        if (tr > 0) {
            Real s = std::sqrt(tr + 1.0_r) * 2.0_r;
            q = {0.25_r * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s};
        } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            Real s = std::sqrt(1.0_r + m[0][0] - m[1][1] - m[2][2]) * 2.0_r;
            q = {(m[2][1] - m[1][2]) / s, 0.25_r * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s};
        } else if (m[1][1] > m[2][2]) {
            Real s = std::sqrt(1.0_r + m[1][1] - m[0][0] - m[2][2]) * 2.0_r;
            q = {(m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25_r * s, (m[1][2] + m[2][1]) / s};
        } else {
            Real s = std::sqrt(1.0_r + m[2][2] - m[0][0] - m[1][1]) * 2.0_r;
            q = {(m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25_r * s};
        }
        return q.normalized();
    }
    // Shortest-arc rotation taking direction a to direction b.
    static Quaternion fromTwoVectors(const Vector3& a, const Vector3& b) {
        Vector3 u = normalize(a), v = normalize(b);
        Real c = dot(u, v);
        if (c < -0.999999_r) return fromAxisAngle(anyPerpendicular(u), kPi); // opposite: any perpendicular axis
        Vector3 axis = cross(u, v);
        return Quaternion{1.0_r + c, axis.x, axis.y, axis.z}.normalized();
    }
    // Exponential map so(3) -> SO(3): rotation by the rotation vector r (axis * angle).
    static Quaternion fromRotationVector(const Vector3& r) { return Quaternion().integrated(r, 1.0_r); }

    Quaternion operator*(const Quaternion& b) const {
        return {w * b.w - x * b.x - y * b.y - z * b.z,
                w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w};
    }
    Quaternion normalized() const {
        Real l = std::sqrt(w * w + x * x + y * y + z * z);
        return l > 0 ? Quaternion{w / l, x / l, y / l, z / l} : Quaternion{};
    }
    Quaternion conjugate() const { return {w, -x, -y, -z}; }
    // Inverse of any non-zero quaternion (the conjugate for unit ones).
    Quaternion inverse() const {
        Real n = w * w + x * x + y * y + z * z;
        return n > 0 ? Quaternion{w / n, -x / n, -y / n, -z / n} : Quaternion{};
    }
    Matrix3x3 toMatrix3x3() const {
        Matrix3x3 r;
        r.m[0][0] = 1 - 2 * (y * y + z * z); r.m[0][1] = 2 * (x * y - w * z);     r.m[0][2] = 2 * (x * z + w * y);
        r.m[1][0] = 2 * (x * y + w * z);     r.m[1][1] = 1 - 2 * (x * x + z * z); r.m[1][2] = 2 * (y * z - w * x);
        r.m[2][0] = 2 * (x * z - w * y);     r.m[2][1] = 2 * (y * z + w * x);     r.m[2][2] = 1 - 2 * (x * x + y * y);
        return r;
    }
    Vector3 rotate(const Vector3& v) const { return toMatrix3x3() * v; }
    // Rotation angle in [0, pi].
    Real angle() const { return 2.0_r * ke::atan2(std::sqrt(x * x + y * y + z * z), std::fabs(w)); }

    // Logarithm SO(3) -> so(3): rotation vector (axis * angle) of this unit quaternion, shortest arc.
    // theta = 2 atan2(|v|, w); for small |v| the factor theta/|v| = (2/w)(1 - |v|^2/(3 w^2) + ...)
    // is evaluated by its Taylor series.
    Vector3 log() const {
        Quaternion q = w < 0 ? Quaternion{-w, -x, -y, -z} : *this;
        Real s2 = q.x * q.x + q.y * q.y + q.z * q.z;
        Real f;
        if (s2 < 1e-8_r) {
            Real r = s2 / (q.w * q.w);
            f = 2.0_r / q.w * (1.0_r - r / 3.0_r + r * r / 5.0_r);
        } else {
            Real s = std::sqrt(s2);
            f = 2.0_r * ke::atan2(s, q.w) / s;
        }
        return {q.x * f, q.y * f, q.z * f};
    }
    // Exact rotation by the angular velocity over dt (exponential map): q' = exp(omega*dt/2) * q.
    // For small half-angles th, sin(th)/th and cos(th) come from their Taylor series, which avoids
    // the 0/0 at th -> 0 and keeps full precision where the closed form loses it.
    Quaternion integrated(const Vector3& omega, Real dt) const {
        const Real th = 0.5_r * dt * std::sqrt(omega.x * omega.x + omega.y * omega.y + omega.z * omega.z);
        Real c, sinc; // cos(th), sin(th)/th
        if (th < 1e-2_r) {
            const Real t2 = th * th;
            c = 1.0_r - t2 * (0.5_r - t2 / 24.0_r);           // 1 - th^2/2 + th^4/24
            sinc = 1.0_r - t2 * (1.0_r / 6.0_r - t2 / 120.0_r); // 1 - th^2/6 + th^4/120
        } else {
            c = ke::cos(th);
            sinc = ke::sin(th) / th;
        }
        const Real k = 0.5_r * dt * sinc; // vector part = omega/|omega| * sin(th) = omega * dt/2 * sinc
        Quaternion e{c, omega.x * k, omega.y * k, omega.z * k};
        return (e * (*this)).normalized();
    }
};

inline Real dot(const Quaternion& a, const Quaternion& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
// |q|^2 in double (exact products), for the unit-norm checks of the solvers.

// Spherical linear interpolation along the shortest arc (t = 0 -> a, t = 1 -> b).
inline Quaternion slerp(const Quaternion& a, Quaternion b, Real t) {
    Real c = dot(a, b);
    if (c < 0) { b = Quaternion{-b.w, -b.x, -b.y, -b.z}; c = -c; } // same rotation, shorter way round
    if (c > 0.9995_r) { // nearly parallel: linear interpolation is exact enough and stable
        Quaternion r{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        return r.normalized();
    }
    Real theta = ke::acos(c);
    Real wa = ke::sin((1 - t) * theta) / ke::sin(theta), wb = ke::sin(t * theta) / ke::sin(theta);
    return Quaternion{a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb}.normalized();
}

// Rotational part R of a 3x3 matrix A (A = R S, the polar decomposition), found iteratively:
// each step rotates R towards the columns of A. Robust for degenerate / inverted A;
// start from last frame's rotation and a few iterations suffice.
inline Quaternion extractRotation(const Matrix3x3& A, Quaternion q, int iterations = 20) {
    for (int it = 0; it < iterations; ++it) {
        const Matrix3x3 R = q.toMatrix3x3();
        Vector3 torque(0.0_r);
        Real align = 0;
        for (int c = 0; c < 3; ++c) {
            torque += cross(R.col(c), A.col(c));
            align += dot(R.col(c), A.col(c));
        }
        const Vector3 omega = torque / (std::fabs(align) + 1e-9_r);
        const Real w = length(omega);
        if (w < 1e-9_r) break;
        q = (Quaternion::fromAxisAngle(omega / w, w) * q).normalized();
    }
    return q;
}

} // namespace ke
