// The physics plugin on Box3D (extern/box3d) for the contract src/physics/PhysicsWorld.h
// (docs/contracts/physics.md) - the only file of the repository that includes Box3D. Box3D's conventions
// met here: its revolute joint turns about its frames' z and its spherical cone opens about frame A's z,
// where ours turn about x (a quarter turn about y in between);
// its joint's body A is our b, the one the coordinates are measured from, and its body B our a, so the
// angles keep their sign and the joint's force on B is the force on our a. A static body of no shapes
// stands for the world in a joint to it. The mouse's hand is a motor joint from a kinematic body, as Box3D's
// samples hold a body.
//
// A hinge without limits is re-zeroed before every step: its frame B turned back by the angle it has,
// so the frames' relative turn stays near zero. Box3D (be8c731) prepares the revolute joint's warm start
// from the relative quaternion without bringing it to s >= 0, as its solve does, and past half a turn
// the warm start pumps energy into the off-axis turns - a spinning wheel flies apart
// (docs/box3d-backend.md). Our joint angle is read from the bodies' poses, so it does not see this.
#include "physics/HullPoints.h"
#include "physics/PhysicsWorld.h"

#include <cstdlib>
#include <thread>
#include <box3d/box3d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ke {

namespace {

b3Vec3 b3v(const Vector3& v) { return {float(v.x), float(v.y), float(v.z)}; }
Vector3 kev(b3Vec3 v) { return {Real(v.x), Real(v.y), Real(v.z)}; }
b3Quat b3q(const Quaternion& q) { return {{float(q.x), float(q.y), float(q.z)}, float(q.w)}; }
Quaternion keq(b3Quat q) { return {Real(q.s), Real(q.v.x), Real(q.v.y), Real(q.v.z)}; }
b3Transform b3t(const Vector3& p, const Quaternion& q) { return {b3v(p), b3q(q)}; }

// Box3D builds a hull of at most B3_MAX_HULL_VERTICES vertices (and bounded faces and edges): the points as given
// when they make one, else their extremes along fewer and fewer directions (HullPoints.h) until the hull fits.
b3HullData* boundedHull(const std::vector<Vector3>& all) {
    const auto make = [](const std::vector<Vector3>& points) -> b3HullData* {
        std::vector<b3Vec3> v;
        for (const Vector3& p : points) v.push_back(b3v(p));
        return v.size() >= 4 ? b3CreateHull(v.data(), int(v.size()), B3_MAX_HULL_VERTICES) : nullptr;
    };
    if (all.size() <= 256)
        if (b3HullData* hull = make(all)) return hull;
    for (int n : {kHullDirections, 80, 48, 26})
        if (int(all.size()) > n)
            if (b3HullData* hull = make(extremePoints(all, n))) return hull;
    return nullptr;
}

// Our frames turn about x, Box3D's about z: the frame turned so that its z lies along our x.
const Quaternion kZonX = Quaternion::fromAxisAngle({0, 1, 0}, 0.5_r * kPi);
const Real kMostAngle = 0.99_r * kPi; // Box3D's limit on a revolute's or a twist's angle

void* handle(int index) { return reinterpret_cast<void*>(intptr_t(index) + 1); }
int indexOf(void* user) { return int(reinterpret_cast<intptr_t>(user)) - 1; }

struct RayContext {
    int ignore = -1;
    b3ShapeId shape = b3_nullShapeId;
    b3Vec3 point{}, normal{};
    float fraction = 1;
};

float closestNotIgnored(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void* context) {
    auto* c = static_cast<RayContext*>(context);
    if (indexOf(b3Body_GetUserData(b3Shape_GetBody(shape))) == c->ignore && c->ignore >= 0) return -1; // skip this shape
    c->shape = shape, c->point = point, c->normal = normal, c->fraction = fraction;
    return fraction; // clip the ray here: only nearer hits follow
}

class Box3dWorld final : public PhysicsWorld {
public:
    Box3dWorld(const PhysicsSettings& s, const Vector3& gravity, int threads) : settings_(s) {
        b3WorldDef def = b3DefaultWorldDef();
        def.gravity = b3v(gravity);
        def.contactHertz = float(s.contactHertz);
        def.contactDampingRatio = float(s.contactDamping);
        def.enableSleep = s.sleeping;
        def.enableContinuous = s.continuous;
        def.workerCount = uint32_t(std::max(threads, 1)); // Box3D's own scheduler; the same bits on any count (tested)
        world_ = b3CreateWorld(&def);
        b3BodyDef ground = b3DefaultBodyDef();
        ground_ = b3CreateBody(world_, &ground);
    }
    ~Box3dWorld() override { b3DestroyWorld(world_); }

