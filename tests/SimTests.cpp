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
#include "scene/Assembly.h"
#include "scene/Kinematics.h"
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
