// Assembling mechanisms in the scene graph: the six-freedom joints survive the scene file to the bit,
// a shape put against any of the six faces of a turned box touches it exactly, the core edits of the
// API undo each other, posing a joint turns its chain, inverse kinematics answers the closed forms, an
// IK goal's path is walked as asked. The joints as the physics engine runs them are tested with it.
#include "TestRunner.h"

#include "scene/Assembly.h"
#include "scene/CameraHousing.h"
#include "scene/Describe.h"
#include "scene/Kinematics.h"
#include "scene/SceneEdits.h"
#include "scene/SceneGraph.h"
#include "scene/SceneOps.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

using namespace ke;

namespace {

Entity block(const char* name, const Vector3& size, const Vector3& position, bool fixed) {
    Entity e;
    e.name = name;
    e.size = size;
    e.position = position;
    e.rigid.enabled = true;
    e.rigid.fixed = fixed;
    e.rigid.density = 1000;
    e.collider.enabled = true;
    return e;
}

} // namespace

void testSceneJointsRoundTrip() {
    SceneGraph g;
    g.entities.push_back(block("A", Vector3(0.2_r), {0, 1, 0}, true));
    g.entities.back().id = 1;
    g.entities.push_back(block("B", Vector3(0.2_r), {0.2_r, 1, 0}, false));
    g.entities.back().id = 2;
    const char* kinds[] = {"hinge", "slider", "cylinder", "ball", "weld"};
    for (const char* k : kinds) {
        addJoint(g, JointType::Hinge, 2, 1, {0.1_r, 1.03_r, 0.01_r}, {0, 0.3_r, 1});
        setJointKind(g.joints.back(), std::string(k));
    }
    JointFreedom& rx = g.joints[0].dof[DofRx];
    rx.motion = JointMotion::Limited, rx.lower = -1.25_r, rx.upper = 0.7_r;
    rx.drive.enabled = true, rx.drive.targetVelocity = 2.5_r, rx.drive.maxForce = 12;
    JointFreedom& tx = g.joints[1].dof[DofTx];
    tx.motion = JointMotion::Limited, tx.lower = -0.05_r, tx.upper = 0.05_r;
    tx.drive.enabled = true, tx.drive.stiffness = 3000, tx.drive.damping = 80;
    JointDrive& dc = g.joints[2].dof[DofRx].drive;
    dc.enabled = true, dc.voltage = 12, dc.resistance = 1.5_r, dc.constant = 0.05_r;
    g.joints[3].collide = true;
    g.joints[4].b = 0;
    g.joints[4].name = "Сварка \"к миру\"";
    const std::string text = g.save();
    SceneGraph back;
    std::string error;
    const bool loaded = back.load(text, error);
    std::printf("  %zu joints in %zu bytes; save -> load -> save %s; kinds back: %s %s %s %s %s\n", back.joints.size(), text.size(),
                loaded && back.save() == text ? "the same" : "DIFFERENT", jointKindWord(back.joints[0]), jointKindWord(back.joints[1]),
                jointKindWord(back.joints[2]), jointKindWord(back.joints[3]), jointKindWord(back.joints[4]));
    CHECK(loaded, "a scene with joints does not load: %s", error.c_str());
    CHECK(back.save() == text, "save -> load -> save changed the joints:\n%s\n---\n%s", text.c_str(), back.save().c_str());
    CHECK(back.joints.size() == 5 && back.joints[0].dof[DofRx].motion == JointMotion::Limited && back.joints[0].dof[DofRx].drive.enabled &&
          back.joints[1].dof[DofTx].drive.stiffness == 3000 && back.joints[2].dof[DofRx].drive.constant == 0.05_r && back.joints[3].collide,
          "the joints' freedoms did not come back");
    CHECK(std::string(jointKindWord(back.joints[2])) == "cylinder" && jointKind(back.joints[0]) == JointType::Hinge, "the kinds changed");
}

// Every face of a box turned 30 degrees about y and 20 about x: the face found from a point on it is
// that face, the new shape's centre is half its own edge out along the normal from the face's
// centre, and the two boxes touch without a gap or an overlap.
void testSnapToFace() {
    SceneGraph g;
    Entity t = block("Цель", {0.4_r, 0.2_r, 0.3_r}, {1, 2, 3}, true);
    t.id = 5;
    t.rotationDeg = {20, 30, 0};
    g.entities.push_back(t);
    const Vector3 size(0.1_r, 0.16_r, 0.12_r);
    const Quaternion q = entityRotation(t);
    Real worst = 0;
    int wrongFace = 0;
    for (int f = 0; f < 6; ++f) {
        const int a = f / 2;
        const Real sign = f % 2 == 0 ? 1.0_r : -1.0_r;
        Vector3 local(0.0_r);
        local[a] = sign * 0.5_r * t.size[a];
        local[(a + 1) % 3] = 0.3_r * 0.5_r * t.size[(a + 1) % 3]; // off the centre, still on the face
        const FaceSnap s = snapToFace(g, 5, t.position + q.rotate(local), size);
        wrongFace += !s.found || int(s.face) != f;
        Vector3 n(0.0_r);
        n[a] = sign;
        Vector3 centre(0.0_r);
        centre[a] = sign * 0.5_r * t.size[a];
        const Real gap = dot(s.position - t.position, q.rotate(n)) - 0.5_r * (t.size[a] + size[a]);
        worst = std::max({worst, std::fabs(gap), length(s.anchor - (t.position + q.rotate(centre))), length(s.normal - q.rotate(n)),
                          std::fabs(dot(s.across[0], s.normal)) + std::fabs(dot(s.across[1], s.normal))});
    }
    std::printf("  six faces of a turned box: %d found wrong, worst gap / anchor / normal error %.2g m\n", wrongFace, double(worst));
    CHECK(wrongFace == 0, "%d faces were found wrong", wrongFace);
    CHECK(worst < 1e-5_r, "the snapped shape is off the face by %g", double(worst));
    CHECK(!snapToFace(g, 99, Vector3(0.0_r), size).found, "a missing target was found");
}