    std::string engine() const override {
        const b3Version v = b3GetVersion();
        return "Box3D " + std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.revision);
    }

    int addBody(const BodyDesc& body, const std::vector<ShapeDesc>& shapes, std::string& error) override {
        const int index = int(bodies_.size());
        b3BodyDef d = b3DefaultBodyDef();
        d.type = body.type == BodyDesc::Type::Static ? b3_staticBody : body.type == BodyDesc::Type::Kinematic ? b3_kinematicBody : b3_dynamicBody;
        d.position = b3v(body.position);
        d.rotation = b3q(body.rotation.normalized());
        d.linearVelocity = b3v(body.velocity);
        d.angularVelocity = b3v(body.angularVelocity);
        d.linearDamping = float(body.linearDamping);
        d.angularDamping = float(body.angularDamping);
        d.userData = handle(index);
        const b3BodyId id = b3CreateBody(world_, &d);
        for (const ShapeDesc& s : shapes)
            if (!addShape(id, body, s, error)) {
                b3DestroyBody(id);
                return -1;
            }
        if ((body.massGiven || body.centreGiven || body.inertiaGiven) && body.type == BodyDesc::Type::Dynamic) {
            if (body.massGiven && !(body.mass > 0)) return b3DestroyBody(id), error = "the mass is not positive", -1;
            b3MassData m = b3Body_GetMassData(id); // the shapes' own at the density: what is not given stays theirs
            if (body.massGiven) {
                const float scale = m.mass > 0 ? float(body.mass) / m.mass : 1.0f; // the same shape, heavier or lighter
                if (!body.inertiaGiven) m.inertia = {b3MulSV(scale, m.inertia.cx), b3MulSV(scale, m.inertia.cy), b3MulSV(scale, m.inertia.cz)};
                m.mass = float(body.mass);
            }
            if (body.centreGiven) m.center = b3v(body.centre);
            if (body.inertiaGiven) m.inertia = {b3v(body.inertia.col(0)), b3v(body.inertia.col(1)), b3v(body.inertia.col(2))};
            b3Body_SetMassData(id, m);
        }
        bodies_.push_back(id);
        return index;
    }

    void removeBody(int body) override {
        if (!alive(body)) return;
        b3DestroyBody(bodies_[size_t(body)]);
        bodies_[size_t(body)] = b3_nullBodyId;
        dropHands();
    }
    bool alive(int body) const override {
        return body >= 0 && size_t(body) < bodies_.size() && b3Body_IsValid(bodies_[size_t(body)]);
    }

    BodyState state(int body) const override {
        BodyState s;
        if (!alive(body)) return s;
        const b3BodyId id = bodies_[size_t(body)];
        s.position = kev(b3Body_GetPosition(id));
        s.rotation = keq(b3Body_GetRotation(id));
        s.centre = kev(b3Body_GetWorldCenter(id));
        s.velocity = kev(b3Body_GetLinearVelocity(id));
        s.angularVelocity = kev(b3Body_GetAngularVelocity(id));
        s.mass = b3Body_GetType(id) == b3_dynamicBody ? Real(b3Body_GetMass(id)) : 0;
        if (s.mass > 0) { // the engine keeps it about the centre in the body's axes: turned into the world's
            const b3Matrix3 I = b3Body_GetLocalRotationalInertia(id);
            const b3Vec3 col[3] = {I.cx, I.cy, I.cz};
            Matrix3x3 local = Matrix3x3::zero();
            for (int c = 0; c < 3; ++c) local.m[0][c] = Real(col[c].x), local.m[1][c] = Real(col[c].y), local.m[2][c] = Real(col[c].z);
            const Matrix3x3 R = s.rotation.toMatrix3x3();
            s.inertia = R * local * R.transposed();
        }
        s.awake = b3Body_IsAwake(id);
        return s;
    }

