// The answers of a running world (WorldOps.h). The arguments are read as the editor's Qt JSON read them before this
// moved into the core, because the recorded answers of the window (tests/data/window-transcript.jsonl) hold it to that:
// the small qt* helpers below each say which rule they keep. The live state and the channels are the privileged loop's
// truth of the physics engine: for checks and plots, never a robot's input - a robot reads its joints through encoders.
#include "sim/WorldOps.h"

#include "core/Probe.h"
#include "scene/Assembly.h"
#include "scene/SceneGraph.h"
#include "scene/SensorSpec.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <map>

namespace ke {

namespace {

const Value kNone;

// QJsonValue::toInt(def): a whole number inside the range of an int, else `def` (not a clamp).
int qtInt(const Value& v, int def) {
    if (v.type() != Value::Type::Number) return def;
    const double d = v.number();
    return d == std::floor(d) && d >= double(INT_MIN) && d <= double(INT_MAX) ? int(d) : def;
}

// QString::toUInt() of an object's key: reading stops at a NUL; ASCII blanks around, an optional '+', digits only,
// inside 32 bits - else 0.
uint32_t qtKeyId(const std::string& whole) {
    const std::string key = whole.substr(0, whole.find('\0'));
    const auto blank = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
    size_t a = 0, b = key.size();
    while (a < b && blank(key[a])) ++a;
    while (b > a && blank(key[b - 1])) --b;
    if (a < b && key[a] == '+') ++a;
    if (a == b) return 0;
    uint64_t n = 0;
    for (; a < b; ++a) {
        if (key[a] < '0' || key[a] > '9') return 0;
        n = n * 10 + uint64_t(key[a] - '0');
        if (n > UINT32_MAX) return 0;
    }
    return uint32_t(n);
}

// QJsonValue::toArray(): a value that is no array has no items.
const std::vector<Value>& qtItems(const Value& list) {
    static const std::vector<Value> none;
    return list.type() == Value::Type::List ? list.items() : none;
}

// QJsonArray::at(k): what is past the end is undefined (toDouble() 0, toInt(def) def).
const Value& qtAt(const Value& list, size_t k) { return k < qtItems(list).size() ? qtItems(list)[k] : kNone; }

// A QJsonObject's keys: sorted by UTF-16 code units, and none in a value that is no object (toObject()).
// In UTF-8 bytes that is byte order except that U+10000.. (lead F0..F4) sorts below U+E000..U+FFFF (lead EE, EF).
std::vector<std::string> qtKeys(const Value& map) {
    std::vector<std::string> keys = map.type() == Value::Type::Map ? map.keys() : std::vector<std::string>();
    const auto rank = [](unsigned char c) { return c >= 0xF0 ? c - 2 : c >= 0xEE ? c + 8 : int(c); };
    std::sort(keys.begin(), keys.end(), [&](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
            [&](char x, char y) { return rank((unsigned char)x) < rank((unsigned char)y); });
    });
    return keys;
}

Vector3 vector3Of(const Value& list) {
    return Vector3(Real(qtAt(list, 0).number()), Real(qtAt(list, 1).number()), Real(qtAt(list, 2).number()));
}

Value listOf(const Vector3& v) {
    Value l = Value::list();
    l.push(double(v.x)), l.push(double(v.y)), l.push(double(v.z));
    return l;
}

bool fail(std::string& error, std::string why) { return error = std::move(why), false; }

// What the running world said about a thing it could not build («Плечо»: ...), else "".
std::string buildNote(const RobotWorld& robot, const std::string& name) {
    for (const std::string& n : robot.notes())
        if (n.rfind("«" + name + "»", 0) == 0) return n;
    return std::string();
}

// One joint now: its kind, its six coordinates (travel of frame a from frame b along b's x y z [m], turns about
// them [rad]), its main coordinate (a hinge's angle, a slider's travel), its drive's effort and, for a DC motor,
// its current and the power it takes.
Value jointState(const Simulation& s, const JointObject& j) {
    Value r = Value::map();
    r.set("id", j.id), r.set("name", j.name), r.set("kind", jointKindWord(j)), r.set("built", false);
    const RobotWorld& robot = *s.robot();
    Real x[6];
    if (!robot.jointCoordinates(*s.physics(), j.id, x)) {
        r.set("problem", buildNote(robot, j.name));
        return r;
    }
    Value coords = Value::list();
    for (const Real c : x) coords.push(double(c));
    r.set("built", true);
    r.set("coordinates", coords);
    const int main = mainFreedom(j);
    if (main < 0) return r;
    r.set(main >= DofRx ? "angle" : "travel", double(x[main]));
    const JointDrive& d = j.dof[main].drive;
    if (!d.enabled) return r;
    const double effort = double(robot.jointEffort(*s.physics(), j.id));
    r.set("effort", effort);
    if (d.constant > 0) { // a DC motor's torque is K I
        const double current = effort / double(d.constant);
        Value motor = Value::map(), motors = Value::list();
        motor.set("dof", main), motor.set("current", current), motor.set("power", double(d.voltage) * current);
        motors.push(motor);
        r.set("motors", motors);
    }
    return r;
}

// A sensor's quantities by its kind's words (what Python's kinetica.Sensor reads): an IMU's accel and gyro, a wrench's
// force and torque, a sonar's distance, a touch pad's force and points, an effort's effort, an encoder's count and the
// coordinate it means (`reading`: with a known zero only). The truth has the quantities of the probe: no counts.
Value sensorValues(SensorKind kind, const SensorValues& v, bool referenced, bool truth) {
    Value r = Value::map();
    switch (kind) {
    case SensorKind::Imu: r.set("accel", listOf(v.a)), r.set("gyro", listOf(v.b)); break;
    case SensorKind::Wrench: r.set("force", listOf(v.a)), r.set("torque", listOf(v.b)); break;
    case SensorKind::Sonar: r.set("distance", double(v.value)); break;
    case SensorKind::Touch:
        r.set("force", double(v.value));
        if (!truth) r.set("points", double(v.count));
        break;
    case SensorKind::Effort: r.set("effort", double(v.value)); break;
    case SensorKind::Encoder:
        if (!truth) r.set("count", double(v.count)), r.set("relative", double(v.value));
        r.set("reading", referenced ? Value(double(v.value)) : Value());
        break;
    case SensorKind::Stereo: break;
    }
    return r;
}

// One sensor now as the robot reads it (its datasheet's device) - nothing of the truth: what a controller gets.
Value sensorReading(const LiveSensor& x) {
    const SensorReading& r = x.reading();
    Value out = sensorValues(x.spec().kind, r, r.referenced, false);
    out.set("id", x.spec().id);
    out.set("kind", sensorKindKey(x.spec().kind));
    if (!x.spec().model.empty()) out.set("model", x.spec().model);
    out.set("sample_ns", double(r.sampleNs));
    if (r.saturated) out.set("saturated", true);
    out.set("problem", r.problem);
    return out;
}

// One sensor now: what the robot reads, and beside it the truth of its probe - the privileged loop's, for checks.
Value sensorState(const LiveSensor& x) {
    Value out = sensorReading(x);
    out.set("truth", sensorValues(x.spec().kind, x.truth(), true, true));
    return out;
}

// sim_measure's arguments as the channels to measure: their labels are the ids, as the plots' are names.
ChannelRequests channelRequests(const Value& a) {
    ChannelRequests r;
    const auto refs = [](const Value& list) {
        std::vector<ObjectRef> out;
        for (const Value& v : qtItems(list))
            out.push_back({uint32_t(qtInt(v, 0)), std::to_string(qtInt(v, 0))});
        return out;
    };
    r.scene = a["scene"].truth();
    for (const ObjectRef& o : refs(a["objects"])) r.objects.push_back(o);
    for (const ObjectRef& o : refs(a["joints"])) r.joints.push_back({o.entity, o.label});
    for (const ObjectRef& o : refs(a["goals"])) r.goals.push_back({o.entity, o.label});
    if (a.contains("group")) r.groups.push_back(GroupRef{"group", refs(a["group"])});
    return r;
}

// Three finite numbers a Real can hold.
bool vectorArg(const Value& value, Vector3& out) {
    if (value.type() != Value::Type::List || value.items().size() != 3) return false;
    for (size_t k = 0; k < 3; ++k) {
        const Value& item = value.items()[k];
        const double x = item.number();
        if (item.type() != Value::Type::Number || !std::isfinite(x) || std::fabs(x) > double(std::numeric_limits<Real>::max())) return false;
        out[int(k)] = Real(x);
    }
    return true;
}

bool controllerInput(Simulation* world, uint32_t first, const Value& a, Value& answer, std::string& error) {
    if (!world) return fail(error, "the scene is not running: ▶ or sim_step first");
    const uint32_t id = a.contains("id") ? uint32_t(qtInt(a["id"], 0)) : first;
    std::vector<std::pair<std::string, Vector3>> values;
    const Value& given = a["values"];
    for (const std::string& key : qtKeys(given)) {
        const Value& v = given[key];
        values.push_back({key, v.type() == Value::Type::List ? Vector3(Real(qtAt(v, 0).number()), Real(qtAt(v, 1).number()), 0) : Vector3(Real(v.number()), 0, 0)});
    }
    if (values.empty()) return fail(error, "values: {action: value}, e.g. {\"move\": [0, 1]}");
    std::string why = world->robot() ? std::string() : "the scene is not built";
    for (const auto& [name, v] : values)
        if (why.empty() && !world->robot()->setControllerInput(id, name, v, why)) break;
    if (!why.empty()) return fail(error, why);
    answer.set("id", id);
    return true;
}

bool sensorRead(Simulation* world, uint32_t, const Value& a, Value& answer, std::string& error) {
    if (!world) return fail(error, "the scene is not running: ▶ or sim_step first - sensors read a running world");
    std::vector<uint32_t> ids;
    for (const Value& v : qtItems(a["ids"])) ids.push_back(uint32_t(qtInt(v, 0)));
    Value sensors = Value::list();
    std::string missing;
    if (const RobotWorld* robot = world->robot()) {
        for (const auto& x : robot->sensors())
            if (ids.empty() || std::find(ids.begin(), ids.end(), x->spec().id) != ids.end()) sensors.push(sensorReading(*x));
        for (uint32_t id : ids)
            if (!robot->sensor(id)) missing += (missing.empty() ? "" : ", ") + std::to_string(id);
    }
    if (!missing.empty()) return fail(error, "no sensor " + missing + " (scene_list names the sensors)");
    answer.set("time", double(world->time())), answer.set("sensors", sensors);
    return true;
}

bool robotCommand(Simulation* world, uint32_t, const Value& a, Value& answer, std::string& error) {
    if (!world) return fail(error, "the scene is not running: ▶ or sim_step first - commands drive a running world");
    if (qtKeys(a["commands"]).empty()) return fail(error, "commands: {id: {target | velocity | duty: number}}");
    error = applyCommands(*world, a["commands"]);
    return error.empty();
}

bool measure(Simulation* world, uint32_t, const Value& a, Value& answer, std::string& error) {
    if (!world) return fail(error, "the scene is not running: play or step it first");
    const ChannelRequests requests = channelRequests(a);
    if (requests.empty()) return fail(error, "nothing asked: scene, objects, group, joints or goals");
    Value channels = Value::list();
    for (const Measurement& m : measureAll(*world, requests)) {
        Value c = Value::map();
        c.set("id", m.info.id), c.set("value", m.value), c.set("unit", m.info.unit), c.set("name", m.info.name);
        channels.push(c);
    }
    answer.set("channels", channels), answer.set("time", double(world->time()));
    return true;
}

// The order of the checks is the answer when two things are wrong: the world, an unknown field, the body, the impulse, the point.
bool impulse(Simulation* world, uint32_t, const Value& a, Value& answer, std::string& error) {
    if (!world) return fail(error, "the scene is not running: play or step it first");
    for (const std::string& key : qtKeys(a))
        if (key != "body" && key != "impulse" && key != "point") return fail(error, "unknown sim_impulse field: " + key);
    const Value& body = a["body"];
    const double id = body.number(-1);
    if (body.type() != Value::Type::Number || !std::isfinite(id) || id < 1 || id > double(UINT32_MAX) || id != std::floor(id))
        return fail(error, "body is a positive scene entity id");
    Vector3 force(0.0_r), point(0.0_r);
    if (!vectorArg(a["impulse"], force)) return fail(error, "impulse is three finite N s values");
    const bool atPoint = a.contains("point");
    if (atPoint && !vectorArg(a["point"], point)) return fail(error, "point is three finite world metres");
    const uint32_t entity = uint32_t(id);
    PhysicsWorld* physics = world->physics();
    const int index = world->robot() ? world->robot()->bodyOf(entity) : -1;
    if (index < 0 || !physics->alive(index)) return fail(error, "body entity has no live rigid body");
    if (!(physics->state(index).mass > 0)) return fail(error, "body must be dynamic (a fixed one does not move)");
    physics->wake(index);
    physics->applyImpulse(index, force, atPoint ? point : physics->state(index).centre);
    answer.set("accepted", true), answer.set("body", entity);
    return true;
}

std::vector<WorldOp> makeOps() {
    return {
        {"controller_input",
         "The values of a controller's actions while the scene runs, held until set again: a vector2 [x, y] (move: x "
         "along the floor's x, y away), an axis or a button a number, -1..1 (lift, turn, grip 0..1); the scene's first "
         "controller when no id - the UR5e's panel moves its IK goal and the arm follows",
         "{id?: int, values: {action: number | [x, y]}}", "window", controllerInput},
        {"sensor_read",
         "What the scene's sensors read now, as the robot reads them (their datasheets' devices): no truth - the "
         "input of a controller. The scene must be running or paused",
         "{ids?: [int]}", "query", sensorRead},
        {"robot_command",
         "Commands to the running world, not edits of the scene: a joint's target (its servo, rad or m) or velocity (its "
         "velocity motor), a driver's duty (-1..1), held until commanded again; a body's force and torque ([x, y, z] N, "
         "N m, world, at its centre of mass) over the next frame. Stop forgets them (the scene as authored is untouched, "
         "no undo step, no journal line). E.g. {commands: {\"7\": {\"target\": 0.8}, \"23\": {\"force\": [0, 2, 0]}}}",
         "{commands: {}}", "window", robotCommand},
        {"sim_impulse",
         "Apply one physical world-space impulse in N s to an already built dynamic rigid body addressed by scene entity id. "
         "Optional point is in world metres; omitted point applies at the centre of mass. Wakes a sleeping body; adds momentum "
         "without teleporting, resetting velocity or editing the graph. Available while playing or paused after sim_step(0). "
         "Missing or fixed bodies and malformed or nonfinite commands are refused. Returns only acceptance, not the body's pose "
         "or velocity: this is external actuation, not a sensor observation.",
         "{body: positive scene entity id, impulse: [x,y,z] N s, point?: [x,y,z] world metres}", "window", impulse},
        {"sim_measure",
         "The plots' channels measured now (the physics engine's truth, for checks): scene - energy of motion, rotation and "
         "height, momentum, angular momentum, the deepest contact; objects - each body by entity id: mass, centre, height, "
         "velocity, energy, spin, sleep, contact force; group - several bodies as one; joints - each freedom's coordinate (0: the "
         "pose the joint was made in), a "
         "servo's target, the drive's effort; goals - an IK goal's miss (mm) and turn miss (°). Each channel: id "
         "(\"<id>/<quantity>\"), value, unit, name",
         "{scene?: bool, objects?: [int], group?: [int], joints?: [int], goals?: [int]}", "query", measure},
    };
}

} // namespace

