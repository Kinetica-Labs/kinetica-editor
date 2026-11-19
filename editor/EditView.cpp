// The scene graph drawn and picked as authored (see EditView.h).
#include <QDir>
#include "EditView.h"

#include "scene/Assembly.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ke;

namespace {


// Geometry only (no role) is drawn muted: the colour half-way to a cool grey.
Vector3 displayColor(const Entity& e) {
    if (!entityIsGeometryOnly(e)) return e.color;
    return e.color * 0.55f + Vector3(0.56f, 0.59f, 0.64f) * 0.45f;
}

} // namespace

bool rayTriangle(const Vector3& o, const Vector3& d, const Vector3& a, const Vector3& b, const Vector3& c, float& t) {
    const Vector3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false; // the ray runs along the triangle
    const float inv = 1.0f / det;
    const Vector3 s = o - a;
    const float u = dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    const Vector3 q = cross(s, e1);
    const float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = dot(e2, q) * inv;
    return t > 0;
}

const EditView::Source& EditView::source(const SceneGraph& g, const Entity& e) {
    const std::string key = entityGeometryKey(e, g.baseDirectory);
    if (cache_.size() > 512 && !cache_.count(key)) cache_.clear(); // sizes left behind by scale drags
    Source& s = cache_[key];
    if (s.mesh) return s;
    s.key = key;
    s.mesh = std::make_shared<const TriMesh>(entityLocalMesh(e, g.baseDirectory));
    s.bounds = s.mesh->bounds();
    s.bvh.reset();
    if (s.mesh->triangles.size() > 2000) {
        s.bvh = std::make_shared<MeshBVH>();
        s.bvh->build(*s.mesh);
    }
    return s;
}

std::shared_ptr<Frame> EditView::snapshot(const SceneGraph& g, const std::string& name, std::vector<uint32_t>& bodyEntity) {
    auto snap = std::make_shared<Frame>();
    const Vector3 w = g.world.size;
    snap->domain = AABB({-0.5f * w.x, 0.0f, -0.5f * w.z}, {0.5f * w.x, w.y, 0.5f * w.z});
    snap->sceneName = name;
    describeLights(g, *snap); // the scene's lights shade the authored scene as they will the played one
    bodyEntity.clear();
    for (const Entity& e : worldEntities(g)) {
        if (!e.visible) continue;
        const Source& s = source(g, e);
        if (s.mesh->triangles.empty()) continue; // an unreadable model file
        Frame::Body b;
        b.object = e.id;
        // The sphere gets smooth normals; everything else, models included, keeps its facets - a
        // low-poly model smoothed looks like a blob (MeshNormals.h).
        b.smooth = e.shape == ShapeKind::Sphere;
        b.pos = e.position;
        b.rot = entityRotation(e);
        b.color = displayColor(e);
        b.mesh = s.mesh;
        b.texture = e.texture;
        if (!e.texture.file.empty())
            b.texture.file = QDir(QString::fromStdString(g.baseDirectory)).filePath(QString::fromStdString(e.texture.file)).toStdString();
        if (!e.texture.heightFile.empty())
            b.texture.heightFile = QDir(QString::fromStdString(g.baseDirectory)).filePath(QString::fromStdString(e.texture.heightFile)).toStdString();
        b.movable = false;
        b.opacity = e.goalOf ? 0.1f : e.targetOf ? 0.35f : 1.0f; // an IK goal: see-through, the hand it drives shows inside (0.3:
                                            // drawn on both faces it hid what the fingers did to a cube); a camera's target: a ghost cube
        b.marker = isHandle(e);             // intent, not matter: the robot's cameras never draw it
        snap->bodies.push_back(b);
        bodyEntity.push_back(e.id);
    }
    for (const WheelObject& w : g.wheels) { // a point wheel as authored: its tyre at the hub, the suspension extended
        if (!effectivelyVisible(g, w)) continue;
        Frame::Body b{};
        b.object = w.id;
        Quaternion frame;
        wheelPose(g, w, b.pos, frame);
        b.rot = frame * wheelMeshTurn();
        b.color = w.color;
        b.mesh = wheelMesh(w);
        b.movable = false;
        snap->bodies.push_back(b);
        bodyEntity.push_back(w.id);
    }
    return snap;
}

bool EditView::raycastLocal(const Source& s, const Vector3& o, const Vector3& d, float& t, int* triangle) const {
    Ray local{o, d};
    float tNear, tFar;
    if (!intersectBox(local, s.bounds, tNear, tFar)) return false;
    if (s.bvh) {
        RayHit hit;
        if (!s.bvh->raycast(o, d, kInf, hit)) return false;
        t = hit.t;
        if (triangle) *triangle = hit.triangle;
        return true;
    }
    t = kInf;
    const TriMesh& m = *s.mesh;
    for (size_t i = 0; i < m.triangles.size(); ++i) {
        const auto& tri = m.triangles[i];
        float ti;
        if (rayTriangle(o, d, m.positions[tri[0]], m.positions[tri[1]], m.positions[tri[2]], ti) && ti < t) {
            t = ti;
            if (triangle) *triangle = int(i);
        }
    }
    return t < kInf;
}

namespace {
// The hit polygon's normal in the world, turned to face the ray (the side the mouse sees).
Vector3 hitNormal(const TriMesh& m, int triangle, const Matrix3x3& R, const Vector3& dir) {
    if (triangle < 0 || triangle >= int(m.triangles.size())) return Vector3(0.0f);
    const auto& tri = m.triangles[size_t(triangle)];
    const Vector3 a = m.positions[tri[0]], n = R * cross(m.positions[tri[1]] - a, m.positions[tri[2]] - a);
    if (!(length(n) > 0)) return Vector3(0.0f);
    return normalize(dot(n, dir) > 0 ? n * -1.0f : n);
}
} // namespace

std::vector<PickHit> EditView::pickAll(const SceneGraph& g, const Ray& ray) {
    std::vector<PickHit> hits;
    for (const Entity& e : worldEntities(g)) {
        if (!e.visible || e.locked) continue;
        const Source& s = source(g, e);
        if (s.mesh->triangles.empty()) continue;
        const Matrix3x3 R = entityRotation(e).toMatrix3x3(), Rt = R.transposed(); // the ray in the entity's frame
        float t;
        int triangle = -1;
        if (raycastLocal(s, Rt * (ray.origin - e.position), Rt * ray.dir, t, &triangle))
            hits.push_back({e.id, t, ray.at(t), e.name, hitNormal(*s.mesh, triangle, R, ray.dir)});
    }
    std::sort(hits.begin(), hits.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    return hits;
}

AABB EditView::worldBounds(const SceneGraph& g, const Entity& e) {
    const Source& s = source(g, e);
    const Matrix3x3 R = entityRotation(e).toMatrix3x3();
    AABB box;
    for (int k = 0; k < 8; ++k) {
        const Vector3 c(k & 1 ? s.bounds.hi.x : s.bounds.lo.x, k & 2 ? s.bounds.hi.y : s.bounds.lo.y, k & 4 ? s.bounds.hi.z : s.bounds.lo.z);
        box.expand(e.position + R * c);
    }
    return box;
}
