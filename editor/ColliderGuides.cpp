// Guide lines of colliders (see ColliderGuides.h): per shape kind in the shape's own frame, a hull's
// edges from its convex hull (an incremental hull, as the engine builds its own from the same points),
// cached per collider, then placed where the body or the object is.
#include "ColliderGuides.h"

#include "core/Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace ke;

namespace {

constexpr int kCircle = 48; // segments of a full circle: smooth at any zoom, cheap

// A circle of radius r around `centre` in the plane spanned by the unit axes u and v.
void appendCircle(std::vector<Vector3>& out, const Vector3& centre, const Vector3& u, const Vector3& v, float r,
                  float from = 0.0f, float to = 2.0f * kPi, int segments = kCircle) {
    Vector3 prev = centre + (u * std::cos(from) + v * std::sin(from)) * r;
    for (int k = 1; k <= segments; ++k) {
        const float a = from + (to - from) * float(k) / float(segments);
        const Vector3 next = centre + (u * std::cos(a) + v * std::sin(a)) * r;
        out.push_back(prev);
        out.push_back(next);
        prev = next;
    }
}

// The convex hull of points as a closed triangle mesh, incremental: a tetrahedron of extreme points,
// then each point outside the hull so far replaces the faces it sees by a fan to their horizon. Points
// closer than `eps` to a face count as on it. Fewer than four points not in one plane: an empty mesh.
TriMesh convexHull(std::vector<Vector3> pts) {
    TriMesh out;
    if (pts.size() < 4) return out;
    AABB box;
    for (const Vector3& p : pts) box.expand(p);
    const float eps = 1e-5f * std::max(maxComp(box.extent()), 1e-6f);
    // The first tetrahedron: the farthest pair, the point farthest from their line, from their plane.
    size_t i0 = 0, i1 = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (pts[i].x < pts[i0].x) i0 = i;
        if (pts[i].x > pts[i1].x) i1 = i;
    }
    if (i0 == i1) return out;
    size_t i2 = 0, i3 = 0;
    float best = 0;
    const Vector3 d = normalize(pts[i1] - pts[i0]);
    for (size_t i = 0; i < pts.size(); ++i) {
        const float far = length(cross(pts[i] - pts[i0], d));
        if (far > best) best = far, i2 = i;
    }
    if (best < eps) return out;
    Vector3 n = normalize(cross(pts[i1] - pts[i0], pts[i2] - pts[i0]));
    best = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const float far = std::fabs(dot(pts[i] - pts[i0], n));
        if (far > best) best = far, i3 = i;
    }
    if (best < eps) return out;
    struct Face { std::array<size_t, 3> v; Vector3 n; float d; bool alive; };
    std::vector<Face> faces;
    const Vector3 inside = (pts[i0] + pts[i1] + pts[i2] + pts[i3]) * 0.25f;
    auto addFace = [&](size_t a, size_t b, size_t c) {
        Vector3 fn = normalize(cross(pts[b] - pts[a], pts[c] - pts[a]));
        if (dot(fn, pts[a] - inside) < 0) std::swap(b, c), fn = -fn;
        faces.push_back({{a, b, c}, fn, dot(fn, pts[a]), true});
    };
    addFace(i0, i1, i2), addFace(i0, i1, i3), addFace(i0, i2, i3), addFace(i1, i2, i3);
    for (size_t p = 0; p < pts.size(); ++p) {
        if (p == i0 || p == i1 || p == i2 || p == i3) continue;
        std::map<std::pair<size_t, size_t>, int> edges; // directed edges of the visible faces
        bool sees = false;
        for (Face& f : faces) {
            if (!f.alive || dot(f.n, pts[p]) - f.d <= eps) continue;
            f.alive = false, sees = true;
            for (int k = 0; k < 3; ++k) ++edges[{f.v[size_t(k)], f.v[size_t((k + 1) % 3)]}];
        }
        if (!sees) continue;
        for (const auto& [e, count] : edges) // the horizon: an edge whose reverse is not visible
            if (!edges.count({e.second, e.first})) {
                const Vector3 fn = normalize(cross(pts[e.second] - pts[e.first], pts[p] - pts[e.first]));
                faces.push_back({{e.first, e.second, p}, fn, dot(fn, pts[e.first]), true});
            }
    }
    std::map<size_t, uint32_t> index;
    for (const Face& f : faces) {
        if (!f.alive) continue;
        std::array<uint32_t, 3> t{};
        for (int k = 0; k < 3; ++k) {
            auto it = index.find(f.v[size_t(k)]);
            if (it == index.end()) it = index.emplace(f.v[size_t(k)], uint32_t(out.positions.size())).first, out.positions.push_back(pts[f.v[size_t(k)]]);
            t[size_t(k)] = it->second;
        }
        out.triangles.push_back(t);
    }
    return out;
}

} // namespace

