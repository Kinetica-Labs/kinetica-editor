// The physics plugin on kinetica-physics (extern/kinetica-physics) for the contract src/physics/PhysicsWorld.h: the
// boundary's calls in Kinetica's types turned into the facade's plain ones (KpFacade.h), which alone meets the engine.
// What the engine does not do is said, not faked: a joint's force and its motor's are not told (zero), a hand pulls
// without its ceiling, zones are not there (docs/physics-manifest.md, «Результаты»).
#include "physics/PhysicsWorld.h"
#include "KpFacade.h"

namespace ke {

namespace {

kpf::Vec fv(const Vector3& v) { return {double(v.x), double(v.y), double(v.z)}; }
Vector3 kev(const kpf::Vec& v) { return {Real(v.x), Real(v.y), Real(v.z)}; }
kpf::Turn fq(const Quaternion& q) { return {double(q.w), double(q.x), double(q.y), double(q.z)}; }
Quaternion keq(const kpf::Turn& q) { return {Real(q.w), Real(q.x), Real(q.y), Real(q.z)}; }

kpf::Shape shapeOf(const ShapeDesc& s) {
    kpf::Shape out;
    out.kind = s.kind == ShapeDesc::Kind::Box ? kpf::Shape::Kind::Box : s.kind == ShapeDesc::Kind::Sphere ? kpf::Shape::Kind::Sphere
             : s.kind == ShapeDesc::Kind::Capsule ? kpf::Shape::Kind::Capsule : s.kind == ShapeDesc::Kind::Hull ? kpf::Shape::Kind::Hull
             : kpf::Shape::Kind::Mesh;
    out.half = fv(s.halfExtents), out.radius = double(s.radius), out.halfHeight = double(s.halfHeight);
    for (const Vector3& p : s.points) out.points.push_back(fv(p));
    out.triangles = s.triangles;
    out.position = fv(s.position), out.rotation = fq(s.rotation);
    out.friction = double(s.friction), out.restitution = double(s.restitution);
    return out;
}

class KineticaWorld final : public PhysicsWorld {
public:
    KineticaWorld(const PhysicsSettings& s, const Vector3& gravity)
        : world_({s.substeps, double(s.contactHertz), double(s.contactDamping), s.sleeping}, fv(gravity)) {}

    std::string engine() const override { return kpf::World::engine(); }

    int addBody(const BodyDesc& d, const std::vector<ShapeDesc>& shapes, std::string& error) override {
        kpf::Body b;
        b.kind = d.type == BodyDesc::Type::Static ? kpf::Body::Kind::Static : d.type == BodyDesc::Type::Kinematic ? kpf::Body::Kind::Kinematic
                                                                                                                : kpf::Body::Kind::Dynamic;
        b.position = fv(d.position), b.rotation = fq(d.rotation.normalized());
        b.velocity = fv(d.velocity), b.spin = fv(d.angularVelocity);
        b.density = double(d.density);
        b.massGiven = d.massGiven, b.centreGiven = d.centreGiven, b.inertiaGiven = d.inertiaGiven;
        b.mass = double(d.mass), b.centre = fv(d.centre);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) b.inertia[size_t(r)][size_t(c)] = double(d.inertia.m[r][c]);
        b.linearDamping = double(d.linearDamping), b.angularDamping = double(d.angularDamping), b.gravityScale = double(d.gravityScale);
        b.group = d.group, b.marks = d.marks, b.ignores = d.ignores;
        std::vector<kpf::Shape> solid;
        for (const ShapeDesc& s : shapes)
            if (!s.trigger) solid.push_back(shapeOf(s)); // a zone is not yet: it neither pushes nor tells (triggered)
        return world_.addBody(b, solid, error);
    }
    void removeBody(int body) override { world_.removeBody(body); }
    bool alive(int body) const override { return world_.alive(body); }

