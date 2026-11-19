// The running world of a robot (src/sim) against exact answers. A box of the scene falls onto the
// floor and rests on it. A servo of our own code on a hinge goes to its target and, with gravity on,
// droops by the gravity torque over its stiffness, m g d / k. A DC motor spins a free wheel up to its
// no-load speed U / K with the time constant I R / K^2. The frame holds every body, every joint's glyph
// and the scene's lights. The channels the plots draw read the engine's own numbers (energy, height,
// speed, the weight on the contacts, a joint's coordinate and effort); a servo's new target is taken
// while the world runs; the mouse's hand holds a body under its target; the probe's debug layers draw
// what is on and nothing else; a stiff servo on a light link reaches its IK target (its damper implicit).
#include "TestRunner.h"

#include "physics/HullPoints.h"
#include "scene/Animation.h"
#include "scene/Assembly.h"
#include "scene/Kinematics.h"
#include "scene/SceneOps.h"
#include "sim/Channels.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <cstdio>

using namespace ke;

namespace {

Entity solid(uint32_t id, const char* name, const Vector3& size, const Vector3& at, bool fixed) {
    Entity e;
    e.id = id, e.name = name, e.size = size, e.position = at;
    e.rigid.enabled = e.collider.enabled = true;
    e.rigid.fixed = fixed;
    return e;
}

// A fixed post and a horizontal arm 0.4 m long on a hinge about z at the post's top, the arm's centre
// 0.2 m out along +x: its turn about z is the joint's coordinate (frames with x on the world's z).
SceneGraph armOnPost(const JointDrive& drive) {
    SceneGraph g;
    g.world.physics.sleeping = false;
    g.entities.push_back(solid(1, "Стойка", {0.1_r, 1, 0.1_r}, {0, 0.5_r, 0}, true));
    g.entities.push_back(solid(2, "Рука", {0.4_r, 0.04_r, 0.04_r}, {0.2_r, 1.05_r, 0}, false));
    JointObject j;
    j.id = 3, j.name = "Плечо", j.a = 2, j.b = 1;
    j.anchorA = {-0.2_r, 0, 0}, j.anchorB = {0, 0.55_r, 0};
    j.frameA = j.frameB = {0, -90, 0}; // x onto the world's z
    j.dof[DofRx].motion = JointMotion::Free;
    j.dof[DofRx].drive = drive;
    g.joints.push_back(j);
    return g;
}

Real armAngle(const Simulation& s) {
    const BodyState a = s.physics()->state(s.robot()->bodyOf(2));
    const Vector3 along = a.rotation.rotate(Vector3(1, 0, 0));
    return std::atan2(along.y, along.x);
}

} // namespace

void testSimBoxRestsOnFloor() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.6_r, 0}, false));
    Simulation s;
    s.load(g);
    for (int k = 0; k < 120; ++k) s.stepFrame();
    const Entity& box = s.robot()->live().entities[0];
    Frame f;
    s.fill(f);
    std::printf("  a 0.2 m box dropped from 0.6 m rests at y = %.4f m (0.1) after 2 s; frame: %zu bodies, %zu contacts, %s\n",
                double(box.position.y), f.bodies.size(), f.contacts.size(), f.engine.c_str());
    CHECK(std::fabs(box.position.y - 0.1_r) < 0.006_r, "the box rests at %f, not on the floor at 0.1", double(box.position.y));
    CHECK(f.bodies.size() == 1 && f.contacts.size() >= 3 && !f.engine.empty(), "the frame does not hold the box and its contacts");
}

void testSimServoHoldsAndDroops() {
    JointDrive d;
    d.enabled = true, d.stiffness = 60, d.damping = 4, d.target = 0.5_r, d.maxForce = 100;
    SceneGraph g = armOnPost(d);
    g.world.gravity = Vector3(0.0_r);
    Simulation free;
    free.load(g);
    for (int k = 0; k < 240; ++k) free.stepFrame();
    const Real reached = armAngle(free);
    g.world.gravity = Vector3(0, -9.81_r, 0);
    Simulation heavy;
    heavy.load(g);
    for (int k = 0; k < 360; ++k) heavy.stepFrame();
    const Real held = armAngle(heavy), m = heavy.physics()->state(heavy.robot()->bodyOf(2)).mass;
    // At rest: k (target - theta) = m g d cos(theta), d = 0.2 m from the axis to the arm's centre.
    Real theta = d.target;
    for (int k = 0; k < 50; ++k) theta = d.target - m * 9.81_r * 0.2_r * std::cos(theta) / d.stiffness;
    Frame f;
    heavy.fill(f);
    std::printf("  servo k = 60 N m/rad to 0.5 rad: %.5f rad without gravity; with it the %.3f kg arm droops to %.5f rad "
                "(k (x_t - x) = m g d cos x gives %.5f); %zu joint glyph(s)\n", double(reached), double(m), double(held), double(theta),
                f.joints.size());
    CHECK(std::fabs(reached - 0.5_r) < 1e-3_r, "the servo stopped at %f, not at its target 0.5", double(reached));
    CHECK(std::fabs(held - theta) < 0.005_r, "the arm droops to %f, not to the static balance %f", double(held), double(theta));
    CHECK(f.joints.size() == 1, "the frame holds %zu joint glyphs, not 1", f.joints.size());
}

void testSimDcMotorSpinsUp() {
    JointDrive d;
    d.enabled = true, d.voltage = 6, d.resistance = 2, d.constant = 0.8_r;
    SceneGraph g = armOnPost(d);
    g.world.gravity = Vector3(0.0_r);
    g.entities[1].size = {0.4_r, 0.04_r, 0.4_r};
    Simulation s;
    s.load(g);
    const int arm = s.robot()->bodyOf(2);
    const Real I = s.physics()->state(arm).mass * (0.4_r * 0.4_r + 0.04_r * 0.04_r) / 12 + s.physics()->state(arm).mass * 0.04_r;
    const Real tau = I * d.resistance / (d.constant * d.constant), noLoad = d.voltage / d.constant;
    const int frames = int(std::lround(tau / s.frameDt));
    for (int k = 0; k < frames; ++k) s.stepFrame();
    const Real atTau = s.physics()->state(arm).angularVelocity.z;
    for (int k = 0; k < 6 * frames; ++k) s.stepFrame();
    const Real settled = s.physics()->state(arm).angularVelocity.z;
    std::printf("  DC motor U = 6 V, R = 2 Ohm, K = 0.8: after tau = I R / K^2 = %.3f s %.3f rad/s ((1 - 1/e) U / K = %.3f); settled "
                "%.3f rad/s (U / K = %.1f)\n", double(tau), double(atTau), double((1 - std::exp(-1.0_r)) * noLoad), double(settled), double(noLoad));
    CHECK(std::fabs(settled / noLoad - 1) < 0.01_r, "the motor settles at %f, not U / K = %f", double(settled), double(noLoad));
    CHECK(std::fabs(atTau / ((1 - std::exp(-1.0_r)) * noLoad) - 1) < 0.03_r, "at tau the motor is at %f, not (1 - 1/e) U / K", double(atTau));
}

