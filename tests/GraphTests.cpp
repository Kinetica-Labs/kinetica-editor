// The scene file of a robot: save -> load -> save gives the same text whatever the C library's locale;
// the physics line is written only when it differs from the defaults and comes back.
#include "TestRunner.h"

#include "scene/SceneEdits.h"
#include "scene/SceneGraph.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

using namespace ke;

namespace {

// A base, an arm link on a driven hinge, an encoder and an IMU on it, a battery and a driver for a
// wheel motor, a point wheel, a controller and an IK goal on a path, a light, a camera and a force.
SceneGraph robotGraph() {
    SceneGraph g;
    g.world.gravity = {0, -9.81_r, 0};
    g.world.size = {3, 2, 2};
    g.world.physics.substeps = 8;
    g.world.physics.jointHertz = 120;
    Entity base;
    base.id = 1, base.name = "База \"A\""; // quotes in a name become apostrophes
    base.size = {0.3_r, 0.1_r, 0.3_r};
    base.position = {0, 0.05_r, 0};
    base.rigid.enabled = base.collider.enabled = true;
    base.rigid.fixed = true;
    base.locked = true;
    Entity link = base;
    link.id = 2, link.name = "Плечо";
    link.locked = false;
    link.rigid.fixed = false;
    link.rigid.density = 2700;
    link.size = {0.05_r, 0.4_r, 0.05_r};
    link.position = {0, 0.3_r, 0};
    link.rotationDeg = {10, 20, 30};
    link.collider.kind = ColliderKind::Capsule;
    link.texture.kind = TextureKind::Checker;
    link.texture.normalFile = "maps/steel normal.png";
    Entity goal;
    goal.id = 3, goal.name = "Цель";
    goal.shape = ShapeKind::Sphere;
    goal.size = Vector3(0.04_r);
    goal.goalOf = 2;
    goal.path = {{0.2_r, 0.5_r, 0}, {0.1_r, 0.6_r, 0.1_r}};
    goal.pathSpeed = 0.25_r, goal.pathLoop = true;
    g.entities = {base, link, goal};
    JointObject j;
    j.id = 4, j.name = "Плечевой", j.a = 2, j.b = 1;
    j.anchorA = {0, -0.2_r, 0}, j.anchorB = {0, 0.05_r, 0};
    j.dof[DofRx].motion = JointMotion::Limited;
    j.dof[DofRx].lower = -1.5_r, j.dof[DofRx].upper = 1.5_r;
    j.dof[DofRx].drive.enabled = true;
    j.dof[DofRx].drive.stiffness = 400, j.dof[DofRx].drive.damping = 20, j.dof[DofRx].drive.maxForce = 30;
    g.joints = {j};
    SensorObject encoder;
    encoder.id = 5, encoder.name = "Энкодер", encoder.parent = 4, encoder.lines = 1024;
    SensorObject imu;
    imu.id = 6, imu.name = "IMU", imu.parent = 2, imu.kind = SensorKind::Imu, imu.seed = 7;
    g.sensors = {encoder, imu};
    BatteryObject pack;
    pack.id = 7, pack.name = "Батарея", pack.series = 3, pack.soc = 0.75_r;
    WheelObject wheel;
    wheel.id = 8, wheel.name = "Колесо", wheel.parent = 1, wheel.constant = 0.02_r, wheel.voltage = 6;
    DriverObject driver;
    driver.id = 9, driver.name = "Мост", driver.joint = 8, driver.battery = 7, driver.duty = 0.5_r;
    g.batteries = {pack};
    g.wheels = {wheel};
    g.drivers = {driver};
    ControllerObject panel;
    panel.id = 10, panel.name = "Пульт";
    ControllerAction move;
    move.name = "move", move.value = ControlValue::Vector2, move.drive = ControlDrive::Plane, move.target = 3, move.speed = 0.15_r;
    move.bindings = {{"keys", {"W", "S", "A", "D"}, false, false}};
    panel.actions = {move};
    g.controllers = {panel};
    Light sun;
    sun.id = 11, sun.name = "Солнце", sun.kind = LightKind::Sun, sun.shadows = true;
    Camera eye;
    eye.id = 12, eye.name = "Камера", eye.position = {1, 1, 1}, eye.active = true;
    Force blast;
    blast.id = 13, blast.name = "Взрыв", blast.shape = ForceShape::Radial, blast.strength = 10, blast.radius = 3,
    blast.falloff = ForceFalloff::Linear, blast.start = 2.5_r, blast.tag = "crate", blast.tags = {"blast"};
    g.lights = {sun};
    g.cameras = {eye};
    g.forces = {blast};
    return g;
}

} // namespace

