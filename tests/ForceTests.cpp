// Forces of the scene on rigid bodies (src/sim, SceneGraph::forces), against exact answers: a field's push or pull and
// its falloff, an impulse once and a constant force all the time, a body's own force at its own point - momentum, and
// spin off the centre. The rest of the running world's tests: SimTests.cpp.
#include "TestBodies.h"
#include "TestRunner.h"

#include "scene/SceneOps.h"
#include "sim/Simulation.h"

#include <cmath>

using namespace ke;

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

    // A torque of its own turns its body about its own +y, wherever on the body it stands, and moves it nowhere.
    SceneGraph t = calm();
    t.entities.push_back(solid(1, "Ящик", Vector3(0.2_r), {0, 2, 0}, false));
    Force turn = forceOf(3, Vector3(0.05_r, 0, 0), ForceShape::Torque, ForceMode::Impulse, 0.2_r);
    turn.parent = 1;
    t.forces.push_back(turn);
    Simulation kick;
    kick.load(t);
    kick.stepFrame();
    const BodyState kicked = kick.physics()->state(kick.robot()->bodyOf(1));
    const Real iy = kicked.mass * (0.04_r + 0.04_r) / 12, once = 0.2_r / iy;
    t.forces[0].mode = ForceMode::Constant, t.forces[0].strength = 0.5_r;
    Simulation turning;
    turning.load(t);
    for (int k = 0; k < 60; ++k) turning.stepFrame();
    const BodyState spun = turning.physics()->state(turning.robot()->bodyOf(1));
    std::printf("  a torque on a %.1f kg 20 cm box: 0.2 N m s - spin y %.3f rad/s (L / I = %.3f); 0.5 N m for 1 s - %.3f rad/s (%.3f)\n", double(kicked.mass),
                double(kicked.angularVelocity.y), double(once), double(spun.angularVelocity.y), double(0.5_r / iy));
    CHECK(std::fabs(kicked.angularVelocity.y - once) < 0.01_r * once && length(kicked.velocity) < 1e-6_r, "an impulse of 0.2 N m s: the spin L / I about y, no motion");
    CHECK(std::fabs(spun.angularVelocity.y - 0.5_r / iy) < 0.01_r * 0.5_r / iy && length(spun.velocity) < 1e-6_r, "0.5 N m for a second: the spin tau t / I");
    t.forces[0].parent = 0;
    Simulation none;
    none.load(t);
    for (int k = 0; k < 10; ++k) none.stepFrame();
    CHECK(length(none.physics()->state(none.robot()->bodyOf(1)).angularVelocity) < 1e-9_r, "a torque on no body turns nothing");
}
