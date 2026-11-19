// Assembly edits of a SceneGraph (Assembly.h): which face of a box a point is on, the pose of a shape
// put flat against that face, and joints written into the frame of the entity they hang on.
#include "scene/Assembly.h"

#include "scene/SensorSpec.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ke {

namespace {


// The entity's box in its own frame: centre and half edges. A model's box is its mesh's bounds (its
// size only says how large its largest extent is); every other shape is centred on its origin.
void localBox(const SceneGraph& g, const Entity& e, Vector3& centre, Vector3& half) {
    centre = Vector3(0.0_r);
    half = e.size * 0.5_r;
    if (e.shape != ShapeKind::Mesh) return;
    const AABB b = entityLocalMesh(resolveInstance(g, e), g.baseDirectory).bounds();
    if (!(b.hi.x >= b.lo.x)) return;
    centre = (b.lo + b.hi) * 0.5_r;
    half = (b.hi - b.lo) * 0.5_r;
}

int faceAxis(BoxFace f) { return int(f) / 2; }
Real faceSign(BoxFace f) { return int(f) % 2 == 0 ? 1.0_r : -1.0_r; }

Vector3 unitAxis(int a) {
    Vector3 v(0.0_r);
    v[a] = 1;
    return v;
}

// The Russian names of the joint types, for a new joint's name.
const char* jointLabel(JointType t) {
    switch (t) {
    case JointType::Ball: return "Шаровое";
    case JointType::Hinge: return "Шарнир";
    case JointType::Slider: return "Призматическое";
    case JointType::Fixed: return "Сварка";
    default: return "Сочленение";
    }
}

// A joint holds bodies: an entity made of nothing (only drawn) becomes rigid, with its collider.
void makeBody(Entity& e) {
    if (e.rigid.enabled) return;
    e.rigid.enabled = true;
    e.collider.enabled = true;
}

} // namespace

FaceSnap snapToFace(const SceneGraph& g, uint32_t target, BoxFace face, const Vector3& size) {
    FaceSnap s;
    const Entity* e = findEntity(g, target);
    if (!e) return s;
    Vector3 p, centre, half;
    Quaternion q;
    worldPose(g, *e, p, q);
    localBox(g, *e, centre, half);
    const int a = faceAxis(face);
    const Real sign = faceSign(face);
    const Vector3 n = unitAxis(a) * sign;
    Vector3 onFace = centre;
    onFace[a] += sign * half[a];
    s.found = true;
    s.face = face;
    s.normal = q.rotate(n);
    s.anchor = p + q.rotate(onFace);
    // The new shape is turned as the target, so its own axis a is along the normal too.
    s.position = s.anchor + s.normal * (0.5_r * size[a]);
    s.rotationDeg = e->parent != 0 ? eulerDegrees(q) : e->rotationDeg;
    s.across[0] = q.rotate(unitAxis((a + 1) % 3));
    s.across[1] = q.rotate(unitAxis((a + 2) % 3));
    s.halfAcross[0] = half[(a + 1) % 3];
    s.halfAcross[1] = half[(a + 2) % 3];
    return s;
}

// A box's face by the polygon's normal: the face whose world normal is nearest to it. Anything else
// at the point hit: the nearest box face's snap turned by the shortest rotation that takes that face's
// normal onto the polygon's (Quaternion::fromTwoVectors) - the anchor, the new shape and the directions
// across the face all turned with it.
FaceSnap snapToSurface(const SceneGraph& g, uint32_t target, const Vector3& point, const Vector3& normal, const Vector3& size) {
    const Entity* e = findEntity(g, target);
    if (!e || !(length(normal) > 0)) return FaceSnap();
    Vector3 p;
    Quaternion q;
    worldPose(g, *e, p, q);
    const Vector3 n = normalize(normal), local = q.conjugate().rotate(n);
    int best = 0;
    for (int a = 1; a < 3; ++a)
        if (std::fabs(local[a]) > std::fabs(local[best])) best = a;
    FaceSnap s = snapToFace(g, target, BoxFace(2 * best + (local[best] < 0 ? 1 : 0)), size);
    if (e->shape == ShapeKind::Box || e->shape == ShapeKind::Plane) return s;
    const Quaternion turn = Quaternion::fromTwoVectors(s.normal, n);
    const Real depth = dot(s.position - s.anchor, s.normal);
    s.normal = n;
    s.anchor = point;
    s.position = point + n * depth;
    s.rotationDeg = eulerDegrees((turn * q).normalized());
    s.across[0] = turn.rotate(s.across[0]);
    s.across[1] = turn.rotate(s.across[1]);
    return s;
}

