// Inverse kinematics as an operation of the core (SceneOps.h, the IK in Kinematics.h): assembly_reach puts
// an effector's frame at a point with a turn - a quaternion or a basis of three axes (a rotation matrix,
// rows) - and writes the chain's coordinates as the targets of its servos, so the motors carry the arm there
// with its masses while the scene plays; or poses the scene's links there at once, the servos holding it (set: pose).
#include "scene/SceneOps.h"

#include "scene/Assembly.h"
#include "scene/Kinematics.h"

#include <cmath>

namespace ke {

namespace {

// The target turn from `quaternion` [w, x, y, z] or `basis` [[r00, r01, r02], [r10, ...], [r20, ...]] (rows); a
// basis nearly a rotation is straightened to the nearest one, a skewed one refused. False and `e`: bad.
bool turnArg(const Value& a, Quaternion& q, bool& given, std::string& e) {
    given = a.has("quaternion") || a.has("basis");
    if (a.has("quaternion")) {
        const auto& v = a["quaternion"].items();
        if (v.size() != 4) return e = "quaternion is [w, x, y, z]", false;
        q = Quaternion{Real(v[0].number()), Real(v[1].number()), Real(v[2].number()), Real(v[3].number())};
        if (!(length(Vector3(q.x, q.y, q.z)) + std::fabs(double(q.w)) > 0)) return e = "quaternion is [w, x, y, z], not zero", false;
        q = q.normalized();
        return true;
    }
    if (!a.has("basis")) return true;
    const auto& rows = a["basis"].items();
    Matrix3x3 R = Matrix3x3::identity();
    if (rows.size() != 3) return e = "basis is three rows of three numbers (a rotation matrix)", false;
    for (int i = 0; i < 3; ++i) {
        if (rows[size_t(i)].items().size() != 3) return e = "basis is three rows of three numbers (a rotation matrix)", false;
        for (int k = 0; k < 3; ++k) R.m[i][k] = Real(rows[size_t(i)].items()[size_t(k)].number());
    }
    const Matrix3x3 skew = R.transposed() * R;
    Real off = 0;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) off = std::max(off, Real(std::fabs(double(skew.m[i][k] - (i == k ? 1 : 0)))));
    if (off > 0.1_r) return e = "basis is not a rotation: its axes must be unit and at right angles", false;
    q = extractRotation(R, Quaternion::fromMatrix3x3(R)); // the nearest rotation
    return true;
}

bool reachOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const uint32_t effector = uint32_t(a["effector"].number());
    Vector3 position;
    Quaternion turn;
    bool turned = false;
    if (!findObject(g, effector)) return e = "no effector " + std::to_string(effector), false;
    if (!vectorArg(a, "position", position)) return e = "position is [x, y, z], metres in the world", false;
    if (!turnArg(a, turn, turned, e)) return false;
    const bool pose = a["set"].text("targets") == "pose";
    std::vector<uint32_t> loose;
    const IkResult ik = reachTargets(g, effector, position, turned ? &turn : nullptr, pose, &loose);
    if (ik.links.empty()) return e = "the effector hangs on no joint: nothing to move", false;
    Value joints = Value::list(), undriven = Value::list();
    for (uint32_t j : loose) undriven.push(j);
    for (const IkLink& l : ik.links) {
        Value x = Value::map();
        x.set("joint", l.joint), x.set("coordinate", double(l.coordinate));
        joints.push(x);
    }
    r.set("reached", ik.reached), r.set("position_error", double(ik.positionError)), r.set("angle_error", double(ik.angleError));
    r.set("iterations", ik.iterations), r.set("joints", joints), r.set("undriven", undriven);
    return true;
}