// put / get / set / remove: a new object gets an id; a field set reads back - a freedom's drive by
// its two-word line "drive rx"; the old blocks put back give the old graph to the bit (the inverse of
// an edit); removing an entity takes its joints and leaves its child where it was in the world.
void testSceneEditsCore() {
    SceneGraph g;
    std::string error;
    const uint32_t base = putObject(g, "entity \"Основание\"\n  shape box size 0.2 0.2 0.2 position 0 1 0 rotation 0 45 0 color 1 1 1\nend\n", error);
    const Attached arm = attachToFace(g, base, BoxFace::PlusY, g.entities[0], JointType::Hinge, 0);
    Entity child;
    child.name = "Датчик";
    child.parent = arm.entity;
    child.position = {0, 0.2_r, 0};
    child.id = nextId(g);
    g.entities.push_back(child);
    const std::string before = g.save(), armBlock = getObject(g, arm.entity), jointBlock = getObject(g, arm.joint);
    const uint32_t same = setField(g, arm.entity, "rigid", "density", {"800"}, error);
    const uint32_t named = setField(g, arm.entity, "entity", "", {"Плечо 1"}, error);
    const uint32_t driven = setField(g, arm.joint, "drive rx", "velocity", {"2"}, error);
    const JointDrive& d = g.joints[0].dof[DofRx].drive;
    const bool edited = g.entities[1].rigid.density == 800 && g.entities[1].name == "Плечо 1" && d.enabled && d.targetVelocity == 2;
    putObject(g, armBlock, error);
    putObject(g, jointBlock, error);
    const bool undone = g.save() == before;
    Vector3 p0, p1;
    Quaternion q0, q1;
    worldPose(g, g.entities[2], p0, q0);
    removeObject(g, arm.entity);
    worldPose(g, g.entities[1], p1, q1);
    std::printf("  put -> id %u, attach -> entity %u + joint %u; set (a freedom's drive too): %s; undo by the old blocks: %s; "
                "removed: %zu joints left, the child moved %.2g m\n", base, arm.entity, arm.joint, edited ? "ok" : "WRONG",
                undone ? "the same" : "DIFFERENT", g.joints.size(), double(length(p1 - p0)));
    CHECK(base == 1 && arm.entity == 2 && arm.joint == 3, "ids %u %u %u (expected 1 2 3)", base, arm.entity, arm.joint);
    CHECK(same == arm.entity && named == arm.entity && driven == arm.joint && edited, "setField did not set: %s", error.c_str());
    CHECK(undone, "putting the old blocks back did not undo the edit:\n%s\n---\n%s", before.c_str(), g.save().c_str());
    CHECK(g.joints.empty() && g.entities.size() == 2 && g.entities[1].parent == 0, "removing the arm left its joint or its child's parent");
    CHECK(length(p1 - p0) < 1e-5_r, "the child of the removed entity moved by %g m", double(length(p1 - p0)));
    CHECK(putObject(g, "entity \"x\"\nend\nentity \"y\"\nend\n", error) == 0, "two blocks went in as one object");
}

// Posing a joint in the graph (the joint as the editor's gizmo): a two-link arm on a fixed post, the
// shoulder turned to 0.6 rad carries the elbow link with it; the elbow's own angle stays 0; a limit of
// 0.4 holds the pose at 0.4; a slider poses along its axis.
void testPoseJoint() {
    SceneGraph g;
    Entity post = block("Стойка", Vector3(0.2_r), {0, 1, 0}, true);
    post.id = 1;
    g.entities.push_back(post);
    const Attached upper = attachToFace(g, 1, BoxFace::PlusY, block("Плечо", {0.1_r, 0.3_r, 0.1_r}, Vector3(0.0_r), false), JointType::Hinge, 0);
    const Attached lower = attachToFace(g, upper.entity, BoxFace::PlusY, block("Локоть", {0.1_r, 0.3_r, 0.1_r}, Vector3(0.0_r), false),
                                        JointType::Hinge, 0);
    const Vector3 tipBefore = g.entities[2].position;
    const Real reached = poseJoint(g, upper.joint, DofRx, 0.6_r);
    const Real shoulder = jointPosition(g, *findJoint(g, upper.joint)), elbow = jointPosition(g, *findJoint(g, lower.joint));
    const Real moved = length(g.entities[2].position - tipBefore);
    for (JointObject& j : g.joints)
        if (j.id == upper.joint) j.dof[DofRx].motion = JointMotion::Limited, j.dof[DofRx].lower = -0.4_r, j.dof[DofRx].upper = 0.4_r;
    const Real clamped = poseJoint(g, upper.joint, DofRx, 1.0_r);
    std::printf("  pose the shoulder to 0.6 rad: reached %.6f, reads %.6f, the elbow %.2g, the forearm moved %.3f m; limited to 0.4: %.6f\n",
                double(reached), double(shoulder), double(elbow), double(moved), double(clamped));
    CHECK(std::fabs(reached - 0.6_r) < 1e-4_r && std::fabs(shoulder - 0.6_r) < 1e-4_r, "the shoulder's pose %f", double(shoulder));
    CHECK(std::fabs(elbow) < 1e-4_r && moved > 0.1_r, "the forearm did not go with the shoulder (elbow %f, moved %f)", double(elbow), double(moved));
    CHECK(std::fabs(clamped - 0.4_r) < 1e-4_r, "the limit did not hold the pose (%f)", double(clamped));
}

