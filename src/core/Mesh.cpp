// Triangle meshes and the primitives a robot's links are drawn with: box, sphere, cylinder, cone,
// capsule; loading a mesh from a file (OBJ, STL), and the geometry helpers (bounds, volume,
// orientation, welding). The class is in Mesh.h.
#include "core/Mesh.h"
#include "math/ElementaryFunctions.h"

#include <algorithm>
#include <map>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace ke {

AABB TriMesh::bounds() const {
    AABB b;
    for (const Vector3& p : positions) b.expand(p);
    return b;
}

Vector3 TriMesh::faceNormal(size_t t) const {
    const auto& tri = triangles[t];
    return normalize(cross(positions[tri[1]] - positions[tri[0]], positions[tri[2]] - positions[tri[0]]));
}

Real TriMesh::signedVolume() const {
    // Tetrahedra from a vertex of the mesh, not from the world origin: for a model far from the
    // origin (CAD files in mm) the float terms about the origin would cancel and flip the sign.
    if (positions.empty()) return 0.0_r;
    const Vector3 o = positions[0];
    double v = 0;
    for (const auto& t : triangles)
        v += dot(positions[t[0]] - o, cross(positions[t[1]] - o, positions[t[2]] - o)) / 6.0;
    return Real(v);
}

void TriMesh::append(const TriMesh& o) {
    uint32_t base = uint32_t(positions.size());
    if (coloured() || o.coloured()) { // one of the two coloured: the other in a neutral grey
        triangleColors.resize(triangles.size(), Vector3(0.7_r));
        if (o.coloured()) triangleColors.insert(triangleColors.end(), o.triangleColors.begin(), o.triangleColors.end());
        else triangleColors.resize(triangles.size() + o.triangles.size(), Vector3(0.7_r));
    }
    positions.insert(positions.end(), o.positions.begin(), o.positions.end());
    for (auto t : o.triangles) triangles.push_back({t[0] + base, t[1] + base, t[2] + base});
}

void TriMesh::transform(const Matrix3x3& R, const Vector3& scale, const Vector3& t) {
    for (Vector3& p : positions) p = R * (p * scale) + t;
    // A mirroring scale flips orientation.
    if (scale.x * scale.y * scale.z < 0) flipWinding();
}

void TriMesh::translate(const Vector3& t) {
    for (Vector3& p : positions) p += t;
}

void TriMesh::flipWinding() {
    for (auto& t : triangles) std::swap(t[1], t[2]);
}

void TriMesh::weld(Real eps) {
    if (positions.empty()) return;
    const Real inv = 1.0_r / std::max(eps, 1e-12_r);
    auto key = [&](int x, int y, int z) {
        return (uint64_t(uint32_t(x) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(y) & 0x1FFFFF) << 21) |
               uint64_t(uint32_t(z) & 0x1FFFFF);
    };
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
    std::vector<Vector3> out;
    std::vector<uint32_t> remap(positions.size());
    const Real eps2 = eps * eps;
    for (size_t i = 0; i < positions.size(); ++i) {
        const Vector3& p = positions[i];
        int cx = int(std::floor(p.x * inv)), cy = int(std::floor(p.y * inv)), cz = int(std::floor(p.z * inv));
        int found = -1;
        for (int dz = -1; dz <= 1 && found < 0; ++dz)
            for (int dy = -1; dy <= 1 && found < 0; ++dy)
                for (int dx = -1; dx <= 1 && found < 0; ++dx) {
                    auto it = cells.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == cells.end()) continue;
                    for (uint32_t j : it->second)
                        if (length2(out[j] - p) <= eps2) { found = int(j); break; }
                }
        if (found < 0) {
            found = int(out.size());
            out.push_back(p);
            cells[key(cx, cy, cz)].push_back(uint32_t(found));
        }
        remap[i] = uint32_t(found);
    }
    positions.swap(out);
    for (auto& t : triangles)
        for (auto& v : t) v = remap[v];
    removeDegenerate();
}