// assembly_can_reach: the same solve on a copy of the graph - nothing moves: whether the pose is reached, the errors,
// how far from a singularity, and each joint's room to its limits (the least of them: the tightest) - an agent's
// question before it moves the arm.
bool canReachOp(const SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const uint32_t effector = uint32_t(a["effector"].number());
    Vector3 position;
    Quaternion turn;
    bool turned = false;
    if (!findObject(g, effector)) return e = "no effector " + std::to_string(effector), false;
    if (!vectorArg(a, "position", position)) return e = "position is [x, y, z], metres in the world", false;
    if (!turnArg(a, turn, turned, e)) return false;
    SceneGraph trial = g;
    const IkResult ik = reachTargets(trial, effector, position, turned ? &turn : nullptr, false, nullptr);
    if (ik.links.empty()) return e = "the effector hangs on no joint: nothing to move", false;
    Value joints = Value::list();
    Real tightest = 1e9_r;
    for (const IkLink& l : ik.links) {
        Value x = Value::map();
        x.set("joint", l.joint), x.set("coordinate", double(l.coordinate));
        const JointObject* j = findJoint(g, l.joint);
        if (j && j->dof[size_t(l.dof)].motion == JointMotion::Limited) {
            const Real room = std::min(l.coordinate - j->dof[size_t(l.dof)].lower, j->dof[size_t(l.dof)].upper - l.coordinate);
            x.set("room", double(room)), tightest = std::min(tightest, room);
        }
        joints.push(x);
    }
    r.set("reached", ik.reached), r.set("position_error", double(ik.positionError)), r.set("angle_error", double(ik.angleError));
    r.set("manipulability", double(ik.manipulability)), r.set("joints", joints);
    if (tightest < 1e9_r) r.set("tightest_room", double(tightest));
    return true;
}

// assembly_goal: a goal for an effector - a see-through cube around the effector, at its pose; moving or turning it
// drives the effector there (scene_move and scene_turn call reachGoal; a window at each step of its gizmo's drag).
bool goalOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const uint32_t goal = addGoal(g, uint32_t(a["effector"].number()));
    if (goal == 0) return e = "effector is a shape that hangs on a joint (an arm's last link)", false;
    return r = selected({goal}, goal), true;
}

// assembly_path: a goal's trajectory - waypoints in the world, a speed, a loop; no points: the path cleared. A new path
// starts where the last one ends (the goal is put there first), so an agent strings its moves without a jump.
// assembly_path's start and start_turn (Euler degrees): the new path from there, not from the last one's end - a hand
// re-planned while it walks starts where its goal is now, so the arm goes on without a jump back.
bool startPose(const Value& a, Entity& goal, std::string& e) {
    const auto three = [&](const char* key, Vector3& v) {
        if (a[key].items().empty()) return true;
        if (a[key].items().size() != 3) return e = std::string(key) + " is [x, y, z]", false;
        v = {Real(a[key].items()[0].number()), Real(a[key].items()[1].number()), Real(a[key].items()[2].number())};
        return true;
    };
    if (!three("start", goal.position) || !three("start_turn", goal.rotationDeg)) return false;
    const Value& q = a["start_quaternion"]; // the turn as a quaternion [w, x, y, z]: a joint-space way's poses come so
    if (q.items().empty()) return true;
    if (q.items().size() != 4) return e = "start_quaternion is [w, x, y, z]", false;
    goal.rotationDeg = eulerDegrees(Quaternion{Real(q.items()[0].number()), Real(q.items()[1].number()), Real(q.items()[2].number()),
                                               Real(q.items()[3].number())}.normalized());
    return true;
}

