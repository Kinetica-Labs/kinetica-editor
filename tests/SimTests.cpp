// The running world of a robot (src/sim) against exact answers. A box of the scene falls onto the
// floor and rests on it. A servo of our own code on a hinge goes to its target and, with gravity on,
// droops by the gravity torque over its stiffness, m g d / k. A DC motor spins a free wheel up to its
// no-load speed U / K with the time constant I R / K^2. The frame holds every body, every joint's glyph
// and the scene's lights. The channels the plots draw read the engine's own numbers (energy, height,
// speed, the weight on the contacts, a joint's coordinate and effort); a servo's new target is taken
// while the world runs; the mouse's hand holds a body under its target; the probe's debug layers draw
// what is on and nothing else; a stiff servo on a light link reaches its IK target (its damper implicit). The frame
// is the live scene flattened, every frame, though what a run does not change is found once.
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
#include <cstring>
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

// The frame's entities are the visible ones of the live scene flattened, in its order (worldEntities): a body where the
// engine has it; a shape without one where the scene has it now - a child of a falling box follows it, a goal made and
// moved while the world runs is drawn where it was put. RobotWorld::fill finds once what a run does not change (the
// scene flattened and every mesh keyed again was 122 ms a frame of ten thousand cubes): the frame is the same.
void testSimFrameFollowsTheLiveScene() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.6_r, 0}, false));
    Entity sign; // geometry only, hung on the box
    sign.id = 2, sign.name = "Вывеска", sign.size = Vector3(0.05_r), sign.position = {0, 0.2_r, 0}, sign.parent = 1;
    g.entities.push_back(sign);
    Entity hidden = solid(3, "Образец", Vector3(0.1_r), {1, 0.5_r, 0}, false); // a template: not drawn, its copies are
    hidden.visible = false;
    g.entities.push_back(hidden);
    ArrayObject row;
    row.id = 4, row.name = "Ряд", row.templateId = 3, row.count[0] = 3, row.step = {0.3_r, 0, 0}, row.position = {1, 0.3_r, 1};
    g.arrays.push_back(row);
    Simulation s;
    s.load(g);
    const auto expected = [&s] { // the long way: the live scene flattened anew
        std::vector<Frame::Body> out;
        for (const Entity& e : worldEntities(s.robot()->live())) {
            if (!e.visible) continue;
            Frame::Body b;
            b.object = e.id, b.body = s.robot()->bodyOf(e.id), b.marker = isHandle(e);
            if (b.body >= 0 && s.physics()->alive(b.body)) {
                const BodyState at = s.physics()->state(b.body);
                b.pos = at.position, b.rot = at.rotation, b.sleeping = !at.awake, b.movable = at.mass > 0;
            } else {
                b.pos = e.position, b.rot = entityRotation(e), b.movable = false;
            }
            out.push_back(b);
        }
        return out;
    };
    const auto same = [&] {
        Frame f;
        s.fill(f);
        const std::vector<Frame::Body> want = expected();
        bool ok = f.bodies.size() == want.size();
        for (size_t k = 0; ok && k < want.size(); ++k) {
            const Frame::Body &a = f.bodies[k], &b = want[k];
            ok = a.object == b.object && a.body == b.body && std::memcmp(&a.pos, &b.pos, sizeof a.pos) == 0 &&
                 std::memcmp(&a.rot, &b.rot, sizeof a.rot) == 0 && a.sleeping == b.sleeping && a.movable == b.movable && a.marker == b.marker && a.mesh;
        }
        return ok;
    };
    const auto drawn = [&s](uint32_t object) {
        Frame f;
        s.fill(f);
        for (const Frame::Body& b : f.bodies)
            if (b.object == object) return b;
        return Frame::Body();
    };
    bool every = same();
    for (int k = 0; k < 20; ++k) s.stepFrame(), every = every && same();
    Frame f;
    s.fill(f);
    const Frame::Body box = drawn(1), child = drawn(2);
    std::printf("  the frame of a box, a sign hung on it, a hidden template and its 3 copies: %zu drawn; after 20 frames the box at y = %.4f m, "
                "its sign at %.4f m (0.2 above); the same as the scene flattened anew every frame: %s\n", f.bodies.size(), double(box.pos.y),
                double(child.pos.y), every ? "yes" : "no");
    CHECK(every, "the frame is not the live scene flattened");
    CHECK(f.bodies.size() == 5 && drawn(3).object == 0, "the frame holds %zu entities, not the box, its sign and three copies", f.bodies.size());
    CHECK(child.body < 0 && box.pos.y < 0.6_r && std::fabs(child.pos.y - box.pos.y - 0.2_r) < 1e-3_r, "the sign at %f does not follow its box at %f",
          double(child.pos.y), double(box.pos.y));
    Entity goal; // made while the world runs, then moved: a mark where it was put
    goal.id = 9, goal.name = "Цель", goal.goalOf = 1, goal.size = Vector3(0.05_r), goal.position = {0.5_r, 1, 0};
    s.robot()->setGoals({goal});
    s.stepFrame();
    const bool made = same() && drawn(9).marker;
    goal.position = {0.7_r, 1.2_r, 0};
    s.robot()->setGoals({goal});
    s.stepFrame();
    const Vector3 put = drawn(9).pos;
    std::printf("  a goal made while it runs: in the frame %s; moved to (0.7, 1.2, 0): drawn at (%.2f, %.2f, %.2f)\n", made ? "yes" : "no", double(put.x),
                double(put.y), double(put.z));
    CHECK(made, "a goal made while the world runs is not in the frame");
    CHECK(same() && std::memcmp(&put, &goal.position, sizeof put) == 0, "a goal moved while the world runs is drawn where it was");
}

// A box beyond the floor's edge falls: 100 m below the floor (RobotWorld::kLostDepth) it leaves the engine - it would
// fall for ever, awake, stepped with every substep - and the scene keeps it where it was last seen; the box on the floor
// and a fixed one far below stay. The frame lists the contact points only when asked (Simulation::frameContacts) - a
// pass over every body no one may be reading -, and the channels read the same points either way.
void testSimLostBodiesLeaveTheWorld() {
    SceneGraph g;
    g.world.physics.substeps = 4;
    g.entities.push_back(solid(1, "На полу", Vector3(0.2_r), {0, 0.1_r, 0}, false));
    g.entities.push_back(solid(2, "За краем", Vector3(0.2_r), {200, -60, 0}, false)); // the floor's slab ends 52 m out
    g.entities.push_back(solid(3, "Опора внизу", Vector3(0.2_r), {0, -300, 0}, true));
    Simulation s;
    s.load(g);
    s.channels.scene = true;
    const int rests = s.robot()->bodyOf(1), falls = s.robot()->bodyOf(2), fixed = s.robot()->bodyOf(3);
    int left = -1;
    Real last = 0;
    for (int k = 0; k < 400 && left < 0; ++k) {
        if (s.physics()->alive(falls)) last = s.physics()->state(falls).position.y;
        s.stepFrame();
        if (!s.physics()->alive(falls)) left = k + 1;
    }
    const Real kept = s.robot()->live().entities[1].position.y;
    std::printf("  a box let go 60 m below the floor, beyond its edge: gone from the engine after %d frames, last stepped at y = %.1f m; "
                "the scene keeps it at %.1f m; the box on the floor %s, the fixed one 300 m below %s\n", left, double(last), double(kept),
                s.physics()->alive(rests) ? "stays" : "gone", s.physics()->alive(fixed) ? "stays" : "gone");
    CHECK(left > 0 && kept < -RobotWorld::kLostDepth && kept > -RobotWorld::kLostDepth - 60, "the falling box left after %d frames, kept at %f", left, double(kept));
    CHECK(s.physics()->alive(rests) && s.physics()->alive(fixed), "a body that did not fall out of the world was taken out of it");
    Frame with, without;
    s.fill(with);
    s.frameContacts = false;
    s.stepFrame();
    s.fill(without);
    double deepest = -1;
    for (const Measurement& m : s.measurements())
        if (m.info.id == "rigid/deepest penetration") deepest = m.value;
    std::printf("  the frame: %zu contact points listed when asked, %zu when not; the deepest contact is measured all the same: %.2e m\n",
                with.contacts.size(), without.contacts.size(), deepest);
    CHECK(with.contacts.size() >= 3 && without.contacts.empty(), "the frame lists %zu contacts when asked and %zu when not", with.contacts.size(),
          without.contacts.size());
    CHECK(deepest >= 0, "the scene's deepest contact is not measured without the frame's contacts");
    CHECK(without.bodies.size() == 3, "the frame holds %zu bodies: the one that left is still drawn where it was", without.bodies.size());
}