// The face whose plane the point is nearest to, measured in units of the box: the largest of
// |x - c| / h over the three axes picks the axis, its sign the side.
FaceSnap snapToFace(const SceneGraph& g, uint32_t target, const Vector3& point, const Vector3& size) {
    const Entity* e = findEntity(g, target);
    if (!e) return FaceSnap();
    Vector3 p, centre, half;
    Quaternion q;
    worldPose(g, *e, p, q);
    localBox(g, *e, centre, half);
    const Vector3 local = q.conjugate().rotate(point - p) - centre;
    int best = 0;
    Real bestRatio = -1;
    for (int a = 0; a < 3; ++a) {
        const Real ratio = std::fabs(local[a]) / std::max(half[a], 1e-9_r);
        if (ratio > bestRatio) best = a, bestRatio = ratio;
    }
    return snapToFace(g, target, BoxFace(2 * best + (local[best] < 0 ? 1 : 0)), size);
}

Vector3 snapAxis(const FaceSnap& s, int axis) { return axis == 2 ? s.normal : s.across[std::clamp(axis, 0, 1)]; }

// An entity's world pose (0, or none: the world itself).
static void poseOf(const SceneGraph& g, uint32_t id, Vector3& p, Quaternion& q) {
    p = Vector3(0.0_r);
    q = Quaternion();
    if (const Entity* e = findEntity(g, id)) worldPose(g, *e, p, q);
}

// The kinds of joint by the freedoms they leave: the API's and the file's words for them.
struct KindWord { const char* word; int free; const char* label; };
const KindWord kKinds[] = {{"weld", 0, "Сварка"}, {"hinge", 1 << DofRx, "Шарнир"}, {"slider", 1 << DofTx, "Призматическое"},
                           {"ball", 1 << DofRx | 1 << DofRy | 1 << DofRz, "Шаровое"}, {"cylinder", 1 << DofTx | 1 << DofRx, "Цилиндрическое"},
                           {"universal", 1 << DofRy | 1 << DofRz, "Карданное"}, {"planar", 1 << DofTy | 1 << DofTz | 1 << DofRx, "Плоское"}};

static int freeMask(const JointObject& j) {
    int mask = 0;
    for (int k = 0; k < 6; ++k)
        if (j.dof[k].motion != JointMotion::Locked) mask |= 1 << k;
    return mask;
}

// The world frame with x along the axis and y = anyPerpendicular(x) - the zero of a hinge's angle -
// written into each link's frame: origin q^-1 (x - p), turn q^-1 Q, as Euler degrees.
uint32_t addJoint(SceneGraph& g, JointType type, uint32_t a, uint32_t b, const Vector3& worldAnchor, const Vector3& worldAxis) {
    JointObject j;
    j.id = nextId(g);
    j.name = std::string(jointLabel(type)) + " " + std::to_string(j.id);
    setJointKind(j, type);
    j.a = a;
    j.b = b;
    placeJoint(g, j, worldAnchor, worldAxis);
    g.joints.push_back(j);
    return j.id;
}

void placeJoint(const SceneGraph& g, JointObject& j, const Vector3& worldAnchor, const Vector3& worldAxis) {
    const Vector3 x = normalize(worldAxis), y = normalize(anyPerpendicular(x)), z = cross(x, y);
    Matrix3x3 basis;
    for (int r = 0; r < 3; ++r) basis.m[r][0] = x[r], basis.m[r][1] = y[r], basis.m[r][2] = z[r];
    const Quaternion Q = Quaternion::fromMatrix3x3(basis).normalized();
    Vector3 pa, pb;
    Quaternion qa, qb;
    poseOf(g, j.a, pa, qa);
    poseOf(g, j.b, pb, qb);
    j.anchorA = qa.conjugate().rotate(worldAnchor - pa);
    j.anchorB = qb.conjugate().rotate(worldAnchor - pb);
    j.frameA = eulerDegrees((qa.conjugate() * Q).normalized());
    j.frameB = eulerDegrees((qb.conjugate() * Q).normalized());
}

