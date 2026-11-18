// The physics boundary (src/physics) on Box3D against exact answers. A falling ball's speed is g t to
// the last bits of float (semi-implicit Euler updates the speed first), its height within the
// integrator's first-order error. A torque about a principal axis spins a box at tau t / I - the
// inertia Box3D computes from the shape's volume, and one given by hand, which the state reports in the
// world's axes. A pendulum's period is the
// compound pendulum's 2 pi sqrt(I / (m g L)). The frames' conventions: a hinge's limit stops the swing
// on the side our coordinate says - softly, overshooting less with stiffer joints - and its motor
// turns it the way ours counts. A ray meets a box's top at
// its distance, past a ball it is told to ignore. A box on the floor is carried by contacts summing to
// m g; a hanging bob's joint pulls up with m g. A cantilever of welded boxes sags, less with stiffer
// joints and more substeps - the numbers go to docs/box3d-backend.md. Two worlds built alike stay
// equal to the bit.
#include "TestRunner.h"

#include "physics/PhysicsWorld.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace ke;

namespace {

const Real kDt = 1.0_r / 60;
const Vector3 kGravity(0, -9.81_r, 0);

// A world at the scene's default settings (PhysicsSettings), or at given substeps and joint stiffness.
std::unique_ptr<PhysicsWorld> world(const Vector3& gravity = kGravity, int substeps = 0, Real jointHertz = 0) {
    PhysicsSettings s;
    s.sleeping = false;
    if (substeps) s.substeps = substeps, s.contactHertz = std::min(s.contactHertz, Real(15 * substeps));
    if (jointHertz > 0) s.jointHertz = jointHertz;
    return makePhysicsWorld(s, gravity);
}

ShapeDesc sphere(Real r) {
    ShapeDesc s;
    s.kind = ShapeDesc::Kind::Sphere;
    s.radius = r;
    return s;
}

ShapeDesc box(const Vector3& half) {
    ShapeDesc s;
    s.kind = ShapeDesc::Kind::Box;
    s.halfExtents = half;
    return s;
}

int body(PhysicsWorld& w, const Vector3& at, const std::vector<ShapeDesc>& shapes, BodyDesc::Type type = BodyDesc::Type::Dynamic,
         Real density = 1000, const Quaternion& turn = Quaternion()) {
    BodyDesc d;
    d.type = type, d.position = at, d.rotation = turn, d.density = density;
    std::string error;
    const int b = w.addBody(d, shapes, error);
    if (b < 0) std::printf("  addBody: %s\n", error.c_str());
    return b;
}

// Our hinge turns about its frames' x: frames that put x along the world's z swing in the xy plane.
const Quaternion kXonZ = Quaternion::fromTwoVectors({1, 0, 0}, {0, 0, 1});

// A bob of radius r on a massless arm of length L from a pivot at `pivot`, turned theta about z.
struct Pendulum {
    int bob = -1, joint = -1;
    Real mass = 0, inertia = 0; // about the pivot
};
Pendulum pendulum(PhysicsWorld& w, const Vector3& pivot, Real L, Real r, Real theta, bool limited = false, Real lower = 0, Real upper = 0) {
    Pendulum p;
    const Quaternion turn = Quaternion::fromAxisAngle({0, 0, 1}, theta);
    p.bob = body(w, pivot + turn.rotate(Vector3(0, -L, 0)), {sphere(r)}, BodyDesc::Type::Dynamic, 8000, turn);
    p.mass = w.state(p.bob).mass;
    p.inertia = 0.4_r * p.mass * r * r + p.mass * L * L;
    JointDesc j;
    j.type = JointType::Hinge, j.a = p.bob, j.b = -1;
    j.anchorA = Vector3(0, L, 0), j.anchorB = pivot;
    j.frameA = j.frameB = kXonZ;
    j.limited = limited, j.lower = lower, j.upper = upper;
    std::string error;
    p.joint = w.addJoint(j, error);
    if (p.joint < 0) std::printf("  addJoint: %s\n", error.c_str());
    return p;
}

// Our coordinate of the pendulum's hinge: frame a relative to frame b, the turn about x.
Real hingeAngle(const PhysicsWorld& w, const Pendulum& p, const Vector3& pivot, Real L) {
    const BodyState s = w.state(p.bob);
    Real c[6];
    frameCoordinates(s.position + s.rotation.rotate(Vector3(0, L, 0)), s.rotation * kXonZ, pivot, kXonZ, c);
    return c[DofRx];
}

} // namespace