// A DC motor through a driver from a battery (RobotPower.cpp). The bridge draws d I from the pack while the motor's
// torque K I spins the arm, so the charge the pack gave is d times the motor's charge: Q = d J w / K exactly, J w the
// arm's angular momentum - Coulomb counting against the engine. Spun up free, the current dies away and the arm runs
// at d E / K, E the pack's voltage with no load. A spent pack opens the bus: the motor gets one substep's kick and no
// more. With no battery the driver runs on an ideal bus at the motor's line voltage: d U / K.
void testSimBatteryDrivesMotor() {
    JointDrive m;
    m.enabled = true, m.voltage = 6, m.resistance = 2, m.constant = 0.8_r;
    auto scene = [&](Real soc, bool battery) {
        SceneGraph g = armOnPost(m);
        g.world.gravity = Vector3(0.0_r);
        g.entities[1].size = {0.4_r, 0.04_r, 0.4_r};
        BatteryObject b;
        b.id = 10, b.name = "Пак", b.series = 2, b.capacityAh = 0.001_r, b.soc = soc; // 3.6 C: the spin-up drains a third
        if (battery) g.batteries.push_back(b);
        DriverObject d;
        d.id = 11, d.name = "Мост", d.joint = 3, d.dof = DofRx, d.battery = battery ? 10 : 0, d.duty = 0.5_r;
        g.drivers.push_back(d);
        return g;
    };
    Simulation s;
    s.load(scene(1, true));
    const int arm = s.robot()->bodyOf(2);
    const Real mass = s.physics()->state(arm).mass, J = mass * (0.4_r * 0.4_r + 0.04_r * 0.04_r) / 12 + mass * 0.04_r;
    for (int k = 0; k < 600; ++k) s.stepFrame();
    RobotWorld::PowerReading pack, bridge;
    const bool read = s.robot()->powerReading(10, pack) && s.robot()->powerReading(11, bridge);
    const double w = s.physics()->state(arm).angularVelocity.z, Q = 0.001 * 3600;
    const double drawn = (1.0 - double(pack.soc)) * Q, momentum = 0.5 * double(J) * w / double(m.constant);
    std::printf("  2S pack, duty 0.5, K = 0.8: charge drawn %.4f C, d J w / K = %.4f C; settled %.3f rad/s at %.3f V (d V / K = %.3f), "
                "%.4f A left\n", drawn, momentum, w, double(pack.voltage), 0.5 * double(pack.voltage) / double(m.constant), double(bridge.current));
    CHECK(read && !pack.spent, "the pack and its driver read in the running world");
    CHECK(std::fabs(drawn / momentum - 1) < 0.02, "the pack gave %f C, not d J w / K = %f C", drawn, momentum);
    CHECK(std::fabs(w / (0.5 * double(pack.voltage) / double(m.constant)) - 1) < 0.01, "the arm runs at %f, not d V / K", w);

    Simulation dead;
    dead.load(scene(0, true));
    for (int k = 0; k < 120; ++k) dead.stepFrame();
    RobotWorld::PowerReading spent;
    dead.robot()->powerReading(10, spent);
    const double still = dead.physics()->state(dead.robot()->bodyOf(2)).angularVelocity.z;
    CHECK(spent.spent && std::fabs(still) < 0.05, "a spent pack opens the bus: %f rad/s, spent %d", still, int(spent.spent));

    Simulation ideal;
    ideal.load(scene(1, false));
    for (int k = 0; k < 600; ++k) ideal.stepFrame();
    const double free = ideal.physics()->state(ideal.robot()->bodyOf(2)).angularVelocity.z, expected = 0.5 * double(m.voltage / m.constant);
    CHECK(std::fabs(free / expected - 1) < 0.01, "with no battery the driver gives d U / K = %f, not %f", expected, free);
    DriverObject full = ideal.robot()->graph().drivers[0];
    full.duty = 1; // slid while the world runs
    ideal.robot()->setDuties({full});
    for (int k = 0; k < 600; ++k) ideal.stepFrame();
    const double faster = ideal.physics()->state(ideal.robot()->bodyOf(2)).angularVelocity.z;
    CHECK(std::fabs(faster / double(m.voltage / m.constant) - 1) < 0.01, "a duty of 1 set while running gives U / K, not %f", faster);
}

// assembly_actuator - "join these two with a motor" - on two shapes that are geometry only, as a newcomer draws them:
// a post and a rod beside it. The post becomes fixed, the rod a body; the hinge goes where they meet; the servo, tuned
// to the rod, holds it level under gravity within a degree and turns it to a target when told.
void testSimActuatorHolds() {
    SceneGraph g;
    Entity post, rod;
    post.id = 1, post.name = "Стойка", post.size = {0.1_r, 1, 0.1_r}, post.position = {0, 0.5_r, 0};
    rod.id = 2, rod.name = "Стержень", rod.size = {0.6_r, 0.04_r, 0.04_r}, rod.position = {0.35_r, 0.9_r, 0};
    g.entities = {post, rod};
    Value args = Value::map(), answer = Value::map(), axis = Value::list();
    for (Real v : {0.0_r, 0.0_r, 1.0_r}) axis.push(v);
    args.set("a", 2), args.set("b", 1), args.set("axis", axis);
    std::string error;
    const bool made = findSceneOp("assembly_actuator")->edit(g, args, answer, error);
    const uint32_t joint = uint32_t(answer["id"].number());
    CHECK(made && joint && answer["note"].text("").find("закреплено") != std::string::npos && g.entities[0].rigid.fixed && g.entities[1].rigid.enabled,
          "the actuator made the post fixed and the rod a body: %s", error.c_str());
    Simulation s;
    s.load(g);
    for (int k = 0; k < 120; ++k) s.stepFrame();
    Real x[6] = {};
    s.robot()->jointCoordinates(*s.physics(), joint, x);
    const int dof = mainFreedom(*findJoint(g, joint));
    const double droop = std::fabs(double(x[dof])) * 180 / 3.141592653589793;
    for (JointObject& j : g.joints) j.dof[dof].drive.target = 0.5_r;
    s.robot()->setJoints(*s.physics(), g.joints);
    for (int k = 0; k < 180; ++k) s.stepFrame();
    Real turned[6] = {};
    s.robot()->jointCoordinates(*s.physics(), joint, turned);
    std::printf("  a rod on its post, the servo tuned by the actuator (k %.1f, c %.2f): droop %.3f deg; target 0.5 rad -> %.4f rad\n",
                answer["stiffness"].number(), answer["damping"].number(), droop, double(turned[dof]));
    CHECK(droop < 1.2, "the tuned servo holds the rod within a degree: %f", droop);
    CHECK(std::fabs(double(turned[dof]) - 0.5) < 0.03, "told 0.5 rad, it turns there: %f", double(turned[dof]));
}

