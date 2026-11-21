// Many objects at once, as data: the hierarchy (an object's pose relative to its parent group),
// instances (entities sharing the geometry and roles of a master), and arrays
// (one object standing for a pattern of copies).
// Everything here only reads the SceneGraph and computes poses; the running world (src/sim) turns the
// result into bodies, and an editor draws it the same way while the scene is being edited.
#include "scene/SceneGraph.h"

#include "scene/Assembly.h"
#include "core/Random.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

template <class T>
const SceneObject* findIn(const std::vector<T>& list, uint32_t id) {
    for (const T& o : list)
        if (o.id == id) return &o;
    return nullptr;
}


// The parents of an object, nearest first; stops at a missing parent or a repeated one (a cycle).
std::vector<const SceneObject*> ancestors(const SceneGraph& g, const SceneObject& o) {
    std::vector<const SceneObject*> chain;
    std::vector<uint32_t> seen{o.id};
    for (uint32_t p = o.parent; p != 0;) {
        if (std::find(seen.begin(), seen.end(), p) != seen.end()) break;
        const SceneObject* parent = findObject(g, p);
        if (!parent) break;
        chain.push_back(parent);
        seen.push_back(p);
        p = parent->parent;
    }
    return chain;
}

// Where copy n of an array sits in the array's own frame (before the jitter).
Vector3 patternOffset(const ArrayObject& a, int n) {
    const int nx = std::max(1, a.count[0]), ny = std::max(1, a.count[1]);
    switch (a.pattern) {
    case ArrayPattern::Line: return a.step * Real(n);
    case ArrayPattern::Grid: {
        const int i = n % nx, j = (n / nx) % ny, k = n / (nx * ny);
        return {a.step.x * Real(i), a.step.y * Real(j), a.step.z * Real(k)};
    }
    case ArrayPattern::Circle: {
        const Real angle = 2.0_r * kPi * Real(n) / Real(nx);
        return {a.radius * ke::cos(angle), 0.0_r, a.radius * ke::sin(angle)};
    }
    }
    return Vector3(0.0_r);
}

int copyCount(const ArrayObject& a) {
    const int nx = std::max(0, a.count[0]);
    if (a.pattern != ArrayPattern::Grid) return nx;
    return nx * std::max(0, a.count[1]) * std::max(0, a.count[2]);
}

} // namespace

const Entity* findEntity(const SceneGraph& g, uint32_t id) {
    return id ? static_cast<const Entity*>(findIn(g.entities, id)) : nullptr;
}

const SceneObject* findObject(const SceneGraph& g, uint32_t id) {
    if (id == 0) return nullptr;
    if (const SceneObject* o = findIn(g.entities, id)) return o;
    if (const SceneObject* o = findIn(g.groups, id)) return o;
    if (const SceneObject* o = findIn(g.arrays, id)) return o;
    if (const SceneObject* o = findIn(g.lights, id)) return o;
    if (const SceneObject* o = findIn(g.cameras, id)) return o;
    if (const SceneObject* o = findIn(g.sensors, id)) return o;
    if (const SceneObject* o = findIn(g.forces, id)) return o;
    if (const SceneObject* o = findIn(g.sequences, id)) return o;
    return findIn(g.wheels, id);
}

Quaternion objectRotation(const SceneObject& o) {
    const Vector3& d = o.rotationDeg;
    return Quaternion::fromAxisAngle({0, 1, 0}, degToRad(d.y)) * Quaternion::fromAxisAngle({1, 0, 0}, degToRad(d.x)) *
           Quaternion::fromAxisAngle({0, 0, 1}, degToRad(d.z));
}

// Euler angles in degrees of a rotation R = Ry(y) Rx(x) Rz(z) - the inverse of objectRotation().
// R = [.. .. sy cx; cx sz cx cz -sx; .. .. cy cx] gives x from R12, y from R02 / R22 and z from
// R10 / R11; at x = +-90 degrees (cx = 0) y and z turn about the same axis: z is set to 0.
Vector3 eulerDegrees(const Quaternion& q) {
    const Matrix3x3 R = q.toMatrix3x3();
    const Real sx = clampv(-R.m[1][2], -1.0_r, 1.0_r);
    const Real x = ke::asin(sx);
    Real y, z;
    if (std::fabs(sx) < 0.99999_r) {
        y = ke::atan2(R.m[0][2], R.m[2][2]);
        z = ke::atan2(R.m[1][0], R.m[1][1]);
    } else {
        y = ke::atan2(-R.m[2][0], R.m[0][0]);
        z = 0.0_r;
    }
    return {radToDeg(x), radToDeg(y), radToDeg(z)};
}

// A sensor on a joint rides the joint's frame on b (Assembly.h, jointWorldFrames): its pose is an
// offset in that frame, as any other child's is in its parent's.
static bool jointMount(const SceneGraph& g, const SceneObject& o, Vector3& p, Quaternion& q) {
    for (const SensorObject& s : g.sensors)
        if (s.id == o.id)
            for (const JointObject& j : g.joints)
                if (j.id == s.parent) {
                    Vector3 oa;
                    Quaternion qa;
                    jointWorldFrames(g, j, oa, qa, p, q);
                    return true;
                }
    return false;
}