// A world of bodies alone - nothing of ours to do between the engine's substeps - is one step of the engine a frame, the
// contacts searched once (RobotWorld::engineSteps); a world with a servo is a step a substep, as the servo's explicit
// spring needs. Either way a resting box's contacts carry its weight: the channel divides the impulse by the engine
// step's own length.
void testSimBodiesAloneAreOneEngineStep() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.1_r, 0}, false));
    Simulation s;
    s.load(g);
    s.channels.objects.push_back({1, "Ящик"});
    for (int k = 0; k < 15; ++k) s.stepFrame(); // at rest and still awake: asleep, the channel says m g without the contacts
    double weight = 0;
    for (const Measurement& m : s.measurements())
        if (m.info.id == "Ящик/contact force") weight = m.value;
    const BodyState box = s.physics()->state(s.robot()->bodyOf(1));
    const Real mass = box.mass;
    JointDrive d;
    d.enabled = true, d.stiffness = 60, d.damping = 4, d.target = 0.5_r, d.maxForce = 100;
    Simulation arm;
    arm.load(armOnPost(d));
    const int substeps = g.world.physics.substeps;
    std::printf("  %d substeps a frame: a box alone - %d step(s) of the engine, a servo's arm - %d; the box's contacts carry %.3f N (m g = %.3f)\n",
                substeps, s.robot()->engineSteps(substeps), arm.robot()->engineSteps(substeps), weight, double(mass) * 9.81);
    CHECK(s.robot()->engineSteps(substeps) == 1 && arm.robot()->engineSteps(substeps) == substeps, "the engine steps %d and %d times a frame",
          s.robot()->engineSteps(substeps), arm.robot()->engineSteps(substeps));
    CHECK(box.awake && std::fabs(weight / (double(mass) * 9.81) - 1) < 0.02, "the contacts of the box (awake: %d) carry %f N, not the weight %f N",
          int(box.awake), weight, double(mass) * 9.81);
}

// A kinematic body goes as its velocity says: gravity does not take it, the box in its way does not stop it - it
// pushes the box; a body that is fixed as well stays static. In the file the word is written only when set, so a
// scene without such a body is read by an editor that does not know the word.
void testSimKinematicBodyLeads() {
    SceneGraph g;
    Entity lift = solid(1, "Толкатель", Vector3(0.2_r), {0, 0.1_r, 0}, false); // on the floor, as the box it will meet
    lift.rigid.kinematic = true, lift.rigid.velocity = {1, 0, 0};
    Entity held = solid(3, "Опора", Vector3(0.2_r), {0, 0.5_r, 2}, true);
    held.rigid.kinematic = true, held.rigid.velocity = {1, 0, 0};
    g.entities.push_back(lift);
    g.entities.push_back(solid(2, "Ящик", Vector3(0.2_r), {0.5_r, 0.1_r, 0}, false));
    g.entities.push_back(held);
    Entity hung = solid(4, "Полка", Vector3(0.2_r), {0, 1.5_r, -2}, false);
    hung.rigid.kinematic = true;
    g.entities.push_back(hung);
    Simulation s;
    s.load(g);
    s.startRecording();
    for (int k = 0; k < 60; ++k) s.stepFrame();
    CHECK(std::fabs(s.physics()->state(s.robot()->bodyOf(4)).position.y - 1.5_r) < 1e-5_r, "gravity does not take a kinematic body hung in the air");
    // What rides on it and what is recorded read the world's own scene: the body is there where the engine has it.
    const Entity* shown = findEntity(s.robot()->live(), 1);
    CHECK(shown && std::fabs(shown->position.x - 1) < 0.01_r, "the world's scene does not follow a kinematic body: x %f (the engine has it at 1)",
          shown ? double(shown->position.x) : -1.0);
    CHECK(s.robot()->isFree(1) && !s.robot()->isFree(3) && !s.robot()->stands(s.robot()->bodyOf(1)) && s.robot()->stands(s.robot()->bodyOf(3)),
          "a kinematic body moves freely and does not stand; one that is fixed too stands");
    SequenceObject take;
    const bool taken = s.takeRecording("Запись", take);
    const bool tracked = std::any_of(take.tracks.begin(), take.tracks.end(), [](const auto& t) { return t.object == 1; });
    CHECK(taken && tracked, "a take has no track of a kinematic body that moved (%zu tracks)", take.tracks.size());
    const BodyState pusher = s.physics()->state(s.robot()->bodyOf(1)), box = s.physics()->state(s.robot()->bodyOf(2)),
                    post = s.physics()->state(s.robot()->bodyOf(3));
    std::printf("  after 1 s: the kinematic body at x %.3f y %.3f (mass %.1f), the box it met at x %.3f, the fixed one at x %.3f\n",
                double(pusher.position.x), double(pusher.position.y), double(pusher.mass), double(box.position.x), double(post.position.x));
    CHECK(std::fabs(pusher.position.x - 1) < 0.01_r && std::fabs(pusher.position.y - 0.1_r) < 1e-4_r && pusher.mass == 0,
          "a kinematic body goes 1 m in a second at 1 m/s at its own height: x %f y %f", double(pusher.position.x), double(pusher.position.y));
    CHECK(box.position.x > 1.1_r, "the box in its way is pushed ahead of it: x %f", double(box.position.x));
    CHECK(std::fabs(post.position.x) < 1e-5_r, "fixed wins: a body that is both stays where it stands");
    SceneGraph back;
    std::string error;
    const std::string text = g.save();
    CHECK(back.load(text, error) && back.entities[0].rigid.kinematic && !back.entities[1].rigid.kinematic && back.save() == text,
          "the kinematic word comes back from the file: %s", error.c_str());
    CHECK(text.find("kinematic 1") != std::string::npos && SceneGraph().save().find("kinematic") == std::string::npos,
          "the word is written for a kinematic body only");
}

// A body's mass, centre of mass and inertia given in the scene instead of what its shape and density make: the
// engine has them; a mass alone scales the shape's own inertia; a centre of mass put beyond the base tips the box
// over; the file keeps the words and refuses a tensor no body can have.
void testSimGivenMass() {
    SceneGraph g;
    Entity heavy = solid(1, "Гиря", Vector3(0.2_r), {0, 2, 0}, false); // a 0.2 m cube: 4 kg at 500 kg/m^3
    heavy.rigid.mass = 10;
    Entity wheel = solid(2, "Маховик", Vector3(0.2_r), {1, 2, 0}, false);
    wheel.rigid.mass = 3, wheel.rigid.inertiaGiven = true, wheel.rigid.inertiaDiagonal = {0.5_r, 0.7_r, 0.9_r};
    Entity tipped = solid(3, "Неваляшка наоборот", Vector3(0.2_r), {2, 0.1_r, 0}, false);
    tipped.rigid.centerGiven = true, tipped.rigid.centerOfMass = {0.3_r, 0, 0}; // 0.2 m out past the edge of its base
    g.entities.push_back(heavy), g.entities.push_back(wheel), g.entities.push_back(tipped);
    g.entities.push_back(solid(4, "Ящик", Vector3(0.2_r), {3, 0.1_r, 0}, false));
    Simulation s;
    s.load(g);
    const BodyState a = s.physics()->state(s.robot()->bodyOf(1)), b = s.physics()->state(s.robot()->bodyOf(2));
    const Real cube = 10 * 0.2_r * 0.2_r / 6; // m s^2 / 6 about each axis
    std::printf("  given 10 kg: mass %.3f kg, inertia %.4f (a 10 kg cube: %.4f); given 3 kg and 0.5 0.7 0.9: %.2f kg, %.2f %.2f %.2f\n", double(a.mass),
                double(a.inertia.m[0][0]), double(cube), double(b.mass), double(b.inertia.m[0][0]), double(b.inertia.m[1][1]), double(b.inertia.m[2][2]));
    CHECK(std::fabs(a.mass - 10) < 1e-4_r && std::fabs(a.inertia.m[0][0] / cube - 1) < 0.01_r, "a given mass is the body's, the shape's inertia scaled with it");
    CHECK(std::fabs(b.mass - 3) < 1e-4_r && std::fabs(b.inertia.m[0][0] - 0.5_r) < 1e-4_r && std::fabs(b.inertia.m[1][1] - 0.7_r) < 1e-4_r &&
          std::fabs(b.inertia.m[2][2] - 0.9_r) < 1e-4_r, "a given inertia is the body's");
    for (int k = 0; k < 60; ++k) s.stepFrame();
    const BodyState over = s.physics()->state(s.robot()->bodyOf(3)), still = s.physics()->state(s.robot()->bodyOf(4));
    std::printf("  after 1 s: the box whose centre of mass hangs past its base has turned (w %.3f), the plain one stands (w %.4f)\n",
                double(over.rotation.w), double(still.rotation.w));
    CHECK(std::fabs(over.rotation.w) < 0.95_r && std::fabs(still.rotation.w) > 0.9999_r, "a centre of mass beyond the base tips the box over; a plain box stands");
    SceneGraph back;
    std::string error;
    const std::string text = g.save();
    CHECK(back.load(text, error) && back.save() == text && back.entities[0].rigid.mass == 10 && back.entities[1].rigid.inertiaGiven &&
          back.entities[2].rigid.centerGiven && !back.entities[3].rigid.centerGiven, "mass, center_of_mass and inertia come back from the file: %s", error.c_str());
    CHECK(text.find("mass 10") != std::string::npos && text.find("center_of_mass 0.3 0 0") != std::string::npos &&
          text.find("inertia 0.5 0.7 0.9 0 0 0") != std::string::npos && SceneGraph().save().find("mass") == std::string::npos,
          "the words are written for the bodies that have them only");
    const std::string head = "entity \"X\"\n  object id 9\n  rigid density 500 ";
    CHECK(!back.load(head + "inertia 1 0.1 0.1 0 0 0\nend\n", error) && !back.load(head + "inertia 0.5 0.7 -0.9 0 0 0\nend\n", error) &&
          !back.load(head + "mass -2\nend\n", error), "an inertia no body can have, and a mass below zero, are refused");
}