// The channels the plots draw, against the engine's own numbers. A box falling free from rest keeps its
// mechanical energy (the potential turns into motion, m g h = m v^2 / 2, within the integrator's error)
// and reads its height and speed; resting, its contacts carry its weight. The servo's joint reads the
// coordinate the engine's poses give, its target and the torque that holds the arm (m g d cos x); a new
// target set while the world runs is taken at once, and a joint changed in what it is is refused.
void testSimChannels() {
    SceneGraph g;
    g.world.physics.sleeping = false;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 2.1_r, 0}, false));
    Simulation s;
    s.load(g);
    s.channels.scene = true;
    s.channels.objects.push_back({1, "Ящик"});
    auto value = [&](const std::string& id) {
        for (const Measurement& m : s.measurements())
            if (m.info.id == id) return m.value;
        return std::nan("");
    };
    s.stepFrame();
    const double e0 = value("rigid/mechanical energy");
    for (int k = 0; k < 29; ++k) s.stepFrame(); // 0.5 s: 1.23 m down, still in the air
    const double e1 = value("rigid/mechanical energy"), v = value("Ящик/speed"), h = value("Ящик/height");
    const double m = value("Ящик/mass"), t = double(s.time());
    std::printf("  falling 0.5 s: E %.4f -> %.4f J, speed %.4f m/s (g t = %.4f), height %.4f m (2.1 - g t^2 / 2 = %.4f)\n", e0, e1, v,
                9.81 * t, h, 2.1 - 0.5 * 9.81 * t * t);
    CHECK(std::fabs(e1 - e0) < 0.01 * m * 9.81 * 2.1, "the falling box's energy went %f -> %f J", e0, e1);
    CHECK(std::fabs(v - 9.81 * t) < 1e-3 && std::fabs(h - (2.1 - 0.5 * 9.81 * t * t)) < 0.02, "speed %f, height %f off the free fall", v, h);
    for (int k = 0; k < 150; ++k) s.stepFrame();
    const double weight = value("Ящик/contact force y");
    std::printf("  resting: contacts carry %.3f N (m g = %.3f N), %g points\n", weight, m * 9.81, value("Ящик/contacts"));
    CHECK(std::fabs(weight / (m * 9.81) - 1) < 0.02, "the contacts carry %f N, not the weight %f N", weight, m * 9.81);

    JointDrive d;
    d.enabled = true, d.stiffness = 60, d.damping = 4, d.target = 0.5_r, d.maxForce = 100;
    Simulation arm;
    arm.load(armOnPost(d));
    arm.channels.joints.push_back({3, "Плечо"});
    for (int k = 0; k < 360; ++k) arm.stepFrame();
    auto joint = [&](const std::string& id) {
        for (const Measurement& x : arm.measurements())
            if (x.info.id == id) return x.value;
        return std::nan("");
    };
    const double angle = joint("Плечо/rx coordinate"), target = joint("Плечо/rx target"), effort = joint("Плечо/rx effort");
    const double mass = double(arm.physics()->state(arm.robot()->bodyOf(2)).mass), hold = mass * 9.81 * 0.2 * std::cos(angle);
    std::printf("  joint: angle %.5f rad (engine's poses %.5f), target %.2f, effort %.4f N m (m g d cos x = %.4f)\n", angle,
                double(armAngle(arm)), target, effort, hold);
    CHECK(std::fabs(angle - double(armAngle(arm))) < 1e-4 && target == 0.5, "the joint reads %f (poses %f), target %f", angle,
          double(armAngle(arm)), target);
    CHECK(std::fabs(effort - hold) < 0.02 * hold, "the servo holds with %f N m, not m g d cos x = %f", effort, hold);
    std::vector<JointObject> joints = arm.robot()->graph().joints;
    joints[0].dof[DofRx].drive.target = 0.2_r;
    const bool taken = arm.robot()->setJoints(*arm.physics(), joints);
    for (int k = 0; k < 360; ++k) arm.stepFrame();
    joints[0].dof[DofRx].upper = 1, joints[0].dof[DofRx].motion = JointMotion::Limited;
    const bool refused = !arm.robot()->setJoints(*arm.physics(), joints);
    std::printf("  a new target 0.2 rad set while running: %s, the arm at %.4f rad; a new limit: %s\n", taken ? "taken" : "refused",
                double(armAngle(arm)), refused ? "refused (build again)" : "taken");
    CHECK(taken && std::fabs(armAngle(arm) - 0.2_r) < 0.05_r, "the new target was not taken: the arm at %f", double(armAngle(arm)));
    CHECK(refused, "a joint changed in what it is was taken in place");
    // An IK goal made while the world runs: a mark in the frame (no body), and its miss is measured.
    Entity goal;
    goal.id = 9, goal.name = "Цель", goal.goalOf = 2, goal.size = Vector3(0.05_r), goal.position = {0.5_r, 1.05_r, 0};
    arm.robot()->setGoals({goal});
    arm.channels.goals.push_back({9, "Цель"});
    arm.stepFrame();
    Frame f;
    arm.fill(f);
    bool marked = false;
    for (const Frame::Body& b : f.bodies) marked = marked || (b.object == 9 && b.marker && b.body < 0);
    const double miss = joint("Цель/miss");
    Vector3 hp;
    Quaternion hq;
    worldPose(arm.robot()->live(), arm.robot()->live().entities[1], hp, hq);
    std::printf("  a goal made while running: drawn as a mark %s, miss %.1f mm (%.1f)\n", marked ? "yes" : "no", miss,
                1000.0 * double(length(goal.position - hp)));
    CHECK(marked && std::fabs(miss - 1000.0 * double(length(goal.position - hp))) < 0.1, "the goal set while running is not drawn or measured");
}

