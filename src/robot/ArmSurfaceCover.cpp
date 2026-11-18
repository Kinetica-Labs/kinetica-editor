// A sphere is convex: containing all three vertices contains their triangle.
// Subdivide long edges, assign each small triangle by its centroid to a spatial cell,
// and enclose the union of its vertices by the sphere of the accumulated bounding box.
#include "robot/ArmSurfaceCover.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace ke {
namespace {
struct Bounds { Vector3 lo, hi; };
using Cells = std::map<std::array<int, 3>, Bounds>;

void put(Cells& cells, Real cell, const Vector3& a, const Vector3& b, const Vector3& c) {
    const Vector3 centre = (a + b + c) / 3;
    std::array<int, 3> key;
    for (int axis = 0; axis < 3; ++axis) {
        const double index = std::floor(double(centre[axis]) / double(cell));
        if (!std::isfinite(index) || index <= double(std::numeric_limits<int>::min()) ||
            index >= double(std::numeric_limits<int>::max())) throw std::invalid_argument("surface cover cell overflow");
        key[size_t(axis)] = int(index);
    }
    const Vector3 lo = vmin(a, vmin(b, c)), hi = vmax(a, vmax(b, c));
    const auto inserted = cells.emplace(key, Bounds{lo, hi});
    if (!inserted.second) {
        auto& bounds = inserted.first->second;
        bounds.lo = vmin(bounds.lo, lo); bounds.hi = vmax(bounds.hi, hi);
    }
}

void triangle(Cells& cells, Real cell, const Vector3& a, const Vector3& b, const Vector3& c) {
    const Real longest = std::max({length(b - a), length(c - b), length(a - c)});
    const double count = std::ceil(double(longest) / (double(cell) * 0.5));
    if (!std::isfinite(count) || count > 2048) throw std::invalid_argument("surface cover subdivision budget exceeded");
    const int n = std::max(1, int(count));
    const auto p = [&](int i, int j) { return a + (b - a) * (Real(i) / n) + (c - a) * (Real(j) / n); };
    for (int i = 0; i < n; ++i) for (int j = 0; i + j < n; ++j) {
        put(cells, cell, p(i,j), p(i+1,j), p(i,j+1));
        if (i + j + 1 < n) put(cells, cell, p(i+1,j), p(i+1,j+1), p(i,j+1));
    }
}
} // namespace

std::vector<ArmSphere> armSurfaceCover(const TriMesh& mesh, Real cell) {
    if (!std::isfinite(cell) || cell <= 0) throw std::invalid_argument("surface cover cell must be positive");
    for (const auto& p : mesh.positions)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) throw std::invalid_argument("invalid surface vertex");
    Cells cells;
    for (const auto& t : mesh.triangles) {
        for (auto index : t) if (index >= mesh.positions.size()) throw std::invalid_argument("invalid surface triangle");
        triangle(cells, cell, mesh.positions[t[0]], mesh.positions[t[1]], mesh.positions[t[2]]);
    }
    std::vector<ArmSphere> result;
    for (const auto& item : cells) {
        const auto& box = item.second;
        const Vector3 centre = box.lo * 0.5_r + box.hi * 0.5_r;
        // Roundoff allowance for interpolation, bounding-box centre and radius operations.
        const Real scale = 1 + length(box.lo) + length(box.hi);
        const Real radius = length(box.hi - box.lo) * 0.5_r + 32 * std::numeric_limits<Real>::epsilon() * scale;
        if (!std::isfinite(radius)) throw std::invalid_argument("surface cover radius overflow");
        result.push_back({centre, radius});
    }
    return result;
}
} // namespace ke
