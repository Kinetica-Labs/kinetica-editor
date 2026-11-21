// The scene file of a robot: save -> load -> save gives the same text whatever the C library's locale;
// the physics line is written only when it differs from the defaults and comes back.
#include "TestRunner.h"

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
// wheel motor, a point wheel, a controller and an IK goal on a path, a light and a camera.
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
    g.lights = {sun};
    g.cameras = {eye};
    return g;
}

} // namespace

// The format's promise (docs/contracts/operations.md): it only grows. tests/data/scene-format-1.kscene holds every
// line and key the format had when the promise was made and is never edited: it loads, and every word of it - a
// key or a value - is still in what is saved (new keys with their defaults may join them). A key under the name it
// had before the format settled on one style loads too and is saved under the new one.
void testSceneFormatOnlyGrows() {
    std::string error;
    // 2: what the format gained after 1 - kinematic, tags; 3: a plugin's component on what is no shape; 4: a given mass
    for (const char* name : {"scene-format-1.kscene", "scene-format-2.kscene", "scene-format-3.kscene", "scene-format-4.kscene"}) {
        std::ifstream file(std::string(KE_TEST_DATA) + "/" + name, std::ios::binary);
        std::stringstream sample;
        sample << file.rdbuf();
        SceneGraph g;
        const bool loaded = !sample.str().empty() && g.load(sample.str(), error);
        CHECK(loaded, "the format's sample %s does not load: %s", name, error.c_str());
        std::map<std::string, int> left;
        size_t words = 0;
        for (std::string w; sample >> w; ++words) ++left[w];
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
          back.entities[1].rigid.density == 2700 && back.entities[1].texture.kind == TextureKind::Checker, "the link is not restored");
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
