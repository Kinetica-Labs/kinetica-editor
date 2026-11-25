// The facade over kinetica-physics (KpFacade.h): the only file of Kinetica that includes the engine. A body of ours is
// one engine body: its shapes become pieces placed in our body's frame and are joined into a compound, whose own frame -
// the centre of mass and the principal axes - is kept as `frame`, the engine body's pose in ours. A pose goes in and out
// through it. Given mass properties re-place the compound's parts about the frame they make. A joint's engine body a is
// our b and its b our a: the engine counts its hinge's angle and its drive's speed as its b relative to its a, our
// coordinate is frame a relative to frame b. The world in a joint is a still body of no shape at the origin, as is a
// mouse's hand's target. Steps are the engine's constant-cost step (World::realtime, its soft step built as Box3D's).
#include "KpFacade.h"

#include <physics.h>
#include <body/CompoundBodyBuilder.h>
#include <body/PrincipalAxes.h>
#include <collision/PairFinder.h>

#include <cmath>

namespace kpf {

namespace {

kp::Vector3 kv(const Vec& v) { return {v.x, v.y, v.z}; }
Vec fv(const kp::Vector3& v) { return {v.x, v.y, v.z}; }
kp::Quaternion kq(const Turn& q) { return kp::Quaternion{q.w, q.x, q.y, q.z}.unit(); }
Turn fq(const kp::Quaternion& q) { return {q.w, q.x, q.y, q.z}; }
kp::Vector3 times(double k, const kp::Vector3& v) { return {k * v.x, k * v.y, k * v.z}; }

// The turn that takes `from` to `to`, as a rotation vector (axis times angle): what a kinematic body spins with.
kp::Vector3 turnBetween(const kp::Quaternion& from, const kp::Quaternion& to) {
    kp::Quaternion d = to * from.conjugate();
    if (d.w < 0) d = {-d.w, -d.x, -d.y, -d.z}; // the shorter way round
    const kp::Vector3 axis{d.x, d.y, d.z};
    const double s = axis.length();
    return s < 1e-12 ? kp::Vector3{} : times(2 * std::atan2(s, d.w) / s, axis);
}

// One shape as an engine body standing where it stands in our body's frame: the pieces a compound is made of.
bool piece(const Shape& s, double density, kp::Body& out, std::string& error) {
    switch (s.kind) {
    case Shape::Kind::Box: out = kp::Body::box(kv(s.half), density); break;
    case Shape::Kind::Sphere: out = kp::Body::sphere(s.radius, density); break;
    case Shape::Kind::Capsule: out = kp::Body::capsule(s.halfHeight, s.radius, density); break;
    case Shape::Kind::Hull: {
        std::vector<kp::Vector3> points;
        for (const Vec& p : s.points) points.push_back(kv(p));
        if (points.size() < 4) return error = "the points make no solid hull", false;
        out = kp::Body::hull(points, density); // its pose: the hull's centre of mass and principal axes in the points' axes
        break;
    }
    case Shape::Kind::Mesh: { // a static floor of triangles; it comes placed already
        if (density != 0) return error = "a mesh collider is a static body's", false;
        kp::TriangleMesh mesh;
        const kp::Transform at{kv(s.position), kq(s.rotation)};
        for (const Vec& p : s.points) mesh.vertices.push_back(at.point(kv(p)));
        for (const auto& t : s.triangles) mesh.triangles.push_back({int(t[0]), int(t[1]), int(t[2])});
        if (mesh.vertices.size() < 3 || mesh.triangles.empty()) return error = "the mesh has no triangles", false;
        out = kp::CompoundBodyBuilder::ground(mesh);
        out.friction = s.friction, out.restitution = s.restitution;
        return true;
    }
    }
    const kp::Quaternion turn = kq(s.rotation);
    out.position = kv(s.position) + turn.rotate(out.position);
    out.orientation = turn * out.orientation;
    out.friction = s.friction, out.restitution = s.restitution;
    return true;
}

// A compound's parts placed about another frame of the same body (a given centre of mass or principal axes): each part
// where it stood, the bounding sphere about the new centre. The parts' tree, an index for many parts, is let go.
void reframe(kp::Body& body, const kp::Transform& to) {
    const kp::Transform from{body.position, body.orientation};
    auto parts = std::make_shared<std::vector<kp::Body::Part>>();
    double radius = 0;
    for (const kp::Body::Part& p : *body.parts) {
        const kp::Transform placed{from.point(p.placement.position), from.orientation * p.placement.orientation};
        parts->push_back({p.shape, to.relative(placed)});
        radius = std::max(radius, parts->back().placement.position.length() + p.shape->boundingRadius());
    }
    body.parts = std::move(parts);
    body.partTree = nullptr;
    body.shape = std::make_shared<kp::Sphere>(radius);
    body.position = to.position, body.orientation = to.orientation;
}

} // namespace

struct World::Inside {
    kp::World world;
    kp::BodyId ground{};       // the world in a joint: a still body of no shape at the origin
    std::vector<int> owner;    // ours by the engine's index; -1: the ground, a hand's target
    struct Entry {
        kp::BodyId id;
        kp::Transform frame; // the engine body's pose in our body's frame
        Body::Kind kind;
        double gravityScale = 1;
        bool alive = true;
        // A body of no shape goes as it is led by the facade, not by the engine: the engine reads the shape of every body
        // it moves (World::quiet) and has none to read. Its velocity and spin, world.
        kp::Vector3 led, ledSpin;
    };
    std::vector<Entry> bodies; // ours, by our index
    struct Link {
        size_t index;              // in the engine's joints
        int a, b;                  // ours, -1 the world
        bool alive = true;
        kp::BodyId target{-1};     // a hand's: the still body it pulls towards
    };
    std::vector<Link> joints;
    int substeps = 4;
    double lastStep = 0; // the last step's length: a contact's impulse is its mean force over it

