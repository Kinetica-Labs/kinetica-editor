// The operations of controllers (SceneGraph.h's ControllerObject): controller_add puts a ready panel for an arm's
// IK goal - a scheme to record teleoperated demonstrations with (keyboard and gamepad): move across the floor (W S A D, the left stick), lift (R F, the right stick up and
// down), turn about the vertical (Z X, the right stick sideways), grip (Space or the left mouse button, the right
// trigger - how far it is pulled). Its actions and bindings are then the scene's data, changed as any object's
// (scene_get / scene_put). controller_input sets the actions' values on a running scene (src/sim).
#include "scene/SceneOps.h"

#include "scene/Assembly.h"

namespace ke {

namespace {

ControlBinding bind(const char* device, std::vector<std::string> controls, bool invert = false, bool toggle = false) {
    ControlBinding b;
    b.device = device, b.controls = std::move(controls), b.invert = invert, b.toggle = toggle;
    return b;
}

ControllerAction action(const char* name, ControlValue value, ControlDrive drive, uint32_t target, Real speed, std::vector<ControlBinding> b) {
    ControllerAction a;
    a.name = name, a.value = value, a.drive = drive, a.target = target, a.speed = speed, a.bindings = std::move(b);
    return a;
}

bool hasFingers(const SceneGraph& g, uint32_t effector) {
    for (const JointObject& j : g.joints) {
        const int dof = mainFreedom(j);
        if (dof >= 0 && dof <= 2 && (j.a == effector || j.b == effector) && j.dof[dof].motion != JointMotion::Locked) return true;
    }
    return false;
}

// The arm's panel: speeds a person steers at (15 cm/s across, 10 cm/s up, 60 degrees/s), the grip capped at 2 N.
ControllerObject armPanel(const Entity& goal, const std::string& effector, bool fingers) {
    ControllerObject c;
    c.name = "Пульт «" + effector + "»";
    c.actions.push_back(action("move", ControlValue::Vector2, ControlDrive::Plane, goal.id, 0.15_r,
                               {bind("keys", {"W", "S", "A", "D"}), bind("gamepad", {"left_stick"})}));
    c.actions.push_back(action("lift", ControlValue::Axis, ControlDrive::Lift, goal.id, 0.10_r,
                               {bind("keys", {"R", "F"}), bind("gamepad", {"right_y"})}));
    c.actions.push_back(action("turn", ControlValue::Axis, ControlDrive::Turn, goal.id, 60.0_r,
                               {bind("keys", {"Z", "X"}), bind("gamepad", {"right_x"}, true)}));
    if (fingers)
        c.actions.push_back(action("grip", ControlValue::Axis, ControlDrive::Grip, goal.id, 2.0_r,
                                   {bind("keys", {"Space"}, false, true), bind("mouse", {"left"}), bind("gamepad", {"right_trigger"}),
                                    bind("gamepad", {"a"}, false, true)}));
    return c;
}

// A joint's panel: one jog action on its main freedom (a servo's target moved at speed while the value is held).
bool addJointController(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const JointObject* j = findJoint(g, uint32_t(a["joint"].number()));
    const int dof = j ? mainFreedom(*j) : -1;
    if (!j || dof < 0) return e = "joint is the id of a joint with a free or limited freedom (a hinge, a slider)", false;
    if (!j->dof[dof].drive.enabled || j->dof[dof].drive.stiffness <= 0)
        return e = j->name + " has no servo to move (assembly_actuator makes a joint with one, assembly_drive kind servo adds it)", false;
    const bool turn = dof >= DofRx;
    const Real speed = Real(a["speed"].number(turn ? 0.5 : 0.05)); // rad/s or m/s
    ControllerAction x = action(a["action"].text("move").c_str(), ControlValue::Axis, ControlDrive::Jog, j->id, turn ? speed * (180.0_r / kPi) : speed, {});
    x.dof = dof;
    ControllerObject c;
    c.id = nextId(g), c.name = a["name"].text("Пульт «" + j->name + "»"), c.actions.push_back(x);
    g.controllers.push_back(c);
    Value names = Value::list();
    names.push(x.name);
    r.set("id", c.id), r.set("actions", names);
    return true;
}

bool addController(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    if (a.has("joint")) return addJointController(g, a, r, e);
    const uint32_t id = uint32_t(a["goal"].number());
    const Entity* goal = nullptr;
    for (const Entity& x : g.entities)
        if (x.id == id && x.goalOf != 0) goal = &x;
    if (!goal) return e = "goal is the id of an IK goal (assembly_goal makes one), not " + std::to_string(id), false;
    if (a["preset"].text("arm") != "arm") return e = "preset is 'arm'", false;
    std::string effector = goal->name;
    for (const Entity& x : g.entities)
        if (x.id == goal->goalOf) effector = x.name;
    ControllerObject c = armPanel(*goal, effector, hasFingers(g, goal->goalOf));
    c.id = nextId(g);
    if (a.has("name")) c.name = a["name"].text(c.name);
    g.controllers.push_back(c);
    Value names = Value::list();
    for (const ControllerAction& x : c.actions) names.push(x.name);
    r.set("id", c.id), r.set("actions", names);
    return true;
}

} // namespace

std::vector<SceneOp> controllerSceneOps() {
    return {{"controller_add",
             "A controller: for an arm's IK goal (preset 'arm') actions move (across the floor), lift, turn and - when the goal's "
             "effector has fingers - grip, bound to W S A D, R F, Z X, Space and the left mouse button, and to a game controller's "
             "sticks, right trigger and A; for a joint with a servo one axis action (`action`, default 'move') that moves its target "
             "at speed (rad/s or m/s) while its value is held (-1..1). Its actions and bindings are then the scene's data "
             "(scene_get / scene_put); controller_input sets its values while the scene runs",
             "{goal: int, preset?: 'arm', name?: str} | {joint: int, action?: str, speed?: number, name?: str}", {}, addController, false}};
}

} // namespace ke