// A body's own share of gravity and its own damping, apart from the world's: with gravity_scale 0 it floats, with 0.5
// it falls half as far; with a damping of its own it loses the speed the world's would leave it. The file keeps the
// words for the bodies that have them only.
void testSimBodyOwnGravityAndDamping() {
    SceneGraph g;
    g.world.physics.linearDamping = 0;
    Entity plain = solid(1, "Обычное", Vector3(0.2_r), {0, 5, 0}, false);
    Entity half = solid(2, "Вполовину", Vector3(0.2_r), {1, 5, 0}, false);
    half.rigid.gravityScale = 0.5_r;
    Entity floats = solid(3, "Парит", Vector3(0.2_r), {2, 5, 0}, false);
    floats.rigid.gravityScale = 0;
    Entity glider = solid(4, "Скользит", Vector3(0.2_r), {3, 5, 0}, false);
    glider.rigid.gravityScale = 0, glider.rigid.velocity = {0, 0, 2};
    Entity slowed = solid(5, "Вязнет", Vector3(0.2_r), {4, 5, 0}, false);
    slowed.rigid.gravityScale = 0, slowed.rigid.velocity = {0, 0, 2}, slowed.rigid.linearDamping = 2;
    for (const Entity& e : {plain, half, floats, glider, slowed}) g.entities.push_back(e);
    Simulation s;
    s.load(g);
    for (int k = 0; k < 30; ++k) s.stepFrame(); // half a second: g t^2 / 2 = 1.226 m
    const auto at = [&s](uint32_t id) { return s.physics()->state(s.robot()->bodyOf(id)); };
    const Real fell = 5 - at(1).position.y, halved = 5 - at(2).position.y;
    std::printf("  in 0.5 s: fell %.3f m, with gravity_scale 0.5 %.3f m, with 0 %.4f m; at 2 m/s the free one went %.3f m, the damped one %.3f m (speed %.3f)\n",
                double(fell), double(halved), double(5 - at(3).position.y), double(at(4).position.z), double(at(5).position.z), double(at(5).velocity.z));
    CHECK(std::fabs(fell - 1.226_r) < 0.06_r && std::fabs(halved / fell - 0.5_r) < 0.02_r && std::fabs(5 - at(3).position.y) < 1e-4_r,
          "gravity_scale 0.5 halves the fall, 0 stops it");
    CHECK(std::fabs(at(4).position.z - 1) < 0.02_r && at(5).position.z < 0.7_r && at(5).velocity.z < 0.9_r && at(5).velocity.z > 0.5_r,
          "a damping of its own slows the body: 2 m/s after 0.5 s at 2 1/s is about 0.74 m/s");
    SceneGraph back;
    std::string error;
    const std::string text = g.save();
    CHECK(back.load(text, error) && back.save() == text && back.entities[1].rigid.gravityScale == 0.5_r && back.entities[4].rigid.linearDamping == 2 &&
          back.entities[0].rigid.linearDamping < 0, "gravity_scale and the damping come back from the file: %s", error.c_str());
    CHECK(text.find("gravity_scale 0.5") != std::string::npos && text.find("linear_damping 2") != std::string::npos &&
          SceneGraph().save().find("gravity_scale") == std::string::npos, "the words are written for the bodies that have them only");
}