    // Our frame's pose from the engine body's, and back.
    kp::Transform ours(const Entry& e) const {
        const kp::Body& b = world.body(e.id);
        const kp::Quaternion turn = b.orientation * e.frame.orientation.conjugate();
        return {b.position - turn.rotate(e.frame.position), turn};
    }
    static kp::Transform engines(const Entry& e, const kp::Vector3& position, const kp::Quaternion& turn) {
        return {position + turn.rotate(e.frame.position), turn * e.frame.orientation};
    }
    kp::BodyId engineBody(int ours) const { return ours >= 0 ? bodies[size_t(ours)].id : ground; }
    // A pose in our body's frame (the world's for -1) in the engine body's axes.
    kp::Transform inEngine(int ours, const kp::Transform& t) const { return ours >= 0 ? bodies[size_t(ours)].frame.relative(t) : t; }
    int ours(int engine) const { return engine >= 0 && size_t(engine) < owner.size() ? owner[size_t(engine)] : -1; }
    // A still body of no shape: the ground, a hand's target. The engine's index has no body of ours.
    kp::BodyId still(const kp::Vector3& at) {
        kp::Body b;
        b.position = at;
        const kp::BodyId id = world.addBody(b);
        owner.resize(size_t(id) + 1, -1);
        return id;
    }
    // A body of no mass led at a velocity and spin: by the engine when it has a shape, else by the facade's step.
    void lead(int i, const kp::Vector3& velocity, const kp::Vector3& spin) {
        Entry& e = bodies[size_t(i)];
        kp::Body& b = world.body(e.id);
        if (b.shape) b.drivenVelocity = velocity, b.drivenSpin = spin;
        else e.led = velocity, e.ledSpin = spin;
    }
    bool moving(int i) const { return i >= 0 && size_t(i) < bodies.size() && bodies[size_t(i)].alive && bodies[size_t(i)].kind == Body::Kind::Dynamic; }
    // A sleeping body is massless while it sleeps: woken first, it takes what is put on it.
    kp::Body& woken(int i) {
        world.wake(bodies[size_t(i)].id);
        return world.body(bodies[size_t(i)].id);
    }
};

World::World(const Settings& s, const Vec& gravity) : in_(std::make_unique<Inside>()) {
    in_->world.gravity = kv(gravity);
    in_->world.realtime = true;
    in_->world.soft.contactHertz = s.contactHertz;
    in_->world.soft.contactDamping = s.contactDamping;
    in_->world.sleeping = s.sleeping;
    in_->substeps = s.substeps;
    in_->ground = in_->still({});
}
World::~World() = default;

std::string World::engine() { return "kinetica-physics"; }

int World::addBody(const Body& d, const std::vector<Shape>& shapes, std::string& error) {
    const bool dynamic = d.kind == Body::Kind::Dynamic;
    const double density = dynamic ? d.density : 0; // a body of no mass stands, or goes as it is led
    std::vector<kp::Body> pieces;
    for (const Shape& s : shapes) {
        kp::Body p;
        if (!piece(s, s.kind == Shape::Kind::Mesh ? 0 : density, p, error)) return -1;
        pieces.push_back(std::move(p));
    }
    kp::Body body = kp::CompoundBodyBuilder::compound(pieces);
    if (dynamic && (d.massGiven || d.centreGiven || d.inertiaGiven)) {
        if (d.massGiven && !(d.mass > 0)) return error = "the mass is not positive", -1;
        kp::MassProperties& m = body.mass;
        if (d.massGiven) { // the same shapes, heavier or lighter: their inertia scales with the mass unless it is given
            const double was = m.inverseMass > 0 ? 1 / m.inverseMass : d.mass;
            if (!d.inertiaGiven) m.inverseInertia = times(was / d.mass, m.inverseInertia);
            m.inverseMass = 1 / d.mass;
        }
        kp::Transform frame{body.position, body.orientation};
        if (d.centreGiven) frame.position = kv(d.centre);
        if (d.inertiaGiven) {
            double tensor[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) tensor[r][c] = d.inertia[size_t(r)][size_t(c)];
            const kp::PrincipalAxes axes = kp::PrincipalAxes::of(tensor);
            if (!(axes.moments.x > 0 && axes.moments.y > 0 && axes.moments.z > 0)) return error = "the inertia is not positive", -1;
            frame.orientation = axes.orientation;
            m.inverseInertia = {1 / axes.moments.x, 1 / axes.moments.y, 1 / axes.moments.z};
        }
        if (body.parts) reframe(body, frame);
        else body.position = frame.position, body.orientation = frame.orientation;
    }
    Inside::Entry e{kp::BodyId(0), {body.position, body.orientation}, d.kind, d.gravityScale, true};
    const kp::Transform at = Inside::engines(e, kv(d.position), kq(d.rotation));
    body.position = at.position, body.orientation = at.orientation;
    body.linearDamping = d.linearDamping, body.angularDamping = d.angularDamping;
    body.group = d.group, body.categoryBits = d.marks, body.ignoreBits = d.ignores; // the engine's filter, its words
    e.id = in_->world.addBody(body);
    in_->bodies.push_back(e);
    const int index = int(in_->bodies.size()) - 1;
    in_->owner.resize(size_t(e.id) + 1, -1);
    in_->owner[size_t(e.id)] = index;
    setVelocity(index, d.velocity, d.spin);
    return index;
}

void World::removeBody(int i) {
    if (!alive(i)) return;
    Inside::Entry& e = in_->bodies[size_t(i)];
    kp::Body& b = in_->world.body(e.id);
    b = kp::Body{}; // shapeless and massless: it touches nothing and nothing moves it
    e.alive = false;
    for (size_t j = 0; j < in_->joints.size(); ++j) // its joints go with it
        if (in_->joints[j].alive && (in_->joints[j].a == i || in_->joints[j].b == i)) removeJoint(int(j));
}

namespace {
// What drives a hinge's or a slider's one freedom: its limits, and a velocity motor holding `speed` with at most
// `maxForce`.
void drive(kp::SingleFreedomJoint& f, const Joint& j) {
    if (j.limited) f.lower = j.lower, f.upper = j.upper;
    f.holdSpeed = j.motor, f.driveSpeed = j.speed, f.driveStrength = j.maxForce;
}
} // namespace

int World::addJoint(const Joint& j, std::string& error) {
    if (!alive(j.a)) return error = "the joint's link a has no body", -1;
    if (j.b >= 0 && !alive(j.b)) return error = "the joint's link b has no body", -1;
    if (j.a == j.b) return error = "the joint holds a body to itself", -1;
    if (j.kind == Joint::Kind::Ball && j.limited) return error = "kinetica-physics has no cone limit for a ball joint", -1;
    kp::World& w = in_->world;
    const kp::BodyId A = in_->engineBody(j.b), B = in_->engineBody(j.a); // its a is our b, its b our a
    const kp::Transform onA = in_->inEngine(j.b, {kv(j.anchorB), kq(j.frameB)}), onB = in_->inEngine(j.a, {kv(j.anchorA), kq(j.frameA)});
    const kp::Body& b = w.body(B);
    const kp::Vector3 anchor = kp::frameOf(b).point(onB.position), axis = (b.orientation * onB.orientation).rotate({1, 0, 0});
    kp::Joint* made = nullptr;
    switch (j.kind) {
    case Joint::Kind::Hinge: made = &w.addJoint<kp::HingeJoint>(A, B, anchor, axis); break;
    case Joint::Kind::Slider: made = &w.addJoint<kp::SliderJoint>(A, B, anchor, axis); break;
    case Joint::Kind::Ball: made = &w.addJoint<kp::BallAndSocketJoint>(A, B, anchor); break;
    case Joint::Kind::Fixed: made = &w.addJoint<kp::FixedJoint>(A, B, anchor); break;
    }
    // Its frames as ours give them, not as the bodies stand now: two parts joined apart come together.
    made->anchorA = onA.position, made->frameA = onA.orientation, made->anchorB = onB.position, made->frameB = onB.orientation;
    if (auto* f = dynamic_cast<kp::SingleFreedomJoint*>(made)) drive(*f, j);
    w.wake(A), w.wake(B);
    in_->joints.push_back({w.joints().size() - 1, j.a, j.b});
    return int(in_->joints.size()) - 1;
}

// The engine keeps every joint: a removed one is set between the ground and itself - two still bodies, which a joint
// holds nothing of -, and its drive and spring are let go.
void World::removeJoint(int i) {
    if (!jointAlive(i)) return;
    Inside::Link& l = in_->joints[size_t(i)];
    kp::Joint& joint = in_->world.joint<kp::Joint>(l.index);
    const int a = joint.a, b = joint.b;
    if (auto* f = dynamic_cast<kp::SingleFreedomJoint*>(&joint)) f->holdSpeed = false, f->driveForce = 0;
    if (auto* t = dynamic_cast<kp::TetherJoint*>(&joint)) t->stiffness = t->damping = 0;
    joint.a = joint.b = int(in_->ground);
    in_->world.wake(kp::BodyId(a)), in_->world.wake(kp::BodyId(b));
    l.alive = false;
}
bool World::jointAlive(int i) const { return i >= 0 && size_t(i) < in_->joints.size() && in_->joints[size_t(i)].alive; }

void World::setMotor(int i, bool on, double speed, double maxForce) {
    if (!jointAlive(i)) return;
    kp::Joint& joint = in_->world.joint<kp::Joint>(in_->joints[size_t(i)].index);
    if (auto* f = dynamic_cast<kp::SingleFreedomJoint*>(&joint)) {
        f->holdSpeed = on, f->driveSpeed = speed, f->driveStrength = maxForce;
        in_->world.wake(kp::BodyId(joint.a)), in_->world.wake(kp::BodyId(joint.b));
    }
}

// The engine's tether: a spring with a damper between a still target and the held point, of the frequency and damping
// asked for the held body's mass. It has no ceiling on its pull and no brake on the body's turn.
int World::addHand(const Hand& h, std::string& error) {
    if (!in_->moving(h.body)) return error = "only a moving body can be held", -1;
    const kp::BodyId held = in_->bodies[size_t(h.body)].id;
    in_->world.wake(held);
    const double m = 1 / in_->world.body(held).mass.inverseMass, w = 2 * 3.141592653589793 * h.hertz;
    const kp::BodyId target = in_->still(kv(h.point));
    in_->world.addJoint<kp::TetherJoint>(target, held, kv(h.point), kv(h.point), m * w * w, 2 * h.dampingRatio * m * w);
    in_->joints.push_back({in_->world.joints().size() - 1, -1, h.body, true, target});
    return int(in_->joints.size()) - 1;
}
void World::moveHand(int i, const Vec& to, double dt) {
    if (!jointAlive(i) || int(in_->joints[size_t(i)].target) < 0 || !(dt > 0)) return;
    in_->world.body(in_->joints[size_t(i)].target).position = kv(to); // the target, of no shape, is set where it is led
}
bool World::alive(int i) const { return i >= 0 && size_t(i) < in_->bodies.size() && in_->bodies[size_t(i)].alive; }

State World::state(int i) const {
    State s;
    if (!alive(i)) return s;
    const Inside::Entry& e = in_->bodies[size_t(i)];
    const kp::Body& b = in_->world.body(e.id);
    const kp::Transform frame = in_->ours(e);
    s.position = fv(frame.position), s.rotation = fq(frame.orientation);
    s.centre = fv(b.position);
    s.awake = !in_->world.asleep(e.id);
    if (e.kind != Body::Kind::Dynamic) {
        s.velocity = fv(b.shape ? b.drivenVelocity : e.led), s.spin = fv(b.shape ? b.drivenSpin : e.ledSpin);
        return s;
    }
    const kp::MassProperties& m = in_->world.mass(e.id); // a sleeping body's, as it will be awake
    s.velocity = fv(times(m.inverseMass, b.linearMomentum));
    s.spin = fv(b.orientation.rotate(m.inverseInertia.scaled(b.orientation.rotateInverse(b.angularMomentum))));
    s.mass = m.inverseMass > 0 ? 1 / m.inverseMass : 0;
    const kp::Vector3 moments{m.inverseInertia.x > 0 ? 1 / m.inverseInertia.x : 0, m.inverseInertia.y > 0 ? 1 / m.inverseInertia.y : 0,
                              m.inverseInertia.z > 0 ? 1 / m.inverseInertia.z : 0};
    for (int c = 0; c < 3; ++c) { // R diag(I) R^T, a column a world axis
        const kp::Vector3 axis{c == 0 ? 1.0 : 0.0, c == 1 ? 1.0 : 0.0, c == 2 ? 1.0 : 0.0};
        const kp::Vector3 col = b.orientation.rotate(moments.scaled(b.orientation.rotateInverse(axis)));
        s.inertia[0][size_t(c)] = col.x, s.inertia[1][size_t(c)] = col.y, s.inertia[2][size_t(c)] = col.z;
    }
    return s;
}

void World::setVelocity(int i, const Vec& v, const Vec& w) {
    if (!alive(i)) return;
    const Inside::Entry& e = in_->bodies[size_t(i)];
    if (e.kind != Body::Kind::Dynamic) {
        in_->lead(i, kv(v), kv(w));
        return;
    }
    kp::Body& b = in_->woken(i);
    const kp::MassProperties& m = b.mass;
    const kp::Vector3 moments{m.inverseInertia.x > 0 ? 1 / m.inverseInertia.x : 0, m.inverseInertia.y > 0 ? 1 / m.inverseInertia.y : 0,
                              m.inverseInertia.z > 0 ? 1 / m.inverseInertia.z : 0};
    b.linearMomentum = times(m.inverseMass > 0 ? 1 / m.inverseMass : 0, kv(v));
    b.angularMomentum = b.orientation.rotate(moments.scaled(b.orientation.rotateInverse(kv(w))));
}

void World::setPose(int i, const Vec& p, const Turn& q) {
    if (!alive(i)) return;
    const Inside::Entry& e = in_->bodies[size_t(i)];
    if (e.kind == Body::Kind::Dynamic) in_->world.wake(e.id);
    const kp::Transform at = Inside::engines(e, kv(p), kq(q));
    kp::Body& b = in_->world.body(e.id);
    b.position = at.position, b.orientation = at.orientation;
}

void World::wake(int i) {
    if (alive(i)) in_->world.wake(in_->bodies[size_t(i)].id);
}
void World::applyForce(int i, const Vec& f, const Vec& p) {
    if (in_->moving(i)) kp::applyForce(in_->woken(i), kv(f), kv(p));
}
void World::applyTorque(int i, const Vec& t) {
    if (in_->moving(i)) kp::applyTorque(in_->woken(i), kv(t));
}
void World::applyImpulse(int i, const Vec& j, const Vec& p) {
    if (in_->moving(i)) kp::applyImpulse(in_->woken(i), kv(j), kv(p));
}
void World::applyAngularImpulse(int i, const Vec& j) {
    if (!in_->moving(i)) return;
    kp::Body& b = in_->woken(i);
    b.angularMomentum = b.angularMomentum + kv(j);
}

void World::moveKinematic(int i, const Vec& p, const Turn& q, double dt) {
    if (!alive(i) || !(dt > 0) || in_->bodies[size_t(i)].kind != Body::Kind::Kinematic) return;
    const Inside::Entry& e = in_->bodies[size_t(i)];
    const kp::Transform to = Inside::engines(e, kv(p), kq(q));
    const kp::Body& b = in_->world.body(e.id);
    in_->lead(i, times(1 / dt, to.position - b.position), times(1 / dt, turnBetween(b.orientation, to.orientation)));
}

bool World::castRay(const Vec& from, const Vec& to, Hit& hit, int ignore) const {
    const kp::Vector3 start = kv(from), way = kv(to) - kv(from);
    const double reach = way.length();
    if (!(reach > 0)) return false;
    const kp::Body* skipped = alive(ignore) ? &in_->world.body(in_->bodies[size_t(ignore)].id) : nullptr;
    const kp::RayHit h = kp::castRay(in_->world.bodies(), kp::Ray{start, times(1 / reach, way), reach}, skipped);
    if (!h || in_->ours(h.body) < 0) return false;
    hit.body = in_->ours(h.body), hit.distance = h.distance, hit.point = fv(h.point), hit.normal = fv(h.normal);
    return true;
}

// The touching points now: the engine's pairs found afresh (its world keeps its own to itself), each point's normal force
// from the last step's memory of forces, times that step's length.
std::vector<Contact> World::contacts(int body) const {
    std::vector<Contact> out;
    const kp::World& w = in_->world;
    const kp::ContactForces& forces = kp::StepObserver::cachedContactForces(w);
    for (const kp::ContactPair& pair : kp::PairFinder(w.bodies(), w.gravity).pairs(0, w.joints())) {
        const int a = in_->ours(pair.a), b = in_->ours(pair.b);
        if (!pair.inContact() || a < 0 || b < 0 || (body >= 0 && a != body && b != body)) continue;
        for (int k = 0; k < pair.manifold.count; ++k) {
            const kp::ContactPoint& p = pair.manifold.points[size_t(k)];
            if (p.separation > kp::kContactDistance) continue; // a point that will touch next, not one that does
            const kp::ContactForces::Entry* f = forces.find({pair.a, pair.b, p.id, pair.partA, pair.partB});
            const double normal = f != forces.end() ? f->second.normal : 0;
            out.push_back({a, b, fv(times(0.5, p.onA + p.onB)), fv(pair.manifold.normal), -p.separation, normal * in_->lastStep});
        }
    }
    return out;
}

void World::setGravity(const Vec& g) { in_->world.gravity = kv(g); }

void World::step(double dt, int substeps) {
    // The engine has one gravity for all: a body that takes more or less of it gets the difference as a force.
    for (size_t i = 0; i < in_->bodies.size(); ++i)
        if (in_->moving(int(i)) && in_->bodies[i].gravityScale != 1 && !in_->world.asleep(in_->bodies[i].id)) {
            kp::Body& b = in_->world.body(in_->bodies[i].id);
            kp::applyForce(b, times((in_->bodies[i].gravityScale - 1) / b.mass.inverseMass, in_->world.gravity), b.position);
        }
    for (Inside::Entry& e : in_->bodies) // the bodies of no shape the facade leads
        if (e.alive && e.kind == Body::Kind::Kinematic && !in_->world.body(e.id).shape) {
            kp::Body& b = in_->world.body(e.id);
            b.position = b.position + times(dt, e.led);
            b.orientation = (kp::Quaternion::fromRotationVector(times(dt, e.ledSpin)) * b.orientation).unit();
        }
    in_->world.soft.substeps = substeps > 0 ? substeps : in_->substeps;
    in_->world.step(dt);
    in_->lastStep = dt;
}

} // namespace kpf