// The mouse's hand: a box taken at its top and pulled 0.5 m up hangs at the target, its weight carried (without it
// the hand's spring of 7.5 Hz would leave it g / w^2 = 4.4 mm under); held still 3 s and let go, it falls back to
// the floor - the world sleeps bodies at rest, and a held body counting its time to sleep would stay in the air.
void testSimGrab() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.1_r, 0}, false));
    Simulation s;
    s.load(g);
    for (int k = 0; k < 30; ++k) s.stepFrame();
    const int box = s.robot()->bodyOf(1);
    const Vector3 top = s.physics()->state(box).position + Vector3(0, 0.1_r, 0);
    s.robot()->grab(*s.physics(), box, top);
    s.robot()->setGrabTarget(top + Vector3(0, 0.5_r, 0));
    for (int k = 0; k < 180; ++k) s.stepFrame();
    const Real held = s.physics()->state(box).position.y + 0.1_r;
    s.robot()->releaseGrab(*s.physics());
    for (int k = 0; k < 120; ++k) s.stepFrame();
    const Real dropped = s.physics()->state(box).position.y;
    std::printf("  held point at %.4f m (target %.4f); let go: back at %.4f m\n", double(held), double(top.y + 0.5_r), double(dropped));
    CHECK(std::fabs(held - (top.y + 0.5_r)) < 0.002_r, "the held point is at %f, not at the target %f", double(held), double(top.y + 0.5_r));
    CHECK(std::fabs(dropped - 0.1_r) < 0.01_r, "let go, the box is at %f, not on the floor", double(dropped));
}

// The probe's debug layers drawn from the engine's states: with the contact points and the centres of mass
// on, a box resting on the floor draws its four contacts and its centre's three strokes; with no layer on, a
// frame draws nothing.
void testSimDebugLayers() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.1_r, 0}, false));
    Simulation s;
    s.load(g);
    for (int k = 0; k < 30; ++k) s.stepFrame();
    Probe::setLayers(Probe::bit(DrawLayer::ContactPoints) | Probe::bit(DrawLayer::CentreOfMass));
    s.stepFrame();
    Frame on;
    s.fill(on);
    size_t centre = 0, contacts = 0;
    for (const Probe::Line& l : on.probe.lines) centre += l.layer == DrawLayer::CentreOfMass;
    for (const Probe::Point& p : on.probe.points) contacts += p.layer == DrawLayer::ContactPoints;
    Probe::setLayers(0);
    s.stepFrame();
    Frame off;
    s.fill(off);
    std::printf("  layers on: %zu contact points, %zu centre strokes; off: %zu primitives\n", contacts, centre,
                off.probe.lines.size() + off.probe.points.size() + off.probe.labels.size());
    CHECK(contacts == on.contacts.size() && contacts >= 3 && centre == 3, "the layers drew %zu contacts (%zu), %zu centre strokes", contacts,
          on.contacts.size(), centre);
    CHECK(off.probe.lines.empty() && off.probe.points.empty(), "with no layer on a frame still draws");
}

// A stiff servo on a light link: a planar arm of two hinges (k = 4000 N m/rad, c = 150 N m s/rad) and a hand,
// its servos given the targets IK solves for a goal 16 cm aside and down. The forearm's inertia about the
// elbow is small (c h / I > 2 at 32 substeps): an explicit damper buzzed there and held the elbow 0.05 rad
// off; the implicit one brings the hand to the goal within a millimetre.
void testSimStiffServoOnLightLink() {
    auto link = [](uint32_t id, const Vector3& size, const Vector3& at, bool fixed) {
        Entity e = solid(id, "Звено", size, at, fixed);
        return e;
    };
    auto hinge = [](uint32_t id, uint32_t a, uint32_t b, const Vector3& anchorA, const Vector3& anchorB) {
        JointObject j;
        j.id = id, j.a = a, j.b = b, j.anchorA = anchorA, j.anchorB = anchorB;
        j.frameA = j.frameB = {0, -90, 0};
        j.dof[DofRx].motion = JointMotion::Free;
        JointDrive& d = j.dof[DofRx].drive;
        d.enabled = true, d.stiffness = 4000, d.damping = 150, d.maxForce = 400;
        return j;
    };
    SceneGraph g;
    g.entities = {link(1, {0.3_r, 0.1_r, 0.3_r}, {0, 0.05_r, 0}, true), link(2, {0.08_r, 0.4_r, 0.08_r}, {0, 0.3_r, 0}, false),
                  link(3, {0.06_r, 0.3_r, 0.06_r}, {0, 0.65_r, 0}, false), link(4, {0.08_r, 0.04_r, 0.08_r}, {0, 0.82_r, 0}, false)};
    g.joints = {hinge(10, 2, 1, {0, -0.2_r, 0}, {0, 0.05_r, 0}), hinge(11, 3, 2, {0, -0.15_r, 0}, {0, 0.2_r, 0})};
    JointObject weld;
    weld.id = 12, weld.a = 4, weld.b = 3, weld.anchorA = {0, -0.02_r, 0}, weld.anchorB = {0, 0.15_r, 0};
    g.joints.push_back(weld);
    const uint32_t goal = addGoal(g, 4);
    Vector3 at;
    for (Entity& e : g.entities)
        if (e.id == goal) e.position += Vector3(0.16_r, -0.16_r, 0), at = e.position;
    reachGoal(g, goal); // the servos' targets; the arm stays as it stands
    Simulation s;
    s.load(g);
    for (int k = 0; k < 300; ++k) s.stepFrame();
    const Vector3 hand = s.physics()->state(s.robot()->bodyOf(4)).position;
    Real elbow[6];
    s.robot()->jointCoordinates(*s.physics(), 11, elbow);
    std::printf("  stiff servos on a light forearm: the hand %.2f mm from the goal; the elbow at %.4f rad (target %.4f)\n",
                1000.0 * double(length(hand - at)), double(elbow[DofRx]), double(g.joints[1].dof[DofRx].drive.target));
    CHECK(length(hand - at) < 0.001_r, "the hand is %f m from the goal", double(length(hand - at)));
}

// The UR5e of examples/ (meshes, eight joints, a gripper): its hinges 7 and 13 are written with frames whose
// quaternions agree in the world but differ in sign - Box3D read that sign and spun the links up to
// thousands of rad/s within two frames, and its base's mesh of tens of thousands of points made no hull (now its
// collider is a cylinder, and a mesh's hull takes its extremes, HullPoints.h).
// Built, it holds its gripper on its IK goal under gravity, its links still.
SceneGraph ur5e() {
    std::ifstream file(std::string(KE_EXAMPLES) + "/ur5e.kscene");
    std::stringstream text;
    text << file.rdbuf();
    SceneGraph g;
    std::string error;
    CHECK(g.load(text.str(), error), "examples/ur5e.kscene loads: %s", error.c_str());
    g.baseDirectory = KE_EXAMPLES;
    return g;
}