void TriMesh::removeDegenerate(Real areaEps) {
    std::vector<std::array<uint32_t, 3>> kept;
    std::vector<Vector3> colours;
    const bool keepColours = coloured();
    kept.reserve(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        const auto& t = triangles[i];
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
        Vector3 c = cross(positions[t[1]] - positions[t[0]], positions[t[2]] - positions[t[0]]);
        if (length2(c) <= areaEps * areaEps) continue;
        kept.push_back(t);
        if (keepColours) colours.push_back(triangleColors[i]);
    }
    triangles.swap(kept);
    if (keepColours) triangleColors.swap(colours);
}

void TriMesh::orientOutward() {
    if (signedVolume() < 0) flipWinding();
}

void TriMesh::fitTo(const Vector3& center, Real size) {
    AABB b = bounds();
    if (!b.valid()) return;
    Real ext = maxComp(b.extent());
    Real s = ext > 0 ? size / ext : 1.0_r;
    Vector3 c = b.center();
    for (Vector3& p : positions) p = (p - c) * s + center;
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
namespace primitives {

TriMesh box(const Vector3& h) {
    TriMesh m;
    for (int i = 0; i < 8; ++i)
        m.positions.push_back({(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z});
    // Each face as quad (a,b,c,d) counter-clockwise seen from outside.
    const int q[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    for (auto& f : q) {
        m.triangles.push_back({uint32_t(f[0]), uint32_t(f[1]), uint32_t(f[2])});
        m.triangles.push_back({uint32_t(f[0]), uint32_t(f[2]), uint32_t(f[3])});
    }
    m.orientOutward();
    return m;
}

TriMesh revolve(const std::vector<std::pair<Real, Real>>& profile, int seg) {
    TriMesh m;
    const Real rEps = 1e-7_r;
    std::vector<std::vector<uint32_t>> rings;
    for (auto [x, r] : profile) {
        std::vector<uint32_t> ring(seg);
        if (r <= rEps) {
            uint32_t id = uint32_t(m.positions.size());
            m.positions.push_back({x, 0, 0});
            for (int j = 0; j < seg; ++j) ring[j] = id;
        } else {
            for (int j = 0; j < seg; ++j) {
                Real th = 2.0_r * kPi * j / seg;
                ring[j] = uint32_t(m.positions.size());
                m.positions.push_back({x, r * ke::cos(th), r * ke::sin(th)});
            }
        }
        rings.push_back(std::move(ring));
    }
    for (size_t i = 0; i + 1 < rings.size(); ++i)
        for (int j = 0; j < seg; ++j) {
            int jn = (j + 1) % seg;
            uint32_t a = rings[i][j], b = rings[i][jn], c = rings[i + 1][jn], d = rings[i + 1][j];
            m.triangles.push_back({a, b, c});
            m.triangles.push_back({a, c, d});
        }
    m.removeDegenerate();
    m.orientOutward();
    return m;
}

TriMesh ellipsoid(const Vector3& radii, int seg, int rings) {
    std::vector<std::pair<Real, Real>> prof;
    for (int i = 0; i <= rings; ++i) {
        Real t = kPi * i / rings;
        prof.push_back({-ke::cos(t), ke::sin(t)});
    }
    TriMesh m = revolve(prof, seg);
    for (Vector3& p : m.positions) p = p * radii;
    return m;
}

TriMesh sphere(Real r, int seg, int rings) { return ellipsoid(Vector3(r), seg, rings); }

TriMesh cylinder(Real radius, Real length, int seg) {
    Real h = length * 0.5_r;
    std::vector<std::pair<Real, Real>> prof = {{-h, 0}, {-h, radius}, {h, radius}, {h, 0}};
    TriMesh m = revolve(prof, seg);
    // Axis X -> Z.
    for (Vector3& p : m.positions) p = {p.z, p.y, p.x};
    m.flipWinding(); // the swap above is a reflection
    return m;
}

TriMesh cone(Real radius, Real length, int seg) {
    std::vector<std::pair<Real, Real>> prof = {{-length * 0.5_r, 0}, {length * 0.5_r, radius}, {length * 0.5_r, 0}};
    return revolve(prof, seg);
}

} // namespace primitives

// ---------------------------------------------------------------------------
// IO
// ---------------------------------------------------------------------------
static std::string lowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

static bool loadOBJ(const std::string& path, TriMesh& out, std::string& err) {
    std::ifstream f(std::filesystem::u8path(path));
    if (!f) { err = "cannot open file"; return false; }
    std::string line;
    std::vector<int> face;
    std::vector<Vector3> vertexColours; // "v x y z r g b": a colour a vertex, a triangle takes its first corner's
    bool coloured = true;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            Vector3 p, c;
            std::istringstream ss(line.substr(2));
            ss >> p.x >> p.y >> p.z;
            coloured = coloured && bool(ss >> c.x >> c.y >> c.z);
            out.positions.push_back(p);
            vertexColours.push_back(c);
        } else if (line[0] == 'f' && line[1] == ' ') {
            face.clear();
            std::istringstream ss(line.substr(2));
            std::string tok;
            while (ss >> tok) {
                int idx = std::atoi(tok.c_str()); // stops at '/'
                if (idx < 0) idx = int(out.positions.size()) + idx;
                else idx -= 1;
                if (idx < 0 || idx >= int(out.positions.size())) { err = "bad face index"; return false; }
                face.push_back(idx);
            }
            for (size_t i = 1; i + 1 < face.size(); ++i)
                out.triangles.push_back({uint32_t(face[0]), uint32_t(face[i]), uint32_t(face[i + 1])});
        }
    }
    if (coloured && !out.positions.empty())
        for (const auto& t : out.triangles) out.triangleColors.push_back(vertexColours[t[0]]);
    return true;
}

static bool loadSTL(const std::string& path, TriMesh& out, std::string& err) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { err = "cannot open file"; return false; }
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 84) {
        uint32_t n;
        std::memcpy(&n, data.data() + 80, 4);
        if (size_t(84) + size_t(n) * 50 == data.size()) {
            for (uint32_t t = 0; t < n; ++t) {
                const char* rec = data.data() + 84 + size_t(t) * 50 + 12;
                for (int v = 0; v < 3; ++v) {
                    Real xyz[3];
                    std::memcpy(xyz, rec + v * 12, 12);
                    out.positions.push_back({xyz[0], xyz[1], xyz[2]});
                }
                out.triangles.push_back({3 * t, 3 * t + 1, 3 * t + 2});
            }
            return true;
        }
    }
    std::istringstream ss(std::string(data.begin(), data.end()));
    std::string tok;
    while (ss >> tok) {
        if (tok == "vertex") {
            Vector3 p;
            ss >> p.x >> p.y >> p.z;
            out.positions.push_back(p);
            if (out.positions.size() % 3 == 0) {
                uint32_t b = uint32_t(out.positions.size() - 3);
                out.triangles.push_back({b, b + 1, b + 2});
            }
        }
    }
    if (out.triangles.empty()) { err = "no triangles found"; return false; }
    return true;
}

bool loadMesh(const std::string& path, TriMesh& out, std::string& error) {
    out = TriMesh();
    std::string ext = lowerExt(path);
    bool ok = false;
    if (ext == "obj") ok = loadOBJ(path, out, error);
    else if (ext == "stl") ok = loadSTL(path, out, error);
    else error = "unsupported format (use .obj or .stl)";
    if (!ok) return false;
    AABB b = out.bounds();
    if (!b.valid() || out.triangles.empty()) { error = "empty mesh"; return false; }
    out.weld(1e-5_r * length(b.extent()));
    out.orientOutward();
    return true;
}

} // namespace ke
