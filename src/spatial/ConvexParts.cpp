// The convex parts of a solid mesh (ConvexParts.h): its voxels, the cuts of the most concave part, the hull that judges
// them, and the mesh's own points of each part.
#include "spatial/ConvexParts.h"

#include "math/ElementaryFunctions.h"
#include "spatial/HullPoints.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>

namespace ke {

ConvexHull convexHull(const std::vector<Vector3>& pts) {
    ConvexHull hull;
    const size_t n = pts.size();
    if (n < 4) return hull;
    AABB box;
    for (const Vector3& p : pts) box.expand(p);
    const Real scale = maxComp(box.extent());
    if (!(scale > 0)) return hull;
    const Real eps = scale * (sizeof(Real) == sizeof(float) ? 1e-5_r : 1e-10_r);
    // A first tetrahedron: the point least in x, the one farthest from it, the farthest from their line, from their plane.
    const auto farthest = [&](auto measure) {
        size_t best = 0;
        Real far = -1;
        for (size_t i = 0; i < n; ++i)
            if (const Real m = measure(pts[i]); m > far) far = m, best = i;
        return std::pair{best, far};
    };
    const size_t i0 = farthest([](const Vector3& p) { return -p.x; }).first;
    const auto [i1, d1] = farthest([&](const Vector3& p) { return length2(p - pts[i0]); });
    if (d1 <= eps * eps) return hull;
    const Vector3 line = normalize(pts[i1] - pts[i0]);
    const auto [i2, d2] = farthest([&](const Vector3& p) { const Vector3 v = p - pts[i0]; return length2(v - line * dot(v, line)); });
    if (d2 <= eps * eps) return hull;
    const Vector3 flat = normalize(cross(pts[i1] - pts[i0], pts[i2] - pts[i0]));
    const auto [i3, d3] = farthest([&](const Vector3& p) { return std::fabs(dot(p - pts[i0], flat)); });
    if (d3 <= eps) return hull;
    const Vector3 inside = (pts[i0] + pts[i1] + pts[i2] + pts[i3]) * 0.25_r; // stays inside as the hull grows

    struct Face {
        uint32_t v[3];
        Vector3 n;
        Real d;
        bool alive;
    };
    std::vector<Face> faces;
    std::map<std::pair<uint32_t, uint32_t>, size_t> edges; // a directed edge -> the face it bounds
    const auto add = [&](uint32_t a, uint32_t b, uint32_t c) {
        Vector3 normal = cross(pts[b] - pts[a], pts[c] - pts[a]);
        if (dot(normal, pts[a] - inside) < 0) std::swap(b, c), normal = -normal; // outward
        const Real len = length(normal);
        Face f{{a, b, c}, len > 0 ? normal / len : normal, 0, true};
        f.d = dot(f.n, pts[a]);
        faces.push_back(f);
        for (int k = 0; k < 3; ++k) edges[{f.v[k], f.v[(k + 1) % 3]}] = faces.size() - 1;
    };
    add(uint32_t(i0), uint32_t(i1), uint32_t(i2)), add(uint32_t(i0), uint32_t(i1), uint32_t(i3));
    add(uint32_t(i0), uint32_t(i2), uint32_t(i3)), add(uint32_t(i1), uint32_t(i2), uint32_t(i3));
    std::vector<size_t> visible;
    std::vector<std::pair<uint32_t, uint32_t>> horizon;
    for (size_t p = 0; p < n; ++p) {
        if (p == i0 || p == i1 || p == i2 || p == i3) continue;
        visible.clear(), horizon.clear();
        for (size_t f = 0; f < faces.size(); ++f)
            if (faces[f].alive && dot(faces[f].n, pts[p]) - faces[f].d > eps) visible.push_back(f);
        if (visible.empty()) continue; // inside
        for (size_t f : visible) faces[f].alive = false;
        for (size_t f : visible) // an edge between a face the point sees and one it does not: the new faces stand on it
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = faces[f].v[k], b = faces[f].v[(k + 1) % 3];
                if (const auto it = edges.find({b, a}); it != edges.end() && faces[it->second].alive) horizon.push_back({a, b});
            }
        for (size_t f : visible)
            for (int k = 0; k < 3; ++k) edges.erase({faces[f].v[k], faces[f].v[(k + 1) % 3]});
        for (const auto& [a, b] : horizon) add(a, b, uint32_t(p));
    }
    for (const Face& f : faces) {
        if (!f.alive) continue;
        hull.normals.push_back(f.n), hull.offsets.push_back(f.d);
        for (uint32_t v : f.v) hull.triangles.push_back(pts[v]);
        hull.volume += dot(pts[f.v[0]] - inside, cross(pts[f.v[1]] - inside, pts[f.v[2]] - inside)) / 6;
    }
    return hull;
}