void testSimUr5eHolds() {
    const SceneGraph g = ur5e();
    Simulation sim;
    sim.load(g);
    for (int k = 0; k < 120; ++k) sim.stepFrame();
    Real fastest = 0;
    std::string which;
    for (const Entity& e : g.entities)
        if (const int b = sim.robot()->bodyOf(e.id); b >= 0 && length(sim.physics()->state(b).angularVelocity) > fastest)
            fastest = length(sim.physics()->state(b).angularVelocity), which = e.name;
    const BodyState gripper = sim.physics()->state(sim.robot()->bodyOf(16));
    const Vector3 goal = std::find_if(g.entities.begin(), g.entities.end(), [](const Entity& e) { return e.id == 52; })->position;
    std::printf("  after 2 s: notes %d, the fastest body (%s) turns %.3f rad/s, the gripper %.1f mm from its goal\n", int(sim.robot()->notes().size()),
                which.c_str(), double(fastest), double(1000 * length(gripper.position - goal)));
    CHECK(sim.robot()->notes().empty() && fastest < 0.5_r && length(gripper.position - goal) < 0.005_r,
          "the UR5e built with notes, its links turning %f rad/s, its gripper %f m from the goal", double(fastest), double(length(gripper.position - goal)));
}

// The mouse tugs the UR5e by a finger of 17 g: the arm follows the hand as a whole robot would be pulled (the spring
// scaled by the robot's mass - by the finger's own, 1/1000 of it, the arm's motors would hold it within a centimetre).
// Let go, its servos bring the gripper back to where it stood along their setpoints' profile: never a frame further
// off than before, no joint faster than twice the profile's 180 deg/s - its springs, let go at the edge of their
// linear zone at full force, catch up with their setpoints (the servos at full force without the profile bounced
// the gripper 56 mm away and whipped the wrists at 45 rad/s).
void testSimTugUr5e() {
    Simulation sim;
    sim.load(ur5e());
    for (int k = 0; k < 120; ++k) sim.stepFrame();
    const int finger = sim.robot()->bodyOf(18), gripper = sim.robot()->bodyOf(16);
    const Vector3 home = sim.physics()->state(gripper).position, from = sim.physics()->state(finger).position;
    const Vector3 to = from + Vector3(0.15_r, 0.25_r, 0);
    sim.robot()->grab(*sim.physics(), finger, from);
    sim.robot()->setGrabTarget(to);
    for (int k = 0; k < 120; ++k) sim.stepFrame();
    const Real miss = length(sim.physics()->state(finger).position - to);
    sim.robot()->releaseGrab(*sim.physics());
    const auto angles = [&](Real out[6]) {
        const uint32_t hinges[6] = {5, 7, 9, 11, 13, 15};
        for (int i = 0; i < 6; ++i) {
            Real x[6];
            sim.robot()->jointCoordinates(*sim.physics(), hinges[i], x);
            out[i] = x[DofRx];
        }
    };
    Real before[6], now[6], fastest = 0, bounce = 0, nearest = length(sim.physics()->state(gripper).position - home);
    angles(before);
    for (int k = 0; k < 480; ++k) {
        sim.stepFrame();
        angles(now);
        for (int i = 0; i < 6; ++i) fastest = std::max(fastest, std::fabs(now[i] - before[i]) * 60), before[i] = now[i];
        const Real off = length(sim.physics()->state(gripper).position - home);
        bounce = std::max(bounce, off - nearest), nearest = std::min(nearest, off);
    }
    const Real back = length(sim.physics()->state(gripper).position - home);
    std::printf("  tugged 29 cm by a finger: the finger %.1f mm from the mouse; let go: back within %.2f mm after 8 s, bounced off %.2f mm "
                "at most, the fastest joint %.2f rad/s\n", double(miss * 1000), double(back * 1000), double(bounce * 1000), double(fastest));
    CHECK(miss < 0.02_r, "the finger stayed %f m from the mouse: the arm did not follow", double(miss));
    CHECK(back < 0.002_r, "let go, the gripper is %f m from where it stood", double(back));
    CHECK(bounce < 0.001_r && fastest < 2 * RobotWorld::kServoTurnSpeed, "on its way back the gripper bounced %f m away, a joint turned %f rad/s",
          double(bounce), double(fastest));
}

// A mesh's own collider (auto) - the UR5e base's 2427 points: the engine and the editor's collider guide get its
// extremes along 120 directions (HullPoints.h), not every point; the hull of all of them took the window 24 s to
// draw, and Box3D builds no more than 128 vertices anyway. The thinned hull reaches the mesh's box within the
// directions' spacing: 120 over the sphere lie sqrt(4 pi / 120) = 0.32 rad apart, so along any axis the hull falls
// short by R (1 - cos 0.16) = 1.3 % of the mesh's size at most.
void testSimMeshColliderThinned() {
    SceneGraph g = ur5e();
    Entity base = *std::find_if(g.entities.begin(), g.entities.end(), [](const Entity& e) { return e.id == 3; });
    base.collider.kind = ColliderKind::Auto;
    const TriMesh mesh = entityLocalMesh(base, g.baseDirectory);
    const std::vector<ShapeDesc> shapes = colliderShapes(base, mesh);
    AABB box(Vector3(1e9_r), Vector3(-1e9_r));
    for (const Vector3& p : shapes.front().points) box.expand(p);
    const Real gap = length(box.extent() - mesh.bounds().extent());
    std::printf("  the base's mesh of %zu points: a collider of %zu points, its box off the mesh's by %.2e m\n", mesh.positions.size(),
                shapes.front().points.size(), double(gap));
    CHECK(shapes.size() == 1 && shapes.front().points.size() <= size_t(kHullDirections) && gap < 0.013_r * maxComp(mesh.bounds().extent()),
          "the mesh's collider has %zu points, its box off the mesh's by %f", shapes.front().points.size(), double(gap));
}

