#pragma once
// A mesh's points thinned for a convex hull: those extreme along n directions spread evenly over the sphere
// (a Fibonacci lattice). Their hull touches the hull of all the points in each of the n directions. A link's
// mesh of tens of thousands of points becomes a collider an engine can take (Box3D builds 128 vertices at most,
// plugins/physics/box3d) and a guide can draw at once (editor/ColliderGuides.h): both see the same points.
#include "math/Math.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ke {

constexpr int kHullDirections = 120; // a mesh's collider: its extremes along 120 directions

inline std::vector<Vector3> extremePoints(const std::vector<Vector3>& points, int directions) {
    if (int(points.size()) <= directions) return points;
    std::vector<Vector3> picked;
    for (int k = 0; k < directions; ++k) {
        const Real y = 1 - 2 * (Real(k) + 0.5_r) / Real(directions), r = std::sqrt(std::max(0.0_r, 1 - y * y)), a = 2.39996323_r * Real(k);
        const Vector3 d(r * std::cos(a), y, r * std::sin(a));
        const Vector3& extreme = *std::max_element(points.begin(), points.end(), [&](const Vector3& p, const Vector3& q) { return dot(p, d) < dot(q, d); });
        if (std::none_of(picked.begin(), picked.end(), [&](const Vector3& p) { return p.x == extreme.x && p.y == extreme.y && p.z == extreme.z; }))
            picked.push_back(extreme);
    }
    return picked;
}

} // namespace ke