// The world through the edits core: its "world" and "solver" lines out (both, always) and back in -
// zero gravity and no damping set as text, as an agent sets them; a text with an object in it is
// refused. A joint still called by its kind's label takes the new kind's label with the kind.
void testWorldAndKindEdits() {
    SceneGraph g, other;
    std::string error;
    std::string text = getWorld(g);
    const bool both = text.find("world ") == 0 && text.find("\nphysics ") != std::string::npos;
    text.replace(text.find("gravity 0 -9.81 0"), 17, "gravity 0 0 0");
    text.replace(text.find("jointHertz 480"), 14, "jointHertz 120");
    const bool put = putWorld(other, text, error);
    const bool refused = !putWorld(other, "entity \"Куб\"\n  object id 3\nend\n", error);
    g.entities.push_back(block("Основание", Vector3(0.2_r), Vector3(0.0_r), true));
    g.entities.back().id = 1;
    const Attached link = attachToFace(g, 1, BoxFace::PlusX, block("Звено", Vector3(0.2_r), Vector3(0.0_r), false), JointType::Hinge, 2);
    JointObject& j = g.joints[0];
    const std::string hinge = j.name;
    setJointKind(j, std::string("slider"));
    const std::string slider = j.name;
    j.name = "Мой шарнир";
    setJointKind(j, std::string("ball"));
    std::printf("  world: both lines out: %s; put back: gravity (%g, %g, %g), joints at %g Hz; an object refused: %s; joint %u: \"%s\" -> \"%s\", "
                "a name of its own kept: \"%s\"\n", both ? "yes" : "no", double(other.world.gravity.x), double(other.world.gravity.y),
                double(other.world.gravity.z), double(other.world.physics.jointHertz), refused ? "yes" : "no", link.joint, hinge.c_str(),
                slider.c_str(), j.name.c_str());
    CHECK(both && put && length(other.world.gravity) == 0 && other.world.physics.jointHertz == 120 && refused, "the world's edits: %s", error.c_str());
    CHECK(slider == "Призматическое " + std::to_string(link.joint) && j.name == "Мой шарнир", "the kind's label: %s, %s", slider.c_str(), j.name.c_str());
}