// assembly_goal_pose moves the UR5e's goal 15 cm and turns it 40 degrees at once while the scene plays: the arm leads
// its gripper there in a straight line and settles - the gripper on the goal within 5 mm and 1 degree, moving less
// than 5 mm/s over the last half second. A shape that is not a goal is refused.
void testSimGoalPoseSettles() {
    SceneGraph g = ur5e();
    Simulation sim;
    sim.load(g);
    for (int k = 0; k < 60; ++k) sim.stepFrame();
    const auto goalIn = [](const SceneGraph& graph) {
        return *std::find_if(graph.entities.begin(), graph.entities.end(), [](const Entity& e) { return e.id == 52; });
    };
    const Entity before = goalIn(g);
    const SceneOp* pose = findSceneOp("assembly_goal_pose");
    Value args = Value::map(), answer;
    args.set("goal", 52), args.set("position", vectorValue(before.position + Vector3(0.15_r, -0.05_r, 0.05_r)));
    args.set("turn", vectorValue(before.rotationDeg + Vector3(0, 40, 0)));
    std::string error;
    const bool posed = pose && pose->edit(g, args, answer, error);
    CHECK(posed, "assembly_goal_pose: %s", error.c_str());
    if (!posed) return;
    std::vector<Entity> goals;
    for (const Entity& e : g.entities)
        if (e.goalOf != 0) goals.push_back(e);
    sim.robot()->setGoals(goals); // as the window sends a goal moved while the scene plays
    const int gripper = sim.robot()->bodyOf(16);
    Real fastest = 0;
    for (int k = 0; k < 360; ++k) {
        sim.stepFrame();
        if (k >= 330) fastest = std::max(fastest, length(sim.physics()->state(gripper).velocity));
    }
    const Entity goal = goalIn(g);
    const BodyState s = sim.physics()->state(gripper);
    const Real off = length(s.position - goal.position);
    const Real turnOff = 2 * std::acos(std::min(1.0_r, std::fabs(dot(s.rotation, objectRotation(goal))))) * 180 / kPi;
    Value notGoal = Value::map();
    notGoal.set("goal", 16);
    const bool refused = !pose->edit(g, notGoal, answer, error);
    std::printf("  the goal moved 16 cm and turned 40 deg at once: 6 s later the gripper %.2f mm and %.2f deg off it, moving %.2f mm/s; "
                "a shape refused as a goal: %s\n", double(off * 1000), double(turnOff), double(fastest * 1000), refused ? "yes" : "no");
    CHECK(off < 0.005_r && turnOff < 1 && fastest < 0.005_r && refused, "the gripper %f m and %f deg off the posed goal, moving %f m/s", double(off),
          double(turnOff), double(fastest));
}

// The sensors of the running world, each its probe of the physics and its datasheet's device, against exact answers. A
// 0.2 m box resting on the floor: an IMU on its top reads 1 g up on its z and no turn, a touch pad under it its weight
// m g, a sonar on its side the wall 0.85 m away; an IMU on a box falling free reads no force at all. A hinge spun by a
// velocity motor past a full turn: its encoder counts on (the turn unwrapped), its reading within a step of the truth.
// A servo holding an arm against gravity: the wrench sensor on its hinge reads the arm's weight, the effort sensor the
// servo's torque m g d cos x. With its datasheet's noise an IMU reads other than the truth - the robot's input is never
// the physics' number.
void testSimSensors() {
    SceneGraph g;
    g.world.physics.sleeping = false;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.1_r, 0}, false));
    g.entities.push_back(solid(2, "Стена", {0.1_r, 1, 2}, {1, 0.5_r, 0}, true));
    g.entities.push_back(solid(3, "Камень", Vector3(0.2_r), {-1, 2.5_r, 0}, false));
    const auto put = [&g](uint32_t on, BoxFace face, SensorKind kind, bool noise) {
        const uint32_t id = attachSensor(g, on, snapToFace(g, on, face, Vector3(0.0_r)), kind);
        g.sensors.back().noise = noise;
        return id;
    };
    const uint32_t imu = put(1, BoxFace::PlusY, SensorKind::Imu, false), noisy = put(1, BoxFace::PlusY, SensorKind::Imu, true);
    const uint32_t touch = put(1, BoxFace::MinusY, SensorKind::Touch, false), sonar = put(1, BoxFace::PlusX, SensorKind::Sonar, false);
    const uint32_t falling = put(3, BoxFace::PlusY, SensorKind::Imu, false);
    Simulation s;
    s.load(g);
    for (int k = 0; k < 20; ++k) s.stepFrame();
    const SensorReading fall = s.robot()->sensor(falling)->reading();
    for (int k = 0; k < 100; ++k) s.stepFrame();
    const RobotWorld& w = *s.robot();
    const SensorReading rest = w.sensor(imu)->reading(), pad = w.sensor(touch)->reading(), echo = w.sensor(sonar)->reading();
    const Real weight = s.physics()->state(w.bodyOf(1)).mass * 9.81_r, range = w.sensor(sonar)->truth().value;
    const Vector3 off = w.sensor(noisy)->reading().a - w.sensor(noisy)->truth().a;
    std::printf("  resting box: IMU accel (%.4f %.4f %.4f) m/s^2, gyro |%.5f| rad/s; touch pad %.2f N on %lld points (m g %.2f N); sonar %.4f m "
                "(truth %.4f, 0.85); falling IMU |a| %.4f m/s^2; with noise the IMU reads %.3f m/s^2 off the truth\n", double(rest.a.x), double(rest.a.y),
                double(rest.a.z), double(length(rest.b)), double(pad.value), static_cast<long long>(pad.count), double(weight), double(echo.value),
                double(range), double(length(fall.a)), double(length(off)));
    CHECK(std::fabs(rest.a.z - 9.81_r) < 0.05_r && std::fabs(rest.a.x) < 0.05_r && std::fabs(rest.a.y) < 0.05_r && length(rest.b) < 0.01_r,
          "an IMU at rest does not read 1 g up");
    CHECK(length(fall.a) < 0.05_r && fall.sampleNs > 0, "an IMU falling free reads %f m/s^2, not none", double(length(fall.a)));
    CHECK(std::fabs(pad.value / weight - 1) < 0.02_r && pad.count > 0, "the pad under the box reads %f N, not its weight %f", double(pad.value), double(weight));
    CHECK(std::fabs(range - 0.85_r) < 1e-3_r && std::fabs(echo.value - range) < 1e-6_r, "the sonar reads %f m, the wall is 0.85 m away", double(echo.value));
    CHECK(length(off) > 0.01_r, "an IMU with its datasheet's noise reads the truth itself");

    JointDrive spin;
    spin.enabled = true, spin.targetVelocity = 2, spin.maxForce = 100;
    SceneGraph motor = armOnPost(spin);
    motor.world.gravity = Vector3(0.0_r);
    const uint32_t encoder = attachEncoder(motor, 3, 500);
    Simulation m;
    m.load(motor);
    for (int k = 0; k < 240; ++k) m.stepFrame();
    const LiveSensor& e = *m.robot()->sensor(encoder);
    const double step = 2 * kPi / 2000;
    std::printf("  encoder 500 lines on a hinge spun 4 s at 2 rad/s: count %lld, reads %.4f rad, the hinge turned %.4f rad (unwrapped); "
                "referenced: %s\n", static_cast<long long>(e.reading().count), double(e.reading().value), double(e.truth().value), e.reading().referenced ? "yes" : "no");
    CHECK(e.truth().value > 2 * kPi && std::fabs(double(e.reading().value - e.truth().value)) <= step, "the encoder past a turn: %f vs %f",
          double(e.reading().value), double(e.truth().value));

    JointDrive servo;
    servo.enabled = true, servo.stiffness = 60, servo.damping = 4, servo.target = 0.5_r, servo.maxForce = 100;
    SceneGraph held = armOnPost(servo);
    const uint32_t wrench = attachJointSensor(held, 3, SensorKind::Wrench), effort = attachJointSensor(held, 3, SensorKind::Effort);
    held.sensors[0].noise = false;
    Simulation h;
    h.load(held);
    for (int k = 0; k < 360; ++k) h.stepFrame();
    const Real armWeight = h.physics()->state(h.robot()->bodyOf(2)).mass * 9.81_r, x = armAngle(h);
    const SensorReading load = h.robot()->sensor(wrench)->reading(), drive = h.robot()->sensor(effort)->reading();
    std::printf("  servo holding the arm at %.4f rad: wrench |F| %.4f N (m g %.4f), effort %.5f N m (m g d cos x %.5f)\n", double(x),
                double(length(load.a)), double(armWeight), double(drive.value), double(armWeight * 0.2_r * std::cos(x)));
    CHECK(std::fabs(length(load.a) / armWeight - 1) < 0.02_r, "the wrench reads %f N, not the arm's weight %f", double(length(load.a)), double(armWeight));
    CHECK(std::fabs(drive.value / (armWeight * 0.2_r * std::cos(x)) - 1) < 0.02_r, "the effort reads %f N m", double(drive.value));
}