void worldPose(const SceneGraph& g, const SceneObject& o, Vector3& position, Quaternion& rotation) {
    Vector3 jp;
    Quaternion jq;
    if (jointMount(g, o, jp, jq)) {
        position = jp + jq.rotate(o.position);
        rotation = (jq * objectRotation(o)).normalized();
        return;
    }
    position = o.position;
    rotation = objectRotation(o);
    for (const SceneObject* p : ancestors(g, o)) { // nearest parent first: wrap outward
        const Quaternion pr = objectRotation(*p);
        position = p->position + pr.rotate(position);
        rotation = (pr * rotation).normalized();
    }
}

void setWorldPose(const SceneGraph& g, SceneObject& o, const Vector3& position, const Quaternion& rotation) {
    Vector3 jp;
    Quaternion jq;
    if (jointMount(g, o, jp, jq)) {
        o.position = jq.conjugate().rotate(position - jp);
        o.rotationDeg = eulerDegrees((jq.conjugate() * rotation).normalized());
        return;
    }
    const std::vector<const SceneObject*> chain = ancestors(g, o);
    if (chain.empty()) { // a root: its pose is the world pose
        o.position = position;
        o.rotationDeg = eulerDegrees(rotation);
        return;
    }
    Vector3 pp;
    Quaternion pq;
    worldPose(g, *chain.front(), pp, pq);
    const Quaternion inv = pq.conjugate();
    o.position = inv.rotate(position - pp);
    o.rotationDeg = eulerDegrees((inv * rotation).normalized());
}

bool effectivelyVisible(const SceneGraph& g, const SceneObject& o) {
    if (!o.visible) return false;
    for (const SceneObject* p : ancestors(g, o))
        if (!p->visible) return false;
    return true;
}

uint32_t gluedGroupOf(const SceneGraph& g, const SceneObject& o) {
    for (const SceneObject* p : ancestors(g, o))
        for (const Group& gr : g.groups)
            if (gr.id == p->id && gr.glued) return gr.id;
    return 0;
}

Entity resolveInstance(const SceneGraph& g, const Entity& e) {
    const Entity* master = &e;
    std::vector<uint32_t> seen{e.id};
    while (master->instanceOf != 0) {
        const Entity* next = findEntity(g, master->instanceOf);
        if (!next || std::find(seen.begin(), seen.end(), next->id) != seen.end()) break;
        seen.push_back(next->id);
        master = next;
    }
    Entity out = *master;                                             // geometry and roles of the master
    static_cast<SceneObject&>(out) = static_cast<const SceneObject&>(e); // name, pose, visibility, id, tags: its own
    out.components = master->components; // a plugin's components are the master's, as its roles are (component_set writes there)
    out.instanceOf = e.instanceOf;
    return out;
}

uint32_t arrayCopyId(uint32_t arrayId, int copy) {
    return 0x80000000u | ((arrayId & 0x7FFFu) << 16) | (uint32_t(copy) & 0xFFFFu);
}
bool isArrayCopyId(uint32_t id) { return (id & 0x80000000u) != 0; }
uint32_t arrayOfCopyId(uint32_t id) { return isArrayCopyId(id) ? (id >> 16) & 0x7FFFu : 0; }

// Copy n: at the pattern's place (plus its jitter) in the array's frame, turned by n rotation steps
// and then by the template's own rotation; the template's own position is not used.
std::vector<Entity> expandArray(const SceneGraph& g, const ArrayObject& a) {
    std::vector<Entity> out;
    const Entity* tmpl = findEntity(g, a.templateId);
    if (!tmpl) return out;
    const Entity base = resolveInstance(g, *tmpl);
    Vector3 ap;
    Quaternion aq;
    worldPose(g, a, ap, aq);
    const bool visible = effectivelyVisible(g, a);
    const int n = copyCount(a);
    out.reserve(size_t(std::max(0, n)));
    for (int i = 0; i < n; ++i) {
        Random jitter(a.seed * 2654435761u + uint32_t(i) * 40503u + 1u); // the same offsets every run
        Vector3 offset = patternOffset(a, i);
        if (a.jitter > 0) offset += jitter.symmetric3() * a.jitter;
        SceneObject turn;
        turn.rotationDeg = a.rotationStepDeg * Real(i);
        Entity c = base;
        c.id = arrayCopyId(a.id, i);
        c.name = a.name + "[" + std::to_string(i) + "]";
        c.parent = 0;
        c.instanceOf = 0;
        c.visible = visible;
        c.locked = a.locked;
        for (const std::string& t : a.tags)
            if (std::find(c.tags.begin(), c.tags.end(), t) == c.tags.end()) c.tags.push_back(t); // the array's tags too
        c.position = ap + aq.rotate(offset);
        c.rotationDeg = eulerDegrees((aq * objectRotation(turn) * objectRotation(base)).normalized());
        out.push_back(std::move(c));
    }
    return out;
}