// A stereo camera with its housing (CameraHousing.h). Free, a box housing at (0, 1, 0) looking at (1, 0, 0): its +x
// along the line to the point, its +y level (no vertical part), fixed; the stereo sensor its child 1 mm before the
// box's front (half depth 15 mm). A model housing - a box 40 x 200 x 50 mm whose centre is 10 mm before its origin -
// sits at its longest side, its collider its own box (not a cube of that side), and the eyes at hi.x - centre.x + 1 mm
// = 21 mm. On a moving box's +x face the housing's
// back meets the face and a weld holds it; on a fixed box it is fixed, no weld. An unreadable model puts nothing.
void testStereoCameraHousing() {
    SceneGraph g;
    CameraHousing box;
    box.baseline = 0.12_r;
    const PlacedCamera c = addStereoCamera(g, box, {0, 1, 0}, {1, 0, 0});
    const Entity& h = g.entities.back();
    const SensorObject& s = g.sensors.back();
    const Quaternion q = objectRotation(h);
    const Vector3 look = normalize(Vector3(1, -1, 0));
    CHECK(c.housing == h.id && c.sensor == s.id && s.parent == h.id && c.joint == 0 && h.rigid.fixed, "the camera: housing, sensor, no weld");
    CHECK(length(q.rotate(Vector3(1, 0, 0)) - look) < 1e-5_r && std::fabs(double(q.rotate(Vector3(0, 1, 0)).y)) < 1e-6,
          "it looks at the point, its baseline level");
    CHECK(std::fabs(double(s.position.x) - 0.016) < 1e-6 && s.baseline == 0.12_r && s.kind == SensorKind::Stereo, "eyes %.6f m before the centre",
          double(s.position.x));
    const std::string obj = (std::filesystem::temp_directory_path() / "rf-camera-housing-test.obj").string();
    std::FILE* f = std::fopen(obj.c_str(), "w");
    for (int k = 0; k < 8; ++k)
        std::fprintf(f, "v %g %g %g\n", k & 1 ? 0.03 : -0.01, k & 2 ? 0.1 : -0.1, k & 4 ? 0.025 : -0.025);
    std::fputs("f 1 3 4 2\nf 5 6 8 7\nf 1 2 6 5\nf 3 7 8 4\nf 1 5 7 3\nf 2 4 8 6\n", f);
    std::fclose(f);
    CameraHousing model;
    model.model = obj;
    addStereoCamera(g, model, {0, 1, 0}, {0, 1, -1});
    const ColliderRole& hull = g.entities.back().collider;
    CHECK(std::fabs(double(g.entities.back().size.x) - 0.2) < 1e-6 && std::fabs(double(g.sensors.back().position.x) - 0.021) < 1e-6 &&
              !hull.fitToGeometry && length(hull.size - Vector3(0.04_r, 0.2_r, 0.05_r)) < 1e-6_r,
          "a model: size %.4f m, eyes %.6f m, its collider the model's own box %.3f x %.3f x %.3f", double(g.entities.back().size.x),
          double(g.sensors.back().position.x), double(hull.size.x), double(hull.size.y), double(hull.size.z));
    Entity moving = block("Тележка", Vector3(0.4_r), {0, 0.5_r, 0}, false), wall = block("Стена", Vector3(0.4_r), {2, 0.5_r, 0}, true);
    moving.id = 100, wall.id = 101;
    g.entities.push_back(moving), g.entities.push_back(wall);
    const PlacedCamera on = mountStereoCamera(g, box, 100, snapToFace(g, 100, BoxFace::PlusX, Vector3(0.0_r)));
    const Entity& carried = g.entities.back();
    CHECK(on.joint != 0 && !carried.rigid.fixed && length(carried.position - Vector3(0.2_r + 0.015_r, 0.5_r, 0)) < 1e-6_r,
          "on a moving box: welded, its back on the face (%.4f)", double(carried.position.x));
    const PlacedCamera fixedOn = mountStereoCamera(g, box, 101, snapToFace(g, 101, BoxFace::PlusX, Vector3(0.0_r)));
    CHECK(fixedOn.joint == 0 && g.entities.back().rigid.fixed, "on a fixed box: fixed, no weld");
    const size_t before = g.entities.size();
    model.model = obj + ".missing";
    CHECK(!addStereoCamera(g, model, {0, 1, 0}, {1, 1, 0}).problem.empty() && g.entities.size() == before, "an unreadable model puts nothing");
    std::remove(obj.c_str());
}

// The scene in words (Describe.h): a wheel module reads as what it is - its wheel under its shock
// absorber under the chassis, the axle's direction in the chassis's frame, the motor's driver and duty,
// the encoder on the named axle, the sonar looking forward.
void testDescribeScene() {
    SceneGraph g;
    Entity chassis = block("Шасси", Vector3(0.6_r, 0.15_r, 0.35_r), Vector3(0, 0.25_r, 0), false);
    chassis.id = 1, chassis.rigid.density = 300;
    g.entities.push_back(chassis);
    addWheelModule(g, 1, Side::FrontLeft);
    attachSensor(g, 1, snapToFace(g, 1, BoxFace::PlusX, Vector3(0.0_r)), SensorKind::Sonar);
    const std::string text = describeScene(g);
    std::printf("%s", text.c_str());
    const size_t chassisAt = text.find("«Шасси»"), shockAt = text.find("«Амортизатор спереди слева»"), wheelAt = text.find("- «Колесо спереди слева»");
    CHECK(chassisAt < shockAt && shockAt < wheelAt && wheelAt != std::string::npos, "the tree: chassis, its shock absorber, its wheel");
    CHECK(text.find("ползун, ось вверх") != std::string::npos && text.find("шарнир, ось влево") != std::string::npos &&
              text.find("драйвер «Драйвер спереди слева» ШИМ 0") != std::string::npos,
          "the strut slides up, the axle points left, its driver stands at duty 0");
    CHECK(text.find("на сочленении «Ось спереди слева»") != std::string::npos && text.find("смотрит вперёд") != std::string::npos,
          "the encoder on the named axle, the sonar looking forward");
}