    void setVelocity(int body, const Vector3& v, const Vector3& w) override {
        if (!alive(body)) return;
        b3Body_SetLinearVelocity(bodies_[size_t(body)], b3v(v));
        b3Body_SetAngularVelocity(bodies_[size_t(body)], b3v(w));
    }
    void setPose(int body, const Vector3& p, const Quaternion& q) override {
        if (alive(body)) b3Body_SetTransform(bodies_[size_t(body)], b3v(p), b3q(q.normalized()));
    }
    void wake(int body) override {
        if (alive(body)) b3Body_SetAwake(bodies_[size_t(body)], true);
    }
    void applyForce(int body, const Vector3& f, const Vector3& p) override {
        if (alive(body)) b3Body_ApplyForce(bodies_[size_t(body)], b3v(f), b3v(p), true);
    }
    void applyTorque(int body, const Vector3& t) override {
        if (alive(body)) b3Body_ApplyTorque(bodies_[size_t(body)], b3v(t), true);
    }
    void applyImpulse(int body, const Vector3& j, const Vector3& p) override {
        if (alive(body)) b3Body_ApplyLinearImpulse(bodies_[size_t(body)], b3v(j), b3v(p), true);
    }

    int addJoint(const JointDesc& j, std::string& error) override {
        if (!alive(j.a)) return error = "the joint's link a has no body", -1;
        if (j.b >= 0 && !alive(j.b)) return error = "the joint's link b has no body", -1;
        if (j.a == j.b) return error = "the joint holds a body to itself", -1;
        const b3BodyId A = j.b >= 0 ? bodies_[size_t(j.b)] : ground_, B = bodies_[size_t(j.a)];
        const bool turned = j.type == JointType::Hinge || j.type == JointType::Ball;
        auto fill = [&](b3JointDef& b) {
            b.bodyIdA = A, b.bodyIdB = B;
            b.localFrameA = b3t(j.anchorB, turned ? j.frameB * kZonX : j.frameB);
            b.localFrameB = b3t(j.anchorA, turned ? j.frameA * kZonX : j.frameA);
            // q and -q are one turn, but Box3D's joints read the frames' relative quaternion with its sign: frames
            // written as (0, 90, 90) and (-90, -180, 0) agree yet start 360 degrees apart, and the joint spins its
            // links up out of nothing. Frame B is given the sign that makes the two agree in the world.
            const b3Quat qa = b3MulQuat(b3Body_GetRotation(A), b.localFrameA.q), qb = b3MulQuat(b3Body_GetRotation(B), b.localFrameB.q);
            if (qa.v.x * qb.v.x + qa.v.y * qb.v.y + qa.v.z * qb.v.z + qa.s * qb.s < 0)
                b.localFrameB.q = {{-b.localFrameB.q.v.x, -b.localFrameB.q.v.y, -b.localFrameB.q.v.z}, -b.localFrameB.q.s};
            b.constraintHertz = float(settings_.jointHertz);
            b.constraintDampingRatio = float(settings_.jointDamping);
            b.collideConnected = j.collide;
            b.userData = handle(int(joints_.size()));
        };
        b3JointId id = b3_nullJointId;
        switch (j.type) {
        case JointType::Hinge: {
            b3RevoluteJointDef d = b3DefaultRevoluteJointDef();
            fill(d.base);
            d.enableLimit = j.limited;
            d.lowerAngle = float(std::clamp(j.lower, -kMostAngle, kMostAngle));
            d.upperAngle = float(std::clamp(j.upper, -kMostAngle, kMostAngle));
            d.enableMotor = j.motor, d.motorSpeed = float(j.motorSpeed), d.maxMotorTorque = float(j.maxMotorForce);
            id = b3CreateRevoluteJoint(world_, &d);
            break;
        }
        case JointType::Slider: {
            b3PrismaticJointDef d = b3DefaultPrismaticJointDef();
            fill(d.base);
            d.enableLimit = j.limited, d.lowerTranslation = float(j.lower), d.upperTranslation = float(j.upper);
            d.enableMotor = j.motor, d.motorSpeed = float(j.motorSpeed), d.maxMotorForce = float(j.maxMotorForce);
            id = b3CreatePrismaticJoint(world_, &d);
            break;
        }
        case JointType::Ball: {
            b3SphericalJointDef d = b3DefaultSphericalJointDef();
            fill(d.base);
            d.enableConeLimit = j.limited, d.coneAngle = float(std::clamp(j.upper, 0.0_r, kPi));
            id = b3CreateSphericalJoint(world_, &d);
            break;
        }
        case JointType::Fixed: {
            b3WeldJointDef d = b3DefaultWeldJointDef();
            fill(d.base);
            id = b3CreateWeldJoint(world_, &d);
            break;
        }
        default: return error = "Box3D has no joint with this mix of free turns and travels", -1;
        }
        joints_.push_back({id, j.type, j.type == JointType::Hinge && !j.limited});
        return int(joints_.size()) - 1;
    }