// The active sequence in the running world: a pusher it moves from x 0 to 2 m in 2 s, a free box in its way at 0.6 m.
// The pusher is a kinematic body led along its track: at 1 s it stands at 1 m, at 2 s at 2 m and stays there (held);
// the box it meets is pushed ahead of it, past 2 m - the animation moves the world, it is not a ghost.
void testSimSequence() {
    SceneGraph g;
    g.world.physics.sleeping = true; // as a scene of the editor: a kinematic body led wakes, never sleeps on its track
    g.entities.push_back(solid(1, "Толкатель", {0.2_r, 0.2_r, 0.4_r}, {0, 0.1_r, 0}, false));
    g.entities.push_back(solid(2, "Ящик", Vector3(0.2_r), {0.6_r, 0.1_r, 0}, false));
    SequenceObject q;
    q.id = 10, q.name = "Толчок", q.active = true;
    q.tracks.push_back({1, {{0, {0, 0.1_r, 0}, Vector3(0.0_r)}, {2, {2, 0.1_r, 0}, Vector3(0.0_r)}}});
    g.sequences.push_back(q);
    Simulation s;
    s.load(g);
    const auto x = [&s](uint32_t id) { return s.physics()->state(s.robot()->bodyOf(id)).position.x; };
    while (s.time() < 1 - 1e-6_r) s.stepFrame();
    const Real half = x(1);
    double speed = -1; // the plots' channel: a led body has no mass, it still has a speed
    for (const Measurement& m : measureObject(s, {1, "Толкатель"}))
        if (m.info.id == "Толкатель/speed") speed = m.value;
    while (s.time() < 2 - 1e-6_r) s.stepFrame();
    const Real end = x(1);
    while (s.time() < 2.5_r - 1e-6_r) s.stepFrame();
    const Real held = x(1), box = x(2);
    std::printf("  a pusher on the active sequence (0 -> 2 m in 2 s): at 1 s %.4f m, its speed channel %.4f m/s; at 2 s %.4f m, at 2.5 s %.4f m; "
                "the box from 0.6 m pushed to %.4f m\n", double(half), speed, double(end), double(held), double(box));
    CHECK(std::fabs(half - 1) < 1e-3_r && std::fabs(end - 2) < 1e-3_r && std::fabs(held - 2) < 1e-3_r, "the pusher off its track");
    CHECK(box > 2.15_r, "the box in the pusher's way was not pushed ahead of it: %f m", double(box));
    CHECK(std::fabs(speed - 1) < 1e-3, "the pusher's speed channel reads %f m/s, it goes 1 m/s", speed);
}

// A take of a run (RobotWorld::startRecording, the Sequencer's record): a box thrown sideways off 1 m falls, lands and
// slides; a fixed post stands. Recorded 1.5 s, the take has one track - the box's, the post never moved - thinned to far
// fewer keys than frames, and its keys give every frame's pose back within 0.5 mm. Put into the scene as the active
// sequence and played, the box is a kinematic body that goes through the same poses: the run as an animation.
void testSimRecording() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 1, 0}, false));
    g.entities.back().rigid.velocity = Vector3(1.5_r, 0, 0);
    g.entities.back().rigid.angularVelocity = Vector3(0, 0, -3);
    g.entities.push_back(solid(2, "Столб", {0.1_r, 0.5_r, 0.1_r}, {-1, 0.25_r, 0}, true));
    Simulation s;
    s.load(g);
    s.startRecording();
    std::vector<AnimKey> truth{{0, Vector3(0, 1, 0), Vector3(0.0_r)}};
    while (s.time() < 1.5_r - 1e-6_r) {
        s.stepFrame();
        const SceneObject* o = findObject(s.robot()->live(), 1);
        truth.push_back({s.time(), o->position, o->rotationDeg});
    }
    SequenceObject take;
    const bool taken = s.takeRecording("Запись 1", take) && !s.robot()->recording();
    const std::vector<AnimKey>& keys = take.tracks.empty() ? truth : take.tracks[0].keys;
    Real worst = 0;
    for (const AnimKey& k : truth) {
        Vector3 p;
        Quaternion q;
        samplePose(keys, k.time, false, 0, p, q);
        worst = std::max(worst, length(p - k.position));
    }
    const bool shownOnly = take.active && !take.play; // the next ▶ is physics, not this take
    take.id = 10, take.play = true;
    g.sequences.push_back(take);
    Simulation replay;
    replay.load(g);
    Real replayWorst = 0;
    for (size_t k = 1; k < truth.size(); ++k) {
        replay.stepFrame();
        replayWorst = std::max(replayWorst, length(replay.physics()->state(replay.robot()->bodyOf(1)).position - truth[k].position));
    }
    std::printf("  a box thrown off 1 m recorded 1.5 s: %s, %zu track(s), %zu keys for %zu frames; the keys give the run back within %.3f mm; "
                "replayed as the active sequence within %.3f mm\n", taken ? "taken" : "none", take.tracks.size(), keys.size(), truth.size(),
                double(worst) * 1e3, double(replayWorst) * 1e3);
    CHECK(taken && shownOnly && take.tracks.size() == 1 && take.tracks[0].object == 1, "the take holds the box's track and not the post's, shown, not played");
    CHECK(keys.size() * 3 < truth.size() && worst <= 0.0005_r + 1e-6_r, "the take thinned to %zu keys of %zu frames, %f mm off", keys.size(), truth.size(),
          double(worst) * 1e3);
    CHECK(replayWorst < 0.001_r, "the take replayed: the box %f mm off its run", double(replayWorst) * 1e3);
}

