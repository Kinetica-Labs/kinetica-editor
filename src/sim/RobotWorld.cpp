// The running world of a robot (RobotWorld.h): the graph's entities and joints built in the physics
// engine, the drives the engine does not have stepped by our code, and the frame a viewer draws.
#include "sim/RobotWorld.h"

#include "core/Probe.h"
#include "spatial/ConvexParts.h"
#include "spatial/HullPoints.h"
#include "scene/Assembly.h"
#include "scene/Collider.h"
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

int64_t forceDueNs(const Force& f) { return std::llround(double(f.start) * 1e9); }

Quaternion eulerTurn(const Vector3& degrees) {
    SceneObject o;
    o.rotationDeg = degrees;
    return objectRotation(o);
}

// The robots of a graph: the entities its joints tie together (a joint marked `collide` does not tie), a group
// number for each robot. Its links never touch one another - their colliders overlap at the joints - as
// URDF simulators leave a robot's own links out of its contacts.
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

// The collider's numbers (scene/Collider.h) as the engine's shape; a collider not switched on is Авто.
std::vector<ShapeDesc> colliderShapes(const Entity& e, const TriMesh& local) {
    Entity built = e;
    if (!e.collider.enabled) built.collider.kind = ColliderKind::Auto;
    const ColliderRole c = heldCollider(built, local);
    ShapeDesc s;
    switch (c.kind) {
    case ColliderKind::Box: s.kind = ShapeDesc::Kind::Box, s.halfExtents = c.size * 0.5_r; break;
    case ColliderKind::Sphere: s.kind = ShapeDesc::Kind::Sphere, s.radius = 0.5_r * c.size.x; break;
    case ColliderKind::Capsule:
        s.kind = ShapeDesc::Kind::Capsule;
        s.radius = 0.5_r * c.size.x;
        s.halfHeight = std::max(0.0_r, 0.5_r * c.size.y - s.radius);
        break;
    case ColliderKind::Cylinder: {
        TriMesh round = primitives::cylinder(0.5_r * c.size.x, c.size.y, 24); // its axis is z: turned onto the collider's y
        round.transform(Quaternion::fromAxisAngle({1, 0, 0}, -0.5_r * kPi).toMatrix3x3(), Vector3(1.0_r), Vector3(0.0_r));
        s = hullOf(round.positions);
        break;
    }
    case ColliderKind::Mesh:
        if (e.rigid.enabled && !e.rigid.fixed) { // a moving body: the mesh as convex parts (triangles have no inside to weigh)
            std::vector<ShapeDesc> parts;
            ConvexPartsOptions o;
            o.hulls = c.hulls, o.concavity = c.concavity;
            for (const std::vector<Vector3>& part : convexParts(local, o)) parts.push_back(hullOf(part));
            for (ShapeDesc& p : parts) p.friction = e.rigid.friction, p.restitution = e.rigid.restitution, p.trigger = e.collider.trigger;
            return parts;
        }
        s.kind = ShapeDesc::Kind::Mesh, s.points = local.positions, s.triangles = local.triangles;
        break;
    default: s = hullOf(local.positions); break; // a hull: the geometry's own
    }
    if (sizedByNumbers(c.kind)) s.position = c.offset, s.rotation = colliderTurn(c);
    s.friction = e.rigid.friction, s.restitution = e.rigid.restitution;
    s.trigger = e.collider.enabled && e.collider.trigger;
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

// The world, not the scene, says where the object is: a key of the playing sequence or a moving body, up its parents.
bool RobotWorld::led(uint32_t id) const {
    const SequenceObject* q = playingSequence(live_);
    for (const SceneObject* o = findObject(live_, id); o; o = o->parent ? findObject(live_, o->parent) : nullptr)
        if ((q && trackOf(*q, o->id)) || !stands(bodyOf(o->id))) return true;
    return false;
}

void RobotWorld::describeCameras(Frame& out) const {
    for (const Camera& c : live_.cameras) {
        const bool aimed = c.kind == CameraKind::Target && c.target;
        if (!led(c.id) && !(aimed && led(c.target))) continue;
        Frame::CameraPose pose;
        pose.id = c.id;
        worldPose(live_, c, pose.position, pose.rotation);
        if (c.kind == CameraKind::Target) pose.rotation = lookTurn(pose.position, cameraTarget(live_, c));
        out.cameras.push_back(pose);
    }
    for (const Force& f : live_.forces) {
        if (!led(f.id)) continue;
        Frame::CameraPose pose;
        pose.id = f.id;
        worldPose(live_, f, pose.position, pose.rotation);
        out.forces.push_back(pose);
    }
    out.firedForces = firedIds_;
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
    flatOfBody_.assign(1, -1);
    firedIds_.clear();
    physics.addBody(floor, {slab}, error); // body 0: the floor, no entity
    flat_ = worldEntities(graph_);
    robotOf_ = robotGroups(graph_);
    const std::unordered_map<uint32_t, int>& groups = robotOf_;
    const std::unordered_set<uint32_t> animated = animatedEntities(graph_);
    posed_ = graph_;
    const PhysicsSettings& p = graph_.world.physics;
    // The tags some collider ignores, in the order met - a bit each (the engine tells marks by bits): a body's marks
    // are its tags among them, its ignores its collider's.
    std::vector<std::string> ignored;
    for (const Entity& e : flat_)
        if (e.collider.enabled)
            for (const std::string& t : e.collider.ignores)
                if (std::find(ignored.begin(), ignored.end(), t) == ignored.end()) ignored.push_back(t);
    if (ignored.size() > 64) notes_.push_back("коллайдеры называют больше 64 разных меток в «ignores»: лишние не учтены"), ignored.resize(64);
    const auto bits = [&ignored](const std::vector<std::string>& tags) {
        uint64_t out = 0;
        for (size_t k = 0; k < ignored.size(); ++k)
            if (std::find(tags.begin(), tags.end(), ignored[k]) != tags.end()) out |= uint64_t(1) << k;
        return out;
    };
    for (const Entity& e : flat_) {
        if (!e.visible || entityIsGeometryOnly(e)) continue;
        BodyDesc d;
        d.marks = bits(e.tags);
        if (e.collider.enabled) d.ignores = bits(e.collider.ignores);
        d.type = animated.count(e.id) ? BodyDesc::Type::Kinematic // the sequence's: led along its track, it pushes what it meets
               : e.rigid.dynamic() ? BodyDesc::Type::Dynamic
               : e.rigid.enabled && e.rigid.kinematic && !e.rigid.fixed ? BodyDesc::Type::Kinematic // its own velocity and spin lead it
               : BodyDesc::Type::Static;
        d.position = e.position, d.rotation = entityRotation(e);
        d.velocity = e.rigid.velocity, d.angularVelocity = e.rigid.angularVelocity;
        d.density = e.rigid.density;
        if (e.rigid.mass > 0) d.massGiven = true, d.mass = e.rigid.mass;
        if (e.rigid.centerGiven) d.centreGiven = true, d.centre = e.rigid.centerOfMass;
        if (e.rigid.inertiaGiven) {
            const Vector3& i = e.rigid.inertiaDiagonal;
            const Vector3& p = e.rigid.inertiaProducts;
            d.inertiaGiven = true;
            d.inertia.m[0][0] = i.x, d.inertia.m[1][1] = i.y, d.inertia.m[2][2] = i.z;
            d.inertia.m[0][1] = d.inertia.m[1][0] = p.x, d.inertia.m[0][2] = d.inertia.m[2][0] = p.y, d.inertia.m[1][2] = d.inertia.m[2][1] = p.z;
        }
        d.linearDamping = e.rigid.linearDamping >= 0 ? e.rigid.linearDamping : p.linearDamping;
        d.angularDamping = e.rigid.angularDamping >= 0 ? e.rigid.angularDamping : p.angularDamping;
        d.gravityScale = e.rigid.gravityScale;
        if (const auto g = groups.find(e.id); g != groups.end()) d.group = g->second;
        const std::shared_ptr<const TriMesh> mesh = meshOf(e);
        const int b = physics.addBody(d, colliderShapes(e, *mesh), error);
        if (b < 0) {
            notes_.push_back("«" + e.name + "»: " + error);
            continue;
        }
        bodyOfEntity_[e.id] = b;
        if (size_t(b) >= stands_.size()) stands_.resize(size_t(b) + 1, 1);
        stands_[size_t(b)] = d.type == BodyDesc::Type::Static;
        if (e.collider.enabled && e.collider.trigger) zones_.push_back({e.id, b});
        if (animated.count(e.id)) animated_.push_back({e.id, b});
        else if (d.type != BodyDesc::Type::Static && !isArrayCopyId(e.id)) free_.push_back(e.id); // a take records it: a robot's link, a body led by its own velocity
        if (entityOfBody_.size() <= size_t(b)) entityOfBody_.resize(size_t(b) + 1, 0);
        entityOfBody_[size_t(b)] = e.id;
        if (flatOfBody_.size() <= size_t(b)) flatOfBody_.resize(size_t(b) + 1, -1);
        flatOfBody_[size_t(b)] = int(&e - flat_.data());
    }
    for (const JointObject& j : graph_.joints) buildJoint(physics, j);
    buildPower();
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

bool RobotWorld::setJoints(PhysicsWorld& physics, const std::vector<JointObject>& given) {
    if (given.size() != joints_.size()) return false;
    // A joint the panel drives (a gripper's finger closed by «grip», a jogged servo) keeps the panel's drive when the
    // edit left it as authored: an IK goal moved by the API while the gripper held a cube opened it and dropped the cube.
    auto sameDrives = [](const JointObject& x, const JointObject& y) {
        for (int k = 0; k < 6; ++k) {
            const JointDrive &a = x.dof[k].drive, &b = y.dof[k].drive;
            if (a.enabled != b.enabled || a.stiffness != b.stiffness || a.damping != b.damping || a.target != b.target ||
                a.targetVelocity != b.targetVelocity || a.maxForce != b.maxForce || a.voltage != b.voltage || a.constant != b.constant)
                return false;
        }
        return true;
    };
    std::vector<JointObject> joints = given;
    for (JointObject& n : joints) {
        if (!handed_.count(n.id)) continue;
        const JointObject* authored = findJoint(graph_, n.id);
        const JointObject* driven = findJoint(live_, n.id);
        if (!authored || !driven || !sameDrives(*authored, n)) {
            handed_.erase(n.id); // edited by hand: the edit wins
            continue;
        }
        for (int k = 0; k < 6; ++k) n.dof[k].drive = driven->dof[k].drive;
    }
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
        for (const JointObject& n : given)
            if (n.id == j.id) j = n;
    live_.joints = joints;
    return true;
}

void RobotWorld::setGoals(const std::vector<Entity>& goals) {
    const auto same = [](const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    for (const Entity& goal : goals) { // a path set while the scene runs begins now (SceneGraph.h, Entity::path)
        const Entity* was = findEntity(graph_, goal.id);
        const bool kept = was && was->path.size() == goal.path.size() && std::equal(goal.path.begin(), goal.path.end(), was->path.begin(), same) &&
                          was->pathSpeed == goal.pathSpeed && was->pathLoop == goal.pathLoop && was->pathSmooth == goal.pathSmooth &&
                          same(was->position, goal.position);
        if (!goal.path.empty() && !kept) pathFrom_[goal.id] = Real(double(clockNs_) * 1e-9);
    }
    for (SceneGraph* g : {&graph_, &live_}) {
        g->entities.erase(std::remove_if(g->entities.begin(), g->entities.end(), [](const Entity& e) { return isHandle(e); }),
                          g->entities.end());
        g->entities.insert(g->entities.end(), goals.begin(), goals.end());
    }
    drawnKnown_ = false;
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

Real RobotWorld::jointSpeed(const PhysicsWorld& physics, uint32_t joint) const {
    for (const LiveJoint& j : joints_)
        if (j.spec.id == joint) return j.dof >= 0 && physics.jointAlive(j.joint) ? axisOf(physics, j).speed : 0;
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
RobotWorld::JointAxis RobotWorld::axisOf(const PhysicsWorld& physics, const LiveJoint& j) const {
    JointAxis x;
    x.A = physics.state(j.a);
    x.B = j.b >= 0 ? physics.state(j.b) : BodyState();
    x.at = x.A.position + x.A.rotation.rotate(j.spec.anchorA);
    x.axis[j.dof % 3] = 1;
    x.axis = (x.B.rotation * eulerTurn(j.spec.frameB)).rotate(x.axis);
    x.turn = j.dof >= DofRx;
    auto pointVelocity = [](const BodyState& s, const Vector3& p) { return s.velocity + cross(s.angularVelocity, p - s.centre); };
    x.speed = x.turn ? dot(x.A.angularVelocity - x.B.angularVelocity, x.axis) : dot(pointVelocity(x.A, x.at) - pointVelocity(x.B, x.at), x.axis);
    return x;
}

void RobotWorld::drive(PhysicsWorld& physics, const LiveJoint& j, Real h) const {
    if (!j.ownDrive || !physics.jointAlive(j.joint)) return;
    const JointAxis x = axisOf(physics, j);
    const BodyState &A = x.A, &B = x.B;
    const Vector3 oA = x.at, axis = x.axis;
    const bool turn = x.turn;
    const Real v = x.speed;
    Real c[6];
    coordinates(physics, j, c);
    const int k = j.dof;
    const JointDrive& d = j.spec.dof[k].drive;
    // How readily the two links turn (or slide) against each other along the axis: 1 / I = a's + b's.
    auto mobility = [&](const BodyState& s) {
        if (s.mass <= 0) return Real(0);
        return turn ? dot(axis, s.inertia.inverse() * axis) : 1 / s.mass;
    };
    const Real m = mobility(A) + mobility(B);
    const Real damping = m > 0 ? d.damping / (1 + d.damping * h * m) : d.damping;
    Real f;
    if (d.constant > 0 && j.driver >= 0) f = d.constant * drivers_[size_t(j.driver)].current; // through its driver (RobotPower.cpp)
    else if (d.constant > 0) f = d.constant * (d.voltage - d.constant * v) / std::max(d.resistance, Real(1e-6));
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
    const Real now = Real(double(clockNs_) * 1e-9);
    for (Entity& goal : live_.entities) {
        const int hand = goal.goalOf ? bodyOf(goal.goalOf) : -1;
        if (hand < 0 || !physics.alive(hand)) continue;
        Vector3 to;
        Quaternion turn;
        if (const Entity* authored = goal.path.empty() ? nullptr : findEntity(graph_, goal.id)) {
            // A goal on its path (Entity::path): where the path has walked it by now, from the authored goal's place - the
            // live goal stands there (drawn there, the frame's goals), and the arm follows it by inverse kinematics.
            const auto from = pathFrom_.find(goal.id);
            pathPose(*authored, now - (from == pathFrom_.end() ? 0 : from->second), to, turn);
            setWorldPose(live_, goal, to, turn);
        }
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

// The entities the playing sequence moves: those it binds (a robot's link recorded in a take among them), and those
// inside a group it binds - not a robot's link a joint drives there (it rides on its animated base).
std::unordered_set<uint32_t> RobotWorld::animatedEntities(const SceneGraph& g) {
    std::unordered_set<uint32_t> out, driven;
    const SequenceObject* q = playingSequence(g);
    if (!q || q->tracks.empty()) return out;
    for (const JointObject& j : g.joints) driven.insert(j.a);
    for (const Entity& e : g.entities) {
        if (trackOf(*q, e.id)) {
            out.insert(e.id);
            continue;
        }
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

// The playing sequence's keyed fields a running robot takes by command, at the world's time: a servo's target or velocity
// ("drive <dof>"), a motor's duty ("bridge duty"), a force's strength - as a controller or robot_command sets them. The
// rest - a colour, a light - the live scene takes when the frame is read (readPoses seeks it).
void RobotWorld::playFields(PhysicsWorld& physics) {
    const SequenceObject* q = playingSequence(graph_);
    if (!q || q->fields.empty()) return;
    const Real time = Real(double(clockNs_) * 1e-9), length = q->length();
    std::vector<Real> values;
    std::string ignored;
    for (const FieldTrack& t : q->fields) {
        const bool drive = t.line.rfind("drive", 0) == 0 && (t.key == "target" || t.key == "velocity");
        const bool duty = t.line == "bridge" && t.key == "duty", strength = t.key == "strength";
        if ((drive || duty || strength) && sampleField(t.keys, time, q->loop, length, values) && !values.empty())
            command(physics, t.object, t.key, values[0], ignored);
    }
}

void RobotWorld::step(PhysicsWorld& physics, Real dt, int substeps) {
    contactsKnown_ = false;
    playFields(physics);
    if (!controls_.empty()) driveControllers(physics, dt);
    leadGoals(physics, dt);
    physics.moveHand(grabJoint_, grabTarget_, dt);
    const int n = engineSteps(substeps), inner = std::max(substeps, 1) / n;
    for (int s = 0; s < n; ++s) {
        {
            Probe::Timer timer("physics/drives ms");
            if (!drivers_.empty()) power(physics, dt / Real(n));
            for (const LiveJoint& j : joints_) drive(physics, j, dt / Real(n));
            carryGrab(physics);
            for (const auto& [body, ft] : pushes_) { // a plugin's forces over the whole frame, not its first substep
                if (!physics.alive(body)) continue;
                physics.applyForce(body, ft.first, physics.state(body).centre);
                physics.applyTorque(body, ft.second);
            }
            for (const Force& f : graph_.forces) {
                if (f.mode == ForceMode::Constant) actForce(physics, f);
                else if (!fired(f.id) && clockNs_ >= forceDueNs(f)) actForce(physics, f), markFired(f.id);
            }
            if (!animated_.empty()) leadAnimation(physics, Real(double(clockNs_) * 1e-9) + dt / Real(n), dt / Real(n)); // the clock moves on per substep
        }
        {
            Probe::Timer timer("physics/engine ms");
            physics.step(dt / Real(n), inner);
        }
        clockNs_ += std::llround(double(dt) / n * 1e9);
        Probe::Timer timer("physics/sensors ms");
        for (const auto& x : sensors_) x->step(physics, *this, dt / Real(n), clockNs_);
    }
    pushes_.clear();
    if (!zones_.empty()) inside_ = physics.triggered();
    readPoses(physics);
    if (--framesToLostCheck_ < 0) framesToLostCheck_ = 29, dropLost(physics); // after the poses: the scene keeps where it was last
    if (recording_) sampleTake(physics);
}

void RobotWorld::markFired(uint32_t id) {
    firedIds_.insert(std::lower_bound(firedIds_.begin(), firedIds_.end(), id), id);
}

// A body's own force (forceBody: a directional force or a torque whose parent is a shape) acts on that body alone, at the
// force's own point as the body stands now - off the centre of mass it turns the body; its strength is N s or N (a
// torque: about the force's own +y, N m s or N m). A
// field acts on each rigid body of its reach alike, light or heavy, at the centre of mass (no torque): its strength
// is the velocity (m/s) or the acceleration (m/s^2) it gives, times the falloff's weight, along the axis or away from
// the force's origin; its poses are the end of the last frame's.
void RobotWorld::actForce(PhysicsWorld& physics, const Force& f) {
    if (!effectivelyVisible(live_, f)) return;
    if (forceBody(live_, f)) {
        const int own = bodyOf(f.parent);
        if (own < 0 || !physics.alive(own)) return;
        const BodyState s = physics.state(own);
        if (s.mass <= 0) return; // fixed or led: nothing pushes it
        const Vector3 at = s.position + s.rotation.rotate(f.position), push = forceAxis(s.rotation * objectRotation(f)) * f.strength;
        if (f.shape == ForceShape::Torque) { // about its own +y, wherever on the body it stands
            if (f.mode == ForceMode::Constant) physics.applyTorque(own, push);
            else physics.applyAngularImpulse(own, push);
        } else if (f.mode == ForceMode::Constant) physics.applyForce(own, push, at);
        else physics.applyImpulse(own, push, at);
        return;
    }
    if (f.shape == ForceShape::Torque) return; // a torque on no body turns nothing
    Vector3 p;
    Quaternion q;
    worldPose(live_, f, p, q);
    const Vector3 axis = forceAxis(q);
    for (size_t b = 1; b < entityOfBody_.size(); ++b) {
        if (entityOfBody_[b] == 0 || !physics.alive(int(b))) continue;
        const BodyState s = physics.state(int(b));
        if (s.mass <= 0) continue;
        if (!f.tag.empty()) {
            const std::vector<std::string>& tags = flat_[size_t(flatOfBody_[b])].tags;
            if (std::find(tags.begin(), tags.end(), f.tag) == tags.end()) continue;
        }
        const Vector3 away = s.centre - p;
        const Real d = length(away);
        if (f.radius > 0 && d > f.radius) continue;
        if (f.shape == ForceShape::Radial && d < 1e-6_r) continue;
        const Real m = std::max(d, 1_r);
        const Real w = f.falloff == ForceFalloff::Linear && f.radius > 0 ? 1_r - d / f.radius
                     : f.falloff == ForceFalloff::InverseSquare ? 1_r / (m * m) : 1_r;
        // A field's strength is what it does to a body, whatever the body weighs: m/s of velocity, m/s^2 of acceleration.
        const Vector3 push = (f.shape == ForceShape::Radial ? away / d : axis) * (f.strength * w * s.mass);
        if (f.mode == ForceMode::Constant) physics.applyForce(int(b), push, s.centre);
        else physics.applyImpulse(int(b), push, s.centre);
    }
}

bool RobotWorld::fireForce(PhysicsWorld& physics, uint32_t id, std::string& error) {
    const auto f = std::find_if(graph_.forces.begin(), graph_.forces.end(), [id](const Force& x) { return x.id == id; });
    if (f == graph_.forces.end()) return error = "no force with id " + std::to_string(id) + " in the scene", false;
    if (f->mode == ForceMode::Constant) return error = "a constant force acts on its own: only an impulse is fired", false;
    actForce(physics, *f);
    if (!fired(id)) markFired(id);
    return true;
}

void RobotWorld::setForces(const std::vector<Force>& forces) {
    const std::vector<Force> old = std::move(graph_.forces);
    std::vector<uint32_t> kept;
    for (const Force& f : forces) {
        if (f.mode != ForceMode::Impulse) continue;
        const bool isNew = std::none_of(old.begin(), old.end(), [&f](const Force& o) { return o.id == f.id; });
        if (isNew ? clockNs_ >= forceDueNs(f) : fired(f.id)) kept.push_back(f.id); // a new one already due does not fire by itself
    }
    std::sort(kept.begin(), kept.end());
    firedIds_ = std::move(kept);
    graph_.forces = live_.forces = forces;
}

int RobotWorld::engineSteps(int substeps) const {
    const bool between = !drivers_.empty() || !sensors_.empty() || !animated_.empty() ||
                         std::any_of(joints_.begin(), joints_.end(), [](const LiveJoint& j) { return j.ownDrive; });
    return between ? std::max(substeps, 1) : 1;
}

void RobotWorld::dropLost(PhysicsWorld& physics) {
    for (size_t b = 0; b < entityOfBody_.size(); ++b) {
        if (entityOfBody_[b] == 0 || !physics.alive(int(b))) continue;
        const BodyState s = physics.state(int(b));
        if (s.mass > 0 && s.position.y < -kLostDepth) physics.removeBody(int(b)), contactsKnown_ = false;
    }
}

const std::vector<ContactPoint>& RobotWorld::contactsNow(const PhysicsWorld& physics) const {
    if (!contactsKnown_) contacts_ = physics.contacts(), contactsKnown_ = true;
    return contacts_;
}

bool RobotWorld::push(const PhysicsWorld& physics, uint32_t entity, const Vector3& force, const Vector3& torque, std::string& error) {
    const int b = bodyOf(entity);
    if (b < 0 || !physics.alive(b) || physics.state(b).mass <= 0) return error = "entity " + std::to_string(entity) + " has no moving body", false;
    auto& ft = pushes_[b];
    ft.first = ft.first + force, ft.second = ft.second + torque;
    return true;
}

void RobotWorld::startRecording(const PhysicsWorld& physics, Real from) {
    recording_ = true;
    takeFrom_ = from;
    take_.assign(free_.size(), {});
    sampleTake(physics);
}

// Each free body's pose on its parent and the velocity of its frame's origin (v + w x r from the centre of mass), turned
// onto its parent.
void RobotWorld::sampleTake(const PhysicsWorld& physics) {
    const Real t = Real(double(clockNs_) * 1e-9);
    if (t < takeFrom_ - 1e-4_r) return; // before the take's start: the world runs, nothing is kept
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

// Every sample within the take's tolerances of the first: 0.5 mm and 0.2 degrees.
static bool stoodStill(const std::vector<AnimKey>& samples) {
    if (samples.size() < 2) return true;
    const std::vector<AnimKey> first{samples.front()};
    Vector3 p;
    Quaternion q0, q;
    samplePose(first, 0, false, 0, p, q0);
    for (const AnimKey& k : samples) {
        samplePose({k}, 0, false, 0, p, q);
        if (length(k.position - samples.front().position) > 0.0005_r || std::fabs(q.w * q0.w + q.x * q0.x + q.y * q0.y + q.z * q0.z) < std::cos(0.1_r * kPi / 180))
            return false;
    }
    return true;
}

SequenceObject RobotWorld::takeRecording(const std::string& name) {
    SequenceObject q;
    q.name = name, q.active = true, q.play = false; // shown, not replayed: the next ▶ is physics again
    for (size_t i = 0; recording_ && i < free_.size(); ++i) {
        if (stoodStill(take_[i])) continue; // a resting box's settling, a held link: no track
        std::vector<AnimKey> keys = thinKeys(take_[i], 0.0005_r, 0.2_r);
        q.tracks.push_back({free_[i], std::move(keys)});
    }
    // What led the run comes along - a camera's keys, a light's, a led body's: the take alone gives the whole run back.
    if (const SequenceObject* played = playingSequence(graph_); played && !q.tracks.empty())
        for (const SequenceTrack& t : played->tracks)
            if (!trackOf(q, t.object)) q.tracks.push_back(t);
    recording_ = false;
    take_.clear();
    return q;
}

void RobotWorld::readPoses(const PhysicsWorld& physics) {
    if (SequenceObject* q = playingSequence(live_)) seekSequence(live_, *q, Real(double(clockNs_) * 1e-9)); // lights, cameras, shapes it moves
    for (Entity& e : live_.entities) {
        const int b = bodyOf(e.id);
        if (b < 0 || !physics.alive(b)) continue;
        if (stands(b)) continue; // static: where it was put. A kinematic body has no mass and moves
        const BodyState s = physics.state(b);
        setWorldPose(live_, e, s.position, s.rotation);
    }
}

uint32_t RobotWorld::objectIdOfBody(int body) const {
    if (body < 0 || size_t(body) >= entityOfBody_.size()) return 0;
    const uint32_t id = entityOfBody_[size_t(body)];
    return isArrayCopyId(id) ? arrayOfCopyId(id) : id;
}

bool RobotWorld::stands(int body) const { return body < 0 || size_t(body) >= stands_.size() || stands_[size_t(body)] != 0; }

bool RobotWorld::isFree(uint32_t entity) const { return std::find(free_.begin(), free_.end(), entity) != free_.end(); }

int RobotWorld::bodyOf(uint32_t entity) const {
    auto it = bodyOfEntity_.find(entity);
    return it == bodyOfEntity_.end() ? -1 : it->second;
}

// An array's copy is no object of its own: a zone names the array it belongs to, once, and the copies of an array
// of zones are one zone - the array.
std::vector<std::pair<uint32_t, std::vector<uint32_t>>> RobotWorld::zones() const {
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> out;
    for (const auto& [entity, body] : zones_) {
        const uint32_t zone = isArrayCopyId(entity) ? arrayOfCopyId(entity) : entity;
        auto row = std::find_if(out.begin(), out.end(), [zone](const auto& z) { return z.first == zone; });
        if (row == out.end()) row = out.insert(out.end(), {zone, {}});
        for (const auto& [sensor, visitor] : inside_) {
            if (sensor != body || (stands(body) && stands(visitor))) continue; // the floor under a zone on it, a wall through it: they were always there
            if (const uint32_t id = objectIdOfBody(visitor); id != 0 && id != zone) row->second.push_back(id);
        }
    }
    for (auto& [zone, ids] : out) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    return out;
}

void RobotWorld::fill(const PhysicsWorld& physics, Frame& out, bool contacts) const {
    out.domain = AABB(Vector3(-0.5_r * graph_.world.size.x, 0, -0.5_r * graph_.world.size.z),
                      Vector3(0.5_r * graph_.world.size.x, graph_.world.size.y, 0.5_r * graph_.world.size.z));
    out.engine = physics.engine();
    if (!drawnKnown_) findDrawn();
    out.bodies.reserve(drawn_.size());
    for (const Drawn& d : drawn_) {
        Frame::Body b = d.body;
        if (b.body >= 0 && physics.alive(b.body)) {
            const BodyState s = physics.state(b.body);
            b.pos = s.position, b.rot = s.rotation;
            b.sleeping = !s.awake;
            b.movable = s.mass > 0;
        } else if (d.entity != SIZE_MAX) { // no body of its own: where the scene has it now - a goal dragged, a parent moved
            const Entity source = worldEntity(live_, live_.entities[d.entity]);
            b.pos = source.position, b.rot = entityRotation(source);
        }
        out.bodies.push_back(std::move(b));
    }
    for (const Entity& e : live_.entities) // the goals on paths, where the world has walked them (the gizmo, a hand taking one)
        if (e.goalOf && !e.path.empty()) {
            const Entity w = worldEntity(live_, e);
            out.goals.push_back({e.id, w.position, entityRotation(w)});
        }
    out.joints = jointVisuals(live_);
    out.sensors = sensorVisuals(live_);
    for (SensorVis& v : out.sensors) { // the glyph shows what the robot reads: a sonar's distance, an encoder's coordinate, a lidar's scan
        const LiveSensor* x = sensor(v.id);
        if (x && (v.kind == int(SensorKind::Sonar) || v.kind == int(SensorKind::Encoder))) v.reading = x->reading().value;
        if (x && v.kind == int(SensorKind::Lidar)) v.ranges = x->reading().ranges;
    }
    if (contacts || Probe::layerOn(DrawLayer::ContactPoints))
        for (const ContactPoint& c : contactsNow(physics)) out.contacts.push_back({c.point, c.normal, c.depth, c.normalImpulse, c.a, c.b});
    describeLights(live_, out);
    describeCameras(out);
    for (const std::string& n : notes_) out.info.push_back({"Не построено", n});
    out.grips = grips(physics);
}

// The frame's entities in the scene's order - the visible ones of worldEntities(live_) - with all a frame says of them
// that a run does not change. A body's pose comes from the engine every frame; an entity without one is posed from
// the scene every frame (fill).
void RobotWorld::findDrawn() const {
    drawn_.clear();
    const std::vector<Entity> world = worldEntities(live_);
    for (size_t k = 0; k < world.size(); ++k) {
        const Entity& source = world[k];
        if (!source.visible) continue;
        Drawn d;
        Frame::Body& b = d.body;
        b.object = source.id;
        b.body = bodyOf(source.id);
        b.mesh = meshOf(source);
        b.color = source.color;
        b.texture = source.texture;
        for (std::string* file : {&b.texture.file, &b.texture.heightFile, &b.texture.normalFile}) // a relative picture: from the scene file's folder
            if (!file->empty() && !std::filesystem::u8path(*file).is_absolute() && !graph_.baseDirectory.empty())
                *file = (std::filesystem::u8path(graph_.baseDirectory) / std::filesystem::u8path(*file)).u8string();
        b.marker = isHandle(source);
        b.robot = robotOf_.count(source.id) > 0;
        b.opacity = source.goalOf ? 0.1_r : source.targetOf ? 0.35_r : source.opacity;
        b.smooth = source.shape == ShapeKind::Sphere;
        b.pos = source.position, b.rot = entityRotation(source);
        b.movable = false;
        if (k < live_.entities.size()) d.entity = k; // worldEntities: the graph's entities in their order, then the arrays' copies
        drawn_.push_back(std::move(d));
    }
    drawnKnown_ = true;
}

// Each IK goal's gripper - the sliders on its effector driven as servos - as the gripper reports it: closing when its
// fingers' targets lie past half their travel; holding when the fingers stand still more than 2 mm short of their
// targets (they stopped on something); closed on nothing when they reached them.
std::vector<Frame::GripInfo> RobotWorld::grips(const PhysicsWorld& physics) const {
    std::vector<Frame::GripInfo> out;
    for (const Entity& goal : live_.entities) {
        if (!goal.goalOf) continue;
        Frame::GripInfo grip{goal.id};
        bool fingers = false, closing = false, moving = false, reached = true;
        for (const LiveJoint& j : joints_) {
            if (j.dof < 0 || j.dof > 2 || (j.spec.a != goal.goalOf && j.spec.b != goal.goalOf) || !physics.jointAlive(j.joint)) continue;
            const JointFreedom& f = j.spec.dof[j.dof];
            if (!f.drive.enabled || f.motion == JointMotion::Locked) continue;
            Real x[6];
            if (!coordinates(physics, j, x)) continue;
            fingers = true;
            closing = closing || f.drive.target > (f.lower + f.upper) / 2;
            moving = moving || std::fabs(axisOf(physics, j).speed) > 0.005_r;
            const Real gap = std::fabs(f.drive.target - x[j.dof]);
            reached = reached && gap < 0.002_r;
            grip.gap = std::max(grip.gap, gap);
        }
        if (!fingers) continue;
        using S = Frame::GripState;
        grip.state = !closing ? S::Open : moving ? S::Closing : reached ? S::Empty : S::Holding;
        out.push_back(grip);
    }
    return out;
}

void RobotWorld::drawLayers(const PhysicsWorld& physics, Real substepDt) const {
    if (Probe::layers() == 0) return;
    using L = DrawLayer;
    for (const ContactPoint& c : contactsNow(physics)) {
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