// Who touches whom, by tags: a body whose collider ignores a tag falls through the table marked with it, and one that
// ignores nothing lands on the same kind of table. A trigger collider touches nothing and tells what is inside it:
// the ball falls through the zone, and the zone names the ball while it is there - and not the one that never came.
void testSimIgnoresAndTriggers() {
    SceneGraph g;
    Entity table = solid(1, "Стол-призрак", Vector3(0.5_r), {0, 0.25_r, 0}, true);
    table.tags = {"призрак"};
    Entity through = solid(2, "Сквозь", Vector3(0.2_r), {0, 1, 0}, false);
    through.collider.ignores = {"призрак"};
    Entity zone = solid(5, "Зона", Vector3(0.6_r), {4, 0.3_r, 0}, true);
    zone.rigid.enabled = false, zone.collider.trigger = true; // a collider alone: it stands, and is a zone
    g.entities.push_back(table), g.entities.push_back(through);
    g.entities.push_back(solid(3, "Стол", Vector3(0.5_r), {2, 0.25_r, 0}, true));
    g.entities.push_back(solid(4, "На стол", Vector3(0.2_r), {2, 1, 0}, false));
    g.entities.push_back(zone);
    g.entities.push_back(solid(6, "В зону", Vector3(0.2_r), {4, 1.5_r, 0}, false));
    g.entities.push_back(solid(7, "Мимо", Vector3(0.2_r), {6, 1.5_r, 0}, false));
    g.entities.push_back(solid(8, "Плита под зоной", {1, 0.1_r, 1}, {4, 0.05_r, 0}, true)); // it stands in the zone that stands: not news
    Simulation s;
    s.load(g);
    const auto inside = [&s] {
        const auto zones = s.robot()->zones();
        return zones.size() == 1 && zones[0].first == 5 ? zones[0].second : std::vector<uint32_t>{999};
    };
    s.stepFrame();
    const std::vector<uint32_t> early = inside();
    for (int k = 0; k < 120; ++k) s.stepFrame();
    const auto y = [&s](uint32_t id) { return double(s.physics()->state(s.robot()->bodyOf(id)).position.y); };
    const std::vector<uint32_t> late = inside();
    std::printf("  after 2 s: the body that ignores the table's tag at y %.3f (the floor), the plain one at y %.3f (on its table); the ball fell through "
                "the zone to y %.3f; inside the zone at first %zu, then %zu (entity %u)\n", y(2), y(4), y(6), early.size(), late.size(),
                late.empty() ? 0u : late[0]);
    CHECK(std::fabs(y(2) - 0.1) < 0.02 && std::fabs(y(4) - 0.6) < 0.02, "the body that ignores the tag falls through the table: y %f; the plain one lands on its: y %f", y(2), y(4));
    CHECK(std::fabs(y(6) - 0.2) < 0.02, "a trigger touches nothing: the ball lies on the slab inside it, y %f", y(6));
    CHECK(early.empty() && late == std::vector<uint32_t>{6}, "the zone names what is inside it - nothing while the ball is above, the ball once it is in");
    SceneGraph back;
    std::string error;
    const std::string text = g.save();
    CHECK(back.load(text, error) && back.save() == text && back.entities[1].collider.ignores == std::vector<std::string>{"призрак"} &&
          back.entities[4].collider.trigger && !back.entities[0].collider.trigger, "ignores and trigger come back from the file: %s", error.c_str());
    CHECK(text.find("ignores призрак") != std::string::npos && text.find("trigger 1") != std::string::npos &&
          SceneGraph().save().find("ignores") == std::string::npos, "the words are written for the colliders that have them only");

    // A row of an array falls into a zone: its copies are no objects of their own - the zone names the array, once.
    SceneGraph rows;
    Entity bin = zone;
    bin.id = 1, bin.position = {0.3_r, 0.3_r, 0}, bin.size = {2, 0.6_r, 0.6_r};
    rows.entities.push_back(bin);
    rows.entities.push_back(solid(2, "Кирпич", Vector3(0.1_r), {0, 1, 0}, false));
    Value row = Value::map(), ids = Value::list(), step = Value::list(), made = Value::map();
    ids.push(2), step.push(0.3), step.push(0), step.push(0);
    row.set("ids", ids), row.set("count", 3), row.set("step", step);
    CHECK(findSceneOp("scene_array")->edit(rows, row, made, error) && rows.arrays.size() == 1, "a row of three bricks: %s", error.c_str());
    Simulation fall;
    fall.load(rows);
    for (int k = 0; k < 120; ++k) fall.stepFrame();
    const auto named = fall.robot()->zones();
    std::printf("  three copies of an array in a zone: %zu zone(s), %zu name(s) inside\n", named.size(), named.empty() ? size_t(0) : named[0].second.size());
    CHECK(named.size() == 1 && named[0].first == 1 && named[0].second == std::vector<uint32_t>{rows.arrays[0].id},
          "a zone names the array whose copies are inside it, once, not the copies");

    // A row of zones is one zone, the array: a ball in its second copy is inside the array.
    SceneGraph bins;
    Entity cell = zone;
    cell.id = 1, cell.position = {0, 0.3_r, 0}, cell.size = Vector3(0.6_r);
    bins.entities.push_back(cell);
    bins.entities.push_back(solid(2, "Шар", Vector3(0.2_r), {1, 1.5_r, 0}, false)); // over the second copy, a metre along
    Value cells = Value::map(), cellIds = Value::list(), apart = Value::list(), rowOf = Value::map();
    cellIds.push(1), apart.push(1), apart.push(0), apart.push(0);
    cells.set("ids", cellIds), cells.set("count", 2), cells.set("step", apart);
    CHECK(findSceneOp("scene_array")->edit(bins, cells, rowOf, error) && bins.arrays.size() == 1, "a row of two zones: %s", error.c_str());
    Simulation drop;
    drop.load(bins);
    for (int k = 0; k < 120; ++k) drop.stepFrame();
    const auto cellsNow = drop.robot()->zones();
    CHECK(cellsNow.size() == 1 && cellsNow[0].first == bins.arrays[0].id && cellsNow[0].second == std::vector<uint32_t>{2},
          "a row of zones is not one zone under its array's id with the ball inside (%zu zones)", cellsNow.size());
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

    // The same rod given its real weight: the servo is tuned to those kilograms, not to the shape's density.
    SceneGraph heavy;
    rod.rigid.enabled = true, rod.rigid.mass = 5;
    heavy.entities = {post, rod};
    Value tuned = Value::map();
    CHECK(findSceneOp("assembly_actuator")->edit(heavy, args, tuned, error), "the actuator on a rod of a given mass: %s", error.c_str());
    Simulation h;
    h.load(heavy);
    for (int k = 0; k < 120; ++k) h.stepFrame();
    Real hx[6] = {};
    h.robot()->jointCoordinates(*h.physics(), uint32_t(tuned["id"].number()), hx);
    const double sag = std::fabs(double(hx[dof])) * 180 / 3.141592653589793;
    std::printf("  the same rod given 5 kg: k %.1f, droop %.3f deg\n", tuned["stiffness"].number(), sag);
    CHECK(tuned["stiffness"].number() > 5 * answer["stiffness"].number() && sag < 1.2,
          "a rod given 5 kg droops %f deg under a servo of k %f", sag, tuned["stiffness"].number());

    // Its centre of mass given far out on the rod: the servo is tuned to that arm of the weight, not to the shape's middle.
    SceneGraph tipped;
    rod.rigid.centerGiven = true, rod.rigid.centerOfMass = {0.2_r, 0, 0};
    tipped.entities = {post, rod};
    Value outer = Value::map();
    CHECK(findSceneOp("assembly_actuator")->edit(tipped, args, outer, error), "the actuator on a rod of a given centre of mass: %s", error.c_str());
    Simulation t;
    t.load(tipped);
    for (int k = 0; k < 120; ++k) t.stepFrame();
    Real tx[6] = {};
    t.robot()->jointCoordinates(*t.physics(), uint32_t(outer["id"].number()), tx);
    const double tip = std::fabs(double(tx[dof])) * 180 / 3.141592653589793;
    std::printf("  its centre of mass 0.2 m further out: k %.1f, droop %.3f deg\n", outer["stiffness"].number(), tip);
    CHECK(outer["stiffness"].number() > 1.4 * tuned["stiffness"].number() && tip < 1.2,
          "a rod whose centre of mass is given further out droops %f deg under a servo of k %f", tip, outer["stiffness"].number());
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

// scene_collider holds the collider as numbers: a 0.2 x 0.4 x 0.6 m box's fitted collider becomes a sphere round its
// largest edge (radius 0.3 m), a capsule as wide as the larger of x and z (0.6 m) and as tall as y (0.4 m: no straight
// part left, half height 0), a box with its offset and turn kept; each kind refuses the other kinds' numbers, naming its
// own, and the engine's shape (colliderShapes) is exactly the numbers.
void testSceneColliderNumbers() {
    SceneGraph g;
    Entity box;
    box.id = 1, box.name = "Брус", box.shape = ShapeKind::Box, box.size = Vector3(0.2_r, 0.4_r, 0.6_r);
    box.collider.enabled = true, box.collider.kind = ColliderKind::Box; // fitted to the geometry
    g.entities.push_back(box);
    std::string error;
    Value answer;
    const auto run = [&](Value a) { a.set("id", 1); answer = Value::map(); return findSceneOp("scene_collider")->edit(g, a, answer, error); };
    const auto shape = [&] { return colliderShapes(g.entities[0], entityLocalMesh(g.entities[0])).front(); };
    Value sphere = Value::map();
    sphere.set("kind", "sphere");
    const bool round = run(sphere) && shape().kind == ShapeDesc::Kind::Sphere && std::fabs(shape().radius - 0.3_r) < 1e-6_r &&
                       !g.entities[0].collider.fitToGeometry;
    Value capsule = Value::map();
    capsule.set("kind", "capsule");
    const bool capsuled = run(capsule) && shape().kind == ShapeDesc::Kind::Capsule && std::fabs(shape().radius - 0.3_r) < 1e-6_r;
    Value taller = Value::map();
    taller.set("height", 1.0), taller.set("diameter", 0.1);
    const bool tall = run(taller) && std::fabs(shape().radius - 0.05_r) < 1e-6_r && std::fabs(shape().halfHeight - 0.45_r) < 1e-6_r &&
                      answer["height"].number() == 1.0;
    Value wrong = Value::map(), offset = Value::list(), size = Value::list();
    for (Real v : {0.0_r, 0.1_r, 0.0_r}) offset.push(v);
    for (Real v : {0.3_r, 0.2_r, 0.1_r}) size.push(v);
    wrong.set("size", size);
    const bool sizeRefused = !run(wrong) && error.find("diameter and height") != std::string::npos;
    Value boxed = Value::map();
    boxed.set("kind", "box"), boxed.set("size", size), boxed.set("offset", offset);
    const bool boxy = run(boxed) && shape().kind == ShapeDesc::Kind::Box && length(shape().halfExtents - Vector3(0.15_r, 0.1_r, 0.05_r)) < 1e-6_r &&
                      length(shape().position - Vector3(0, 0.1_r, 0)) < 1e-6_r;
    Value diameter = Value::map();
    diameter.set("diameter", 0.2);
    const bool diameterRefused = !run(diameter) && error.find("size [x, y, z]") != std::string::npos;
    std::printf("  a fitted 0.2 x 0.4 x 0.6 m collider: a sphere %s, a capsule %s, 0.1 x 1 m %s; size refused on a capsule %s, "
                "a box with size and offset %s, diameter refused on a box %s (%s)\n", round ? "r 0.3" : "no", capsuled ? "r 0.3" : "no",
                tall ? "r 0.05 h/2 0.45" : "no", sizeRefused ? "yes" : "no", boxy ? "yes" : "no", diameterRefused ? "yes" : "no", error.c_str());
    CHECK(round && capsuled && tall, "a kind's own numbers from the fitted box, then given");
    CHECK(sizeRefused && boxy && diameterRefused, "each kind takes its own numbers and names them when given another's");
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
// A lidar in a running world. A fixed box with a wall turned 45 degrees ahead of its +x face: the sonar on that face
// hears nothing (its mirror blind spot), the lidar's one beam reads the wall at 0.829 m (the face plane 0.05 m before
// the wall's middle at 1 m, along the beam 0.9 - 0.05 / cos 45). A ring of 4 rays over a full turn on the same face:
// back to a wall 1.05 m away, right (+z, its z the world's up) to one 0.95 m away, ahead the turned wall, left nothing.
// 10 scans a second: 10 or 11 scans in a second of the world, the last one taken in its last tenth.
void testSimLidar() {
    SceneGraph g;
    g.world.physics.sleeping = false;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 0.1_r, 0}, true));
    g.entities.push_back(solid(2, "Косая стена", {0.1_r, 1, 2}, {1, 0.5_r, 0}, true));
    g.entities.back().rotationDeg = {0, 45, 0};
    g.entities.push_back(solid(3, "Стена сзади", {0.1_r, 1, 2}, {-1, 0.5_r, 0}, true));
    g.entities.push_back(solid(4, "Стена справа", {2, 1, 0.1_r}, {0, 0.5_r, 1}, true));
    const auto put = [&g](SensorKind kind, int rays) {
        const uint32_t id = attachSensor(g, 1, snapToFace(g, 1, BoxFace::PlusX, Vector3(0.0_r)), kind);
        SensorObject& s = g.sensors.back();
        s.noise = false;
        if (kind == SensorKind::Lidar) s.spec = {{"rays_h", Real(rays)}, {"fov_h_deg", rays > 1 ? 360.0_r : 0.0_r}, {"rays_v", 1}, {"fov_v_deg", 0}, {"rate_hz", 10}};
        return id;
    };
    const uint32_t sonar = put(SensorKind::Sonar, 0), beam = put(SensorKind::Lidar, 1), ring = put(SensorKind::Lidar, 4);
    Simulation s;
    s.load(g);
    for (int k = 0; k < 60; ++k) s.stepFrame();
    const RobotWorld& w = *s.robot();
    const SensorReading& b = w.sensor(beam)->reading();
    const std::vector<Real>& r = w.sensor(ring)->reading().ranges;
    const Real slanted = 0.9_r - 0.05_r / Real(std::sqrt(0.5));
    std::printf("  lidar: sonar before a 45° wall %.3f (none: -1); one beam %.4f m (%.4f); a ring of 4: %.3f %.3f %.3f %.3f m (1.05 0.95 %.3f -1); "
                "%lld scans in 1 s, the last at %.3f s\n", double(w.sensor(sonar)->truth().value), b.ranges.empty() ? -9.0 : double(b.ranges[0]), double(slanted),
                r.size() > 3 ? double(r[0]) : -9.0, r.size() > 3 ? double(r[1]) : -9.0, r.size() > 3 ? double(r[2]) : -9.0, r.size() > 3 ? double(r[3]) : -9.0,
                double(slanted), static_cast<long long>(b.count), double(b.sampleNs) * 1e-9);
    CHECK(w.sensor(sonar)->truth().value < 0, "the sonar hears a wall turned 45 degrees from its beam");
    CHECK(b.ranges.size() == 1 && std::fabs(b.ranges[0] - slanted) < 2e-3_r, "the lidar's beam reads the turned wall at %f m, not %f", b.ranges.empty() ? -1.0 : double(b.ranges[0]), double(slanted));
    CHECK(r.size() == 4 && std::fabs(r[0] - 1.05_r) < 2e-3_r && std::fabs(r[1] - 0.95_r) < 2e-3_r && std::fabs(r[2] - slanted) < 2e-3_r && r[3] == -1,
          "the ring of 4 does not read back, right, ahead and nothing");
    CHECK(b.count >= 10 && b.count <= 11 && b.sampleNs > 900000000, "%lld scans in a second at 10 a second", static_cast<long long>(b.count));
}

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

    // The same run with the take's start at 0.5 s (record_from): the world runs from 0, the take keeps 0.5..1.5 s only,
    // its keys at the world's time - where the box was then.
    g.sequences.pop_back(); // physics again, not the replay
    Simulation late;
    late.load(g);
    late.startRecording(0.5_r);
    while (late.time() < 1.5_r - 1e-6_r) late.stepFrame();
    SequenceObject window;
    const bool got = late.takeRecording("Запись 2", window) && !window.tracks.empty();
    const std::vector<AnimKey>& from = got ? window.tracks[0].keys : truth;
    Vector3 p;
    Quaternion q;
    samplePose(from, from.front().time, false, 0, p, q);
    const Real at = from.front().time, off = length(p - truth[30].position); // frame 30: 0.5 s
    std::printf("  the same run, the take from 0.5 s: its keys %.3f..%.3f s, the first %.3f mm off the run at 0.5 s\n", double(at),
                double(from.back().time), double(off) * 1e3);
    CHECK(got && std::fabs(at - 0.5_r) < 1e-3_r && std::fabs(from.back().time - 1.5_r) < 1e-3_r && off < 0.0005_r,
          "a take from record_from keeps that window only, its keys at the world's time");
}