    BodyState state(int body) const override {
        const kpf::State s = world_.state(body);
        BodyState out;
        out.position = kev(s.position), out.rotation = keq(s.rotation), out.centre = kev(s.centre);
        out.velocity = kev(s.velocity), out.angularVelocity = kev(s.spin);
        out.mass = Real(s.mass);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) out.inertia.m[r][c] = Real(s.inertia[size_t(r)][size_t(c)]);
        out.awake = s.awake;
        return out;
    }
    void setVelocity(int body, const Vector3& v, const Vector3& w) override { world_.setVelocity(body, fv(v), fv(w)); }
    void setPose(int body, const Vector3& p, const Quaternion& q) override { world_.setPose(body, fv(p), fq(q.normalized())); }
    void wake(int body) override { world_.wake(body); }
    void applyForce(int body, const Vector3& f, const Vector3& p) override { world_.applyForce(body, fv(f), fv(p)); }
    void applyTorque(int body, const Vector3& t) override { world_.applyTorque(body, fv(t)); }
    void applyImpulse(int body, const Vector3& j, const Vector3& p) override { world_.applyImpulse(body, fv(j), fv(p)); }
    void applyAngularImpulse(int body, const Vector3& j) override { world_.applyAngularImpulse(body, fv(j)); }
    void moveKinematic(int body, const Vector3& p, const Quaternion& q, Real dt) override { world_.moveKinematic(body, fv(p), fq(q.normalized()), double(dt)); }

    // The two bodies of a joint do not collide in the engine: `collide` is not kept.
    int addJoint(const JointDesc& d, std::string& error) override {
        kpf::Joint j;
        switch (d.type) {
        case JointType::Hinge: j.kind = kpf::Joint::Kind::Hinge; break;
        case JointType::Slider: j.kind = kpf::Joint::Kind::Slider; break;
        case JointType::Ball: j.kind = kpf::Joint::Kind::Ball; break;
        case JointType::Fixed: j.kind = kpf::Joint::Kind::Fixed; break;
        default: return error = "kinetica-physics has no joint with this mix of free turns and travels", -1;
        }
        j.a = d.a, j.b = d.b;
        j.anchorA = fv(d.anchorA), j.anchorB = fv(d.anchorB), j.frameA = fq(d.frameA.normalized()), j.frameB = fq(d.frameB.normalized());
        j.limited = d.limited, j.lower = double(d.lower), j.upper = double(d.upper);
        j.motor = d.motor, j.speed = double(d.motorSpeed), j.maxForce = double(d.maxMotorForce);
        return world_.addJoint(j, error);
    }
    void removeJoint(int joint) override { world_.removeJoint(joint); }
    bool jointAlive(int joint) const override { return world_.jointAlive(joint); }
    void setMotor(int joint, bool on, Real speed, Real maxForce) override { world_.setMotor(joint, on, double(speed), double(maxForce)); }
    Real motorForce(int) const override { return 0; } // the engine does not tell it
    void reaction(int, Vector3& force, Vector3& torque) const override { force = torque = Vector3(0.0_r); } // nor this
    int addHand(const HandDesc& h, std::string& error) override {
        return world_.addHand({h.body, fv(h.point), double(h.hertz), double(h.dampingRatio)}, error);
    }
    void moveHand(int joint, const Vector3& target, Real dt) override { world_.moveHand(joint, fv(target), double(dt)); }

    bool castRay(const Vector3& from, const Vector3& to, BodyHit& hit, int ignore) const override {
        kpf::Hit h;
        if (!world_.castRay(fv(from), fv(to), h, ignore)) return false;
        hit.body = h.body, hit.distance = Real(h.distance), hit.point = kev(h.point), hit.normal = kev(h.normal);
        return true;
    }
    std::vector<ContactPoint> contacts() const override { return points(-1); }
    std::vector<ContactPoint> contactsOf(int body) const override { return body >= 0 ? points(body) : std::vector<ContactPoint>{}; }
    std::vector<std::pair<int, int>> triggered() const override { return {}; }

    void setGravity(const Vector3& g) override { world_.setGravity(fv(g)); }
    void step(Real dt, int substeps) override { world_.step(double(dt), substeps); }

private:
    std::vector<ContactPoint> points(int body) const {
        std::vector<ContactPoint> out;
        for (const kpf::Contact& c : world_.contacts(body))
            out.push_back({c.a, c.b, kev(c.point), kev(c.normal), Real(c.depth), Real(c.impulse)});
        return out;
    }
    kpf::World world_;
};

} // namespace

std::unique_ptr<PhysicsWorld> makeKineticaWorld(const PhysicsSettings& settings, const Vector3& gravity, int) {
    return std::make_unique<KineticaWorld>(settings, gravity);
}

} // namespace ke
