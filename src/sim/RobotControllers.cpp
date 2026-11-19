// The scene's controllers in the running world (RobotWorld.h, setControllerInput): every step each action's value - set
// from a person's keys, a pad, a script, a policy - moves what the action drives, by its speed over the step. An IK goal
// moved so (plane, lift, turn) takes its arm along, as leadGoals leads any goal; the goal starts where it is and is held
// on a leash: a move that would put it farther than kLeash from the hand does not happen, so a goal pushed into the
// table or out of reach waits for the hand instead of running off. The gripper's fingers close by the value's share
// (grip), a joint's servo target walks (jog), a driver's duty is set (duty). Deterministic: the same values in the same
// steps give the same bits, in a window or without one.
#include "sim/RobotWorld.h"

#include "scene/Assembly.h"

#include <algorithm>

namespace ke {

namespace {

constexpr Real kLeash = 0.05_r;  // m: the farthest a controller takes its goal from the hand
constexpr Real kOpenForce = 40;  // N: the fingers opening, as assembly_grip opens them

const ControllerAction* actionOf(const SceneGraph& g, uint32_t controller, const std::string& action, std::string& error) {
    for (const ControllerObject& c : g.controllers) {
        if (c.id != controller) continue;
        for (const ControllerAction& a : c.actions)
            if (a.name == action) return &a;
        return error = "the controller «" + c.name + "» has no action '" + action + "'", nullptr;
    }
    return error = "no controller " + std::to_string(controller), nullptr;
}

Entity* goalIn(SceneGraph& g, uint32_t id) {
    for (Entity& e : g.entities)
        if (e.id == id && e.goalOf != 0) return &e;
    return nullptr;
}

} // namespace

bool RobotWorld::setControllerInput(uint32_t controller, const std::string& action, const Vector3& value, std::string& error) {
    if (!actionOf(graph_, controller, action, error)) return false;
    auto live = std::find_if(controls_.begin(), controls_.end(), [&](const ControlLive& c) { return c.controller == controller && c.action == action; });
    if (live == controls_.end()) controls_.push_back({controller, action}), live = controls_.end() - 1;
    live->value = {std::clamp(value.x, -1.0_r, 1.0_r), std::clamp(value.y, -1.0_r, 1.0_r), std::clamp(value.z, -1.0_r, 1.0_r)};
    return true;
}

void RobotWorld::driveControllers(PhysicsWorld& physics, Real dt) {
    for (ControlLive& live : controls_) {
        std::string ignored;
        const ControllerAction* a = actionOf(graph_, live.controller, live.action, ignored);
        if (!a) continue;
        const Vector3 v = live.value;
        if (a->drive == ControlDrive::Grip) {
            const Real part = std::clamp(v.x, 0.0_r, 1.0_r);
            if (live.applied != part) driveGrip(physics, *a, part), live.applied = part;
        } else if (a->drive == ControlDrive::Jog) {
            jogJoint(physics, *a, v.x, dt);
        } else if (a->drive == ControlDrive::Duty) {
            for (SceneGraph* g : {&graph_, &live_})
                for (DriverObject& d : g->drivers)
                    if (d.id == a->target) d.duty = std::clamp(v.x * a->speed, -1.0_r, 1.0_r);
        } else {
            moveGoal(physics, *a, v, dt);
        }
    }
}

// A goal moved by one action over the step, from where it stands; held when the hand has not caught up.
void RobotWorld::moveGoal(PhysicsWorld& physics, const ControllerAction& a, const Vector3& v, Real dt) {
    Entity* goal = goalIn(live_, a.target);
    const bool still = a.drive == ControlDrive::Plane ? v.x == 0 && v.y == 0 : v.x == 0;
    if (still || !goal) return;
    Vector3 p;
    Quaternion q;
    worldPose(live_, *goal, p, q);
    const Real step = a.speed * dt;
    if (a.drive == ControlDrive::Turn) { // about the world's vertical, degrees/s
        q = (Quaternion::fromAxisAngle({0, 1, 0}, v.x * step * (kPi / 180)) * q).normalized();
    } else {
        const Vector3 to = p + (a.drive == ControlDrive::Plane ? Vector3(v.x, 0, -v.y) : Vector3(0, v.x, 0)) * step;
        const int hand = bodyOf(goal->goalOf);
        if (hand >= 0 && physics.alive(hand)) {
            const Vector3 at = physics.state(hand).position;
            if (length(to - at) > kLeash && length(to - at) > length(p - at)) return; // on the leash: the hand has not caught up
        }
        p = to;
    }
    for (SceneGraph* g : {&live_, &graph_})
        if (Entity* e = goalIn(*g, a.target)) setWorldPose(*g, *e, p, q);
}

// The fingers of the goal's effector - its sliders - sent `part` of their travel from open, capped at the action's
// force (its speed); opening (part 0) with the opening force.
void RobotWorld::driveGrip(PhysicsWorld& physics, const ControllerAction& a, Real part) {
    const Entity* goal = goalIn(live_, a.target);
    const uint32_t effector = goal ? goal->goalOf : a.target;
    for (JointObject& j : live_.joints) {
        const int dof = mainFreedom(j);
        if (dof < 0 || dof > 2 || (j.a != effector && j.b != effector) || j.dof[dof].motion == JointMotion::Locked) continue;
        JointDrive& d = j.dof[dof].drive;
        d.enabled = true;
        d.target = j.dof[dof].lower + part * (j.dof[dof].upper - j.dof[dof].lower);
        d.maxForce = part > 0 ? a.speed : kOpenForce;
        for (LiveJoint& live : joints_)
            if (live.spec.id == j.id) setDrive(physics, live, j);
    }
}

// A joint's servo target moved at the action's speed (m/s, or degrees/s for a turn), within the freedom's limits.
void RobotWorld::jogJoint(PhysicsWorld& physics, const ControllerAction& a, Real v, Real dt) {
    if (v == 0 || a.dof < 0 || a.dof > 5) return;
    for (JointObject& j : live_.joints) {
        if (j.id != a.target) continue;
        const Real perSecond = a.dof >= DofRx ? a.speed * (kPi / 180) : a.speed;
        Real& target = j.dof[a.dof].drive.target;
        target += v * perSecond * dt;
        if (j.dof[a.dof].motion == JointMotion::Limited) target = std::clamp(target, j.dof[a.dof].lower, j.dof[a.dof].upper);
        for (LiveJoint& live : joints_)
            if (live.spec.id == j.id) setDrive(physics, live, j);
    }
}

} // namespace ke