// The format's promise (docs/contracts/operations.md): it only grows. tests/data/scene-format-1.kscene holds every
// line and key the format had when the promise was made and is never edited: it loads, and every word of it - a
// key or a value - is still in what is saved (new keys with their defaults may join them). A key under the name it
// had before the format settled on one style loads too and is saved under the new one.
// A shape's opacity - how solid it looks, 1 .. 0 - goes to the file only when it is below 1 and comes back; a number
// outside 0 .. 1 is refused. Physics does not know it: the frame a view draws carries it.
void testSceneOpacity() {
    SceneGraph g;
    Entity glass;
    glass.id = 2, glass.name = "Стекло", glass.opacity = 0.3_r;
    Entity wall;
    wall.id = 3, wall.name = "Стена";
    g.entities.push_back(glass), g.entities.push_back(wall);
    const std::string text = g.save();
    SceneGraph back;
    std::string error;
    CHECK(back.load(text, error) && back.save() == text && back.entities[0].opacity == 0.3_r && back.entities[1].opacity == 1,
          "the opacity comes back from the file: %s", error.c_str());
    CHECK(text.find("opacity 0.3") != std::string::npos && text.find("opacity 1") == std::string::npos, "the word is written for a see-through shape only");
    const std::string head = "entity \"X\"\n  object id 9\n  shape box size 1 1 1 ";
    CHECK(!back.load(head + "opacity 1.5\nend\n", error) && !back.load(head + "opacity -0.1\nend\n", error) && back.load(head + "opacity 0\nend\n", error),
          "an opacity outside 0 .. 1 is refused");
}