// A camera on keys while a box falls: the frame says where the world has the camera (the scene as written does not), a
// camera nothing moves is not listed, and the take brings the camera's track along with the box's.
void testSimKeyedCamera() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 1, 0}, false));
    Camera keyed, still;
    keyed.id = 2, keyed.position = {5, 1, 0};
    still.id = 3, still.position = {0, 1, 5};
    g.cameras = {keyed, still};
    SequenceObject q;
    q.id = 4, q.active = true;
    q.tracks.push_back({2, {{0, {5, 1, 0}, Vector3(0.0_r)}, {2, {5, 1, 4}, Vector3(0.0_r)}}});
    g.sequences.push_back(q);
    Simulation s;
    s.load(g);
    s.startRecording();
    while (s.time() < 1.0_r - 1e-6_r) s.stepFrame();
    Frame frame;
    s.fill(frame);
    const bool listed = frame.cameras.size() == 1 && frame.cameras[0].id == 2;
    const Real z = listed ? frame.cameras[0].position.z : -1;
    SequenceObject take;
    const bool taken = s.takeRecording("Запись 1", take);
    const SequenceTrack* camera = trackOf(take, 2);
    std::printf("  a camera keyed 0..4 m over 2 s, a box falling: after 1 s the frame lists %zu camera(s), the keyed one at z %.3f m; the take: %zu "
                "tracks, the camera's with %zu keys\n", frame.cameras.size(), double(z), take.tracks.size(), camera ? camera->keys.size() : size_t(0));
    CHECK(listed && std::fabs(z - 2) < 0.05_r, "the frame has the keyed camera halfway, z %f, and not the one nothing moves", double(z));
    CHECK(taken && take.tracks.size() == 2 && trackOf(take, 1) && camera && camera->keys.size() == 2 && !take.play,
          "the take holds the box's run and the camera's keys");
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

namespace {

JointDrive holding() { // a servo of our code: the world steps the engine a substep at a time
    JointDrive d;
    d.enabled = true, d.stiffness = 60, d.damping = 4, d.maxForce = 100;
    return d;
}

SceneGraph calm() {
    SceneGraph g;
    g.world.gravity = Vector3(0.0_r);
    return g;
}

Force forceOf(uint32_t id, const Vector3& at, ForceShape shape, ForceMode mode, Real strength, Real radius = 0) {
    Force f;
    f.id = id, f.name = "Сила", f.position = at, f.shape = shape, f.mode = mode, f.strength = strength, f.radius = radius;
    return f;
}

Real massOf(const Simulation& s, uint32_t id) { return s.physics()->state(s.robot()->bodyOf(id)).mass; }
Vector3 velocityOf(const Simulation& s, uint32_t id) { return s.physics()->state(s.robot()->bodyOf(id)).velocity; }
// The momentum of a body, N s: what a body's own force gives it.
Vector3 momentumOf(const Simulation& s, uint32_t id) { return velocityOf(s, id) * massOf(s, id); }
bool near(const Vector3& a, const Vector3& b, Real tol = 1e-4_r) { return length(a - b) <= tol * (1 + length(b)); }

// One box at `at` and one field, `frames` frames: the box's velocity - what a field gives, whatever the box weighs.
Vector3 kicked(const Force& f, const Vector3& at, int frames = 1) {
    SceneGraph g = calm();
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
    g.forces.push_back(f);
    Simulation s;
    s.load(g);
    for (int k = 0; k < frames; ++k) s.stepFrame();
    return velocityOf(s, 1);
}

} // namespace