    void removeJoint(int joint) override {
        if (!jointAlive(joint)) return;
        const Joint& j = joints_[size_t(joint)];
        if (B3_IS_NON_NULL(j.hand) && b3Body_IsValid(j.held)) b3Body_EnableSleep(j.held, true);
        b3DestroyJoint(j.id, true);
        joints_[size_t(joint)].id = b3_nullJointId;
        dropHands();
    }

    // As Box3D's samples hold a body: a kinematic body of no shapes at the target, moved there over each step,
    // and a motor joint's linear spring from it to the held point; its velocity motor at zero turn brakes the
    // body's spin, as a hand's friction. The held body may not sleep: held still, it would count its time to sleep,
    // and let go - woken only if asleep - it would fall asleep in the air at the next step.
    int addHand(const HandDesc& h, std::string& error) override {
        if (!alive(h.body) || b3Body_GetType(bodies_[size_t(h.body)]) != b3_dynamicBody) return error = "only a moving body can be held", -1;
        const b3BodyId held = bodies_[size_t(h.body)];
        b3BodyDef m = b3DefaultBodyDef();
        m.type = b3_kinematicBody;
        m.position = b3v(h.point);
        m.enableSleep = false;
        const b3BodyId mouse = b3CreateBody(world_, &m);
        b3MotorJointDef d = b3DefaultMotorJointDef();
        d.base.bodyIdA = mouse;
        d.base.bodyIdB = held;
        d.base.localFrameB.p = b3Body_GetLocalPoint(held, b3v(h.point));
        d.linearHertz = float(h.hertz);
        d.linearDampingRatio = float(h.dampingRatio);
        d.maxSpringForce = float(h.maxForce);
        d.maxVelocityTorque = float(h.maxTorque);
        joints_.push_back({b3CreateMotorJoint(world_, &d), JointType::Fixed, false, mouse, held});
        b3Body_EnableSleep(held, false);
        return int(joints_.size()) - 1;
    }
    void moveKinematic(int body, const Vector3& position, const Quaternion& rotation, Real dt) override {
        if (!alive(body) || !(dt > 0)) return;
        const b3BodyId id = bodies_[size_t(body)];
        if (b3Body_GetType(id) == b3_kinematicBody) b3Body_SetTargetTransform(id, {b3v(position), b3q(rotation)}, float(dt), true);
    }

    void moveHand(int joint, const Vector3& target, Real dt) override {
        if (!jointAlive(joint) || B3_IS_NULL(joints_[size_t(joint)].hand) || !(dt > 0)) return;
        b3Body_SetTargetTransform(joints_[size_t(joint)].hand, {b3v(target), b3Quat_identity}, float(dt), true);
    }
    bool jointAlive(int joint) const override {
        return joint >= 0 && size_t(joint) < joints_.size() && b3Joint_IsValid(joints_[size_t(joint)].id);
    }