const JointObject* findJoint(const SceneGraph& g, uint32_t id) {
    for (const JointObject& j : g.joints)
        if (j.id == id && id != 0) return &j;
    return nullptr;
}

int removeJointsOf(SceneGraph& g, uint32_t entity) {
    const size_t before = g.joints.size();
    g.joints.erase(std::remove_if(g.joints.begin(), g.joints.end(),
                                  [entity](const JointObject& j) { return j.a == entity || (j.b == entity && entity != 0); }),
                   g.joints.end());
    return int(before - g.joints.size());
}

void jointWorldFrames(const SceneGraph& g, const JointObject& j, Vector3& originA, Quaternion& frameA, Vector3& originB, Quaternion& frameB) {
    Vector3 pa, pb;
    Quaternion qa, qb;
    poseOf(g, j.a, pa, qa);
    poseOf(g, j.b, pb, qb);
    SceneObject turnA, turnB;
    turnA.rotationDeg = j.frameA;
    turnB.rotationDeg = j.frameB;
    originA = pa + qa.rotate(j.anchorA);
    originB = pb + qb.rotate(j.anchorB);
    frameA = (qa * objectRotation(turnA)).normalized();
    frameB = (qb * objectRotation(turnB)).normalized();
}

void jointWorldFrame(const SceneGraph& g, const JointObject& j, Vector3& anchor, Vector3& axis) {
    Vector3 originA;
    Quaternion frameA, frameB;
    jointWorldFrames(g, j, originA, frameA, anchor, frameB);
    axis = frameB.rotate(Vector3(1, 0, 0));
}

void jointCoordinates(const SceneGraph& g, const JointObject& j, Real out[6]) {
    Vector3 oa, ob;
    Quaternion fa, fb;
    jointWorldFrames(g, j, oa, fa, ob, fb);
    frameCoordinates(oa, fa, ob, fb, out);
}

// The main freedom's coordinate: a hinge's angle, a slider's travel (the first free one otherwise).
Real jointPosition(const SceneGraph& g, const JointObject& j) {
    const int k = mainFreedom(j);
    if (k < 0) return 0;
    Real x[6];
    jointCoordinates(g, j, x);
    return x[k];
}

JointType jointKind(const JointObject& j) {
    const int mask = freeMask(j);
    return mask == 0 ? JointType::Fixed : mask == (1 << DofRx) ? JointType::Hinge : mask == (1 << DofTx) ? JointType::Slider
         : mask == (1 << DofRx | 1 << DofRy | 1 << DofRz) ? JointType::Ball : JointType::Generic;
}

const char* jointKindWord(const JointObject& j) {
    for (const KindWord& k : kKinds)
        if (k.free == freeMask(j)) return k.word;
    return "generic";
}

// The kind's freedoms; a joint still called by its old kind's label and its id is renamed by the new.
bool setJointKind(JointObject& j, const std::string& word) {
    std::string old = "Сочленение";
    for (const KindWord& k : kKinds)
        if (k.free == freeMask(j)) old = k.label;
    for (const KindWord& k : kKinds)
        if (word == k.word) {
            for (int d = 0; d < 6; ++d) j.dof[d].motion = (k.free >> d) & 1 ? JointMotion::Free : JointMotion::Locked;
            if (j.name == old + " " + std::to_string(j.id)) j.name = std::string(k.label) + " " + std::to_string(j.id);
            return true;
        }
    return false;
}

void setJointKind(JointObject& j, JointType kind) {
    const char* word = kind == JointType::Hinge ? "hinge" : kind == JointType::Slider ? "slider" : kind == JointType::Ball ? "ball" : "weld";
    setJointKind(j, std::string(word));
}

int mainFreedom(const JointObject& j) {
    if (j.dof[DofRx].motion != JointMotion::Locked) return DofRx;
    for (int k = 0; k < 6; ++k)
        if (j.dof[k].motion != JointMotion::Locked) return k;
    return -1;
}