void testSceneFormatOnlyGrows() {
    std::string error;
    // 2: what the format gained after 1 - kinematic, tags; 3: a plugin's component on what is no shape; 4: a given mass;
    // 5: a shape's opacity; 6: a body's own gravity and damping; 7: what a collider touches - trigger, ignores; 8: the force block;
    // 9: the world line's play range end, duration; 10: a texture's normal map; 11: the take's start, record_from; 12: a lidar; 13: a mesh collider
    for (const char* name : {"scene-format-1.kscene", "scene-format-2.kscene", "scene-format-3.kscene", "scene-format-4.kscene",
                             "scene-format-5.kscene", "scene-format-6.kscene", "scene-format-7.kscene", "scene-format-8.kscene",
                             "scene-format-9.kscene", "scene-format-10.kscene", "scene-format-11.kscene", "scene-format-12.kscene", "scene-format-13.kscene"}) {
        std::ifstream file(std::string(KE_TEST_DATA) + "/" + name, std::ios::binary);
        std::stringstream sample;
        sample << file.rdbuf();
        SceneGraph g;
        const bool loaded = !sample.str().empty() && g.load(sample.str(), error);
        CHECK(loaded, "the format's sample %s does not load: %s", name, error.c_str());
        std::map<std::string, int> left;
        size_t words = 0;
        for (std::string w; sample >> w; ++words) ++left[presentKey(w)]; // a key under its present name: spin is angular_velocity
        std::istringstream saved(loaded ? g.save() : std::string());
        for (std::string w; saved >> w;) --left[w];
        std::string lost;
        for (const auto& [word, count] : left)
            if (count > 0) lost += " " + word;
        std::printf("  %s: %zu words, lost on load and save:%s\n", name, words, lost.empty() ? " none" : lost.c_str());
        CHECK(lost.empty(), "words of the format's sample %s are not in what is saved:%s", name, lost.c_str());
    }

    SceneGraph old;
    const std::string before = "world gravity 0 -9.81 0 size 4 3 4\nphysics substeps 16 contactHertz 300 jointDamping 7\n"
                               "joint \"J\"\n  object id 5\n  link a 1 b 2 anchorA 1 2 3 frameB 4 5 6\nend\n";
    CHECK(old.load(before, error), "a file with the keys' earlier names does not load: %s", error.c_str());
    const std::string now = old.save();
    auto has = [&now](const char* text) { return now.find(text) != std::string::npos; };
    CHECK(has("contact_hertz 300") && has("joint_damping 7") && has("anchor_a 1 2 3") && has("frame_b 4 5 6") && !has("contactHertz") &&
          !has("anchorA"), "the keys' earlier names are not saved under the new ones:\n%s", now.c_str());
    CHECK(!old.load("joint \"J\"\n  object id 5\n  link a 1 b 2 anchorA 1 2 3 anchor_a 1 2 3\nend\n", error),
          "a key given twice, under its earlier name and the new one, is read");

    // A script written before the rename names a key the old way: it sets and reads the same field.
    CHECK(old.load(before, error), "a file with the keys' earlier names does not load: %s", error.c_str());
    const uint32_t set = setField(old, 5, "link", "anchorA", {"7", "8", "9"}, error);
    CHECK(set == 5 && old.joints[0].anchorA.x == 7 && old.joints[0].anchorA.z == 9, "a key's earlier name does not set its field: %s", error.c_str());
    const std::vector<BlockWord> read = getField(getObject(old, 5), "link", "frameB", 3);
    CHECK(read.size() == 3 && read[0].text == "4" && read[2].text == "6", "a key's earlier name does not read its field");
    CHECK(setWorldField(old, "physics", "contactHertz", {"120"}, error) && old.world.physics.contactHertz == 120,
          "a key's earlier name does not set the world's field: %s", error.c_str());
    CHECK(old.save().find("anchorA") == std::string::npos && old.save().find("contactHertz") == std::string::npos,
          "a field set under a key's earlier name is saved under it");
    // A plugin's component spells its keys itself: one that looks like a key's earlier name is found and kept as it is.
    SceneGraph own;
    CHECK(own.load("entity \"X\"\n  object id 5\n  component wear.meter linearDamping 3\nend\n", error), "a component's key: %s", error.c_str());
    const uint32_t kept = setField(own, 5, "component wear.meter", "linearDamping", {"4"}, error);
    const uint32_t added = setField(own, 5, "component wear.meter", "heightFile", {"9"}, error);
    const std::vector<PluginComponent> none, &meter = kept && added ? own.entities[0].components : none;
    CHECK(meter.size() == 1 && meter[0].values.size() == 2 && meter[0].values[0] == std::make_pair(std::string("linearDamping"), std::string("4")) &&
          meter[0].values[1].first == "heightFile", "a component's key that looks like an earlier name was renamed or doubled: %s", error.c_str());
    // An action named as a number is the same word on its action line and on its bind line.
    SceneGraph panel, again;
    CHECK(panel.load("controller \"Пульт\"\n  object id 9\n  action 7 axis drive grip target 30 speed 2\n  bind 7 keys Space toggle\nend\n", error),
          "a controller: %s", error.c_str());
    const std::string written = panel.save();
    CHECK(written.find("action \"7\"") != std::string::npos && written.find("bind \"7\"") != std::string::npos && again.load(written, error) &&
          again.save() == written, "an action named as a number is not written as a name on both its lines:\n%s", written.c_str());
}