// A force on rigid bodies, against exact answers: the push along the force's own +Y axis or away from its origin, the
// falloff's weight and the reach's edge, the tag filter, who is never touched, when an impulse fires and the button's
// part in it, a constant force the same at any number of engine steps, the pose of the last frame, live edits, and
// the same run twice.
void testSimForces() {
    const Vector3 up(0, 1, 0), at(0, 2, 0);
    const auto I = ForceMode::Impulse;
    const auto C = ForceMode::Constant;
    const auto D = ForceShape::Directional;
    const auto R = ForceShape::Radial;

    // Directional impulse: along +Y; turned 90 degrees about z along -X; a negative strength reverses; no spin.
    CHECK(near(kicked(forceOf(2, at, D, I, 5), at), up * 5), "a field push of 5 m/s along +Y gives the box 5 m/s, whatever it weighs");
    Force turned = forceOf(2, at, D, I, 5);
    turned.rotationDeg = Vector3(0, 0, 90);
    CHECK(near(kicked(turned, at), Vector3(-5, 0, 0)), "turned about z by 90 degrees it pushes along -X");
    CHECK(near(kicked(forceOf(2, at, D, I, -5), at), up * -5), "a negative strength pushes along -Y");

    // Radial: away from the origin, inward for a negative strength, nothing at the origin or beyond the radius.
    CHECK(near(kicked(forceOf(2, at, R, I, 6, 10), Vector3(3, 2, 0)), Vector3(6, 0, 0)), "a burst pushes away from its origin");
    CHECK(near(kicked(forceOf(2, at, R, I, -6, 10), Vector3(3, 2, 0)), Vector3(-6, 0, 0)), "a negative burst pulls in");
    CHECK(length(kicked(forceOf(2, at, R, I, 6, 10), at)) < 1e-6_r, "a body at the origin of a radial force has no direction: skipped");
    CHECK(length(kicked(forceOf(2, at, R, I, 6, 2), Vector3(3, 2, 0))) < 1e-6_r, "beyond the radius nothing");

    // Falloff at d = 4 of a radius of 10, strength 8.
    Force burst = forceOf(2, at, R, I, 8, 10);
    const Vector3 four(4, 2, 0);
    CHECK(near(kicked(burst, four), Vector3(8, 0, 0)), "no falloff: 8");
    burst.falloff = ForceFalloff::Linear;
    CHECK(near(kicked(burst, four), Vector3(4.8_r, 0, 0)), "linear: 8 (1 - 4/10)");
    burst.falloff = ForceFalloff::InverseSquare;
    CHECK(near(kicked(burst, four), Vector3(0.5_r, 0, 0)), "inverse square: 8 / 16");
    CHECK(near(kicked(burst, Vector3(0.5_r, 2, 0)), Vector3(8, 0, 0)), "inverse square is the strength itself within 1 m");
    Force line = forceOf(2, at, D, I, 8);
    line.falloff = ForceFalloff::Linear;
    CHECK(near(kicked(line, Vector3(0, 6, 0)), up * 8), "a directional force without a radius has no linear falloff");
    line.radius = 2;
    CHECK(length(kicked(line, Vector3(0, 5, 0))) < 1e-6_r && kicked(line, Vector3(0, 3, 0)).y > 1, "a directional force's radius limits its reach");

    // The edge of the reach, R = 3: the radius itself is inside; linear is continuous; inverse square steps.
    for (const ForceFalloff fall : {ForceFalloff::None, ForceFalloff::InverseSquare}) {
        Force edge = forceOf(2, at, R, I, 8, 3);
        edge.falloff = fall;
        CHECK(kicked(edge, Vector3(3 - 1e-3_r, 2, 0)).x > 0.1_r && kicked(edge, Vector3(3, 2, 0)).x > 0.1_r && kicked(edge, Vector3(3 + 1e-3_r, 2, 0)).x == 0,
              "falloff %d: acts at the radius and just inside, not just beyond", int(fall));
    }
    Force linear = forceOf(2, at, R, I, 8, 3);
    linear.falloff = ForceFalloff::Linear;
    CHECK(kicked(linear, Vector3(3 - 1e-3_r, 2, 0)).x < 0.01_r && kicked(linear, Vector3(3 + 1e-3_r, 2, 0)).x == 0, "linear falls to zero at the radius, no jump");

    // Tags: the force names one, an array's copies carry the array's tags; static and kinematic bodies are never touched.
    {
        SceneGraph g = calm();
        Entity crate = solid(1, "Ящик", Vector3(0.2_r), {0, 2, 0}, false);
        crate.tags = {"crate"};
        g.entities.push_back(crate);
        g.entities.push_back(solid(2, "Бочка", Vector3(0.2_r), {2, 2, 0}, false));
        g.entities.push_back(solid(3, "Стена", Vector3(0.2_r), {4, 2, 0}, true));
        Entity drifting = solid(4, "Плита", Vector3(0.2_r), {6, 2, 0}, false);
        drifting.rigid.kinematic = true;
        g.entities.push_back(drifting);
        g.forces.push_back(forceOf(10, {0, 0, 0}, D, I, 5));
        g.forces.back().tag = "crate";
        Simulation s;
        s.load(g);
        s.stepFrame();
        CHECK(near(velocityOf(s, 1), up * 5) && length(velocityOf(s, 2)) == 0, "the tagged body is pushed, the other is not");
        CHECK(length(velocityOf(s, 3)) == 0 && length(velocityOf(s, 4)) == 0, "a static or a kinematic body is never touched");
        g.forces[0].tag.clear();
        Simulation all;
        all.load(g);
        all.stepFrame();
        CHECK(near(velocityOf(all, 2), up * 5) && length(velocityOf(all, 3)) == 0 && length(velocityOf(all, 4)) == 0, "no tag: every dynamic body");
        g.forces[0].visible = false;
        Simulation hidden;
        hidden.load(g);
        hidden.stepFrame();
        CHECK(length(velocityOf(hidden, 1)) == 0 && length(velocityOf(hidden, 2)) == 0, "a hidden force does nothing");

        SceneGraph rows = calm();
        rows.entities.push_back(crate);
        rows.entities.push_back(solid(2, "Бочка", Vector3(0.2_r), {0, 4, 0}, false));
        Value row = Value::map(), ids = Value::list(), step = Value::list(), made = Value::map();
        std::string error;
        ids.push(1), step.push(1), step.push(0), step.push(0);
        row.set("ids", ids), row.set("count", 3), row.set("step", step);
        CHECK(findSceneOp("scene_array")->edit(rows, row, made, error), "a row of three crates: %s", error.c_str());
        rows.forces.push_back(forceOf(10, {0, 0, 0}, D, I, 5));
        rows.forces.back().tag = "crate";
        Simulation copies;
        copies.load(rows);
        copies.stepFrame();
        int moved = 0;
        for (const Entity& e : copies.robot()->built())
            if (const int b = copies.robot()->bodyOf(e.id); b >= 0 && length(copies.physics()->state(b).velocity) > 0) ++moved;
        CHECK(moved == 3 && length(velocityOf(copies, 2)) == 0, "the array's copies carry its tag: %d moved of the three crates", moved);

        // The tag on the array object itself (the template stays untagged) reaches the copies as well.
        rows.entities[0].tags.clear();
        rows.arrays[0].tags = {"crate"};
        Simulation arrayTag;
        arrayTag.load(rows);
        arrayTag.stepFrame();
        moved = 0;
        for (const Entity& e : arrayTag.robot()->built())
            if (const int b = arrayTag.robot()->bodyOf(e.id); b >= 0 && length(arrayTag.physics()->state(b).velocity) > 0) ++moved;
        CHECK(moved == 3 && length(velocityOf(arrayTag, 2)) == 0, "a tag on the array object reaches its copies: %d moved", moved);
    }

    // Sleeping bodies: an impulse takes effect at once and wakes; a constant force wakes; the neighbour of a woken one wakes.
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.entities.push_back(solid(2, "Сосед", Vector3(0.2_r), {0.2_r, 2, 0}, false));
        g.forces.push_back(forceOf(10, {-5, 2, 0}, D, I, 5));
        g.forces.back().start = 3600, g.forces.back().tag = "a";
        g.entities[0].tags = {"a"};
        g.forces.push_back(forceOf(11, {5, 5, 5}, D, C, 12));
        g.forces.back().tag = "none"; // no body has it: it only stands for a constant force to be refused
        Simulation s;
        s.load(g);
        int k = 0;
        while (s.physics()->state(s.robot()->bodyOf(1)).awake && ++k < 600) s.stepFrame();
        CHECK(!s.physics()->state(s.robot()->bodyOf(1)).awake, "the box fell asleep");
        std::string error;
        CHECK(s.robot()->fireForce(*s.physics(), 10, error), "fired: %s", error.c_str());
        CHECK(near(velocityOf(s, 1), up * 5) && s.physics()->state(s.robot()->bodyOf(1)).awake, "the impulse is in the velocity at once, the body awake");
        s.stepFrame();
        CHECK(s.physics()->state(s.robot()->bodyOf(1)).position.y > 2.0001_r, "and it has moved");
        CHECK(!s.robot()->fireForce(*s.physics(), 11, error) && error.find("constant") != std::string::npos, "a constant force is not fired: %s", error.c_str());
        CHECK(!s.robot()->fireForce(*s.physics(), 99, error) && error == "no force with id 99 in the scene", "no such force: %s", error.c_str());
    }
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.forces.push_back(forceOf(10, at, D, C, 12));
        Simulation s;
        s.load(g);
        s.stepFrame();
        CHECK(near(velocityOf(s, 1), up * (12 / 60.0_r), 1e-3_r), "a constant field of 12 m/s^2 for one frame gives 12 / 60 m/s");
    }
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.entities.push_back(solid(2, "Сосед", Vector3(0.2_r), {0.2_r, 2, 0}, false));
        g.entities[0].tags = {"a"};
        g.forces.push_back(forceOf(10, {-5, 2, 0}, D, I, 5));
        g.forces.back().start = 3600, g.forces.back().tag = "a";
        Simulation s;
        s.load(g);
        for (int k = 0; k < 600 && s.physics()->state(s.robot()->bodyOf(2)).awake; ++k) s.stepFrame();
        std::string error;
        s.robot()->fireForce(*s.physics(), 10, error);
        s.stepFrame();
        s.stepFrame();
        CHECK(s.physics()->state(s.robot()->bodyOf(2)).awake, "an impulse on one of two touching boxes wakes the other");
    }

    // The moment of an impulse, on the world's integer clock: in the frame whose start is at or after `start`, once.
    for (const bool servo : {false, true}) {
        SceneGraph g = servo ? armOnPost(holding()) : SceneGraph();
        g.world.gravity = Vector3(0.0_r);
        g.entities.push_back(solid(10, "Ящик", Vector3(0.2_r), {3, 3, 0}, false));
        g.forces.push_back(forceOf(20, {3, 3, 0}, D, I, 5));
        g.forces.back().start = 0.5_r;
        Simulation s;
        s.load(g);
        const int64_t due = std::llround(double(g.forces[0].start) * 1e9);
        const int64_t slack = servo ? std::llround(double(s.frameDt) * 1e9) / std::max(g.world.physics.substeps, 1) + 1 : 0;
        bool early = true, once = true;
        for (int k = 0; k < 60; ++k) {
            const int64_t clock = s.robot()->clockNs();
            s.stepFrame();
            const Real vy = velocityOf(s, 10).y;
            if (clock + slack + std::llround(double(s.frameDt) * 1e9) < due && vy != 0) early = false;
            if (clock >= due && std::fabs(vy - 5) > 1e-3_r) once = false;
        }
        CHECK(early && once, "an impulse starting at 0.5 s fires once, not before its moment (%s): v*m %f", servo ? "a servo in the scene" : "bodies alone", double(velocityOf(s, 10).y));
    }
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.forces.push_back(forceOf(10, at, D, I, 5));
        Simulation s;
        s.load(g);
        s.stepFrame();
        CHECK(near(velocityOf(s, 1), up * 5), "start 0 fires in the very first frame");
        g.forces[0].start = 0.5_r;
        Simulation pressed;
        pressed.load(g);
        for (int k = 0; k < 6; ++k) pressed.stepFrame();
        std::string error;
        CHECK(pressed.robot()->fireForce(*pressed.physics(), 10, error), "the button: %s", error.c_str());
        for (int k = 0; k < 54; ++k) pressed.stepFrame();
        CHECK(near(velocityOf(pressed, 1), up * 5), "the button cancels the timed firing: once, not twice (%f)", double(velocityOf(pressed, 1).y));
    }

    // A constant force is the same whatever the number of engine steps a frame is.
    for (const int substeps : {8, 32})
        for (const bool servo : {false, true}) {
            SceneGraph g = servo ? armOnPost(holding()) : SceneGraph();
            g.world.gravity = Vector3(0.0_r), g.world.physics.substeps = substeps;
            g.entities.push_back(solid(10, "Ящик", Vector3(0.2_r), {3, 3, 0}, false));
            g.forces.push_back(forceOf(20, {3, 3, 0}, D, C, 12));
            Simulation s;
            s.load(g);
            for (int k = 0; k < 60; ++k) s.stepFrame();
            const Real got = velocityOf(s, 10).y;
            CHECK(s.robot()->engineSteps(substeps) == (servo ? substeps : 1) && std::fabs(got - 12) < 12e-3_r,
                  "12 m/s^2 for 1 s: %f m/s with %d engine steps a frame, %d substeps", double(got), s.robot()->engineSteps(substeps), substeps);
        }
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.forces.push_back(forceOf(10, at, D, C, 12, 1.5_r)); // a 1.5 m reach: the box leaves it
        Simulation s;
        s.load(g);
        for (int k = 0; k < 200; ++k) s.stepFrame();
        const Real a = velocityOf(s, 1).y;
        for (int k = 0; k < 20; ++k) s.stepFrame();
        CHECK(a > 0 && velocityOf(s, 1).y == a, "a constant force stops when the box is beyond its radius");
    }

    // The pose of the last frame; a force on the body it acts on pushes it.
    {
        SceneGraph g = calm();
        Entity carrier = solid(1, "Тележка", Vector3(0.2_r), {0, 2, 0}, false);
        carrier.rigid.velocity = Vector3(1, 0, 0);
        g.entities.push_back(carrier);
        g.entities.push_back(solid(2, "Ящик", Vector3(0.2_r), {2, 2, 0}, false));
        Force f = forceOf(10, Vector3(0.0_r), R, C, 10, 1.43_r);
        f.parent = 1;
        g.forces.push_back(f);
        Simulation s;
        s.load(g);
        bool agree = true;
        int first = -1;
        for (int k = 0; k < 90; ++k) {
            const bool reach = 2 - s.physics()->state(s.robot()->bodyOf(1)).centre.x <= 1.43_r;
            s.stepFrame();
            const bool moved = velocityOf(s, 2).x > 0;
            if (moved && first < 0) first = k;
            if (reach != (first >= 0 && moved) && reach != moved) agree = false;
        }
        CHECK(agree && first > 20, "a carried force reaches by the carrier's pose of the frame before: first push in frame %d", first);
        SceneGraph own = calm();
        own.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        Force self = forceOf(10, Vector3(0.0_r), D, I, 5);
        self.parent = 1;
        own.forces.push_back(self);
        Simulation t;
        t.load(own);
        t.stepFrame();
        CHECK(near(momentumOf(t, 1), up * 5), "a force parented to its own body pushes it: 5 N s of momentum");
    }

    // The same run twice is the same bit for bit; impulses on different bodies commute.
    {
        SceneGraph g = calm();
        for (uint32_t k = 0; k < 5; ++k) g.entities.push_back(solid(k + 1, "Ящик", Vector3(0.2_r), {Real(k) * 0.7_r, 2, 0}, false));
        g.forces.push_back(forceOf(10, {1, 1, 0}, R, I, 7, 5));
        g.forces.back().falloff = ForceFalloff::InverseSquare, g.forces.back().start = 0.2_r;
        g.forces.push_back(forceOf(11, {0, 2, 0}, D, C, 3));
        g.forces.push_back(forceOf(12, {2, 2, 1}, R, C, -4, 6));
        g.forces.back().falloff = ForceFalloff::Linear;
        Simulation a, b;
        a.load(g), b.load(g);
        bool every = true;
        for (int k = 0; k < 120; ++k) {
            a.stepFrame(), b.stepFrame();
            for (uint32_t id = 1; id <= 5; ++id) {
                const BodyState x = a.physics()->state(a.robot()->bodyOf(id)), y = b.physics()->state(b.robot()->bodyOf(id));
                every = every && std::memcmp(&x.position, &y.position, sizeof(Vector3)) == 0 && std::memcmp(&x.velocity, &y.velocity, sizeof(Vector3)) == 0;
            }
        }
        CHECK(every, "two runs of 120 frames with mixed forces agree bit for bit");
        SceneGraph two = calm();
        two.entities.push_back(solid(1, "А", Vector3(0.2_r), {0, 2, 0}, false));
        two.entities.push_back(solid(2, "Б", Vector3(0.2_r), {2, 2, 0}, false));
        two.entities[0].tags = {"a"}, two.entities[1].tags = {"b"};
        two.forces.push_back(forceOf(10, {0, 0, 0}, D, I, 5)), two.forces.back().tag = "a";
        two.forces.push_back(forceOf(11, {0, 0, 0}, D, I, 7)), two.forces.back().tag = "b";
        SceneGraph swapped = two;
        std::swap(swapped.forces[0], swapped.forces[1]);
        Simulation p, q;
        p.load(two), q.load(swapped);
        for (int k = 0; k < 10; ++k) p.stepFrame(), q.stepFrame();
        CHECK(near(velocityOf(p, 1), velocityOf(q, 1), 0) && near(velocityOf(p, 2), velocityOf(q, 2), 0), "impulses on different bodies commute");
    }

    // Edits while the world runs.
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.forces.push_back(forceOf(10, at, D, C, 6));
        Simulation s;
        s.load(g);
        for (int k = 0; k < 30; ++k) s.stepFrame();
        const Real half = velocityOf(s, 1).y;
        std::vector<Force> edited = g.forces;
        edited[0].strength = 12;
        s.robot()->setForces(edited);
        for (int k = 0; k < 30; ++k) s.stepFrame();
        CHECK(std::fabs(half - 3) < 3e-3_r && std::fabs(velocityOf(s, 1).y - half - 6) < 6e-3_r, "a constant force's new strength acts at once, no rebuild: %f then %f", double(half), double(velocityOf(s, 1).y));
    }
    {
        SceneGraph g = calm();
        g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), at, false));
        g.forces.push_back(forceOf(10, at, D, I, 5));
        Simulation s;
        s.load(g);
        s.stepFrame();
        std::vector<Force> edited = g.forces;
        edited[0].strength = 50;
        s.robot()->setForces(edited);
        s.stepFrame();
        CHECK(near(velocityOf(s, 1), up * 5), "an edit of a fired impulse does not fire it again");
        s.robot()->setForces({});
        s.robot()->setForces(g.forces); // deleted and added again in play, start 0
        s.stepFrame();
        CHECK(near(velocityOf(s, 1), up * 5), "a force added while playing, its moment passed, does not fire by itself");
        std::string error;
        CHECK(s.robot()->fireForce(*s.physics(), 10, error) && near(velocityOf(s, 1), up * 10), "the button fires it");
        s.load(g); // Stop, then play
        s.stepFrame();
        CHECK(near(velocityOf(s, 1), up * 5), "Stop and play again: it fires once more");

        g.forces[0].start = 3600;
        Simulation late;
        late.load(g);
        late.stepFrame();
        CHECK(length(velocityOf(late, 1)) == 0, "an impulse at 3600 s waits");
        edited = g.forces;
        edited[0].start = 0;
        late.robot()->setForces(edited);
        late.stepFrame();
        CHECK(near(velocityOf(late, 1), up * 5), "an unfired impulse moved to a passed moment fires at the next step");
    }
}