bool pathOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    Entity* goal = nullptr;
    for (Entity& x : g.entities)
        if (x.id == uint32_t(a["goal"].number()) && x.goalOf != 0) goal = &x;
    if (!goal) return e = "goal is an IK goal's id (assembly_goal makes one)", false;
    std::vector<Vector3> points;
    for (const Value& p : a["points"].items()) {
        if (p.items().size() != 3) return e = "points are [[x, y, z], ...], metres in the world", false;
        points.push_back({Real(p.items()[0].number()), Real(p.items()[1].number()), Real(p.items()[2].number())});
    }
    std::vector<Vector3> turns; // each waypoint's turn: Euler degrees, or quaternions [w, x, y, z]
    for (const Value& t : a["turns"].items()) {
        if (t.items().size() != 3) return e = "turns are [[rx, ry, rz], ...] in degrees, one a point", false;
        turns.push_back({Real(t.items()[0].number()), Real(t.items()[1].number()), Real(t.items()[2].number())});
    }
    for (const Value& t : a["quaternions"].items()) {
        if (t.items().size() != 4) return e = "quaternions are [[w, x, y, z], ...], one a point", false;
        const Quaternion q = Quaternion{Real(t.items()[0].number()), Real(t.items()[1].number()), Real(t.items()[2].number()), Real(t.items()[3].number())};
        turns.push_back(eulerDegrees(q.normalized()));
    }
    if (!turns.empty() && turns.size() != points.size()) return e = "one turn a point, or none", false;
    const Real speed = Real(a["speed"].number(double(goal->pathSpeed)));
    if (!(speed > 0)) return e = "speed is metres a second, above zero", false;
    if (!goal->path.empty() && !goal->pathLoop) { // the last path's end, turned so: the new one's start
        goal->position = goal->path.back();
        if (!goal->pathTurns.empty()) goal->rotationDeg = goal->pathTurns.back();
    }
    if (!startPose(a, *goal, e)) return false; // a way re-planned on the walk: from where the goal is (sim_state's goals)
    goal->path = points, goal->pathTurns = turns, goal->pathSpeed = speed, goal->pathLoop = a["loop"].truth(false);
    goal->pathSmooth = a["smooth"].truth(false);
    goal->pathTurnLength = Real(a["turn_length"].number(0.0));
    if (!(goal->pathTurnLength >= 0)) return e = "turn_length is metres a radian, 0 or more", false;
    const Real total = pathLength(*goal); // as walked: a spline's is longer than its chords; looped, one round
    r.set("length", double(total)), r.set("seconds", double(total / speed));
    return true;
}

} // namespace

std::vector<SceneOp> kinematicSceneOps() {
    return {{"assembly_reach", "Inverse kinematics: the effector's frame to a point with a turn (quaternion, or basis - a rotation matrix by "
                               "rows; none: the point only), the chain from the body it hangs from solved by damped least squares; the "
                               "coordinates become the targets of the chain's servos (set: pose - the links posed there at once, the servos holding it). Answers "
                               "whether it reached, the errors left, the coordinates, the joints with no servo",
             "{effector: int, position: [x,y,z], quaternion?: [w,x,y,z], basis?: [[3],[3],[3]], set?: targets|pose}", {}, reachOp},
            {"assembly_can_reach", "Inverse kinematics as a question, nothing moved: whether the effector's frame can reach a point with a "
                                   "turn (as assembly_reach takes them), the errors left, the manipulability (zero at a singularity) and each "
                                   "limited joint's room to its nearest limit, the least of them tightest_room (m or rad)",
             "{effector: int, position: [x,y,z], quaternion?: [w,x,y,z], basis?: [[3],[3],[3]]}", canReachOp, {}},
            {"assembly_goal", "A goal for an effector: a see-through cube around it, at its pose; moving or turning the cube drives the "
                              "effector there by inverse kinematics, the chain's servos following (a window's gizmo moves it while the scene plays)",
             "{effector: int}", {}, goalOp},
            {"assembly_path", "A goal's trajectory: from where the goal stands through the points (world metres) at a speed, while the scene "
                              "plays - from the start, or from the moment it is set; held at the end, or round again (loop). Each point may have "
                              "a turn: between two points the goal turns by slerp in step with its travel; smooth: a camera's flight - a "
                              "centripetal Catmull-Rom spline through the points at constant speed, the turns by SQUAD. The arm follows by "
                              "inverse kinematics every frame. A new path starts where the last one ends, or at start (turned start_turn): "
                              "a way re-planned on the walk from where the goal is now (sim_state's goals); no points: the goal stays "
                              "there. Answers its length and how long it takes",
             "{goal: int, points: [[x,y,z], ...], turns?: [[rx,ry,rz] degrees, ...] | quaternions?: [[w,x,y,z], ...], speed?: m/s, loop?: bool, "
             "smooth?: bool, start?: [x,y,z], start_turn?: [rx,ry,rz] degrees | start_quaternion?: [w,x,y,z], turn_length?: m a radian (a turn walked as so much length)}",
             {}, pathOp}};
}

} // namespace ke