void testSceneGraphRoundTrip() {
    const std::string text = robotGraph().save();
    SceneGraph back;
    std::string error;
    CHECK(back.load(text, error), "the saved scene does not load: %s", error.c_str());
    const std::string again = back.save();
    std::printf("  scene file: %zu bytes, %zu entities, %zu joints, %zu sensors; save -> load -> save %s\n", text.size(),
                back.entities.size(), back.joints.size(), back.sensors.size(), again == text ? "identical" : "DIFFERENT");
    CHECK(again == text, "save -> load -> save changed the text:\n%s\n---\n%s", text.c_str(), again.c_str());
    CHECK(back.entities.size() == 3 && back.entities[0].name == "База 'A'" && back.entities[0].locked, "the base is not restored");
    CHECK(back.entities[1].rotationDeg.z == 30.0_r && back.entities[1].collider.kind == ColliderKind::Capsule &&
          back.entities[1].rigid.density == 2700 && back.entities[1].texture.kind == TextureKind::Checker &&
          back.entities[1].texture.normalFile == "maps/steel normal.png" && text.find(" normal_file \"maps/steel normal.png\"") != std::string::npos &&
          back.entities[0].texture.normalFile.empty(), "the link is not restored");
    CHECK(back.entities[2].goalOf == 2 && back.entities[2].path.size() == 2 && back.entities[2].pathLoop, "the goal's path is not restored");
    const JointFreedom& turn = back.joints.at(0).dof[DofRx];
    CHECK(turn.motion == JointMotion::Limited && turn.lower == -1.5_r && turn.drive.enabled && turn.drive.stiffness == 400 &&
          turn.drive.maxForce == 30, "the joint's limit or drive is not restored");
    CHECK(back.sensors.size() == 2 && back.sensors[0].lines == 1024 && back.sensors[1].kind == SensorKind::Imu && back.sensors[1].seed == 7,
          "the sensors are not restored");
    CHECK(back.batteries.size() == 1 && back.batteries[0].soc == 0.75_r && back.drivers.size() == 1 && back.drivers[0].duty == 0.5_r &&
          back.wheels.size() == 1 && back.wheels[0].voltage == 6, "the battery, the driver or the wheel is not restored");
    CHECK(back.controllers.size() == 1 && back.controllers[0].actions.at(0).bindings.at(0).controls.size() == 4,
          "the controller is not restored");
    CHECK(back.lights.size() == 1 && back.lights[0].shadows && back.cameras.size() == 1 && back.cameras[0].active,
          "the light or the camera is not restored");
    CHECK(back.forces.size() == 1 && back.forces[0].shape == ForceShape::Radial && back.forces[0].mode == ForceMode::Impulse &&
          back.forces[0].strength == 10 && back.forces[0].radius == 3 && back.forces[0].falloff == ForceFalloff::Linear &&
          back.forces[0].start == 2.5_r && back.forces[0].tag == "crate" && back.forces[0].tags == std::vector<std::string>{"blast"},
          "the force is not restored");
}

// A force the core could not act on never loads: every refusal names its line.
void testSceneForceFile() {
    const std::string head = "force \"F\"\n  object id 4\n  effect ";
    const char* bad[] = {"shape radial mode impulse strength 1\n", "strength nan\n", "shape sphere\n", "strength 1 strength 2\n",
                         "tag \"a b\"\n", "strength 1e30\n", "radius 1e9\n", "start 1e9\n", "start -1\n", "falloff steep\n", "weight 1\n"};
    for (const char* line : bad) {
        SceneGraph g;
        std::string error;
        CHECK(!g.load(head + line + "end\n", error) && error.compare(0, 8, "line 3: ") == 0, "a force with '%s' is read: %s", line, error.c_str());
    }
    SceneGraph g, back;
    std::string error;
    CHECK(g.load(head + "radius 2 shape radial mode constant strength -4 tag 7\nend\n", error) && g.forces[0].tag == "7", "a force: %s", error.c_str());
    const std::string text = g.save();
    CHECK(text.find("tag \"7\"") != std::string::npos && text.find("falloff") == std::string::npos && text.find("start") == std::string::npos &&
          back.load(text, error) && back.save() == text, "a numeric tag, or the unset words, are not written as the format says:\n%s", text.c_str());
    CHECK(!back.load("force \"F\"\n  object id 4\n  kind point\nend\n", error), "a line that is no effect line is read in a force");
    CHECK(SceneGraph().save().find("force") == std::string::npos, "a scene without forces wrote a force block");
}