// The links that hang on the joint's link a, through joints whose b is one of them - not b's side.
static std::vector<uint32_t> hangingOn(const SceneGraph& g, const JointObject& j) {
    std::vector<uint32_t> out{j.a};
    for (size_t i = 0; i < out.size(); ++i)
        for (const JointObject& k : g.joints)
            if (k.b == out[i] && k.a != j.b && std::find(out.begin(), out.end(), k.a) == out.end()) out.push_back(k.a);
    return out;
}

// The joint turned (slid) to `value` on one freedom: the hanging links turned about frame b's axis k
// through its origin (a turn) or moved along it (a travel) by the difference; an entity whose parent
// moves with it is left to its parent.
Real poseJoint(SceneGraph& g, uint32_t joint, int dof, Real value) {
    const JointObject* found = findJoint(g, joint);
    if (!found || dof < 0 || dof > 5 || found->dof[dof].motion == JointMotion::Locked) return 0;
    const JointObject j = *found;
    if (j.dof[dof].motion == JointMotion::Limited) value = std::clamp(value, j.dof[dof].lower, j.dof[dof].upper);
    Real x[6];
    jointCoordinates(g, j, x);
    Vector3 oa, ob;
    Quaternion fa, fb;
    jointWorldFrames(g, j, oa, fa, ob, fb);
    Vector3 axis(0.0_r);
    axis[dof % 3] = 1;
    axis = fb.rotate(axis);
    const Quaternion turn = dof >= DofRx ? Quaternion::fromAxisAngle(axis, value - x[dof]) : Quaternion();
    const Vector3 shift = dof < DofRx ? axis * (value - x[dof]) : Vector3(0.0_r);
    const std::vector<uint32_t> moving = hangingOn(g, j);
    for (Entity& e : g.entities) {
        if (std::find(moving.begin(), moving.end(), e.id) == moving.end()) continue;
        if (std::find(moving.begin(), moving.end(), e.parent) != moving.end()) continue; // its parent carries it
        Vector3 p;
        Quaternion q;
        worldPose(g, e, p, q);
        setWorldPose(g, e, ob + turn.rotate(p - ob) + shift, (turn * q).normalized());
    }
    jointCoordinates(g, j, x);
    return x[dof];
}

std::vector<JointVis> jointVisuals(const SceneGraph& g) {
    std::vector<JointVis> out;
    for (const JointObject& j : g.joints) {
        JointVis v;
        v.type = jointKind(j);
        v.axis = Vector3(0.0_r);
        Quaternion fa, fb;
        jointWorldFrames(g, j, v.anchorA, fa, v.anchorB, fb);
        const int k = std::max(mainFreedom(j), 0);
        Vector3 e(0.0_r);
        e[k % 3] = 1;
        v.axis = fb.rotate(e);
        v.reference = fb.rotate(Vector3(0, 1, 0)); // the zero of the angle, fixed to b
        v.angle = jointPosition(g, j);
        const JointDrive& d = j.dof[k].drive;
        v.servo = d.enabled && d.stiffness > 0 && d.constant == 0, v.target = d.target;
        v.limited = j.dof[k].motion == JointMotion::Limited, v.lower = j.dof[k].lower, v.upper = j.dof[k].upper;
        v.generic = true;
        v.frameA = fa, v.frameB = fb;
        jointCoordinates(g, j, v.coordinates);
        for (int n = 0; n < 6; ++n)
            v.motion[n] = j.dof[n].motion, v.lowers[n] = j.dof[n].lower, v.uppers[n] = j.dof[n].upper, v.driven[n] = j.dof[n].drive.enabled;
        if (const Entity* a = findEntity(g, j.a)) // a model's ring by its collider (a capsule), not by the whole mesh's box
            v.size = a->shape != ShapeKind::Mesh ? minComp(a->size) / 2
                   : a->collider.enabled && !a->collider.fitToGeometry ? minComp(a->collider.size) / 2 : maxComp(a->size) * 0.12_r;
        out.push_back(v);
    }
    return out;
}