// assembly_drive: a servo put on a hinge's turn by its fields; a later call with the target only keeps the
// stiffness (fields merge); a velocity motor and a DC motor by their kinds, a DC motor without its constant
// refused. The trap an agent fell into - scene_set writing "target" over the stiffness's place, a line with
// "target" twice - is refused now instead of read with the stiffness lost. The scene in words says "серво".
void testAssemblyDrive() {
    SceneGraph g;
    Entity base, arm;
    base.id = 1, arm.id = 2;
    base.rigid.enabled = arm.rigid.enabled = true;
    base.rigid.fixed = true;
    g.entities = {base, arm};
    JointObject hinge;
    hinge.id = 3, hinge.name = "Плечо", hinge.a = 2, hinge.b = 1;
    hinge.dof[DofRx].motion = JointMotion::Free;
    g.joints.push_back(hinge);
    const SceneOp* drive = findSceneOp("assembly_drive");
    auto call = [&](std::initializer_list<std::pair<const char*, Value>> fields, std::string& error) {
        Value args = Value::map(), answer;
        args.set("joint", 3);
        for (const auto& f : fields) args.set(f.first, f.second);
        return drive && drive->edit(g, args, answer, error);
    };
    std::string error;
    const bool servo = call({{"stiffness", 2000.0}, {"damping", 50.0}, {"target", 0.5}}, error);
    const bool moved = call({{"target", 1.0}}, error);
    const JointDrive& d = g.joints[0].dof[DofRx].drive;
    CHECK(servo && moved && d.enabled && d.stiffness == 2000 && d.damping == 50 && d.target == 1 && d.constant == 0,
          "assembly_drive does not put a servo and keep its stiffness when only the target changes: %s", error.c_str());
    CHECK(describeScene(g).find("серво к 1 рад") != std::string::npos, "the scene in words does not call the servo a servo");
    CHECK(call({{"kind", "velocity"}, {"velocity", 3.0}}, error) && d.stiffness == 0 && d.targetVelocity == 3, "a velocity motor: %s", error.c_str());
    CHECK(!call({{"kind", "dcmotor"}, {"constant", 0.0}}, error) && error.find("constant") != std::string::npos, "a DC motor without its constant passes");
    CHECK(call({{"kind", "dcmotor"}, {"voltage", 12.0}, {"resistance", 2.0}, {"constant", 0.05}}, error) && d.constant == 0.05_r, "a DC motor: %s", error.c_str());
    call({{"kind", "servo"}, {"stiffness", 2000.0}}, error);
    const uint32_t trap = setField(g, 3, "drive", "rx", {"target", "-1"}, error);
    std::printf("  the agent's call scene_set {drive rx: target -1}: %s\n", trap ? "taken" : error.c_str());
    CHECK(trap == 0 && g.joints[0].dof[DofRx].drive.stiffness == 2000, "a drive line with 'target' twice is read and the stiffness lost");
}

// Inverse kinematics (Kinematics.h) against the closed form. A planar arm of two hinges about z - links 0.4 and
// 0.3 m - reaches (0.5, 0.3): its elbow angle is the law of cosines' acos((x^2 + y^2 - l1^2 - l2^2) / 2 l1 l2) (one
// branch or the other). Three hinges reach a point and a turn about z in the plane (x, y, phi) - the planar
// 3R's full pose. A point out of reach gives the arm stretched toward it, not reached, and says so.
static SceneGraph planarArm(int links) {
    SceneGraph g;
    Entity base;
    base.id = 1, base.rigid.enabled = base.rigid.fixed = true;
    g.entities.push_back(base);
    const Real length[] = {0.4_r, 0.3_r, 0.2_r};
    Real x = 0;
    for (int k = 0; k < links; ++k) {
        Entity link;
        link.id = uint32_t(2 + k), link.rigid.enabled = true;
        link.position = {x + length[k], 0, 0}; // each link's frame at its far end: the next joint, or the effector
        g.entities.push_back(link);
        JointObject j;
        j.id = uint32_t(10 + k), j.a = link.id, j.b = uint32_t(1 + k);
        j.anchorA = {-length[k], 0, 0}, j.anchorB = {0, 0, 0};
        j.frameA = j.frameB = {0, -90, 0};                     // the frames' x along the world z: a turn about z
        j.dof[DofRx].motion = JointMotion::Free;
        g.joints.push_back(j);
        x += length[k];
    }
    if (links > 0) g.joints[0].anchorB = {0, 0, 0};
    for (int k = 1; k < links; ++k) g.joints[size_t(k)].anchorB = {0, 0, 0};
    return g;
}

void testInverseKinematics() {
    SceneGraph two = planarArm(2);
    const IkResult r2 = solveIk(two, 3, {0.5_r, 0.3_r, 0}, nullptr, 1e-5_r);
    const double c = (0.25 + 0.09 - 0.16 - 0.09) / (2 * 0.4 * 0.3), elbow = std::acos(c);
    std::printf("  2R: reached %d in %d rounds, error %.2e m; elbow %.6f rad, law of cosines +-%.6f\n", int(r2.reached), r2.iterations,
                double(r2.positionError), double(r2.links.size() == 2 ? r2.links[1].coordinate : 0), elbow);
    CHECK(r2.reached && r2.links.size() == 2 && std::fabs(std::fabs(double(r2.links[1].coordinate)) - elbow) < 1e-4,
          "the planar 2R does not reach the point with the law of cosines' elbow: %g", double(r2.links.size() == 2 ? r2.links[1].coordinate : 0));
    // The method's record: the residual per round falls to the tolerance; the manipulability of a planar 2R's
    // point Jacobian is |det J| = l1 l2 |sin theta2| (Yoshikawa 1985) - zero stretched out, the singularity.
    const double w = 0.4 * 0.3 * std::sin(elbow);
    std::printf("  2R residual by round:");
    for (Real e : r2.residual) std::printf(" %.2e", double(e));
    std::printf("\n  2R manipulability %.6f (l1 l2 sin theta2 = %.6f)\n", double(r2.manipulability), w);
    CHECK(r2.residual.size() == size_t(r2.iterations + 1) && r2.residual.front() > 0.1_r && r2.residual.back() < 1e-5_r,
          "the residual record does not run from the start's error down to the tolerance");
    CHECK(std::fabs(double(r2.manipulability) - w) < 1e-4, "the 2R's manipulability %g is not l1 l2 sin theta2 = %g", double(r2.manipulability), w);
    SceneGraph three = planarArm(3);
    const Quaternion turn = Quaternion::fromAxisAngle({0, 0, 1}, 0.7_r);
    const IkResult r3 = solveIk(three, 4, {0.45_r, 0.35_r, 0}, &turn);
    std::printf("  3R: point and turn about z reached %d in %d rounds: %.2e m, %.2e rad\n", int(r3.reached), r3.iterations, double(r3.positionError),
                double(r3.angleError));
    CHECK(r3.reached, "the planar 3R does not reach a point with a turn: %g m, %g rad", double(r3.positionError), double(r3.angleError));
    SceneGraph far = planarArm(2);
    const IkResult rf = solveIk(far, 3, {1.5_r, 0, 0}, nullptr, 1e-5_r);
    CHECK(!rf.reached && std::fabs(double(rf.positionError) - 0.8) < 1e-3, "a point out of reach: %g m left (the arm 0.7 m long, 1.5 m away)",
          double(rf.positionError));
}

