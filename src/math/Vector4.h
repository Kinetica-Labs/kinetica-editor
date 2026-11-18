#pragma once
// 4D vector (float): homogeneous coordinates, RGBA.

#include "math/Vector3.h"

namespace ke {

struct Vector4 {
    Real x = 0, y = 0, z = 0, w = 0;

    constexpr Vector4() = default;
    constexpr Vector4(Real x_, Real y_, Real z_, Real w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vector4(const Vector3& v, Real w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    explicit constexpr Vector4(Real s) : x(s), y(s), z(s), w(s) {}

    Real& operator[](int i) { return (&x)[i]; }
    Real operator[](int i) const { return (&x)[i]; }
    constexpr Vector3 xyz() const { return {x, y, z}; }

    constexpr Vector4 operator+(const Vector4& b) const { return {x + b.x, y + b.y, z + b.z, w + b.w}; }
    constexpr Vector4 operator-(const Vector4& b) const { return {x - b.x, y - b.y, z - b.z, w - b.w}; }
    constexpr Vector4 operator*(const Vector4& b) const { return {x * b.x, y * b.y, z * b.z, w * b.w}; }
    constexpr Vector4 operator*(Real s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr Vector4 operator/(Real s) const { return {x / s, y / s, z / s, w / s}; }
    constexpr Vector4 operator-() const { return {-x, -y, -z, -w}; }
    Vector4& operator+=(const Vector4& b) { x += b.x; y += b.y; z += b.z; w += b.w; return *this; }
    Vector4& operator-=(const Vector4& b) { x -= b.x; y -= b.y; z -= b.z; w -= b.w; return *this; }
    Vector4& operator*=(Real s) { x *= s; y *= s; z *= s; w *= s; return *this; }
};

constexpr Vector4 operator*(Real s, const Vector4& v) { return v * s; }
constexpr Real dot(const Vector4& a, const Vector4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Real length2(const Vector4& a) { return dot(a, a); }
inline Real length(const Vector4& a) { return std::sqrt(dot(a, a)); }
inline Vector4 normalize(const Vector4& a) {
    Real l = length(a);
    return l > 1e-20_r ? a / l : Vector4(0.0_r);
}
inline Vector4 lerp(const Vector4& a, const Vector4& b, Real t) { return a + (b - a) * t; }

} // namespace ke