void testPhysicsFreeFall() {
    auto w = world();
    const int ball = body(*w, {0, 10, 0}, {sphere(0.1_r)});
    for (int k = 0; k < 60; ++k) w->step(kDt);
    const BodyState s = w->state(ball);
    const Real t = 1, exact = 10 - 0.5_r * 9.81_r * t * t, h = kDt / Real(PhysicsSettings{}.substeps);
    std::printf("  after 1 s: v = %.6f m/s (-g t = -9.81), y = %.4f m (exact %.4f, first-order error bound g h t = %.4f)\n",
                double(s.velocity.y), double(s.position.y), double(exact), double(9.81_r * h * t));
    CHECK(std::fabs(s.velocity.y + 9.81_r) < 1e-4_r, "the speed after 1 s is %f, not -9.81", double(s.velocity.y));
    CHECK(std::fabs(s.position.y - exact) <= 9.81_r * h * t, "the height %f is off the parabola %f by more than g h t", double(s.position.y), double(exact));
}

void testPhysicsTorque() {
    auto w = world(Vector3(0.0_r));
    const int computed = body(*w, {0, 0, 0}, {box({0.1_r, 0.2_r, 0.3_r})});
    BodyDesc d;
    d.position = {2, 0, 0};
    d.massGiven = true, d.mass = 2;
    d.inertia.m[0][0] = 0.5_r, d.inertia.m[1][1] = 0.7_r, d.inertia.m[2][2] = 0.9_r;
    std::string error;
    const int given = w->addBody(d, {box({0.1_r, 0.1_r, 0.1_r})}, error);
    for (int k = 0; k < 60; ++k) {
        w->applyTorque(computed, {1, 0, 0});
        w->applyTorque(given, {0, 0, 1});
        w->step(kDt);
    }
    const Real m = w->state(computed).mass, Ixx = m / 12 * (0.4_r * 0.4_r + 0.6_r * 0.6_r);
    const Real spun = w->state(computed).angularVelocity.x, spunGiven = w->state(given).angularVelocity.z;
    std::printf("  box 0.2 x 0.4 x 0.6 m of 1000 kg/m^3: m = %.4f kg (48), after 1 s at 1 N m: %.6f rad/s (1 / Ixx = %.6f); "
                "given I_zz = 0.9: %.6f rad/s (%.6f)\n", double(m), double(spun), double(1 / Ixx), double(spunGiven), 1 / 0.9);
    CHECK(std::fabs(m - 48) < 1e-3_r, "the box's mass %f is not its volume times its density, 48 kg", double(m));
    CHECK(std::fabs(spun * Ixx - 1) < 1e-3_r, "the box spins at %f, not tau t / I = %f", double(spun), double(1 / Ixx));
    CHECK(std::fabs(spunGiven * 0.9_r - 1) < 1e-3_r && given >= 0, "the given inertia spins at %f, not 1 / 0.9", double(spunGiven));
    // The state's inertia is about the centre in the world's axes: a spin about x keeps I_xx, about z I_zz.
    const Matrix3x3 I = w->state(computed).inertia, Ig = w->state(given).inertia;
    CHECK(std::fabs(I.m[0][0] - Ixx) < 1e-3_r * Ixx && std::fabs(Ig.m[2][2] - 0.9_r) < 1e-4_r,
          "the state's world inertia I_xx = %f (%f), given I_zz = %f (0.9)", double(I.m[0][0]), double(Ixx), double(Ig.m[2][2]));
}

void testPhysicsPendulum() {
    auto w = world();
    const Vector3 pivot(0, 2, 0);
    const Real L = 1, r = 0.05_r;
    const Pendulum p = pendulum(*w, pivot, L, r, 0.05_r);
    const Real expected = 2 * kPi * std::sqrt(p.inertia / (p.mass * 9.81_r * L));
    // Upward zero crossings of the angle, interpolated within a step: their spacing is the period.
    std::vector<Real> crossings;
    Real last = hingeAngle(*w, p, pivot, L);
    for (int k = 1; k <= 60 * 8; ++k) {
        w->step(kDt);
        const Real now = hingeAngle(*w, p, pivot, L);
        if (last < 0 && now >= 0) crossings.push_back((Real(k - 1) + last / (last - now)) * kDt);
        last = now;
    }
    const Real period = crossings.size() >= 2 ? (crossings.back() - crossings.front()) / Real(crossings.size() - 1) : 0;
    std::printf("  bob of %.3f kg at L = 1 m, 0.05 rad: period %.5f s over %zu swings (compound pendulum %.5f s, %.3f %%)\n",
                double(p.mass), double(period), crossings.size() - 1, double(expected), double(100 * (period / expected - 1)));
    CHECK(p.joint >= 0 && std::fabs(period / expected - 1) < 0.005_r, "the period %f is not 2 pi sqrt(I / m g L) = %f", double(period), double(expected));
}