void testScenePhysicsLine() {
    SceneGraph plain;
    const bool silent = plain.save().find("\nphysics ") == std::string::npos;
    SceneGraph back;
    std::string error;
    const bool loaded = back.load(robotGraph().save(), error);
    const PhysicsSettings& p = back.world.physics;
    std::printf("  physics line: %s by default; substeps %d, joint %g Hz after a round trip\n", silent ? "left out" : "WRITTEN",
                p.substeps, double(p.jointHertz));
    CHECK(silent, "a scene with the default physics wrote a physics line");
    CHECK(loaded && p.substeps == 8 && p.jointHertz == 120 && p.contactHertz == PhysicsSettings{}.contactHertz && p.sleeping,
          "the physics line did not come back");
    SceneGraph bad;
    CHECK(!bad.load("world gravity 0 -9.81 0\nphysics substeps 0\n", error) && error.find("substeps") != std::string::npos,
          "substeps 0 was taken: %s", error.c_str());
    SceneGraph ranged, again; // the play range's end: written only when set
    const bool took = ranged.load("world gravity 0 -9.81 0 size 4 3 4 duration 2.5\n", error) && again.load(ranged.save(), error);
    CHECK(took && ranged.world.duration == 2.5_r && again.world.duration == 2.5_r, "the play range's end did not come back: %s", error.c_str());
    CHECK(SceneGraph().save().find("duration") == std::string::npos, "a scene without a play range wrote a duration");
    CHECK(!bad.load("world gravity 0 -9.81 0 duration -1\n", error), "a negative duration was taken");
    CHECK(SceneGraph().save().find("record_from") == std::string::npos, "a scene whose take starts at 0 wrote record_from");
    CHECK(!bad.load("world gravity 0 -9.81 0 duration 5 record_from 5\n", error) && error.find("record_from") != std::string::npos,
          "a take starting where the run ends was taken: %s", error.c_str());
    CHECK(bad.load("world gravity 0 -9.81 0 record_from 30\n", error), "a take's start with no end was refused: %s", error.c_str());
}

void testSceneGraphLocale() {
    const std::string text = robotGraph().save();
    const char* chosen = nullptr;
    for (const char* name : {"ru_RU.UTF-8", "ru_UA.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "Russian_Russia.1251", "German_Germany.1252", "de-DE"})
        if (std::setlocale(LC_NUMERIC, name) && std::localeconv()->decimal_point[0] == ',') {
            chosen = name;
            break;
        }
    if (!chosen) {
        std::setlocale(LC_NUMERIC, "C");
        std::printf("  no locale with a decimal comma installed here: skipped\n");
        return;
    }
    const std::string there = robotGraph().save();
    SceneGraph back;
    std::string error;
    const bool loaded = back.load(text, error);
    std::setlocale(LC_NUMERIC, "C");
    std::printf("  C library in %s: the file %s, it %s\n", chosen, there == text ? "is the same" : "CHANGED", loaded ? "loads" : "does not load");
    CHECK(there == text, "the scene file depends on the locale %s:\n%s", chosen, there.c_str());
    CHECK(loaded && back.save() == text, "a file does not load in the locale %s: %s", chosen, error.c_str());
}