    void setMotor(int joint, bool on, Real speed, Real maxForce) override {
        if (!jointAlive(joint)) return;
        const Joint& j = joints_[size_t(joint)];
        if (j.type == JointType::Hinge) {
            b3RevoluteJoint_EnableMotor(j.id, on);
            b3RevoluteJoint_SetMotorSpeed(j.id, float(speed));
            b3RevoluteJoint_SetMaxMotorTorque(j.id, float(maxForce));
        } else if (j.type == JointType::Slider) {
            b3PrismaticJoint_EnableMotor(j.id, on);
            b3PrismaticJoint_SetMotorSpeed(j.id, float(speed));
            b3PrismaticJoint_SetMaxMotorForce(j.id, float(maxForce));
        }
    }
    Real motorForce(int joint) const override {
        if (!jointAlive(joint)) return 0;
        const Joint& j = joints_[size_t(joint)];
        return j.type == JointType::Hinge ? Real(b3RevoluteJoint_GetMotorTorque(j.id))
             : j.type == JointType::Slider ? Real(b3PrismaticJoint_GetMotorForce(j.id)) : 0;
    }
    void reaction(int joint, Vector3& force, Vector3& torque) const override {
        force = torque = Vector3(0.0_r);
        if (!jointAlive(joint)) return;
        force = kev(b3Joint_GetConstraintForce(joints_[size_t(joint)].id));
        torque = kev(b3Joint_GetConstraintTorque(joints_[size_t(joint)].id));
    }

    bool castRay(const Vector3& from, const Vector3& to, BodyHit& hit, int ignore) const override {
        RayContext c;
        c.ignore = ignore;
        b3World_CastRay(world_, b3v(from), b3v(to - from), b3DefaultQueryFilter(), closestNotIgnored, &c);
        if (B3_IS_NULL(c.shape)) return false;
        hit.body = indexOf(b3Body_GetUserData(b3Shape_GetBody(c.shape)));
        hit.distance = Real(c.fraction) * length(to - from);
        hit.point = kev(c.point);
        hit.normal = kev(c.normal);
        return true;
    }

    std::vector<ContactPoint> contacts() const override {
        std::vector<ContactPoint> out;
        std::vector<b3ContactData> data;
        for (size_t i = 0; i < bodies_.size(); ++i) bodyContacts(bodies_[i], false, data, out);
        return out;
    }

    std::vector<ContactPoint> contactsOf(int body) const override {
        std::vector<ContactPoint> out;
        std::vector<b3ContactData> data;
        if (body >= 0 && size_t(body) < bodies_.size()) bodyContacts(bodies_[size_t(body)], true, data, out);
        return out;
    }

    // The touching points of a body's contacts. Listing the world, a pair of two moving bodies comes from its body a
    // only; asked for one body, all of its own.
    void bodyContacts(b3BodyId id, bool all, std::vector<b3ContactData>& data, std::vector<ContactPoint>& out) const {
        if (!b3Body_IsValid(id) || (!all && b3Body_GetType(id) == b3_staticBody)) return;
        data.resize(size_t(b3Body_GetContactCapacity(id)));
        const int n = data.empty() ? 0 : b3Body_GetContactData(id, data.data(), int(data.size()));
        for (int k = 0; k < n; ++k) {
            const b3BodyId a = b3Shape_GetBody(data[size_t(k)].shapeIdA), b = b3Shape_GetBody(data[size_t(k)].shapeIdB);
            const bool mine = B3_ID_EQUALS(a, id);
            const b3BodyId other = mine ? b : a;
            if (!all && !mine && b3Body_GetType(other) != b3_staticBody) continue;
            const Vector3 centreA = kev(b3Body_GetWorldCenter(a));
            for (int m = 0; m < data[size_t(k)].manifoldCount; ++m) {
                const b3Manifold& manifold = data[size_t(k)].manifolds[m];
                for (int p = 0; p < manifold.pointCount; ++p) {
                    const b3ManifoldPoint& q = manifold.points[p];
                    if (q.separation > 0 && q.totalNormalImpulse <= 0) continue; // speculative, not touching
                    out.push_back({indexOf(b3Body_GetUserData(a)), indexOf(b3Body_GetUserData(b)), centreA + kev(q.anchorA),
                                   kev(manifold.normal), Real(-q.separation), Real(q.totalNormalImpulse)});
                }
            }
        }
    }