const std::vector<WorldOp>& worldOps() {
    static const std::vector<WorldOp> ops = makeOps();
    return ops;
}

const WorldOp* worldOp(const std::string& name) {
    for (const WorldOp& op : worldOps())
        if (op.name == name) return &op;
    return nullptr;
}

void startWorld(Simulation& s, const SceneGraph& g) {
    Probe::clear();
    s.load(g);
}

int framesArg(const Value& args) { return std::clamp(qtInt(args["frames"], 1), 0, 6000); }

Value sensorReadings(const Simulation& s) {
    Value out = Value::list();
    if (const RobotWorld* robot = s.robot())
        for (const auto& x : robot->sensors()) out.push(sensorReading(*x));
    return out;
}

Value bodyStates(const Simulation& s) {
    Value out = Value::map();
    const RobotWorld* robot = s.robot();
    const PhysicsWorld* physics = s.physics();
    if (!robot || !physics) return out;
    for (const Entity& e : robot->graph().entities) {
        const int b = robot->bodyOf(e.id);
        if (b < 0 || !physics->alive(b)) continue;
        const BodyState st = physics->state(b);
        if (st.mass <= 0) continue; // a fixed body: nothing a plugin pushes
        Value body = Value::map();
        body.set("position", listOf(st.centre)), body.set("velocity", listOf(st.velocity));
        body.set("spin", listOf(st.angularVelocity)), body.set("mass", double(st.mass));
        out.set(std::to_string(e.id), body);
    }
    return out;
}