// A root keeps its pose exactly as written (no round trip through a quaternion), so a flat scene
// builds bit for bit as it did before hierarchies existed.
Entity worldEntity(const SceneGraph& g, const Entity& e) {
    Entity w = resolveInstance(g, e);
    if (e.parent != 0 && findObject(g, e.parent)) {
        Vector3 p;
        Quaternion q;
        worldPose(g, e, p, q);
        w.position = p;
        w.rotationDeg = eulerDegrees(q);
    }
    w.parent = 0;
    w.visible = effectivelyVisible(g, e);
    return w;
}

std::vector<Entity> worldEntities(const SceneGraph& g) {
    std::vector<Entity> out;
    out.reserve(g.entities.size());
    for (const Entity& e : g.entities) out.push_back(worldEntity(g, e));
    for (const ArrayObject& a : g.arrays) {
        std::vector<Entity> copies = expandArray(g, a);
        out.insert(out.end(), copies.begin(), copies.end());
    }
    return out;
}

uint32_t nextId(const SceneGraph& g) {
    uint32_t next = std::max<uint32_t>(1, g.idFloor);
    for (const Entity& e : g.entities) next = std::max(next, e.id + 1);
    for (const Group& gr : g.groups) next = std::max(next, gr.id + 1);
    for (const ArrayObject& a : g.arrays) next = std::max(next, a.id + 1);
    for (const Light& l : g.lights) next = std::max(next, l.id + 1);
    for (const Camera& c : g.cameras) next = std::max(next, c.id + 1);
    for (const JointObject& j : g.joints) next = std::max(next, j.id + 1);
    for (const BatteryObject& b : g.batteries) next = std::max(next, b.id + 1);
    for (const DriverObject& d : g.drivers) next = std::max(next, d.id + 1);
    for (const SensorObject& s : g.sensors) next = std::max(next, s.id + 1);
    for (const Force& f : g.forces) next = std::max(next, f.id + 1);
    for (const WheelObject& w : g.wheels) next = std::max(next, w.id + 1);
    for (const ControllerObject& c : g.controllers) next = std::max(next, c.id + 1);
    for (const SequenceObject& q : g.sequences) next = std::max(next, q.id + 1);
    return next;
}

Vector3 forceAxis(const Quaternion& q) { return normalize(q.rotate(Vector3(0, 1, 0))); }

const Entity* forceBody(const SceneGraph& g, const Force& f) {
    const Entity* e = f.shape == ForceShape::Directional && f.parent ? findEntity(g, f.parent) : nullptr;
    return e && e->rigid.enabled ? e : nullptr;
}

Real forceUnitPerMetre(const SceneGraph& g, const Force& f) {
    const Entity* body = forceBody(g, f);
    return body ? bodyMass(resolveInstance(g, *body)) * (f.mode == ForceMode::Impulse ? 3_r : 10_r) : 10_r;
}

void applyForceTo(SceneGraph& g, Force& f, const Entity& body) {
    Vector3 p;
    Quaternion q;
    worldPose(g, body, p, q);
    f.parent = body.id;
    setWorldPose(g, f, p + q.rotate(body.rigid.centerGiven ? body.rigid.centerOfMass : Vector3(0.0_r)), Quaternion());
    if (forceBody(g, f)) f.strength = forceUnitPerMetre(g, f) * (f.mode == ForceMode::Impulse ? 1_r : 1.2_r);
}

Vector3 lightDirection(const SceneGraph& g, const Light& l) {
    Vector3 position;
    Quaternion rotation;
    worldPose(g, l, position, rotation);
    return normalize(rotation.rotate(Vector3(0, -1, 0)));
}

Quaternion lookTurn(const Vector3& eye, const Vector3& target) {
    Vector3 forward = target - eye;
    if (length(forward) < 1e-6_r) return Quaternion();
    forward = normalize(forward);
    const Vector3 world = std::fabs(forward.y) > 0.999_r ? Vector3(0, 0, -1) : Vector3(0, 1, 0);
    const Vector3 right = normalize(cross(forward, world)), up = cross(right, forward);
    return Quaternion::fromMatrix3x3(Matrix3x3::fromColumns(right, up, forward * -1.0_r)).normalized(); // its -z the forward
}

Vector3 cameraTarget(const SceneGraph& g, const Camera& c, const std::function<bool(uint32_t, Vector3&)>& objectAt) {
    Vector3 eye;
    Quaternion rotation;
    worldPose(g, c, eye, rotation);
    Vector3 at = eye + rotation.rotate(Vector3(0, 0, -2));
    if (c.kind == CameraKind::Free || !c.target || (objectAt && objectAt(c.target, at))) return at;
    if (const SceneObject* o = findObject(g, c.target)) {
        Quaternion turn;
        worldPose(g, *o, at, turn);
    }
    return at;
}

void cameraFrame(const SceneGraph& g, const Camera& c, Vector3& eye, Vector3& forward, Vector3& up) {
    Quaternion rotation;
    worldPose(g, c, eye, rotation);
    if (c.kind == CameraKind::Target) rotation = lookTurn(eye, cameraTarget(g, c));
    forward = normalize(rotation.rotate(Vector3(0, 0, -1)));
    up = normalize(rotation.rotate(Vector3(0, 1, 0)));
}

} // namespace ke