    void setGravity(const Vector3& g) override { b3World_SetGravity(world_, b3v(g)); }
    void step(Real dt, int substeps) override {
        for (const Joint& j : joints_)
            if (j.rezero && b3Joint_IsValid(j.id)) {
                b3Transform frame = b3Joint_GetLocalFrameB(j.id);
                const b3Quat back = b3MakeQuatFromAxisAngle(b3Vec3{0, 0, 1}, -b3RevoluteJoint_GetAngle(j.id));
                frame.q = b3NormalizeQuat(b3MulQuat(frame.q, back));
                b3Joint_SetLocalFrameB(j.id, frame);
            }
        b3World_Step(world_, float(dt), substeps > 0 ? substeps : settings_.substeps);
    }

private:
    bool addShape(b3BodyId id, const BodyDesc& body, const ShapeDesc& s, std::string& error) {
        b3ShapeDef d = b3DefaultShapeDef();
        d.density = float(body.density);
        d.baseMaterial.friction = float(s.friction);
        d.baseMaterial.restitution = float(s.restitution);
        d.filter.groupIndex = -body.group; // Box3D: a negative group never collides with itself
        switch (s.kind) {
        case ShapeDesc::Kind::Box: {
            const b3BoxHull box = b3MakeTransformedBoxHull(float(s.halfExtents.x), float(s.halfExtents.y), float(s.halfExtents.z),
                                                           b3t(s.position, s.rotation));
            b3CreateHullShape(id, &d, &box.base);
            return true;
        }
        case ShapeDesc::Kind::Sphere: {
            const b3Sphere sphere{b3v(s.position), float(s.radius)};
            b3CreateSphereShape(id, &d, &sphere);
            return true;
        }
        case ShapeDesc::Kind::Capsule: {
            const Vector3 half = s.rotation.rotate(Vector3(0, s.halfHeight, 0));
            const b3Capsule capsule{b3v(s.position - half), b3v(s.position + half), float(s.radius)};
            b3CreateCapsuleShape(id, &d, &capsule);
            return true;
        }
        case ShapeDesc::Kind::Hull: {
            b3HullData* hull = boundedHull(s.points);
            if (!hull) return error = "the points make no solid hull", false;
            b3CreateTransformedHullShape(id, &d, hull, b3t(s.position, s.rotation), b3Vec3{1, 1, 1});
            b3DestroyHull(hull);
            return true;
        }
        }
        return false;
    }

    struct Joint {
        b3JointId id;
        JointType type;
        bool rezero = false;              // a hinge without limits: its frames' relative turn kept near zero
        b3BodyId hand = b3_nullBodyId;    // a mouse's hand: the kinematic body that carries its target,
        b3BodyId held = b3_nullBodyId;    // and the body it holds
    };
    // A hand whose joint is gone (let go, or its body removed - Box3D takes the body's joints with it) takes its
    // kinematic body along.
    void dropHands() {
        for (Joint& j : joints_)
            if (B3_IS_NON_NULL(j.hand) && !b3Joint_IsValid(j.id)) b3DestroyBody(j.hand), j.hand = b3_nullBodyId;
    }
    PhysicsSettings settings_;
    b3WorldId world_;
    b3BodyId ground_;
    std::vector<b3BodyId> bodies_;
    std::vector<Joint> joints_;
};

} // namespace

int physicsThreads() {
    if (const char* given = std::getenv("KE_PHYSICS_THREADS"); given && std::atoi(given) > 0) return std::min(std::atoi(given), 64);
    return std::clamp(int(std::thread::hardware_concurrency()) / 2, 1, 8);
}

std::unique_ptr<PhysicsWorld> makePhysicsWorld(const PhysicsSettings& settings, const Vector3& gravity, int threads) {
    return std::make_unique<Box3dWorld>(settings, gravity, threads > 0 ? threads : physicsThreads());
}

} // namespace ke