// The live state on the simulation's thread: the time, every body's pose and velocity, every joint's
// coordinates and drive, every sensor's reading (and its truth), which entities touch, and what the world could
// not build.
Value liveState(const Simulation& s) {
    Value out = Value::map();
    out.set("time", double(s.time())), out.set("frame", double(s.frame()));
    const RobotWorld* robot = s.robot();
    const PhysicsWorld* physics = s.physics();
    if (!robot || !physics) return out;
    Value shapes = Value::list(), joints = Value::list(), contacts = Value::list(), notes = Value::list();
    for (const Entity& e : robot->graph().entities) {
        const int b = robot->bodyOf(e.id);
        if (b < 0 || !physics->alive(b)) continue;
        const BodyState st = physics->state(b);
        Value shape = Value::map();
        shape.set("id", e.id), shape.set("name", e.name), shape.set("position", listOf(st.position));
        shape.set("rotation", listOf(eulerDegrees(st.rotation))), shape.set("velocity", listOf(st.velocity)), shape.set("spin", listOf(st.angularVelocity));
        if (!st.awake) shape.set("asleep", true); // at rest: its velocity is 0 because it sleeps, not by chance
        if (robot->stands(b)) shape.set("fixed", true);
        else if (st.mass <= 0) shape.set("kinematic", true); // led - by its own velocity or by a sequence: it moves, forces do not move it
        shapes.push(shape);
    }
    for (const JointObject& j : robot->graph().joints) joints.push(jointState(s, j));
    // The contact points of the last substep by the pair of entities they join (0: the floor), their normal
    // force (the impulse over that substep) and the deepest overlap.
    const double h = double(s.frameDt) / robot->engineSteps(robot->graph().world.physics.substeps);
    struct Touch { int points = 0; double force = 0, depth = 0; };
    std::map<std::pair<uint32_t, uint32_t>, Touch> touches;
    for (const ContactPoint& c : physics->contacts()) {
        uint32_t a = robot->objectIdOfBody(c.a), b = robot->objectIdOfBody(c.b);
        if (a > b) std::swap(a, b);
        Touch& t = touches[{a, b}];
        ++t.points;
        t.force += double(c.normalImpulse) / h;
        t.depth = std::max(t.depth, double(c.depth));
    }
    for (const auto& [pair, t] : touches) {
        Value c = Value::map();
        c.set("a", pair.first), c.set("b", pair.second), c.set("points", t.points), c.set("force", t.force), c.set("depth", t.depth);
        contacts.push(c);
    }
    for (const std::string& n : robot->notes()) notes.push(n);
    out.set("shapes", shapes);
    out.set("joints", joints);
    Value grips = Value::list(); // did the hand take it: each goal's gripper as the gripper reports it
    for (const Frame::GripInfo& g : robot->grips(*physics)) {
        static const char* words[] = {"open", "closing", "holding", "empty"};
        Value grip = Value::map();
        grip.set("goal", int(g.goal)), grip.set("state", words[int(g.state)]), grip.set("gap", double(g.gap));
        grips.push(grip);
    }
    if (grips.size()) out.set("grips", grips);
    Value sensors = Value::list();
    for (const auto& x : robot->sensors()) sensors.push(sensorState(*x));
    out.set("sensors", sensors);
    Value timers = Value::map(); // where the last frame's step went: the drives, the engine, the sensors (core/Probe.h)
    for (const Probe::Channel& c : Probe::snapshot().channels)
        if (c.name.rfind("physics/", 0) == 0 && c.name.size() > 3 && c.name.compare(c.name.size() - 3, 3, " ms") == 0)
            timers.set(c.name.substr(8, c.name.size() - 11), c.value);
    out.set("step_ms", timers);
    out.set("contacts", contacts);
    Value triggers = Value::list(); // the zones - the colliders that are triggers - and what stands inside each
    for (const auto& [zone, inside] : robot->zones()) {
        Value ids = Value::list(), trigger = Value::map();
        for (uint32_t id : inside) ids.push(id);
        trigger.set("id", zone), trigger.set("inside", ids);
        triggers.push(trigger);
    }
    if (triggers.size()) out.set("triggers", triggers);
    if (notes.size()) out.set("notes", notes);
    return out;
}

Value stepAnswer(const Simulation& s, int requested, int completed) {
    Value out = liveState(s);
    out.set("requested_frames", requested), out.set("completed_frames", completed), out.set("cancelled", completed != requested);
    return out;
}

std::string applyCommands(Simulation& s, const Value& commands) {
    if (!s.robot() || !s.physics()) return "the scene is not built";
    std::string wrong;
    const auto say = [&wrong](const std::string& why) { wrong += (wrong.empty() ? "" : "; ") + why; };
    for (const std::string& key : qtKeys(commands)) {
        const Value& what = commands[key];
        const std::vector<std::string> words = qtKeys(what);
        if (words.empty()) say(key + ": {target | velocity | duty: number} or {force, torque: [x, y, z]}");
        if (what.contains("force") || what.contains("torque")) { // a body pushed over the next frame (a physics plugin's)
            std::string why;
            if (!s.robot()->push(*s.physics(), qtKeyId(key), vector3Of(what["force"]), vector3Of(what["torque"]), why)) say(why);
            continue;
        }
        for (const std::string& word : words) {
            std::string why;
            if (!s.robot()->command(*s.physics(), qtKeyId(key), word, Real(what[word].number()), why)) say(why);
        }
    }
    return wrong;
}

} // namespace ke
