// What a built world takes from an edited scene (scene/LiveEdit.h), one case per Reload value and per flag, from small
// graphs: a base scene, one change, the verdict - and the order when two sections change at once.
#include "TestRunner.h"

#include "scene/LiveEdit.h"

#include <functional>

namespace {

using Reload = LiveEdit::Reload;

SceneGraph base() {
    SceneGraph g;
    g.entities.resize(2);
    g.entities[0].id = 1;
    g.entities[1].id = 2;
    g.drivers.resize(1);
    g.joints.resize(1);
    g.lights.resize(1);
    g.forces.resize(1);
    g.cameras.resize(1);
    return g;
}

LiveEdit after(const std::function<void(SceneGraph&)>& change) {
    const SceneGraph running = base();
    SceneGraph edited = running;
    change(edited);
    return liveEdit(running, edited);
}

bool only(const LiveEdit& le, bool duties, bool lights, bool goals, bool joints, bool forces = false) {
    return le.reload == Reload::None && le.duties == duties && le.lights == lights && le.goals == goals && le.joints == joints && le.forces == forces;
}

} // namespace

void testLiveEditClassifier() {
    CHECK(only(liveEdit(base(), base()), false, false, false, false), "the same scene asks for nothing");
    CHECK(after([](SceneGraph& g) { g.world.gravity.y = -5; }).reload == Reload::World, "gravity needs a new world");
    CHECK(after([](SceneGraph& g) { g.groups.resize(1); }).reload == Reload::Groups, "a group needs a new world");
    CHECK(after([](SceneGraph& g) { g.arrays.resize(1); }).reload == Reload::Arrays, "an array needs a new world");
    CHECK(after([](SceneGraph& g) { g.sensors.resize(1); }).reload == Reload::Devices, "a sensor needs a new world");
    CHECK(after([](SceneGraph& g) { g.wheels.resize(1); }).reload == Reload::Devices, "a wheel needs a new world");
    CHECK(after([](SceneGraph& g) { g.drivers[0].brake = true; }).reload == Reload::Devices, "a driver's brake needs a new world");
    CHECK(after([](SceneGraph& g) { g.entities[1].size = Vector3(2.0_r); }).reload == Reload::Bodies, "a body's size needs a new world");
    CHECK(after([](SceneGraph& g) { g.entities.pop_back(); }).reload == Reload::Bodies, "a body gone needs a new world");

    CHECK(only(after([](SceneGraph& g) { g.drivers[0].duty = 0.5_r; }), true, false, false, false), "a duty is taken alone");
    CHECK(only(after([](SceneGraph& g) { g.lights[0].intensity = 2; }), false, true, false, false), "a light is taken");
    CHECK(only(after([](SceneGraph& g) { g.cameras[0].fovDeg = 70; }), false, true, false, false), "a camera is taken");
    CHECK(only(after([](SceneGraph& g) { g.entities.push_back(Entity()); g.entities.back().id = 3; g.entities.back().goalOf = 2; }), false, false, true, false),
          "a goal handle made is taken, no body");
    CHECK(after([](SceneGraph& g) { g.entities[1].goalOf = 2; }).reload == Reload::Bodies, "a body turned into a handle is a body gone");
    CHECK(only(after([](SceneGraph& g) { g.forces[0].strength = 9; }), false, false, false, false, true), "a force is taken, no reload");
    CHECK(after([](SceneGraph& g) { g.entities[0].position.y = 3; g.forces[0].strength = 9; }).reload == Reload::Bodies, "a body change beside a force still reloads");
    CHECK(only(after([](SceneGraph& g) { g.joints[0].name = "other"; }), false, false, false, true), "a joint is handed to the world");
    CHECK(only(after([](SceneGraph& g) { g.drivers[0].duty = 1; g.lights[0].range = 9; g.joints[0].name = "x"; }), true, true, false, true),
          "flags come together");

    CHECK(after([](SceneGraph& g) { g.world.gravity.y = -5; g.groups.resize(1); g.entities.pop_back(); }).reload == Reload::World,
          "the world is asked first");
    CHECK(after([](SceneGraph& g) { g.arrays.resize(1); g.sensors.resize(1); g.entities.pop_back(); }).reload == Reload::Arrays,
          "arrays before devices before bodies");
    const LiveEdit stopped = after([](SceneGraph& g) { g.sensors.resize(1); g.drivers[0].duty = 1; });
    CHECK(stopped.reload == Reload::Devices && !stopped.duties, "no flags beside a reload");

    CHECK(std::string(reloadWord(Reload::World)) == "world" && std::string(reloadWord(Reload::Groups)) == "groups" &&
          std::string(reloadWord(Reload::Arrays)) == "arrays" && std::string(reloadWord(Reload::Devices)) == "devices" &&
          std::string(reloadWord(Reload::Bodies)) == "bodies" && std::string(reloadWord(Reload::Joints)) == "joints" &&
          std::string(reloadWord(Reload::None)).empty(), "the words of the API");
}