// The least angle of a 4 kg bob on a 1 m arm released at 1 rad against a lower limit of -0.3 rad.
Real limitedSwing(int substeps, Real hertz) {
    auto w = world(kGravity, substeps, hertz);
    const Vector3 pivot(0, 2, 0);
    const Pendulum p = pendulum(*w, pivot, 1, 0.05_r, 1.0_r, true, -0.3_r, 1.2_r);
    Real least = 1;
    for (int k = 0; k < 120; ++k) {
        w->step(kDt);
        least = std::min(least, hingeAngle(*w, p, pivot, 1));
    }
    return least;
}

void testPhysicsHingeConventions() {
    const Real least = limitedSwing(4, 60), firm = limitedSwing(16, 240);
    auto free = world(Vector3(0.0_r));
    const int disc = body(*free, {0, 0, 0}, {sphere(0.2_r)});
    JointDesc j;
    j.type = JointType::Hinge, j.a = disc, j.b = -1;
    j.frameA = j.frameB = kXonZ;
    j.motor = true, j.motorSpeed = 2, j.maxMotorForce = 10;
    std::string error;
    const int motor = free->addJoint(j, error);
    for (int k = 0; k < 60; ++k) free->step(kDt);
    const Real spin = free->state(disc).angularVelocity.z;
    std::printf("  released at 1 rad against a limit at -0.3 rad (free it would reach -1): least angle %.4f rad (4 substeps, 60 Hz), "
                "%.4f rad (16, 240 Hz); a motor at +2 rad/s about our x (world z): %.4f rad/s, its torque %.4f N m\n", double(least),
                double(firm), double(spin), double(free->motorForce(motor)));
    CHECK(least < -0.29_r && least > -0.6_r && firm > least, "the swing stopped at %f (%f stiffer): not at the lower limit -0.3 of our "
          "coordinate, or a stiffer joint overshot more", double(least), double(firm));
    CHECK(motor >= 0 && std::fabs(spin - 2) < 1e-3_r, "the motor turns the disc at %f about z, not +2 (the sign of our coordinate)", double(spin));
}

void testPhysicsRays() {
    auto w = world();
    body(*w, {0, 0, 0}, {box({0.5_r, 0.5_r, 0.5_r})}, BodyDesc::Type::Static);
    const int ball = body(*w, {0, 2, 0}, {sphere(0.25_r)});
    BodyHit first, past;
    const bool hit = w->castRay({0, 3, 0}, {0, -3, 0}, first);
    const bool hitPast = w->castRay({0, 3, 0}, {0, -3, 0}, past, ball);
    std::printf("  a ray down from y = 3: the ball at %.5f m (0.75), ignoring it the box's top at %.5f m (2.5), normal (%.2f, %.2f, %.2f)\n",
                double(first.distance), double(past.distance), double(past.normal.x), double(past.normal.y), double(past.normal.z));
    CHECK(hit && first.body == ball && std::fabs(first.distance - 0.75_r) < 1e-4_r, "the ray did not meet the ball at 0.75 m: %f", double(first.distance));
    CHECK(hitPast && past.body != ball && std::fabs(past.distance - 2.5_r) < 1e-4_r && past.normal.y > 0.999_r,
          "past the ball the ray did not meet the box's top at 2.5 m: %f", double(past.distance));
    BodyHit none;
    CHECK(!w->castRay({5, 3, 0}, {5, -3, 0}, none), "a ray past everything met something");
}