// An IK goal through the operations, as an agent and the window's gizmo use it: assembly_goal puts a cube at the
// planar 2R's tip; scene_move takes it to (0.5, 0.3) - the servos' targets become the law of cosines' elbow and the
// arm posed at those targets has its tip there. The goal line survives save -> load -> save to the bit.
void testIkGoal() {
    SceneGraph g = planarArm(2);
    for (JointObject& j : g.joints) {
        JointDrive& d = j.dof[DofRx].drive;
        d.enabled = true, d.stiffness = 500, d.damping = 20;
    }
    auto call = [&](const char* op, std::initializer_list<std::pair<const char*, Value>> fields, Value& answer, std::string& error) {
        Value args = Value::map();
        for (const auto& f : fields) args.set(f.first, f.second);
        const SceneOp* o = findSceneOp(op);
        return o && o->edit(g, args, answer, error);
    };
    Value answer = Value::map(), ids = Value::list(), by = Value::list();
    std::string error;
    CHECK(call("assembly_goal", {{"effector", 3}}, answer, error), "assembly_goal on the 2R's tip: %s", error.c_str());
    const uint32_t goal = uint32_t(answer["id"].number());
    SceneGraph back, again; // the links' implicit Auto colliders are written out on the first load: the second round is the bit test
    const bool loaded = back.load(g.save(), error) && again.load(back.save(), error);
    const uint32_t kept = loaded && back.entities.size() == g.entities.size() ? back.entities.back().goalOf : 0;
    CHECK(loaded && kept == 3 && again.save() == back.save(), "a scene with a goal does not come back from its text to the bit: %s", error.c_str());
    ids.push(goal), by.push(-0.2), by.push(0.3), by.push(0.0);
    CHECK(call("scene_move", {{"ids", ids}, {"by", by}}, answer, error), "scene_move of the goal: %s", error.c_str());
    const double elbow = std::acos((0.25 + 0.09 - 0.16 - 0.09) / (2 * 0.4 * 0.3));
    SceneGraph posed = g;
    for (const JointObject& j : g.joints) poseJoint(posed, j.id, DofRx, j.dof[DofRx].drive.target);
    Vector3 tip;
    Quaternion q;
    worldPose(posed, *findObject(posed, 3), tip, q);
    std::printf("  the goal moved to (0.5, 0.3): elbow target %.6f rad (law of cosines +-%.6f), the posed tip %.2e m off\n",
                double(g.joints[1].dof[DofRx].drive.target), elbow, double(length(tip - Vector3(0.5_r, 0.3_r, 0))));
    CHECK(goal != 0 && std::fabs(std::fabs(double(g.joints[1].dof[DofRx].drive.target)) - elbow) < 1e-3 && length(tip - Vector3(0.5_r, 0.3_r, 0)) < 1e-3_r,
          "moving the goal does not give the servos the coordinates that put the tip on it");
    // The servos told the other elbow (its sign flipped): a goal nudged 1 cm keeps that branch - the solve starts from
    // where the servos are sent, not from the links' authored poses, so a dragged arm does not jump between branches.
    const Real e1 = g.joints[1].dof[DofRx].drive.target;
    g.joints[1].dof[DofRx].drive.target = -e1;
    by = Value::list(), by.push(0.01), by.push(0.0), by.push(0.0);
    call("scene_move", {{"ids", ids}, {"by", by}}, answer, error);
    const Real e2 = g.joints[1].dof[DofRx].drive.target;
    std::printf("  the servos sent to the other elbow (%.4f rad), the goal nudged 1 cm: the elbow's target %.4f rad\n", double(-e1), double(e2));
    CHECK(e2 * e1 < 0 && std::fabs(e2 + e1) < 0.1_r, "a goal nudged makes the arm jump to the other elbow: %g after %g", double(e2), double(-e1));
}