// The link's turn: the snap's (the target's, or turned onto a curved surface's normal), then its own.
// Its depth along the normal is the half extent of its turned box: sum_k |n . (Q e_k)| h_k.
Attached attachLink(SceneGraph& g, uint32_t target, const FaceSnap& at, const LinkSpec& link) {
    if (!at.found || !findEntity(g, target)) return Attached();
    Entity e = link.prototype; // first: the prototype may be an entity of g itself
    for (Entity& t : g.entities)
        if (t.id == target) makeBody(t);
    SceneObject base;
    base.rotationDeg = at.rotationDeg;
    const Quaternion Q = (objectRotation(base) * link.turn).normalized();
    const Vector3 h = e.size * 0.5_r;
    Real depth = 0;
    for (int k = 0; k < 3; ++k) depth += std::fabs(dot(at.normal, Q.rotate(unitAxis(k)))) * h[k];
    const Vector3 anchor = at.anchor + at.across[0] * link.u + at.across[1] * link.v;
    e.id = nextId(g);
    e.parent = 0;
    e.instanceOf = 0;
    e.position = anchor + at.normal * depth;
    e.rotationDeg = eulerDegrees(Q);
    e.rigid.fixed = false;
    e.rigid.velocity = e.rigid.angularVelocity = Vector3(0.0_r);
    makeBody(e);
    g.entities.push_back(e);
    Attached out;
    out.entity = e.id;
    out.joint = addJoint(g, link.joint, e.id, target, anchor, snapAxis(at, link.axis) * (link.flip ? -1.0_r : 1.0_r));
    JointObject& j = g.joints.back();
    const int k = mainFreedom(j);
    if (k >= 0) {
        j.dof[k].drive = link.drive;
        if (link.limited) j.dof[k].motion = JointMotion::Limited, j.dof[k].lower = link.lower, j.dof[k].upper = link.upper;
    }
    return out;
}

Attached attachToFace(SceneGraph& g, uint32_t target, BoxFace face, const Entity& prototype, JointType type, int axis) {
    LinkSpec link;
    link.prototype = prototype;
    link.joint = type;
    link.axis = axis;
    return attachLink(g, target, snapToFace(g, target, face, prototype.size), link);
}

// "Сонар 2": the kind's word and one more than the sensors of that kind there are.
static std::string sensorName(const SceneGraph& g, SensorKind kind) {
    const char* words[] = {"Энкодер", "IMU", "Сонар", "Касание", "Сила-момент", "Усилие", "Стереокамера"}; // in the order of SensorKind
    const char* word = words[int(kind)];
    int n = 1;
    for (const SensorObject& s : g.sensors) n += s.kind == kind;
    return std::string(word) + " " + std::to_string(n);
}

// The sensor's frame from the face's normal n and first direction across u, written in the target's
// frame: a sonar (n, u, n x u), an IMU (u, n x u, n) - both right-handed. A stereo camera stands as a
// camera does: its z the world's up off the normal, so its two lenses sit side by side (looking straight
// up or down, the face's own direction across).
uint32_t attachSensor(SceneGraph& g, uint32_t target, const FaceSnap& at, SensorKind kind) {
    const Entity* e = findEntity(g, target);
    if (!e || !at.found || (kind != SensorKind::Imu && kind != SensorKind::Sonar && kind != SensorKind::Touch && kind != SensorKind::Stereo)) return 0;
    Vector3 p;
    Quaternion q;
    worldPose(g, *e, p, q);
    const Vector3 n = normalize(at.normal), up = Vector3(0, 1, 0) - n * n.y;
    Vector3 u = normalize(at.across[0] - n * dot(at.across[0], n));
    if (kind == SensorKind::Stereo && length(up) > 0.3_r) u = normalize(cross(up, n)); // then n x u = up
    const Vector3 w = cross(n, u);
    const Matrix3x3 frame = kind != SensorKind::Imu ? Matrix3x3::fromColumns(n, u, w) : Matrix3x3::fromColumns(u, w, n); // x out: a beam, a pad, a lens
    SensorObject s;
    s.id = nextId(g);
    s.name = sensorName(g, kind);
    s.kind = kind;
    applyDefaultModel(s); // the first model of its table, else a generic unit
    s.parent = target;
    s.position = q.conjugate().rotate(at.anchor - p);
    s.rotationDeg = eulerDegrees((q.conjugate() * Quaternion::fromMatrix3x3(frame)).normalized());
    g.sensors.push_back(s);
    return s.id;
}