void testPhysicsWeightOnContacts() {
    auto w = world();
    body(*w, {0, -0.5_r, 0}, {box({5, 0.5_r, 5})}, BodyDesc::Type::Static);
    const int crate = body(*w, {0, 0.2_r, 0}, {box({0.2_r, 0.2_r, 0.2_r})});
    for (int k = 0; k < 120; ++k) w->step(kDt);
    Real impulse = 0;
    int points = 0;
    for (const ContactPoint& c : w->contacts())
        if (c.a == crate || c.b == crate) impulse += c.normalImpulse, ++points;
    const Real weight = w->state(crate).mass * 9.81_r;
    std::printf("  a crate of %.2f kg at rest: %d contact points carry %.4f N (m g = %.4f N)\n", double(w->state(crate).mass), points,
                double(impulse / kDt), double(weight));
    CHECK(points >= 3, "%d contact points under a box at rest", points);
    CHECK(std::fabs(impulse / kDt / weight - 1) < 0.01_r, "the contacts carry %f N, not m g = %f N", double(impulse / kDt), double(weight));
}

void testPhysicsJointReaction() {
    auto w = world();
    const Vector3 pivot(0, 2, 0);
    const Pendulum p = pendulum(*w, pivot, 1, 0.05_r, 0);
    for (int k = 0; k < 60; ++k) w->step(kDt);
    Vector3 force, torque;
    w->reaction(p.joint, force, torque);
    const Real weight = p.mass * 9.81_r;
    std::printf("  a hanging bob of %.3f kg: the joint pulls it with (%.4f, %.4f, %.4f) N (m g = %.4f N up), torque |%.2e| N m\n",
                double(p.mass), double(force.x), double(force.y), double(force.z), double(weight), double(length(torque)));
    CHECK(std::fabs(force.y / weight - 1) < 0.01_r && std::fabs(force.x) < 1e-3_r * weight, "the joint's force %f is not m g up", double(force.y));
}

// A plate spun at 7 rad/s on a frictionless hinge, nothing driving it: after 5 s it spins as fast.
void testPhysicsHingeCoasts() {
    auto w = world(Vector3(0.0_r));
    BodyDesc d;
    d.position = {0.2_r, 1, 0}, d.velocity = {0, 1.4_r, 0}, d.angularVelocity = {0, 0, 7};
    std::string error;
    const int plate = w->addBody(d, {box({0.2_r, 0.02_r, 0.2_r})}, error);
    JointDesc j;
    j.type = JointType::Hinge, j.a = plate, j.b = -1;
    j.anchorA = {-0.2_r, 0, 0}, j.anchorB = {0, 1, 0};
    j.frameA = j.frameB = kXonZ;
    w->addJoint(j, error);
    for (int k = 0; k < 300; ++k) w->step(kDt);
    const Real spin = w->state(plate).angularVelocity.z;
    std::printf("  a plate spun at 7 rad/s on a free hinge: %.4f rad/s after 5 s (%.2f %% of its energy lost)\n", double(spin),
                double(100 * (1 - spin * spin / 49)));
    CHECK(std::fabs(spin / 7 - 1) < 0.01_r, "the hinge took %.1f %% of the spin in 5 s", double(100 * (1 - spin / 7)));
}

// The tip of a cantilever of six welded 10 cm boxes of 1 kg, held by the world at its root, after 2 s.
Real cantileverSag(int substeps, Real hertz) {
    auto w = world(kGravity, substeps, hertz);
    int last = -1;
    for (int k = 0; k < 6; ++k) {
        const int b = body(*w, {0.05_r + 0.1_r * Real(k), 1, 0}, {box({0.05_r, 0.05_r, 0.05_r})});
        JointDesc j;
        j.type = JointType::Fixed, j.a = b, j.b = last;
        j.anchorA = Vector3(-0.05_r, 0, 0);
        j.anchorB = last >= 0 ? Vector3(0.05_r, 0, 0) : Vector3(0, 1, 0);
        std::string error;
        w->addJoint(j, error);
        last = b;
    }
    for (int k = 0; k < 120; ++k) w->step(kDt);
    return 1 - w->state(last).position.y;
}

void testPhysicsCantileverSag() {
    const Real soft = cantileverSag(4, 60), firm = cantileverSag(8, 120), stiff = cantileverSag(16, 240);
    std::printf("  six welded 10 cm boxes of 1 kg: the tip sags %.2f mm (4 substeps, joints at 60 Hz), %.2f mm (8, 120 Hz), %.2f mm (16, 240 Hz)\n",
                double(soft * 1000), double(firm * 1000), double(stiff * 1000));
    CHECK(stiff < firm && firm < soft && soft > 0, "the sag does not fall with stiffer joints: %f %f %f", double(soft), double(firm), double(stiff));
}