// An agent's question before it moves (assembly_can_reach) and a gripper's two fingers in one command (assembly_drive
// with joints). The planar 2R asked for (0.5, 0.3): reached, the elbow the law of cosines' to 1e-3 rad (the solve stops
// at its tolerance; one branch or the other), the
// manipulability l1 l2 |sin theta2|, the scene's text unchanged to the bit; asked for 1.5 m away: not reached, 0.8 m
// left. Two joints' servo targets set by one call: both changed, by one edit.
void testCanReachAndDrives() {
    SceneGraph g = planarArm(2);
    for (JointObject& j : g.joints) {
        JointDrive& d = j.dof[DofRx].drive;
        d.enabled = true, d.stiffness = 500, d.damping = 20;
    }
    const std::string before = g.save();
    Value args = Value::map(), at = Value::list(), answer = Value::map(), far = Value::map();
    at.push(0.5), at.push(0.3), at.push(0.0);
    args.set("effector", 3), args.set("position", at);
    std::string error;
    const SceneOp* ask = findSceneOp("assembly_can_reach");
    const bool asked = ask && ask->query && ask->query(g, args, answer, error);
    const double elbow = std::acos((0.25 + 0.09 - 0.16 - 0.09) / (2 * 0.4 * 0.3));
    const double coordinate = asked && answer["joints"].items().size() == 2 ? answer["joints"].items()[1]["coordinate"].number() : 0;
    std::printf("  can reach (0.5, 0.3): %s, elbow %.6f rad (law of cosines +-%.6f), manipulability %.6f (l1 l2 sin = %.6f)\n",
                answer["reached"].truth(false) ? "yes" : "no", coordinate, elbow, answer["manipulability"].number(), 0.12 * std::sin(elbow));
    CHECK(asked && answer["reached"].truth(false) && std::fabs(std::fabs(coordinate) - elbow) < 1e-3 && // the solver stops at its tolerance
              std::fabs(answer["manipulability"].number() - 0.12 * std::sin(elbow)) < 1e-4 && g.save() == before,
          "assembly_can_reach does not answer the closed form, or it changed the scene: %s", error.c_str());
    at = Value::list(), at.push(1.5), at.push(0.0), at.push(0.0), args.set("position", at);
    CHECK(ask && ask->query(g, args, far, error) && !far["reached"].truth(true) && std::fabs(far["position_error"].number() - 0.8) < 1e-3,
          "a point 1.5 m away answered as reachable, or not 0.8 m short: %g", far["position_error"].number());
    Value drive = Value::map(), joints = Value::list(), done = Value::map();
    joints.push(10), joints.push(11), drive.set("joints", joints), drive.set("target", 0.3);
    const SceneOp* op = findSceneOp("assembly_drive");
    const bool both = op && op->edit(g, drive, done, error);
    CHECK(both && g.joints[0].dof[DofRx].drive.target == 0.3_r && g.joints[1].dof[DofRx].drive.target == 0.3_r && done["joints"].items().size() == 2,
          "assembly_drive with two joints did not set both targets in one edit: %s", error.c_str());
}

// A camera's flight (Entity::pathSmooth): from the origin through (1, 0, 0), (1, 1, 0), (0, 1, 1), turning on every
// axis on the way - yaw 90, then (45, 90, 30), then (90, 0, 90) degrees. Walked in steps of 1 mm at 1 m/s: it passes
// within 1 mm of each point, turned as that point says; the speed holds (every step's chord within 1 % of 1 mm); its
// heading turns smoothly (no step turns it more than 3 degrees - the polyline turns 90 at a corner); its spin, the turn
// of a step as a vector, does not jump at the points as slerp's does (its axis swings there).
// A way re-planned on the walk (assembly_path's start): the new path begins where it is told - where the goal is now -
// not at the last path's end; the next one without a start goes on from this one's end; a start not of three numbers
// is refused.
void testGoalPathStart() {
    SceneGraph g = planarArm(2);
    const uint32_t goal = addGoal(g, 3);
    const auto point = [](double x, double y, double z) {
        Value p = Value::list();
        p.push(x), p.push(y), p.push(z);
        return p;
    };
    const auto walk = [&](Value points, const Value* start, std::string& error) {
        Value args = Value::map(), answer = Value::map();
        args.set("goal", goal), args.set("points", points), args.set("speed", 0.1);
        if (start) args.set("start", *start), args.set("start_turn", point(0, 30, 0));
        return findSceneOp("assembly_path")->edit(g, args, answer, error);
    };
    std::string error;
    Value first = Value::list(), second = Value::list(), third = Value::list();
    first.push(point(0.5, 1.3, 0)), second.push(point(0.3, 1.4, 0)), third.push(point(0.2, 1.4, 0));
    const Value here = point(0.45, 1.32, 0);
    Value shortStart = Value::list();
    shortStart.push(0.4), shortStart.push(1.3);
    const bool ok1 = walk(first, nullptr, error), ok2 = walk(second, &here, error);
    const Entity* e = nullptr;
    for (const Entity& x : g.entities) e = x.id == goal ? &x : e;
    const Vector3 began = pathPoint(*e, 0), turned = e->rotationDeg;
    const bool ok3 = walk(third, nullptr, error);
    const Vector3 next = pathPoint(*e, 0);
    std::string refused;
    const bool rejected = !walk(third, &shortStart, refused);
    std::printf("  re-planned from (%.2f, %.2f), turned %.0f degrees; the next path from (%.2f, %.2f); a start of two numbers: %s\n", double(began.x),
                double(began.y), double(turned.y), double(next.x), double(next.y), refused.c_str());
    CHECK(ok1 && ok2 && ok3 && length(began - Vector3(0.45_r, 1.32_r, 0)) < 1e-6_r && std::fabs(turned.y - 30) < 1e-6_r &&
              length(next - Vector3(0.3_r, 1.4_r, 0)) < 1e-6_r && rejected,
          "a path's start is off: from (%g %g), the next from (%g %g), %s", double(began.x), double(began.y), double(next.x), double(next.y), error.c_str());
}