std::vector<Vector3> boxGuide(const Vector3& h) {
    std::vector<Vector3> out;
    const Vector3 c[8] = {{-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
                          {-h.x, -h.y, h.z},  {h.x, -h.y, h.z},  {h.x, h.y, h.z},  {-h.x, h.y, h.z}};
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        out.push_back(c[e[0]]);
        out.push_back(c[e[1]]);
    }
    return out;
}

std::vector<Vector3> sphereGuide(float r) {
    std::vector<Vector3> out;
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1), o(0.0f);
    appendCircle(out, o, x, z, r); // equator
    appendCircle(out, o, x, y, r); // two meridians
    appendCircle(out, o, z, y, r);
    return out;
}

// A capsule standing on the y axis: a ring where each cap meets the side, four side lines, and the
// caps as half circles in the xy and zy planes.
std::vector<Vector3> capsuleGuide(float r, float h) {
    std::vector<Vector3> out;
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1), top(0, h, 0), bottom(0, -h, 0);
    appendCircle(out, top, x, z, r);
    appendCircle(out, bottom, x, z, r);
    for (const Vector3& side : {x * r, x * -r, z * r, z * -r}) {
        out.push_back(bottom + side);
        out.push_back(top + side);
    }
    for (const Vector3& u : {x, z}) {
        appendCircle(out, top, u, y, r, 0.0f, kPi, kCircle / 2);
        appendCircle(out, bottom, u, y, r, kPi, 2.0f * kPi, kCircle / 2);
    }
    return out;
}

// The real edges of a polyhedron: every edge whose two triangles are not in one plane (and edges
// with only one triangle). A flat face split into triangles draws only its outline.
std::vector<Vector3> meshEdgeGuide(const TriMesh& mesh) {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<size_t>> edgeFaces;
    for (size_t t = 0; t < mesh.triangles.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            uint32_t a = mesh.triangles[t][size_t(k)], b = mesh.triangles[t][size_t((k + 1) % 3)];
            if (a > b) std::swap(a, b);
            edgeFaces[{a, b}].push_back(t);
        }
    std::vector<Vector3> out;
    for (const auto& [edge, faces] : edgeFaces) {
        const bool bends = faces.size() != 2 || dot(mesh.faceNormal(faces[0]), mesh.faceNormal(faces[1])) < 0.9998f; // > ~1°
        if (!bends) continue;
        out.push_back(mesh.positions[edge.first]);
        out.push_back(mesh.positions[edge.second]);
    }
    return out;
}

std::vector<Vector3> hullGuide(const std::vector<Vector3>& points) { return meshEdgeGuide(convexHull(points)); }

std::vector<Vector3> shapeGuide(const ShapeDesc& s) {
    std::vector<Vector3> local;
    switch (s.kind) {
    case ShapeDesc::Kind::Box: local = boxGuide(s.halfExtents); break;
    case ShapeDesc::Kind::Sphere: local = sphereGuide(float(s.radius)); break;
    case ShapeDesc::Kind::Capsule: local = capsuleGuide(float(s.radius), float(s.halfHeight)); break;
    case ShapeDesc::Kind::Hull: local = hullGuide(s.points); break;
    case ShapeDesc::Kind::Mesh: {
        TriMesh mesh;
        mesh.positions = s.points, mesh.triangles = s.triangles;
        local = meshEdgeGuide(mesh);
        break;
    }
    }
    const Matrix3x3 R = s.rotation.toMatrix3x3();
    for (Vector3& p : local) p = s.position + R * p;
    return local;
}

const std::vector<Vector3>& ColliderGuides::localLines(const std::string& key, const std::vector<ShapeDesc>& shapes) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        Entry e;
        for (const ShapeDesc& s : shapes) {
            const std::vector<Vector3> lines = shapeGuide(s);
            e.lines.insert(e.lines.end(), lines.begin(), lines.end());
        }
        it = cache_.emplace(key, std::move(e)).first;
    }
    it->second.used = true;
    return it->second.lines;
}

void ColliderGuides::append(const std::string& key, const std::vector<ShapeDesc>& shapes, const Vector3& position,
                            const Quaternion& rotation, State state, std::vector<float>& lines) {
    const Matrix3x3 R = rotation.toMatrix3x3();
    for (const Vector3& p : localLines(key, shapes)) {
        const Vector3 w = position + R * p;
        lines.insert(lines.end(), {float(w.x), float(w.y), float(w.z), float(state)}); // the viewport's line layout: x y z scalar
    }
}

void ColliderGuides::prune() {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (!it->second.used) it = cache_.erase(it);
        else it->second.used = false, ++it;
    }
}