// Rest, as the standard scenes of rigid-body engines ask it: bodies put touching, in balance, must stay -
// none moves more than `most` from where it was put, none goes faster than 1 mm/s at the end.
struct Rest {
    Real drift = 0, sink = 0, speed = 0, ms = 0; // m (any way; down), m/s, per frame
};
Rest stayPut(PhysicsWorld& w, const std::vector<int>& bodies, int frames) {
    std::vector<Vector3> start;
    for (int b : bodies) start.push_back(w.state(b).position);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < frames; ++k) w.step(kDt);
    Rest r;
    r.ms = Real(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames);
    for (size_t i = 0; i < bodies.size(); ++i) {
        const BodyState s = w.state(bodies[i]);
        r.drift = std::max(r.drift, length(s.position - start[i]));
        r.sink = std::max(r.sink, start[i].y - s.position.y);
        r.speed = std::max(r.speed, length(s.velocity));
    }
    return r;
}

int floorOf(PhysicsWorld& w) { return body(w, {0, -0.5_r, 0}, {box({20, 0.5_r, 20})}, BodyDesc::Type::Static); }

void testPhysicsTower() {
    auto w = world();
    floorOf(*w);
    std::vector<int> cubes;
    for (int k = 0; k < 10; ++k) cubes.push_back(body(*w, {0, 0.1_r + 0.2_r * Real(k), 0}, {box(Vector3(0.1_r))}));
    const Rest r = stayPut(*w, cubes, 300);
    std::printf("  a tower of ten 0.2 m cubes after 5 s: drift %.3f mm, sink %.3f mm, speed %.3f mm/s, %.3f ms a frame\n",
                double(r.drift * 1000), double(r.sink * 1000), double(r.speed * 1000), double(r.ms));
    CHECK(r.drift < 0.001_r && r.speed < 0.001_r, "the tower moved %f mm, or still moves at %f mm/s", double(r.drift * 1000), double(r.speed * 1000));
}

// A pyramid of 406 cubes: 28 in its bottom row, one fewer in each row above, each row's cubes over the gaps
// of the row below.
void testPhysicsPyramid() {
    auto w = world();
    floorOf(*w);
    std::vector<int> cubes;
    const int base = 28;
    for (int row = 0; row < base; ++row)
        for (int k = 0; k < base - row; ++k)
            cubes.push_back(body(*w, {0.2_r * (Real(k) - 0.5_r * Real(base - row - 1)), 0.1_r + 0.2_r * Real(row), 0}, {box(Vector3(0.1_r))}));
    const Rest r = stayPut(*w, cubes, 180);
    std::printf("  a pyramid of %zu 0.2 m cubes after 3 s: drift %.3f mm, sink %.3f mm, speed %.3f mm/s, %.2f ms a frame\n", cubes.size(),
                double(r.drift * 1000), double(r.sink * 1000), double(r.speed * 1000), double(r.ms));
    CHECK(r.drift < 0.002_r && r.speed < 0.001_r, "the pyramid moved %f mm, or still moves at %f mm/s", double(r.drift * 1000), double(r.speed * 1000));
}

// The links of one group never touch: two boxes overlapping half way stay where they are (a robot's links
// overlap at its joints; RobotWorld gives each robot a group).
void testPhysicsGroups() {
    auto w = world(Vector3(0.0_r));
    BodyDesc d;
    d.group = 1;
    std::string error;
    const auto at = [&](Real x) { d.position = Vector3(x, 0.1_r, 0); return w->addBody(d, {box({0.1_r, 0.1_r, 0.1_r})}, error); };
    const int left = at(2), right = at(2.1_r);
    for (int k = 0; k < 60; ++k) w->step(kDt);
    const Real shift = length(w->state(left).position - Vector3(2, 0.1_r, 0)) + length(w->state(right).position - Vector3(2.1_r, 0.1_r, 0));
    std::printf("  two boxes of one group overlapping half way: moved %.2e m in 1 s\n", double(shift));
    CHECK(shift < 1e-6_r, "two bodies of one group pushed each other apart by %f m", double(shift));
}