// A turn on the spot walked as length (Entity::pathTurnLength): a quarter turn at 0.1 m a radian, 0.1 m/s, takes
// pi/2 s and is half way round at half that time; the path's file keeps the weight, save - load - save to the bit.
void testGoalPathTurnLength() {
    Entity goal;
    goal.id = 2, goal.goalOf = 1, goal.path = {{0, 0, 0}}, goal.pathTurns = {{0, 90, 0}}, goal.pathSpeed = 0.1_r, goal.pathTurnLength = 0.1_r;
    Vector3 p;
    Quaternion q;
    bool done = false;
    pathPose(goal, kPi / 4, p, q, &done);
    const Quaternion half = Quaternion::fromAxisAngle({0, 1, 0}, kPi / 4);
    const Real off = 1 - std::fabs(dot(q, half)), total = pathLength(goal);
    SceneGraph g, back, again;
    g.entities.push_back(goal);
    std::string why;
    const bool kept = back.load(g.save(), why) && again.load(back.save(), why) && again.save() == back.save() && back.entities[0].pathTurnLength == goal.pathTurnLength;
    std::printf("  a quarter turn on the spot at 0.1 m a radian: %.4f m long (pi/20 = %.4f), at pi/4 s 1 - |q . q45| = %.1e, done %d; the weight in the file %s\n",
                double(total), double(kPi / 20), double(off), int(done), kept ? "kept" : "lost");
    CHECK(std::fabs(total - kPi / 20) < 1e-5_r && off < 1e-6_r && !done && kept, "a turn walked as length is off: %g, %s", double(total), why.c_str());
}

void testSmoothPath() {
    static constexpr double kPiArm = 3.14159265358979323846;
    Entity goal;
    goal.goalOf = 1, goal.pathSpeed = 1, goal.path = {{1, 0, 0}, {1, 1, 0}, {0, 1, 1}}, goal.pathTurns = {{0, 90, 0}, {45, 90, 30}, {90, 0, 90}};
    auto walk = [&goal](bool smooth, double& corner, double& speedOff, double& rateJump, double& near, double& turnOff) {
        goal.pathSmooth = smooth;
        const Real ds = 0.001_r, total = pathLength(goal);
        corner = speedOff = rateJump = turnOff = 0, near = 1e9;
        std::vector<double> nearest(goal.path.size(), 1e9);
        std::vector<Quaternion> at(goal.path.size());
        Vector3 p0, p1, p2;
        Quaternion q0, q1, q2;
        pathPose(goal, 0, p0, q0), pathPose(goal, ds, p1, q1);
        for (Real s = 2 * ds; s < total; s += ds) {
            pathPose(goal, s, p2, q2);
            const Vector3 a = p1 - p0, b = p2 - p1;
            corner = std::max(corner, std::acos(std::min(1.0, double(dot(a, b) / (length(a) * length(b))))) * 180 / kPiArm);
            speedOff = std::max(speedOff, std::fabs(double(length(b)) / double(ds) - 1));
            auto spin = [](const Quaternion& from, const Quaternion& to) { // the step's turn as a vector, axis times angle
                Quaternion d = (to * from.conjugate()).normalized();
                if (d.w < 0) d = Quaternion{-d.w, -d.x, -d.y, -d.z};
                return Vector3(d.x, d.y, d.z) * 2.0_r;
            };
            rateJump = std::max(rateJump, double(length(spin(q1, q2) - spin(q0, q1))) * 180 / kPiArm);
            for (size_t k = 0; k < goal.path.size(); ++k)
                if (double(length(p2 - goal.path[k])) < nearest[k]) nearest[k] = double(length(p2 - goal.path[k])), at[k] = q2;
            p0 = p1, p1 = p2, q0 = q1, q1 = q2;
        }
        near = *std::max_element(nearest.begin(), nearest.end());
        for (size_t k = 0; k < goal.path.size(); ++k) {
            SceneObject o;
            o.rotationDeg = goal.pathTurns[k];
            turnOff = std::max(turnOff, 2 * std::acos(std::min(1.0, std::fabs(double(dot(at[k], objectRotation(o)))))) * 180 / kPiArm);
        }
    };
    double c1, v1, j1, n1, t1, c0, v0, j0, n0, t0;
    walk(true, c1, v1, j1, n1, t1);
    walk(false, c0, v0, j0, n0, t0);
    std::printf("  flight (spline + SQUAD), %.3f m: heading turns at most %.2f deg a step (polyline %.1f), speed off %.2f %%, spin jumps %.3f deg "
                "(slerp %.3f), points passed within %.2f mm, turned as they say within %.2f deg\n",
                double((goal.pathSmooth = true, pathLength(goal))), c1, c0, 100 * v1, j1, j0, 1000 * n1, t1);
    CHECK(c1 < 3 && c0 > 80 && v1 < 0.01 && n1 < 1e-3 && t1 < 0.5, "the flight is not smooth, steady and through its points turned as they say");
    CHECK(j1 < 0.5 * j0, "SQUAD's spin jumps as much as slerp's at the points: %g against %g", j1, j0);
}
