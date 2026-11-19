// The running world of a robot (RobotWorld.h): the graph's entities and joints built in the physics
// engine, the drives the engine does not have stepped by our code, and the frame a viewer draws.
#include "sim/RobotWorld.h"

#include "core/Probe.h"
#include "physics/HullPoints.h"
#include "scene/Assembly.h"
#include "scene/Animation.h"
#include "scene/SceneActions.h"
#include "scene/Kinematics.h"
#include "sim/LiveSensors.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <cmath>

namespace ke {

namespace {

Quaternion eulerTurn(const Vector3& degrees) {
    SceneObject o;
    o.rotationDeg = degrees;
    return objectRotation(o);
}

// Where the collider's frame sits on the shape when it is fitted: the box of the geometry in the
// collider's axes - its centre (in the entity's frame) and its edges.
void fitBox(const TriMesh& mesh, const Quaternion& turn, Vector3& centre, Vector3& size) {
    Vector3 lo(std::numeric_limits<Real>::max()), hi(-std::numeric_limits<Real>::max());
    for (const Vector3& p : mesh.positions) {
        const Vector3 q = turn.conjugate().rotate(p);
        lo = vmin(lo, q), hi = vmax(hi, q);
    }
    if (mesh.positions.empty()) lo = hi = Vector3(0.0_r);
    centre = turn.rotate((lo + hi) * 0.5_r);
    size = hi - lo;
}

// The robots of a graph: the entities its joints tie together (a joint marked `collide` does not tie), a group
// number for each robot. Its links never touch one another - their colliders overlap at the joints, and
// self-collision is the planner's business (ArmSelfCollision.h), not the solver's - as URDF simulators
// leave a robot's own links out of its contacts.
std::unordered_map<uint32_t, int> robotGroups(const SceneGraph& g) {
    std::unordered_map<uint32_t, uint32_t> parent;
    const std::function<uint32_t(uint32_t)> root = [&](uint32_t x) {
        const auto it = parent.find(x);
        return it == parent.end() || it->second == x ? x : parent[x] = root(it->second);
    };
    for (const JointObject& j : g.joints)
        if (j.a && j.b && !j.collide) parent[root(j.a)] = root(j.b), parent.emplace(j.b, j.b);
    std::unordered_map<uint32_t, int> number, out;
    for (const auto& link : parent) {
        const uint32_t r = root(link.first);
        out[link.first] = number.emplace(r, int(number.size()) + 1).first->second;
    }
    return out;
}

ShapeDesc hullOf(const std::vector<Vector3>& points) {
    ShapeDesc s;
    s.kind = ShapeDesc::Kind::Hull;
    s.points = extremePoints(points, kHullDirections);
    return s;
}

} // namespace

std::vector<ShapeDesc> colliderShapes(const Entity& e, const TriMesh& local) {
    const ColliderKind kind = e.collider.enabled ? e.collider.kind : ColliderKind::Auto;
    ShapeDesc s;
    if (kind == ColliderKind::Auto) {
        switch (e.shape) {
        case ShapeKind::Box: s.kind = ShapeDesc::Kind::Box, s.halfExtents = e.size * 0.5_r; break;
        case ShapeKind::Plane: s.kind = ShapeDesc::Kind::Box, s.halfExtents = Vector3(0.5_r * e.size.x, 0.01_r, 0.5_r * e.size.z); break;
        case ShapeKind::Sphere: s.kind = ShapeDesc::Kind::Sphere, s.radius = 0.5_r * e.size.x; break;
        default: s = hullOf(local.positions); break;
        }
    } else {
        const ColliderRole& c = e.collider;
        s.rotation = eulerTurn(c.rotationDeg);
        s.position = c.offset;
        Vector3 size = c.size;
        if (c.fitToGeometry) fitBox(local, s.rotation, s.position, size);
        switch (kind) {
        case ColliderKind::Box: s.kind = ShapeDesc::Kind::Box, s.halfExtents = size * 0.5_r; break;
        case ColliderKind::Sphere: s.kind = ShapeDesc::Kind::Sphere, s.radius = 0.5_r * (c.fitToGeometry ? maxComp(size) : size.x); break;
        case ColliderKind::Capsule:
            s.kind = ShapeDesc::Kind::Capsule;
            s.radius = 0.5_r * std::max(size.x, c.fitToGeometry ? size.z : size.x);
            s.halfHeight = std::max(0.0_r, 0.5_r * size.y - s.radius);
            break;
        case ColliderKind::Cylinder: {
            TriMesh round = primitives::cylinder(0.5_r * size.x, size.y, 24); // its axis is z: turned onto the collider's y
            round.transform(Quaternion::fromAxisAngle({1, 0, 0}, -0.5_r * kPi).toMatrix3x3(), Vector3(1.0_r), Vector3(0.0_r));
            const Vector3 at = s.position;
            const Quaternion turn = s.rotation;
            s = hullOf(round.positions);
            s.position = at, s.rotation = turn;
            break;
        }
        default: s = hullOf(local.positions); break; // ConvexHull: the geometry's own
        }
    }
    s.friction = e.rigid.friction, s.restitution = e.rigid.restitution;
    return {s};
}

void describeLights(const SceneGraph& g, Frame& out) {
    for (const Light& l : g.lights) {
        if (!effectivelyVisible(g, l)) continue;
        Frame::LightInfo info;
        Quaternion q;
        worldPose(g, l, info.position, q);
        info.kind = int(l.kind);
        info.direction = lightDirection(g, l);
        info.color = l.color;
        info.intensity = l.intensity, info.range = l.range, info.coneDeg = l.coneDeg, info.softnessDeg = l.softnessDeg;
        info.shadows = l.shadows;
        out.lights.push_back(info);
    }
}

// The scene starts as its active sequence has it at 0 s: an animated body begins where its track does, not with a jump.
static SceneGraph atSequenceStart(SceneGraph g) {
    if (SequenceObject* q = playingSequence(g)) seekSequence(g, *q, 0);
    return g;
}

RobotWorld::RobotWorld(SceneGraph graph) : graph_(atSequenceStart(std::move(graph))), live_(graph_) {}

std::shared_ptr<const TriMesh> RobotWorld::meshOf(const Entity& e) const {
    const std::string key = entityGeometryKey(e, graph_.baseDirectory);
    auto it = meshes_.find(key);
    if (it == meshes_.end()) it = meshes_.emplace(key, std::make_shared<const TriMesh>(entityLocalMesh(e, graph_.baseDirectory))).first;
    return it->second;
}

void RobotWorld::build(PhysicsWorld& physics) {
    const AABB box(Vector3(-0.5_r * graph_.world.size.x, 0, -0.5_r * graph_.world.size.z),
                   Vector3(0.5_r * graph_.world.size.x, graph_.world.size.y, 0.5_r * graph_.world.size.z));
    BodyDesc floor;
    floor.type = BodyDesc::Type::Static;
    floor.position = Vector3(0, -0.5_r, 0);
    ShapeDesc slab;
    slab.halfExtents = Vector3(0.5_r * (box.hi.x - box.lo.x) + 50, 0.5_r, 0.5_r * (box.hi.z - box.lo.z) + 50);
    std::string error;
    entityOfBody_.assign(1, 0);
    physics.addBody(floor, {slab}, error); // body 0: the floor, no entity
    flat_ = worldEntities(graph_);
    robotOf_ = robotGroups(graph_);
    const std::unordered_map<uint32_t, int>& groups = robotOf_;
    const std::unordered_set<uint32_t> animated = animatedEntities(graph_);
    posed_ = graph_;
    const PhysicsSettings& p = graph_.world.physics;
    for (const Entity& e : flat_) {
        if (!e.visible || entityIsGeometryOnly(e)) continue;
        BodyDesc d;
        d.type = animated.count(e.id) ? BodyDesc::Type::Kinematic // the sequence's: led along its track, it pushes what it meets
               : e.rigid.enabled && !e.rigid.fixed ? BodyDesc::Type::Dynamic : BodyDesc::Type::Static;
        d.position = e.position, d.rotation = entityRotation(e);
        d.velocity = e.rigid.velocity, d.angularVelocity = e.rigid.angularVelocity;
        d.density = e.rigid.density;
        d.linearDamping = p.linearDamping, d.angularDamping = p.angularDamping;
        if (const auto g = groups.find(e.id); g != groups.end()) d.group = g->second;
        const std::shared_ptr<const TriMesh> mesh = meshOf(e);
        const int b = physics.addBody(d, colliderShapes(e, *mesh), error);
        if (b < 0) {
            notes_.push_back("«" + e.name + "»: " + error);
            continue;
        }
        bodyOfEntity_[e.id] = b;
        if (animated.count(e.id)) animated_.push_back({e.id, b});
        else if (d.type == BodyDesc::Type::Dynamic && !groups.count(e.id) && !isArrayCopyId(e.id)) free_.push_back(e.id);
        if (entityOfBody_.size() <= size_t(b)) entityOfBody_.resize(size_t(b) + 1, 0);
        entityOfBody_[size_t(b)] = e.id;
    }
    for (const JointObject& j : graph_.joints) buildJoint(physics, j);
    free_.erase(std::remove_if(free_.begin(), free_.end(), [this](uint32_t id) { // a joint's link moves with its robot
        return std::any_of(graph_.joints.begin(), graph_.joints.end(), [id](const JointObject& j) { return j.a == id || j.b == id; });
    }), free_.end());
    for (const SensorObject& x : graph_.sensors) { // each composed of its probe of the physics and its datasheet's device
        std::string why;
        if (auto live = makeSensor(x, graph_, *this, why)) sensors_.push_back(std::move(live));
        else if (!why.empty()) notes_.push_back(why);
    }
}

bool RobotWorld::jointWrench(const PhysicsWorld& physics, uint32_t joint, Vector3& force, Vector3& torque) const {
    for (const LiveJoint& j : joints_) {
        if (j.spec.id != joint || !physics.jointAlive(j.joint)) continue;
        physics.reaction(j.joint, force, torque); // world, at frame a's origin
        const Quaternion frame = (physics.state(j.a).rotation * eulerTurn(j.spec.frameA)).conjugate();
        force = frame.rotate(force), torque = frame.rotate(torque);
        return true;
    }
    return false;
}

const LiveSensor* RobotWorld::sensor(uint32_t id) const {
    for (const auto& s : sensors_)
        if (s->spec().id == id) return s.get();
    return nullptr;
}

void RobotWorld::buildJoint(PhysicsWorld& physics, const JointObject& j) {
    LiveJoint live;
    live.spec = j;
    live.a = bodyOf(j.a);
    live.b = j.b ? bodyOf(j.b) : -1;
    live.dof = mainFreedom(j);
    if (live.a < 0 || (j.b && live.b < 0)) {
        notes_.push_back("«" + j.name + "»: " + (live.a < 0 ? "звено a" : "звено b") + " не стало телом");
        joints_.push_back(live);
        return;
    }
    JointDesc d;
    d.type = jointKind(j);
    d.a = live.a, d.b = live.b;
    d.anchorA = j.anchorA, d.anchorB = j.anchorB;
    d.frameA = eulerTurn(j.frameA), d.frameB = eulerTurn(j.frameB);
    d.collide = j.collide;
    if (live.dof >= 0) {
        const JointFreedom& f = j.dof[live.dof];
        d.limited = f.motion == JointMotion::Limited;
        d.lower = f.lower, d.upper = f.upper;
        const JointDrive& v = f.drive;
        const bool velocityMotor = v.enabled && v.stiffness == 0 && v.damping == 0 && v.constant == 0;
        d.motor = velocityMotor;
        d.motorSpeed = v.targetVelocity;
        d.maxMotorForce = std::min(v.maxForce, Real(1e9));
        live.ownDrive = v.enabled && !velocityMotor;
    }
    std::string error;
    live.joint = physics.addJoint(d, error);
    if (live.joint < 0) notes_.push_back("«" + j.name + "»: " + error);
    joints_.push_back(live);
}

// A new drive on a joint built as before: our code's servo or DC motor from the next substep, the engine's
// velocity motor set or cleared.
void RobotWorld::setDrive(PhysicsWorld& physics, LiveJoint& live, const JointObject& j) {
    live.spec = j;
    live.ownDrive = false;
    if (live.dof < 0) return;
    const JointDrive& v = j.dof[live.dof].drive;
    const bool velocityMotor = v.enabled && v.stiffness == 0 && v.damping == 0 && v.constant == 0;
    live.ownDrive = v.enabled && !velocityMotor;
    if (physics.jointAlive(live.joint)) physics.setMotor(live.joint, velocityMotor, v.targetVelocity, std::min(v.maxForce, Real(1e9)));
}

namespace {
// What the engine was built from: equal, and a new drive is all that changed.
bool sameBuild(const JointObject& x, const JointObject& y) {
    auto same = [](const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    if (x.a != y.a || x.b != y.b || x.collide != y.collide || jointKind(x) != jointKind(y)) return false;
    if (!same(x.anchorA, y.anchorA) || !same(x.anchorB, y.anchorB) || !same(x.frameA, y.frameA) || !same(x.frameB, y.frameB)) return false;
    for (int k = 0; k < 6; ++k)
        if (x.dof[k].motion != y.dof[k].motion || x.dof[k].lower != y.dof[k].lower || x.dof[k].upper != y.dof[k].upper) return false;
    return true;
}
} // namespace

bool RobotWorld::setJoints(PhysicsWorld& physics, const std::vector<JointObject>& joints) {
    if (joints.size() != joints_.size()) return false;
    std::vector<std::pair<LiveJoint*, const JointObject*>> pairs;
    for (const JointObject& j : joints) {
        LiveJoint* live = nullptr;
        for (LiveJoint& l : joints_)
            if (l.spec.id == j.id) live = &l;
        if (!live || !sameBuild(live->spec, j)) return false;
        pairs.push_back({live, &j});
    }
    for (auto& [live, j] : pairs) setDrive(physics, *live, *j);
    for (JointObject& j : graph_.joints)
        for (const JointObject& n : joints)
            if (n.id == j.id) j = n;
    live_.joints = graph_.joints;
    return true;
}

void RobotWorld::setGoals(const std::vector<Entity>& goals) {
    for (SceneGraph* g : {&graph_, &live_}) {
        g->entities.erase(std::remove_if(g->entities.begin(), g->entities.end(), [](const Entity& e) { return isHandle(e); }),
                          g->entities.end());
        g->entities.insert(g->entities.end(), goals.begin(), goals.end());
    }
}

bool RobotWorld::coordinates(const PhysicsWorld& physics, const LiveJoint& j, Real c[6]) const {
    if (!physics.jointAlive(j.joint)) return false;
    const BodyState A = physics.state(j.a);
    const BodyState B = j.b >= 0 ? physics.state(j.b) : BodyState();
    frameCoordinates(A.position + A.rotation.rotate(j.spec.anchorA), A.rotation * eulerTurn(j.spec.frameA),
                     B.position + B.rotation.rotate(j.spec.anchorB), B.rotation * eulerTurn(j.spec.frameB), c);
    return true;
}

bool RobotWorld::jointCoordinates(const PhysicsWorld& physics, uint32_t joint, Real x[6]) const {
    for (const LiveJoint& j : joints_)
        if (j.spec.id == joint) return coordinates(physics, j, x);
    return false;
}

Real RobotWorld::jointEffort(const PhysicsWorld& physics, uint32_t joint) const {
    for (const LiveJoint& j : joints_) {
        if (j.spec.id != joint || j.dof < 0 || !j.spec.dof[j.dof].drive.enabled) continue;
        return j.ownDrive ? j.effort : physics.motorForce(j.joint);
    }
    return 0;
}

const JointObject* RobotWorld::jointSpec(uint32_t joint) const {
    for (const LiveJoint& j : joints_)
        if (j.spec.id == joint) return &j.spec;
    return nullptr;
}

// A servo: f = k (x_t - x) + c (v_t - v); a DC motor: K (U - K v) / R; at most maxForce either way.
// On the links: a turn's torque about frame b's axis, +f on a and -f on b; a travel's force at frame a's
// origin. The spring is explicit at the substep: a servo stays stable while sqrt(k / I) h < 2. The damper
// is implicit (backward Euler): c (v_t - v) / (1 + c h / I), with I the links' inertia about the axis
// against each other - an explicit damper with c h / I > 2 (a stiff servo on a light forearm) reverses
// the speed every substep and saturates the motor in a buzz that holds the joint off its target.
void RobotWorld::drive(PhysicsWorld& physics, const LiveJoint& j, Real h) const {
    if (!j.ownDrive || !physics.jointAlive(j.joint)) return;
    const BodyState A = physics.state(j.a);
    const BodyState B = j.b >= 0 ? physics.state(j.b) : BodyState();
    const Vector3 oA = A.position + A.rotation.rotate(j.spec.anchorA);
    const Quaternion QB = B.rotation * eulerTurn(j.spec.frameB);
    Real c[6];
    coordinates(physics, j, c);
    const int k = j.dof;
    Vector3 axis(0.0_r);
    axis[k % 3] = 1;
    axis = QB.rotate(axis);
    const bool turn = k >= DofRx;
    auto pointVelocity = [](const BodyState& s, const Vector3& p) { return s.velocity + cross(s.angularVelocity, p - s.centre); };
    const Real v = turn ? dot(A.angularVelocity - B.angularVelocity, axis) : dot(pointVelocity(A, oA) - pointVelocity(B, oA), axis);
    const JointDrive& d = j.spec.dof[k].drive;
    // How readily the two links turn (or slide) against each other along the axis: 1 / I = a's + b's.
    auto mobility = [&](const BodyState& s) {
        if (s.mass <= 0) return Real(0);
        return turn ? dot(axis, s.inertia.inverse() * axis) : 1 / s.mass;
    };
    const Real m = mobility(A) + mobility(B);
    const Real damping = m > 0 ? d.damping / (1 + d.damping * h * m) : d.damping;
    Real f;
    if (d.constant > 0) f = d.constant * (d.voltage - d.constant * v) / std::max(d.resistance, Real(1e-6));
    else {
        Real& at = j.setpoint;
        if (!std::isfinite(at)) at = c[k];
        const Real gap = d.target - at, speed = std::min(turn ? kServoTurnSpeed : kServoTravelSpeed, std::fabs(gap) / kServoSettle);
        const Real walk = std::copysign(std::min(speed * h, std::fabs(gap)), gap);
        at += walk;
        const Real reach = d.stiffness > 0 ? d.maxForce / d.stiffness : std::numeric_limits<Real>::infinity();
        at = std::clamp(at, c[k] - reach, c[k] + reach);
        f = d.stiffness * (at - c[k]) + damping * (d.targetVelocity + walk / h - v);
    }
    f = std::clamp(f, -d.maxForce, d.maxForce);
    j.effort = f;
    if (turn) {
        physics.applyTorque(j.a, axis * f);
        if (j.b >= 0) physics.applyTorque(j.b, axis * -f);
    } else {
        physics.applyForce(j.a, axis * f, oA);
        if (j.b >= 0) physics.applyForce(j.b, axis * -f, oA);
    }
}

void RobotWorld::grab(PhysicsWorld& physics, int body, const Vector3& point) {
    releaseGrab(physics);
    if (body < 0 || !physics.alive(body)) return;
    const BodyState s = physics.state(body);
    if (s.mass <= 0) return;
    Real moved = s.mass; // the robot's, for its link
    if (const auto robot = robotOf_.find(entityOfBody_[size_t(body)]); robot != robotOf_.end()) {
        moved = 0;
        for (const auto& [link, group] : robotOf_)
            if (const int b = bodyOf(link); group == robot->second && b >= 0 && physics.alive(b)) moved += physics.state(b).mass;
    }
    const Real g = 9.81_r; // the standard gravity, whatever the world's: the mouse moves bodies with it off too
    const Real lever = std::sqrt((s.inertia.m[0][0] + s.inertia.m[1][1] + s.inertia.m[2][2]) / (3 * s.mass)); // the gyration radius
    HandDesc hand;
    hand.body = body;
    hand.point = point;
    hand.hertz *= std::sqrt(moved / s.mass);
    hand.maxForce = 100 * moved * g;
    hand.maxTorque = 0.5_r * lever * s.mass * g;
    std::string error;
    grabJoint_ = physics.addHand(hand, error);
    if (grabJoint_ < 0) return;
    grabBody_ = body;
    grabLocal_ = s.rotation.conjugate().rotate(point - s.position);
    grabTarget_ = point;
}

void RobotWorld::releaseGrab(PhysicsWorld& physics) {
    physics.removeJoint(grabJoint_);
    grabBody_ = grabJoint_ = -1;
}

void RobotWorld::carryGrab(PhysicsWorld& physics) const {
    if (grabBody_ < 0 || !physics.jointAlive(grabJoint_)) return;
    const BodyState s = physics.state(grabBody_);
    physics.applyForce(grabBody_, graph_.world.gravity * -s.mass, s.position + s.rotation.rotate(grabLocal_));
}

void RobotWorld::leadGoals(PhysicsWorld& physics, Real dt) {
    for (const Entity& goal : live_.entities) {
        const int hand = goal.goalOf ? bodyOf(goal.goalOf) : -1;
        if (hand < 0 || !physics.alive(hand)) continue;
        Vector3 to;
        Quaternion turn;
        worldPose(live_, goal, to, turn);
        auto it = carried_.find(goal.id);
        if (it == carried_.end()) {
            const BodyState s = physics.state(hand);
            it = carried_.emplace(goal.id, std::make_pair(s.position, s.rotation)).first;
        }
        auto& [at, facing] = it->second;
        const Real far = length(to - at), angle = 2 * ke::acos(std::min(1.0_r, std::fabs(dot(facing, turn))));
        if (far < 1e-6_r && angle < 1e-6_r) continue; // there: the servos hold the last coordinates
        const Real share = std::min({1.0_r, kToolSpeed * dt / std::max(far, 1e-9_r), kToolTurn * dt / std::max(angle, 1e-9_r)});
        at += (to - at) * share;
        facing = slerp(facing, turn, share);
        const bool full = ikChain(live_, goal.goalOf).size() >= 6; // fewer freedoms: the point only (reachGoal)
        reachTargets(live_, goal.goalOf, at, full ? &facing : nullptr, true);
        for (LiveJoint& j : joints_)
            for (const JointObject& n : live_.joints)
                if (n.id == j.spec.id && n.dof[std::max(j.dof, 0)].drive.target != j.spec.dof[std::max(j.dof, 0)].drive.target) setDrive(physics, j, n);
    }
}

// The entities the active sequence moves: those it binds or inside a group it binds - not a robot's link a joint drives
// (it rides on its animated base).
std::unordered_set<uint32_t> RobotWorld::animatedEntities(const SceneGraph& g) {
    std::unordered_set<uint32_t> out, driven;
    const SequenceObject* q = playingSequence(g);
    if (!q || q->tracks.empty()) return out;
    for (const JointObject& j : g.joints) driven.insert(j.a);
    for (const Entity& e : g.entities) {
        if (driven.count(e.id)) continue;
        for (const uint32_t id : ancestry(g, e.id))
            if (trackOf(*q, id)) out.insert(e.id);
    }
    return out;
}

// Every animated body led over the next h to where the active sequence has it at `time` (posed_: the graph there).
void RobotWorld::leadAnimation(PhysicsWorld& physics, Real time, Real h) {
    SequenceObject* sequence = playingSequence(posed_);
    if (!sequence) return;
    seekSequence(posed_, *sequence, time);
    for (const auto& [id, body] : animated_) {
        const SceneObject* o = findObject(posed_, id);
        Vector3 p;
        Quaternion q;
        if (!o) continue;
        worldPose(posed_, *o, p, q);
        physics.moveKinematic(body, p, q, h);
    }
}

void RobotWorld::step(PhysicsWorld& physics, Real dt, int substeps) {
    leadGoals(physics, dt);
    physics.moveHand(grabJoint_, grabTarget_, dt);
    const int n = std::max(substeps, 1);
    for (int s = 0; s < n; ++s) {
        {
            Probe::Timer timer("physics/drives ms");
            for (const LiveJoint& j : joints_) drive(physics, j, dt / Real(n));
            carryGrab(physics);
            if (!animated_.empty()) leadAnimation(physics, Real(double(clockNs_) * 1e-9) + dt / Real(n), dt / Real(n)); // the clock moves on per substep
        }
        {
            Probe::Timer timer("physics/engine ms");
            physics.step(dt / Real(n), 1);
        }
        clockNs_ += std::llround(double(dt) / n * 1e9);
        Probe::Timer timer("physics/sensors ms");
        for (const auto& x : sensors_) x->step(physics, *this, dt / Real(n), clockNs_);
    }
    readPoses(physics);
    if (recording_) sampleTake(physics);
}

void RobotWorld::startRecording(const PhysicsWorld& physics) {
    recording_ = true;
    takeFromNs_ = clockNs_;
    take_.assign(free_.size(), {});
    sampleTake(physics);
}

// Each free body's pose on its parent and the velocity of its frame's origin (v + w x r from the centre of mass), turned
// onto its parent.
void RobotWorld::sampleTake(const PhysicsWorld& physics) {
    const Real t = Real(double(clockNs_ - takeFromNs_) * 1e-9);
    for (size_t i = 0; i < free_.size(); ++i) {
        const SceneObject* o = findObject(live_, free_[i]);
        const int b = bodyOf(free_[i]);
        if (!o || b < 0 || !physics.alive(b)) continue;
        const BodyState s = physics.state(b);
        Vector3 v = s.velocity + cross(s.angularVelocity, s.position - s.centre), at;
        Quaternion turn;
        if (const SceneObject* parent = findObject(live_, o->parent)) worldPose(live_, *parent, at, turn), v = turn.conjugate().rotate(v);
        AnimKey k{t, o->position, o->rotationDeg};
        k.velocity = v, k.hasVelocity = true, k.interp = AnimInterp::Smooth;
        take_[i].push_back(k);
    }
}

SequenceObject RobotWorld::takeRecording(const std::string& name) {
    SequenceObject q;
    q.name = name, q.active = true, q.play = false; // shown, not replayed: the next ▶ is physics again
    for (size_t i = 0; recording_ && i < free_.size(); ++i) {
        std::vector<AnimKey> keys = thinKeys(take_[i], 0.0005_r, 0.2_r);
        if (keys.size() < 2 || (length(keys.front().position - keys.back().position) < 1e-6_r && keys.size() == 2 &&
                                keys.front().rotationDeg.x == keys.back().rotationDeg.x && keys.front().rotationDeg.y == keys.back().rotationDeg.y &&
                                keys.front().rotationDeg.z == keys.back().rotationDeg.z))
            continue; // it stood still
        q.tracks.push_back({free_[i], std::move(keys)});
    }
    recording_ = false;
    take_.clear();
    return q;
}

void RobotWorld::readPoses(const PhysicsWorld& physics) {
    if (SequenceObject* q = playingSequence(live_)) seekSequence(live_, *q, Real(double(clockNs_) * 1e-9)); // lights, cameras, shapes it moves
    for (Entity& e : live_.entities) {
        const int b = bodyOf(e.id);
        if (b < 0 || !physics.alive(b)) continue;
        const BodyState s = physics.state(b);
        const bool led = std::any_of(animated_.begin(), animated_.end(), [&](const auto& a) { return a.second == b; });
        if (s.mass <= 0 && !led) continue; // static: where it was put
        setWorldPose(live_, e, s.position, s.rotation);
    }
}

uint32_t RobotWorld::objectIdOfBody(int body) const {
    if (body < 0 || size_t(body) >= entityOfBody_.size()) return 0;
    const uint32_t id = entityOfBody_[size_t(body)];
    return isArrayCopyId(id) ? arrayOfCopyId(id) : id;
}

int RobotWorld::bodyOf(uint32_t entity) const {
    auto it = bodyOfEntity_.find(entity);
    return it == bodyOfEntity_.end() ? -1 : it->second;
}

void RobotWorld::fill(const PhysicsWorld& physics, Frame& out) const {
    out.domain = AABB(Vector3(-0.5_r * graph_.world.size.x, 0, -0.5_r * graph_.world.size.z),
                      Vector3(0.5_r * graph_.world.size.x, graph_.world.size.y, 0.5_r * graph_.world.size.z));
    out.engine = physics.engine();
    for (const Entity& source : worldEntities(live_)) {
        if (!source.visible) continue;
        Frame::Body b;
        b.object = source.id;
        b.body = bodyOf(source.id);
        b.mesh = meshOf(source);
        b.color = source.color;
        b.texture = source.texture;
        for (std::string* file : {&b.texture.file, &b.texture.heightFile}) // a relative picture: from the scene file's folder
            if (!file->empty() && !std::filesystem::u8path(*file).is_absolute() && !graph_.baseDirectory.empty())
                *file = (std::filesystem::u8path(graph_.baseDirectory) / std::filesystem::u8path(*file)).u8string();
        b.marker = isHandle(source);
        b.robot = robotOf_.count(source.id) > 0;
        b.opacity = source.goalOf ? 0.1_r : source.targetOf ? 0.35_r : 1;
        b.smooth = source.shape == ShapeKind::Sphere;
        if (b.body >= 0 && physics.alive(b.body)) {
            const BodyState s = physics.state(b.body);
            b.pos = s.position, b.rot = s.rotation;
            b.sleeping = !s.awake;
            b.movable = s.mass > 0;
        } else {
            b.pos = source.position, b.rot = entityRotation(source);
            b.movable = false;
        }
        out.bodies.push_back(std::move(b));
    }
    out.joints = jointVisuals(live_);
    out.sensors = sensorVisuals(live_);
    for (SensorVis& v : out.sensors) // the glyph shows what the robot reads: a sonar's distance, an encoder's coordinate
        if (const LiveSensor* x = sensor(v.id); x && (v.kind == int(SensorKind::Sonar) || v.kind == int(SensorKind::Encoder)))
            v.reading = x->reading().value;
    for (const ContactPoint& c : physics.contacts())
        out.contacts.push_back({c.point, c.normal, c.depth, c.normalImpulse, c.a, c.b});
    describeLights(live_, out);
    for (const std::string& n : notes_) out.info.push_back({"Не построено", n});
}

void RobotWorld::drawLayers(const PhysicsWorld& physics, Real substepDt) const {
    if (Probe::layers() == 0) return;
    using L = DrawLayer;
    for (const ContactPoint& c : physics.contacts()) {
        Probe::point(L::ContactPoints, c.point, {1, 0.3_r, 0.2_r}, 0.012_r);
        Probe::arrow(L::ContactNormals, c.point, c.normal * 0.1_r, {1, 0.6_r, 0.2_r});
        if (substepDt > 0) Probe::arrow(L::ContactImpulses, c.point, c.normal * (c.normalImpulse / substepDt * Probe::kForceScale), {1, 1, 0.3_r});
        if (c.depth > 0) Probe::line(L::PenetrationDepth, c.point, c.point - c.normal * c.depth, {1, 0.1_r, 0.1_r});
    }
    for (const Entity& e : flat_) {
        const int b = bodyOf(e.id);
        if (b < 0 || !physics.alive(b)) continue;
        const BodyState s = physics.state(b);
        const TriMesh& mesh = *meshOf(e);
        if (Probe::layerOn(L::BodyAabbs) || Probe::layerOn(L::Sleeping)) { // the mesh's box turned into the world's
            const AABB local = mesh.bounds();
            AABB world;
            for (int k = 0; k < 8; ++k)
                world.expand(s.position + s.rotation.rotate(Vector3(k & 1 ? local.hi.x : local.lo.x, k & 2 ? local.hi.y : local.lo.y,
                                                                    k & 4 ? local.hi.z : local.lo.z)));
            if (s.awake) Probe::box(L::BodyAabbs, world, {0.3_r, 0.8_r, 1});
            else Probe::box(L::Sleeping, world, {0.35_r, 0.35_r, 0.35_r});
        }
        if (s.mass <= 0) continue; // a static body: no motion, no inertia of its own
        const Real r = 0.03_r;
        for (int k = 0; k < 3; ++k) {
            Vector3 d(0.0_r);
            d[k] = r;
            Probe::line(L::CentreOfMass, s.centre - d, s.centre + d, {1, 1, 1});
        }
        if (Probe::layerOn(L::InertiaAxes)) {
            Vector3 I;
            Matrix3x3 V;
            symmetricEigen(s.inertia, I, V);
            const Vector3 colours[3] = {{1, 0.2_r, 0.2_r}, {0.2_r, 1, 0.2_r}, {0.2_r, 0.2_r, 1}};
            for (int k = 0; k < 3; ++k) {
                const Vector3 axis(V.m[0][k], V.m[1][k], V.m[2][k]);
                const Real g = std::sqrt(std::max(I[k], Real(0)) / s.mass); // the radius of gyration
                Probe::line(L::InertiaAxes, s.centre - axis * g, s.centre + axis * g, colours[k]);
            }
        }
        Probe::arrow(L::Velocities, s.centre, s.velocity * 0.1_r, {0.2_r, 1, 1});
        Probe::arrow(L::Velocities, s.centre, s.angularVelocity * 0.1_r, {1, 0.3_r, 1});
    }
    if (!Probe::layerOn(L::JointFrames)) return;
    for (const LiveJoint& j : joints_) {
        if (!physics.jointAlive(j.joint)) continue;
        const BodyState A = physics.state(j.a);
        const BodyState B = j.b >= 0 ? physics.state(j.b) : BodyState();
        const Vector3 oA = A.position + A.rotation.rotate(j.spec.anchorA), oB = B.position + B.rotation.rotate(j.spec.anchorB);
        Probe::point(L::JointFrames, oA, {0.2_r, 1, 0.4_r}, 0.012_r);
        Probe::point(L::JointFrames, oB, {0.2_r, 0.6_r, 1}, 0.012_r);
        Probe::line(L::JointFrames, oA, oB, {1, 1, 0.3_r});
        if (j.dof >= 0) {
            Vector3 axis(0.0_r);
            axis[j.dof % 3] = 1;
            axis = (B.rotation * eulerTurn(j.spec.frameB)).rotate(axis);
            Probe::line(L::JointFrames, oB - axis * 0.08_r, oB + axis * 0.08_r, {0.6_r, 0.6_r, 0.6_r});
        }
    }
}

} // namespace ke