// A robot's link in a take: an arm on a post, its shoulder's motor turning it at 2 rad/s. Recorded 1.5 s, the take has
// the arm's track (a link its joint moves, recorded as any body; the fixed post none); played back, the arm is a
// kinematic body on that track - its angle goes as the motor turned it, frame by frame.
void testSimRecordingArm() {
    JointDrive spin;
    spin.enabled = true, spin.targetVelocity = 2, spin.maxForce = 100;
    SceneGraph g = armOnPost(spin);
    g.world.gravity = Vector3(0.0_r);
    Simulation s;
    s.load(g);
    s.startRecording();
    std::vector<Real> angles;
    for (int k = 0; k < 90; ++k) s.stepFrame(), angles.push_back(armAngle(s));
    SequenceObject take;
    const bool taken = s.takeRecording("Запись 1", take);
    const bool armOnly = take.tracks.size() == 1 && take.tracks[0].object == 2;
    take.id = 10, take.play = true;
    g.sequences.push_back(take);
    Simulation replay;
    replay.load(g);
    Real worst = 0;
    for (size_t k = 0; k < angles.size(); ++k) {
        replay.stepFrame();
        Real d = armAngle(replay) - angles[k];
        d = std::atan2(std::sin(d), std::cos(d));
        worst = std::max(worst, std::fabs(d));
    }
    const bool led = replay.physics()->state(replay.robot()->bodyOf(2)).mass <= 0;
    std::printf("  an arm its motor turns at 2 rad/s, recorded 1.5 s (%.2f rad): %s, the arm's track only %s, %zu keys for 90 frames; replayed "
                "kinematic %s, its angle within %.4f deg of the run\n", double(angles.back()), taken ? "taken" : "none", armOnly ? "yes" : "no",
                take.tracks.empty() ? size_t(0) : take.tracks[0].keys.size(), led ? "yes" : "no", double(worst) * 180 / double(kPi));
    CHECK(taken && armOnly, "a take records a robot's link a joint moves");
    CHECK(led && worst < 0.2_r * kPi / 180, "the arm replayed off its run by %f deg", double(worst) * 180 / double(kPi));
}

// The UR5e's panel (its scene's controller «Пульт «Схват»») in the running world: «move» held at (1, 0) for 1 s takes
// the IK goal 0.15 m along x (the action's speed, 0.15 m/s) and the hand after it - it rests within a centimetre of
// that once let go; «grip» at 1 closes the fingers (their slider walks toward its closed end); an action the panel
// does not have is refused.
void testSimController() {
    SceneGraph g = ur5e();
    CHECK(!g.controllers.empty(), "the UR5e scene has its panel");
    if (g.controllers.empty()) return;
    const ControllerObject& panel = g.controllers[0];
    uint32_t goalId = 0;
    for (const ControllerAction& a : panel.actions)
        if (a.name == "move") goalId = a.target;
    const Entity* goal = nullptr;
    for (const Entity& e : g.entities)
        if (e.id == goalId) goal = &e;
    CHECK(goal && goal->goalOf, "the panel's move drives an IK goal");
    if (!goal) return;
    const uint32_t effector = goal->goalOf;
    uint32_t finger = 0;
    for (const JointObject& j : g.joints)
        if ((j.a == effector || j.b == effector) && mainFreedom(j) >= 0 && mainFreedom(j) <= 2) finger = j.id;
    Simulation s;
    s.load(g);
    for (int k = 0; k < 60; ++k) s.stepFrame();
    const auto hand = [&] { return s.physics()->state(s.robot()->bodyOf(effector)).position; };
    const auto goalAt = [&] {
        Vector3 p;
        Quaternion q;
        worldPose(s.robot()->live(), *findObject(s.robot()->live(), goalId), p, q);
        return p;
    };
    const Vector3 hand0 = hand(), goal0 = goalAt();
    std::string error;
    const bool set = s.robot()->setControllerInput(panel.id, "move", {1, 0, 0}, error);
    for (int k = 0; k < 60; ++k) s.stepFrame();
    s.robot()->setControllerInput(panel.id, "move", Vector3(0.0_r), error);
    for (int k = 0; k < 60; ++k) s.stepFrame();
    const Vector3 hand1 = hand(), goal1 = goalAt();
    Real open[6] = {}, closed[6] = {};
    s.robot()->jointCoordinates(*s.physics(), finger, open);
    s.robot()->setControllerInput(panel.id, "grip", {1, 0, 0}, error);
    for (int k = 0; k < 60; ++k) s.stepFrame();
    s.robot()->jointCoordinates(*s.physics(), finger, closed);
    const int dof = finger ? mainFreedom(*std::find_if(g.joints.begin(), g.joints.end(), [&](const JointObject& j) { return j.id == finger; })) : 0;
    const bool refused = !s.robot()->setControllerInput(panel.id, "fly", Vector3(1.0_r), error) && error.find("fly") != std::string::npos;
    std::printf("  the UR5e's panel: «move» (1, 0) for 1 s - the goal %.4f m along x, the hand %.4f m (off it by %.4f m); «grip» 1 - the "
                "finger's slider %.4f -> %.4f m; «fly» refused %s\n", double(goal1.x - goal0.x), double(hand1.x - hand0.x),
                double(length(hand1 - goal1 - (hand0 - goal0))), double(open[dof]), double(closed[dof]), refused ? "yes" : "no");
    CHECK(set && std::fabs(goal1.x - goal0.x - 0.15_r) < 1e-3_r, "the panel's move takes the goal 0.15 m in 1 s");
    CHECK(std::fabs(hand1.x - hand0.x - 0.15_r) < 0.01_r, "the hand follows its goal: %f m", double(hand1.x - hand0.x));
    CHECK(finger && std::fabs(closed[dof] - open[dof]) > 0.005_r && refused, "grip closes the fingers; an unknown action is refused");
    // Closed in the air, the gripper says it holds nothing (its fingers reached their target); an edit of the scene made
    // while it plays (the joints as authored, as the window sends them) leaves the panel's grip as it was.
    const std::vector<Frame::GripInfo> said = s.robot()->grips(*s.physics());
    CHECK(said.size() == 1 && said[0].goal == goalId && said[0].state == Frame::GripState::Empty,
          "closed on nothing, the gripper reports empty (%d)", said.empty() ? -1 : int(said[0].state));
    CHECK(s.robot()->setJoints(*s.physics(), g.joints), "the authored joints are taken while playing");
    for (int k = 0; k < 30; ++k) s.stepFrame();
    Real kept[6] = {};
    s.robot()->jointCoordinates(*s.physics(), finger, kept);
    CHECK(std::fabs(kept[dof] - closed[dof]) < 0.002_r, "an edit made while playing kept the panel's grip: the finger at %f, closed %f",
          double(kept[dof]), double(closed[dof]));
}
