// The stereo camera with its housing (CameraHousing.h): the housing's box read from its model (or a box about the two
// lenses), the housing put as a shape with a box collider - its model fitted to its own longest side, so a metre is
// a metre - and the stereo sensor its child on the lenses' axis just before the housing's front.
#include "scene/CameraHousing.h"

#include "core/Mesh.h"
#include "scene/SceneActions.h"

#include <algorithm>
#include <filesystem>

namespace ke {
namespace {

// The housing's box in its own (the mount's) frame, metres.
struct Measures {
    AABB box;
    std::string problem;
};

Measures measure(const SceneGraph& g, const CameraHousing& h) {
    Measures m;
    if (h.model.empty()) { // a box about the two lenses: 3 cm deep and high, 2 cm past each lens
        const Vector3 half(0.015_r, 0.5_r * h.baseline + 0.02_r, 0.015_r);
        m.box = AABB(-half, half);
        return m;
    }
    namespace fs = std::filesystem;
    const fs::path path = fs::u8path(h.model);
    const std::string file = path.is_absolute() || g.baseDirectory.empty() ? h.model : (fs::u8path(g.baseDirectory) / path).u8string();
    TriMesh mesh;
    if (!loadMesh(file, mesh, m.problem)) m.problem = h.model + ": " + m.problem;
    else m.box = mesh.bounds();
    return m;
}

// The turn whose +x is `forward` and whose +y is level - across the world's up - when `forward` is not within 17
// degrees of the vertical; else +y is `fallback` made square to +x.
Quaternion facing(const Vector3& forward, const Vector3& fallback) {
    const Vector3 x = normalize(forward), level = cross(Vector3(0.0_r, 1.0_r, 0.0_r), x);
    Vector3 y = length(level) > 0.3_r ? normalize(level) : fallback - x * dot(fallback, x);
    if (length(y) < 1e-6_r) y = anyPerpendicular(x);
    y = normalize(y);
    return Quaternion::fromMatrix3x3(Matrix3x3::fromColumns(x, y, cross(x, y))).normalized();
}

// "Стереокамера DEPZ 2": the housing's name and one more than the entities already called so.
std::string housingName(const SceneGraph& g, const std::string& name) {
    int n = 1;
    for (const Entity& e : g.entities) n += e.name.rfind(name + " ", 0) == 0;
    return name + " " + std::to_string(n);
}

uint32_t putHousing(SceneGraph& g, const CameraHousing& h, const Measures& m, const Vector3& position, const Quaternion& turn, bool fixed) {
    const std::string name = housingName(g, h.name);
    const uint32_t id = addShape(g, h.model.empty() ? ShapeKind::Box : ShapeKind::Mesh);
    Entity& e = g.entities.back();
    e.name = name;
    e.meshFile = h.model;
    e.size = h.model.empty() ? m.box.extent() : Vector3(maxComp(m.box.extent())); // a model: uniform, its longest side
    e.position = position;
    e.rotationDeg = eulerDegrees(turn);
    e.color = h.color;
    e.rigid.enabled = e.collider.enabled = true;
    e.rigid.fixed = fixed;
    e.collider.kind = ColliderKind::Box; // the housing's own box: a model's `size` is its longest side on every axis, a
    e.collider.fitToGeometry = false;    // box fitted to that is a cube about it (a 16 cm one for DEPZ) that hits a hand
    e.collider.size = m.box.extent();    // the model is centred on its box (TriMesh::fitTo), so no offset
    return id;
}

// The stereo sensor, the housing's child: on the lenses' axis (the housing's mid-line), 1 mm before its front.
uint32_t putEyes(SceneGraph& g, const CameraHousing& h, const Measures& m, uint32_t housing, const std::string& name) {
    SensorObject s;
    s.id = nextId(g);
    s.name = name + ": стереопара";
    s.kind = SensorKind::Stereo;
    s.parent = housing;
    s.position = Vector3(m.box.hi.x - m.box.center().x + 0.001_r, 0.0_r, 0.0_r);
    s.baseline = h.baseline, s.fovDeg = h.fovDeg, s.width = h.width, s.height = h.height;
    g.sensors.push_back(s);
    return s.id;
}

} // namespace

PlacedCamera addStereoCamera(SceneGraph& g, const CameraHousing& h, const Vector3& position, const Vector3& lookAt) {
    PlacedCamera out;
    const Measures m = measure(g, h);
    if (!m.problem.empty() || !m.box.valid()) return out.problem = m.problem.empty() ? "the model has no extent" : m.problem, out;
    if (length(lookAt - position) < 1e-6_r) return out.problem = "look_at is where the camera stands", out;
    out.housing = putHousing(g, h, m, position, facing(lookAt - position, Vector3(1.0_r, 0.0_r, 0.0_r)), true);
    out.sensor = putEyes(g, h, m, out.housing, g.entities.back().name);
    return out;
}

PlacedCamera mountStereoCamera(SceneGraph& g, const CameraHousing& h, uint32_t target, const FaceSnap& at) {
    PlacedCamera out;
    const auto t = std::find_if(g.entities.begin(), g.entities.end(), [&](const Entity& e) { return e.id == target; });
    if (t == g.entities.end() || !at.found) return out.problem = "no place on the shape", out;
    const Entity carrier = resolveInstance(g, *t);
    const bool moving = carrier.rigid.enabled && !carrier.rigid.fixed;
    const Measures m = measure(g, h);
    if (!m.problem.empty() || !m.box.valid()) return out.problem = m.problem.empty() ? "the model has no extent" : m.problem, out;
    const Vector3 n = normalize(at.normal), position = at.anchor + n * (m.box.center().x - m.box.lo.x); // its back on the face
    out.housing = putHousing(g, h, m, position, facing(n, at.across[0]), !moving);
    out.sensor = putEyes(g, h, m, out.housing, g.entities.back().name);
    if (moving) out.joint = addJoint(g, JointType::Fixed, out.housing, target, at.anchor, n);
    return out;
}

} // namespace ke