namespace {

// Directions spread evenly over the sphere (a Fibonacci lattice): a part's corners farthest along them span its hull.
std::vector<Vector3> lattice(int count) {
    std::vector<Vector3> d;
    for (int k = 0; k < count; ++k) {
        const Real y = 1 - 2 * (Real(k) + 0.5_r) / Real(count), r = std::sqrt(std::max(0.0_r, 1 - y * y)), a = 2.39996323_r * Real(k);
        d.emplace_back(r * ke::cos(a), y, r * ke::sin(a));
    }
    return d;
}
constexpr int kJudge = 64, kCut = 32; // directions a part's hull is judged by, and a cut's two sides

// A triangle and a box overlap unless an axis separates them (the 13 axes of their faces and edges): the box centred at c, of half size r, and the triangle a b d.
bool overlaps(const Vector3& c, Real r, const Vector3& a, const Vector3& b, const Vector3& d) {
    const Vector3 v[3] = {a - c, b - c, d - c};
    const auto apart = [&](const Vector3& axis) {
        const Real p0 = dot(axis, v[0]), p1 = dot(axis, v[1]), p2 = dot(axis, v[2]);
        const Real reach = r * (std::fabs(axis.x) + std::fabs(axis.y) + std::fabs(axis.z));
        return std::min(p0, std::min(p1, p2)) > reach || std::max(p0, std::max(p1, p2)) < -reach;
    };
    const Vector3 e[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
    const Vector3 unit[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (const Vector3& u : unit) {
        if (apart(u)) return false;
        for (const Vector3& edge : e)
            if (apart(cross(u, edge))) return false;
    }
    return !apart(cross(e[0], e[1]));
}

enum Cell : uint8_t { Unknown, Surface, Outside, Inside };

struct Voxels {
    int n[3] = {1, 1, 1};
    Vector3 origin;
    Real h = 1;
    std::vector<uint8_t> cell;
    std::vector<Real> air; // how far a voxel of the air is from the solid (a chamfer distance, steps 3-4-5, in metres); 0 in the solid
    int index(int i, int j, int k) const { return i + n[0] * (j + n[1] * k); }
    int at(int v, int axis) const { return axis == 0 ? v % n[0] : axis == 1 ? (v / n[0]) % n[1] : v / (n[0] * n[1]); }
    Vector3 centre(int v) const {
        return origin + Vector3((Real(at(v, 0)) + 0.5_r) * h, (Real(at(v, 1)) + 0.5_r) * h, (Real(at(v, 2)) + 0.5_r) * h);
    }
    int step(Real x, int axis) const { return std::clamp(int(std::floor((x - origin[axis]) / h)), 0, n[axis] - 1); }
    int of(const Vector3& p) const { return index(step(p.x, 0), step(p.y, 1), step(p.z, 2)); }
    template <class F> void around(int v, bool diagonal, F f) const { // the 6 (or 26) neighbours inside the grid
        const int i = at(v, 0), j = at(v, 1), k = at(v, 2);
        for (int dk = -1; dk <= 1; ++dk)
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    if ((!di && !dj && !dk) || (!diagonal && std::abs(di) + std::abs(dj) + std::abs(dk) != 1)) continue;
                    if (i + di < 0 || j + dj < 0 || k + dk < 0 || i + di >= n[0] || j + dj >= n[1] || k + dk >= n[2]) continue;
                    f(index(i + di, j + dj, k + dk));
                }
    }
};

// The voxels a mesh's surface passes through, the ones outside (reached from the grid's border without crossing it) and
// the rest inside; and points spread over the surface (a voxel apart) with the voxel each is in.
Voxels voxelize(const TriMesh& mesh, int resolution, std::vector<std::pair<int, Vector3>>& samples) {
    Voxels g;
    const AABB box = mesh.bounds();
    const Vector3 extent = box.extent();
    g.h = maxComp(extent) / Real(std::max(resolution, 4));
    // a free layer all round, half a voxel off the mesh: no face touches it, the outside flows round
    for (int a = 0; a < 3; ++a) g.n[a] = std::max(1, int(std::ceil(extent[a] / g.h))) + 3;
    g.origin = box.lo - Vector3(1.5_r * g.h);
    g.cell.assign(size_t(g.n[0]) * size_t(g.n[1]) * size_t(g.n[2]), Unknown);
    const Real half = 0.5_r * g.h;
    for (const auto& t : mesh.triangles) {
        const Vector3 &a = mesh.positions[t[0]], &b = mesh.positions[t[1]], &d = mesh.positions[t[2]];
        AABB around;
        around.expand(a), around.expand(b), around.expand(d);
        for (int k = g.step(around.lo.z, 2); k <= g.step(around.hi.z, 2); ++k)
            for (int j = g.step(around.lo.y, 1); j <= g.step(around.hi.y, 1); ++j)
                for (int i = g.step(around.lo.x, 0); i <= g.step(around.hi.x, 0); ++i)
                    if (const int v = g.index(i, j, k); g.cell[v] != Surface && overlaps(g.centre(v), half, a, b, d)) g.cell[v] = Surface;
        const Real longest = std::max(length(b - a), std::max(length(d - b), length(a - d)));
        const int m = std::clamp(int(std::ceil(longest / g.h)), 1, 256);
        for (int i = 0; i <= m; ++i)
            for (int j = 0; i + j <= m; ++j) {
                const Vector3 p = a + (b - a) * (Real(i) / Real(m)) + (d - a) * (Real(j) / Real(m));
                const int v = g.of(p);
                g.cell[v] = Surface;
                samples.push_back({v, p});
            }
    }
    std::vector<int> stack{0}; // the grid's corner is in the free layer
    g.cell[0] = Outside;
    while (!stack.empty()) {
        const int v = stack.back();
        stack.pop_back();
        g.around(v, false, [&](int w) {
            if (g.cell[w] == Unknown) g.cell[w] = Outside, stack.push_back(w);
        });
    }
    for (uint8_t& c : g.cell)
        if (c == Unknown) c = Inside;
    // The distance to the solid: two raster passes, each taking the 13 neighbours already passed.
    g.air.assign(g.cell.size(), 0);
    for (size_t v = 0; v < g.cell.size(); ++v)
        if (g.cell[v] == Outside) g.air[v] = kInf;
    const Real step[4] = {0, g.h, g.h * 4 / 3, g.h * 5 / 3};
    for (int pass = 0; pass < 2; ++pass) {
        const int sign = pass ? -1 : 1, total = int(g.cell.size());
        for (int v = pass ? total - 1 : 0; v >= 0 && v < total; v += sign) {
            if (g.air[v] == 0) continue;
            const int i = g.at(v, 0), j = g.at(v, 1), k = g.at(v, 2);
            for (int dk = -1; dk <= 0; ++dk)
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        if (dk == 0 && (dj > 0 || (dj == 0 && di >= 0))) continue; // only those before v in the pass
                        const int x = i + sign * di, y = j + sign * dj, z = k + sign * dk;
                        if (x < 0 || y < 0 || z < 0 || x >= g.n[0] || y >= g.n[1] || z >= g.n[2]) continue;
                        g.air[v] = std::min(g.air[v], g.air[g.index(x, y, z)] + step[std::abs(di) + std::abs(dj) + std::abs(dk)]);
                    }
        }
    }
    return g;
}

struct Part {
    std::vector<int> voxels;
    std::vector<Vector3> points; // the mesh's surface points in it: cut exactly by its planes, not by its voxels
    Real concavity = 0;
    bool done = false; // no cut found, or no room for its pieces
};

// The corners of a set of voxels farthest along each direction, each once.
std::vector<Vector3> farthest(const Voxels& g, const std::vector<int>& voxels, const std::vector<Vector3>& dirs) {
    std::vector<Real> best(dirs.size(), -kInf);
    std::vector<Vector3> at(dirs.size());
    const Real half = 0.5_r * g.h;
    for (int v : voxels) {
        const Vector3 c = g.centre(v);
        for (size_t k = 0; k < dirs.size(); ++k) {
            const Vector3& d = dirs[k];
            const Vector3 corner = c + Vector3(d.x >= 0 ? half : -half, d.y >= 0 ? half : -half, d.z >= 0 ? half : -half);
            if (const Real s = dot(corner, d); s > best[k]) best[k] = s, at[k] = corner;
        }
    }
    std::sort(at.begin(), at.end(), [](const Vector3& p, const Vector3& q) { return std::tie(p.x, p.y, p.z) < std::tie(q.x, q.y, q.z); });
    at.erase(std::unique(at.begin(), at.end(), [](const Vector3& p, const Vector3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; }), at.end());
    return at;
}

// How far a part's hull stands out into the air: the farthest from the solid of points spread over its faces, a voxel
// apart: the hull's surface against the shape.
Real concavityOf(const Voxels& g, const Part& part, const std::vector<Vector3>& dirs) {
    const ConvexHull hull = convexHull(farthest(g, part.voxels, dirs));
    Real farthestOut = 0;
    for (size_t t = 0; t + 2 < hull.triangles.size(); t += 3) {
        const Vector3 &a = hull.triangles[t], &b = hull.triangles[t + 1], &d = hull.triangles[t + 2];
        const Real longest = std::max(length(b - a), std::max(length(d - b), length(a - d)));
        const int m = std::clamp(int(std::ceil(longest / g.h)), 1, 128);
        for (int i = 0; i <= m; ++i)
            for (int j = 0; i + j <= m; ++j)
                farthestOut = std::max(farthestOut, g.air[g.of(a + (b - a) * (Real(i) / Real(m)) + (d - a) * (Real(j) / Real(m)))]);
    }
    return farthestOut;
}

// The plane across one axis that cuts a part into two whose hulls hold the least volume: every boundary between its
// layers, each judged by its two sides' farthest corners - the farthest of each layer along each direction, gathered
// from either end. The plane itself goes through a vertex of the mesh between the two layers' centres when there is one
// (`stops`: the mesh's vertex coordinates along each axis, sorted) - a model's own face, a floor, a wall's foot - else
// halfway.
bool bestCut(const Voxels& g, const Part& part, const std::vector<Vector3>& dirs, const std::vector<Real> (&stops)[3], int& axisOut,
             Real& atOut) {
    const size_t D = dirs.size();
    const Real half = 0.5_r * g.h;
    Real cheapest = kInf;
    for (int axis = 0; axis < 3; ++axis) {
        int lo = g.n[axis], hi = -1;
        for (int v : part.voxels) lo = std::min(lo, g.at(v, axis)), hi = std::max(hi, g.at(v, axis));
        const int layers = hi - lo + 1;
        if (layers < 2) continue;
        std::vector<Real> best(size_t(layers) * D, -kInf);
        std::vector<Vector3> at(size_t(layers) * D);
        for (int v : part.voxels) {
            const Vector3 c = g.centre(v);
            const size_t l = size_t(g.at(v, axis) - lo) * D;
            for (size_t k = 0; k < D; ++k) {
                const Vector3& d = dirs[k];
                const Vector3 corner = c + Vector3(d.x >= 0 ? half : -half, d.y >= 0 ? half : -half, d.z >= 0 ? half : -half);
                if (const Real s = dot(corner, d); s > best[l + k]) best[l + k] = s, at[l + k] = corner;
            }
        }
        std::vector<Real> fromLo = best, fromHi = best; // the farthest over the layers up to l, and from l on
        std::vector<Vector3> loAt = at, hiAt = at;
        for (int l = 1; l < layers; ++l)
            for (size_t k = 0; k < D; ++k)
                if (fromLo[(l - 1) * D + k] > fromLo[l * D + k]) fromLo[l * D + k] = fromLo[(l - 1) * D + k], loAt[l * D + k] = loAt[(l - 1) * D + k];
        for (int l = layers - 2; l >= 0; --l)
            for (size_t k = 0; k < D; ++k)
                if (fromHi[(l + 1) * D + k] > fromHi[l * D + k]) fromHi[l * D + k] = fromHi[(l + 1) * D + k], hiAt[l * D + k] = hiAt[(l + 1) * D + k];
        const auto side = [&](const std::vector<Vector3>& points, int l) {
            return convexHull(std::vector<Vector3>(points.begin() + long(size_t(l) * D), points.begin() + long(size_t(l + 1) * D))).volume;
        };
        const int even = std::min(15, layers - 1);
        for (int cut = 1; cut < layers; ++cut) { // the first layer of the upper side
            const Real below = g.origin[axis] + (Real(lo + cut) - 0.5_r) * g.h, above = below + g.h, middle = below + half;
            const std::vector<Real>& v = stops[axis];
            bool onVertex = false;
            Real at = middle;
            for (auto it = std::upper_bound(v.begin(), v.end(), below); it != v.end() && *it < above; ++it) // nearest the middle
                if (!onVertex || std::fabs(*it - middle) < std::fabs(at - middle)) at = *it, onVertex = true;
            const bool evenly = (cut * (even + 1)) % layers < even + 1; // one of `even` planes spread over the part
            if (!onVertex && !evenly) continue;
            // a plane through the model's own faces wins when it costs within 2 % of a plane through none
            if (const Real cost = (side(loAt, cut - 1) + side(hiAt, cut)) * (onVertex ? 0.98_r : 1); cost < cheapest)
                cheapest = cost, axisOut = axis, atOut = at;
        }
    }
    return cheapest < kInf;
}

// A set of voxels as its connected pieces (26 neighbours), each in the order its voxels came.
std::vector<std::vector<int>> pieces(const Voxels& g, const std::vector<int>& voxels, std::vector<int>& mark, int& stamp) {
    const int member = ++stamp, seen = ++stamp;
    for (int v : voxels) mark[v] = member;
    std::vector<std::vector<int>> out;
    std::vector<int> stack;
    for (int v : voxels) {
        if (mark[v] != member) continue;
        out.emplace_back();
        mark[v] = seen, stack.push_back(v);
        while (!stack.empty()) {
            const int w = stack.back();
            stack.pop_back();
            out.back().push_back(w);
            g.around(w, true, [&](int x) {
                if (mark[x] == member) mark[x] = seen, stack.push_back(x);
            });
        }
        std::sort(out.back().begin(), out.back().end());
    }
    return out;
}

std::vector<std::vector<Vector3>> decompose(const TriMesh& mesh, const ConvexPartsOptions& o) {
    if (mesh.empty() || !(maxComp(mesh.bounds().extent()) > 0)) return {};
    std::vector<std::pair<int, Vector3>> samples;
    const Voxels g = voxelize(mesh, o.resolution, samples);
    const std::vector<Vector3> dirs = lattice(kJudge), cutDirs = lattice(kCut);
    const Real limit = o.concavity * length(mesh.bounds().extent());
    std::vector<Real> stops[3];
    for (int a = 0; a < 3; ++a) {
        for (const Vector3& p : mesh.positions) stops[a].push_back(p[a]);
        std::sort(stops[a].begin(), stops[a].end());
        stops[a].erase(std::unique(stops[a].begin(), stops[a].end()), stops[a].end());
    }
    std::vector<int> solid, mark(g.cell.size(), 0), label(g.cell.size(), -1);
    int stamp = 0;
    for (int v = 0; v < int(g.cell.size()); ++v)
        if (g.cell[v] == Surface || g.cell[v] == Inside) solid.push_back(v);
    // Pieces of a set of voxels as parts, each with the points in its voxels (a point past a cut's plane from its voxel's
    // centre is left out: the part's hull stops within half a voxel of the plane, inside the body).
    const auto split = [&](const std::vector<int>& voxels, const std::vector<std::pair<int, Vector3>>& points) {
        std::vector<Part> made;
        for (std::vector<int>& p : pieces(g, voxels, mark, stamp)) made.emplace_back(), made.back().voxels = std::move(p);
        const int member = ++stamp;
        for (int k = 0; k < int(made.size()); ++k)
            for (int v : made[k].voxels) mark[v] = member, label[v] = k;
        for (const auto& [v, p] : points)
            if (mark[v] == member) made[label[v]].points.push_back(p);
        return made;
    };
    std::vector<Part> parts = split(solid, samples);
    for (Part& p : parts) p.concavity = concavityOf(g, p, dirs);
    while (int(parts.size()) < o.hulls) { // the most concave part first, while it is concave and there is room
        int worst = -1;
        for (int i = 0; i < int(parts.size()); ++i)
            if (!parts[i].done && parts[i].concavity > limit && (worst < 0 || parts[i].concavity > parts[worst].concavity)) worst = i;
        if (worst < 0) break;
        int axis = 0;
        Real at = 0;
        if (!bestCut(g, parts[worst], cutDirs, stops, axis, at)) {
            parts[worst].done = true;
            continue;
        }
        std::vector<int> below, above;
        for (int v : parts[worst].voxels) (g.centre(v)[axis] < at ? below : above).push_back(v);
        std::vector<std::pair<int, Vector3>> under, over;
        for (const Vector3& p : parts[worst].points) (p[axis] < at ? under : over).push_back({g.of(p), p});
        std::vector<Part> cut = split(below, under), upper = split(above, over);
        for (Part& p : upper) cut.push_back(std::move(p));
        if (cut.size() < 2 || parts.size() - 1 + cut.size() > size_t(o.hulls)) {
            parts[worst].done = true;
            continue;
        }
        for (Part& p : cut) p.concavity = concavityOf(g, p, dirs);
        parts[worst] = std::move(cut[0]);
        for (size_t i = 1; i < cut.size(); ++i) parts.push_back(std::move(cut[i]));
    }
    if (parts.size() == 1) return {mesh.positions}; // convex enough: the mesh's own hull, as before
    // Each part as the mesh's points in it; one the surface hardly reaches (deep in a thick solid) as its voxels' corners.
    const std::vector<Vector3> fine = lattice(kHullDirections);
    std::vector<std::vector<Vector3>> out;
    for (const Part& part : parts) {
        std::vector<Vector3> own = extremePoints(part.points, kHullDirections);
        const Real filled = Real(part.voxels.size()) * g.h * g.h * g.h;
        if (own.size() < 4 || convexHull(own).volume < 0.25_r * filled) own = farthest(g, part.voxels, fine);
        out.push_back(std::move(own));
    }
    return out;
}

uint64_t fingerprint(const TriMesh& mesh, const ConvexPartsOptions& o) { // FNV-1a over the mesh's numbers and the options
    uint64_t h = 1469598103934665603ull;
    const auto eat = [&h](const void* data, size_t bytes) {
        const unsigned char* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < bytes; ++i) h = (h ^ b[i]) * 1099511628211ull;
    };
    eat(mesh.positions.data(), mesh.positions.size() * sizeof(Vector3));
    eat(mesh.triangles.data(), mesh.triangles.size() * sizeof(mesh.triangles[0]));
    eat(&o.resolution, sizeof o.resolution), eat(&o.hulls, sizeof o.hulls), eat(&o.concavity, sizeof o.concavity);
    return h;
}

} // namespace

std::vector<std::vector<Vector3>> convexParts(const TriMesh& mesh, const ConvexPartsOptions& options) {
    static std::mutex guard; // the window's guides and the world's thread both ask
    static std::map<uint64_t, std::vector<std::vector<Vector3>>> kept;
    const uint64_t key = fingerprint(mesh, options);
    {
        std::lock_guard<std::mutex> lock(guard);
        if (const auto it = kept.find(key); it != kept.end()) return it->second;
    }
    std::vector<std::vector<Vector3>> parts = decompose(mesh, options);
    std::lock_guard<std::mutex> lock(guard);
    if (kept.size() >= 16) kept.clear();
    return kept[key] = std::move(parts);
}

} // namespace ke