// A body's own force: a directional force that is the child of a rigid box acts on that box alone, at the force's own
// point. Through the centre of mass it gives the momentum and no spin; 0.1 m off it, the same momentum and the spin
// r x J / I. A neighbour 0.3 m away - inside any reach - is not touched. A metre of its arrow is 3 m/s of the body's
// velocity (an impulse) or 10 m/s^2 (a constant one), and applyForceTo makes a new force a metre long, upright.
void testSimForceOnBody() {
    SceneGraph g = calm();
    g.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 2, 0}, false));
    g.entities.push_back(solid(2, "Сосед", Vector3(0.2_r), {0.3_r, 2, 0}, false));
    Force own = forceOf(3, Vector3(0.0_r), ForceShape::Directional, ForceMode::Impulse, 4, 1.5_r); // at the box's centre, in its frame
    own.parent = 1;
    g.forces.push_back(own);
    const auto after = [&](BodyState& box, Vector3& neighbour, size_t& listed) {
        Simulation s;
        s.load(g);
        s.stepFrame();
        box = s.physics()->state(s.robot()->bodyOf(1));
        neighbour = momentumOf(s, 2);
        Frame frame;
        s.fill(frame);
        listed = frame.forces.size() == 1 && frame.forces[0].id == 3 ? 1 : 0;
    };
    BodyState box;
    Vector3 neighbour;
    size_t listed = 0;
    after(box, neighbour, listed);
    CHECK(near(box.velocity * box.mass, Vector3(0, 4, 0)) && length(box.angularVelocity) < 1e-4_r, "at the centre of mass: 4 N s of momentum and no spin");
    CHECK(length(neighbour) < 1e-6_r, "the neighbour inside its radius is not touched: the force is the box's own");
    CHECK(listed == 1, "the frame lists the force that rides on a moving body");

    g.forces[0].position = Vector3(0.1_r, 0, 0);
    after(box, neighbour, listed);
    const Real inertia = box.mass * (0.04_r + 0.04_r) / 12, spin = 0.4_r / inertia; // a 0.2 m cube about z; r x J = (0.1, 0, 0) x (0, 4, 0) = 0.4 z
    std::printf("  a push of 4 N s on a %.1f kg box 0.1 m off its centre: momentum y %.4f, spin z %.3f rad/s (r x J / I = %.3f)\n", double(box.mass),
                double(box.velocity.y * box.mass), double(box.angularVelocity.z), double(spin));
    CHECK(near(box.velocity * box.mass, Vector3(0, 4, 0)) && std::fabs(box.angularVelocity.z - spin) < 0.01_r * spin && length(neighbour) < 1e-6_r,
          "0.1 m off the centre: the same momentum, and the spin r x J / I");

    g.forces[0].position = Vector3(0.0_r), g.forces[0].mode = ForceMode::Constant, g.forces[0].strength = 8;
    Simulation thrust;
    thrust.load(g);
    for (int k = 0; k < 60; ++k) thrust.stepFrame();
    CHECK(near(momentumOf(thrust, 1), Vector3(0, 8, 0), 1e-3_r) && length(momentumOf(thrust, 2)) < 1e-6_r, "a constant 8 N on its own body for a second: 8 N s, the neighbour none");
    // The robot's own code sets a thruster's throttle by a command, not by an edit of the scene.
    std::string why;
    const Vector3 was = momentumOf(thrust, 1);
    CHECK(thrust.robot()->command(*thrust.physics(), 3, "strength", 20, why), "a force's strength is commanded: %s", why.c_str());
    CHECK(!thrust.robot()->command(*thrust.physics(), 99, "strength", 1, why) && why == "no force 99 (strength is a force's)", "no such force: %s", why.c_str());
    for (int k = 0; k < 60; ++k) thrust.stepFrame();
    CHECK(near(momentumOf(thrust, 1) - was, Vector3(0, 20, 0), 1e-3_r), "commanded to 20 N, a second gives 20 N s more");

    const Real mass = bodyMass(g.entities[0]);
    CHECK(forceBody(g, g.forces[0]) == &g.entities[0] && forceUnitPerMetre(g, g.forces[0]) == mass * 10, "a metre of a constant force's arrow on its body: 10 m/s^2 of it");
    g.forces[0].mode = ForceMode::Impulse;
    CHECK(forceUnitPerMetre(g, g.forces[0]) == mass * 3, "of an impulse's: 3 m/s");
    g.forces[0].shape = ForceShape::Radial;
    CHECK(!forceBody(g, g.forces[0]) && forceUnitPerMetre(g, g.forces[0]) == 3, "a radial force is a field whatever its parent is: a metre is 3 m/s, no mass in it");
    g.entities[0].rotationDeg = Vector3(0, 0, 40), g.entities[0].rigid.centerGiven = true, g.entities[0].rigid.centerOfMass = Vector3(0, 0.05_r, 0);
    Force made = forceOf(4, Vector3(5, 5, 5), ForceShape::Directional, ForceMode::Constant, 1);
    applyForceTo(g, made, g.entities[0]);
    Vector3 p, centre;
    Quaternion q, bodyTurn;
    worldPose(g, g.entities[0], centre, bodyTurn);
    worldPose(g, made, p, q);
    CHECK(made.parent == 1 && near(p, centre + bodyTurn.rotate(Vector3(0, 0.05_r, 0))) && near(forceAxis(q), Vector3(0, 1, 0)) && near(Vector3(made.strength, 0, 0), Vector3(mass * 12, 0, 0)),
          "applyForceTo: the body's child at its centre of mass, upright in the world though the body is tilted, 1.2 m of arrow (12 m/s^2)");
}
