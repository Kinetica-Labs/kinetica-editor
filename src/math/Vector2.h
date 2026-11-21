#pragma once
// 2D vector (float).

#include "math/Real.h"

namespace ke {

struct Vector2 {
    Real x = 0, y = 0;

    constexpr Vector2() = default;
    constexpr Vector2(Real x_, Real y_) : x(x_), y(y_) {}
    explicit constexpr Vector2(Real s) : x(s), y(s) {}

    Real& operator[](int i) { return (&x)[i]; }
    Real operator[](int i) const { return (&x)[i]; }

    constexpr Vector2 operator+(const Vector2& b) const { return {x + b.x, y + b.y}; }
    constexpr Vector2 operator-(const Vector2& b) const { return {x - b.x, y - b.y}; }
    constexpr Vector2 operator*(const Vector2& b) const { return {x * b.x, y * b.y}; }
    constexpr Vector2 operator/(const Vector2& b) const { return {x / b.x, y / b.y}; }
    constexpr Vector2 operator*(Real s) const { return {x * s, y * s}; }
    constexpr Vector2 operator/(Real s) const { return {x / s, y / s}; }
    constexpr Vector2 operator-() const { return {-x, -y}; }
    Vector2& operator+=(const Vector2& b) { x += b.x; y += b.y; return *this; }
    Vector2& operator-=(const Vector2& b) { x -= b.x; y -= b.y; return *this; }
    Vector2& operator*=(Real s) { x *= s; y *= s; return *this; }
    Vector2& operator/=(Real s) { x /= s; y /= s; return *this; }
};

constexpr Vector2 operator*(Real s, const Vector2& v) { return v * s; }
constexpr Real dot(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
// z component of the 3D cross product (signed parallelogram area).
constexpr Real cross(const Vector2& a, const Vector2& b) { return a.x * b.y - a.y * b.x; }
// Counter-clockwise perpendicular.
constexpr Vector2 perp(const Vector2& a) { return {-a.y, a.x}; }
inline Real length2(const Vector2& a) { return dot(a, a); }
inline Real length(const Vector2& a) { return std::sqrt(dot(a, a)); }
inline Vector2 normalize(const Vector2& a) {
    Real l = length(a);
    return l > 1e-20_r ? a / l : Vector2(0, 0);
}
inline Vector2 vmin(const Vector2& a, const Vector2& b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline Vector2 vmax(const Vector2& a, const Vector2& b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline Vector2 lerp(const Vector2& a, const Vector2& b, Real t) { return a + (b - a) * t; }

} // namespace ke