// The mouse's hand, a soft spring solved with the contacts: a box lifted by its top 0.5 m off the floor, held 3 s
// and led 0.3 m aside, hangs under the target by its static sag g / w^2 (w = 2 pi hertz), the hand pulling it with
// m g; let go, it falls back. A hand that may pull half the weight leaves it on the floor.
void testPhysicsHand() {
    PhysicsSettings settings; // sleeping on: a body held still must not fall asleep in the hand
    settings.sleeping = true;
    auto w = makePhysicsWorld(settings, kGravity);
    body(*w, {0, -0.5_r, 0}, {box({5, 0.5_r, 5})}, BodyDesc::Type::Static);
    struct Lift { Real sag = 0, aside = 0, pull = 0, fallen = 0; };
    const auto lift = [&](Real share) {
        const int b = body(*w, {0, 0.1_r, 0}, {box({0.1_r, 0.1_r, 0.1_r})});
        for (int k = 0; k < 30; ++k) w->step(kDt);
        const Real weight = w->state(b).mass * 9.81_r;
        HandDesc h;
        h.body = b, h.point = w->state(b).position + Vector3(0, 0.1_r, 0), h.maxForce = share * weight;
        h.maxTorque = 0.5_r * std::sqrt(0.2_r * 0.2_r / 6) * weight; // the editor's brake: half a weight on the gyration radius
        std::string error;
        const int hand = w->addHand(h, error);
        const Vector3 up = h.point + Vector3(0, 0.5_r, 0), target = up + Vector3(0.3_r, 0, 0);
        for (int k = 0; k < 180; ++k) w->moveHand(hand, up, kDt), w->step(kDt);
        for (int k = 1; k <= 120; ++k) w->moveHand(hand, up + (target - up) * std::min(1.0_r, Real(k) / 60), kDt), w->step(kDt);
        const BodyState s = w->state(b);
        const Vector3 held = s.position + s.rotation.rotate(Vector3(0, 0.1_r, 0));
        Lift out;
        out.sag = target.y - held.y, out.aside = std::fabs(target.x - held.x);
        Vector3 force, torque;
        w->reaction(hand, force, torque);
        out.pull = length(force) / weight;
        w->removeJoint(hand);
        for (int k = 0; k < 120; ++k) w->step(kDt);
        out.fallen = w->state(b).position.y;
        w->removeBody(b);
        return out;
    };
    const Lift strong = lift(100), weak = lift(0.5_r);
    const Real omega = 2 * kPi * HandDesc().hertz, expected = 9.81_r / (omega * omega);
    std::printf("  lifted by its top, held 3 s, led 0.3 m aside: %.2f mm under the target (g / w^2 %.2f mm), %.2f mm aside, the pull %.3f m g; let go: at %.4f m\n",
                double(strong.sag * 1000), double(expected * 1000), double(strong.aside * 1000), double(strong.pull), double(strong.fallen));
    std::printf("  a hand of half the weight: the box %.1f cm under the target, the pull %.2f m g\n", double(weak.sag * 100), double(weak.pull));
    CHECK(std::fabs(strong.sag - expected) < 0.1_r * expected, "the held box hangs %f m under the target, not g / w^2 = %f m", double(strong.sag), double(expected));
    CHECK(strong.aside < 0.001_r, "the held box stayed %f m off the target led aside", double(strong.aside));
    CHECK(std::fabs(strong.pull - 1) < 0.01_r, "the hand pulls %f weights, not one", double(strong.pull));
    CHECK(std::fabs(strong.fallen - 0.1_r) < 0.01_r, "let go, the box is at %f, not back on the floor", double(strong.fallen));
    CHECK(weak.sag > 0.49_r && std::fabs(weak.pull - 0.5_r) < 0.01_r, "a hand of half the weight lifted the box to %f m under the target, pulling %f weights",
          double(weak.sag), double(weak.pull));
}

void testPhysicsRepeatable() {
    auto run = [] {
        auto w = world();
        body(*w, {0, -0.5_r, 0}, {box({5, 0.5_r, 5})}, BodyDesc::Type::Static);
        std::vector<int> crates;
        for (int k = 0; k < 10; ++k)
            crates.push_back(body(*w, {0.03_r * Real(k % 3), 0.3_r + 0.45_r * Real(k), 0.02_r * Real(k % 2)}, {box({0.2_r, 0.2_r, 0.2_r})}));
        for (int k = 0; k < 180; ++k) w->step(kDt);
        std::vector<Real> out;
        for (int c : crates) {
            const BodyState s = w->state(c);
            out.insert(out.end(), {s.position.x, s.position.y, s.position.z, s.rotation.w, s.rotation.x, s.rotation.y, s.rotation.z});
        }
        return out;
    };
    const std::vector<Real> a = run(), b = run();
    std::printf("  ten falling crates, two worlds built alike: %s after 3 s\n", a == b ? "equal to the bit" : "DIFFERENT");
    CHECK(a == b, "two worlds built alike went apart");
}