uint32_t attachJointSensor(SceneGraph& g, uint32_t joint, SensorKind kind, int dof) {
    const JointObject* j = findJoint(g, joint);
    if (!j || (kind != SensorKind::Wrench && kind != SensorKind::Effort)) return 0;
    SensorObject s;
    s.id = nextId(g);
    s.name = sensorName(g, kind);
    s.kind = kind;
    applyDefaultModel(s);
    s.parent = joint;
    s.dof = dof >= 0 && dof < 6 ? dof : std::max(mainFreedom(*j), 0);
    g.sensors.push_back(s);
    return s.id;
}

uint32_t attachEncoder(SceneGraph& g, uint32_t joint, int lines) {
    const JointObject* j = findJoint(g, joint);
    const bool wheel = std::any_of(g.wheels.begin(), g.wheels.end(), [&](const WheelObject& w) { return w.id == joint && joint != 0; });
    if (!j && !wheel) return 0;
    SensorObject s;
    s.id = nextId(g);
    s.name = sensorName(g, SensorKind::Encoder);
    s.parent = joint;
    s.dof = j ? std::max(mainFreedom(*j), 0) : int(DofRx); // a point wheel: its turn about its axle
    s.lines = std::max(lines, 1);
    g.sensors.push_back(s);
    return s.id;
}

std::vector<SensorVis> sensorVisuals(const SceneGraph& g) {
    std::vector<SensorVis> out;
    for (const SensorObject& s : g.sensors) {
        if (!effectivelyVisible(g, s)) continue;
        SensorVis v;
        v.id = s.id;
        v.kind = int(s.kind);
        v.reading = s.kind == SensorKind::Sonar ? Real(sonarSpec(s).maxM) : 0.0_r;
        if (s.kind == SensorKind::Stereo) v.fovDeg = s.fovDeg, v.baseline = s.baseline, v.aspect = Real(s.height) / Real(s.width);
        if (const JointObject* j = findJoint(g, s.parent)) {
            Vector3 oa, ob;
            Quaternion qa, qb;
            jointWorldFrames(g, *j, oa, qa, ob, qb);
            v.position = ob;
            v.rotation = s.dof % 3 == 0 ? qb : qb * Quaternion::fromTwoVectors(Vector3(1, 0, 0), s.dof % 3 == 1 ? Vector3(0, 1, 0) : Vector3(0, 0, 1));
        } else if (const auto w = std::find_if(g.wheels.begin(), g.wheels.end(), [&](const WheelObject& x) { return x.id == s.parent; }); w != g.wheels.end()) {
            Quaternion frame; // on a point wheel: at its hub, its x the axle
            wheelPose(g, *w, v.position, frame);
            v.rotation = frame * Quaternion::fromTwoVectors(Vector3(1, 0, 0), Vector3(0, 0, 1));
        } else {
            worldPose(g, s, v.position, v.rotation);
        }
        out.push_back(v);
    }
    return out;
}

void wheelPose(const SceneGraph& g, const WheelObject& w, Vector3& hub, Quaternion& frame) {
    Vector3 p(0.0_r);
    Quaternion q;
    if (const SceneObject* parent = findObject(g, w.parent)) worldPose(g, *parent, p, q);
    frame = q * objectRotation(w);
    hub = p + q.rotate(w.position) + frame.rotate(Vector3(0.0_r, -w.rest, 0.0_r));
}

std::shared_ptr<const TriMesh> wheelMesh(const WheelObject& w) {
    Entity tyre;
    tyre.shape = ShapeKind::Cylinder;
    tyre.size = Vector3(2 * w.radius, w.width, 2 * w.radius);
    return std::make_shared<const TriMesh>(entityLocalMesh(tyre));
}

Quaternion wheelMeshTurn() { return Quaternion::fromAxisAngle(Vector3(1, 0, 0), 0.5_r * kPi); } // the mesh's y onto the axle z

} // namespace ke
