// The operations of a running world (sim/WorldOps.h) without Qt and without a window: what each answers with no world
// built, with the arguments the editor's Qt JSON conversions once decided (the recorded window answers hold the window
// to the same), and the live state against what the Simulation says itself.
#include "TestRunner.h"

#include "core/Json.h"
#include "sim/WorldOps.h"

#include <cmath>
#include <limits>

namespace {

struct Result {
    bool ok;
    Value answer;
    std::string error;
};

Result call(Simulation* world, const char* name, const Value& args, uint32_t firstController = 0) {
    for (const WorldOp& op : worldOps())
        if (op.name == name) {
            Result r{false, Value::map(), ""};
            r.ok = op.run(world, firstController, args, r.answer, r.error);
            return r;
        }
    CHECK(false, "no world operation %s", name);
    return {false, Value(), ""};
}

Result call(Simulation* world, const char* name, const char* json, uint32_t firstController = 0) {
    return call(world, name, parseJson(json), firstController);
}

Entity solid(uint32_t id, const char* name, const Vector3& at, bool fixed) {
    Entity e;
    e.id = id, e.name = name, e.size = Vector3(0.2_r), e.position = at;
    e.rigid.enabled = e.collider.enabled = true;
    e.rigid.fixed = fixed;
    return e;
}

// A post (1, fixed), an arm on it (2) turning about a hinge (3) with a servo, a box falling (4), a kinematic one led along
// x (5), a box resting on the floor (6, it falls asleep), an IMU on the arm (7) and a panel (8) that jogs the hinge.
SceneGraph smallWorld() {
    SceneGraph g;
    g.entities.push_back(solid(1, "Стойка", {0, 0.5_r, 0}, true));
    g.entities.push_back(solid(2, "Рука", {0.2_r, 1.05_r, 0}, false));
    g.entities.back().size = {0.4_r, 0.04_r, 0.04_r};
    g.entities.push_back(solid(4, "Падает", {3, 2, 0}, false));
    g.entities.push_back(solid(5, "Ведомый", {-3, 0.5_r, 0}, false));
    g.entities.back().rigid.kinematic = true, g.entities.back().rigid.velocity = {1, 0, 0};
    g.entities.push_back(solid(6, "Лежит", {6, 0.1_r, 0}, false));
    JointObject j;
    j.id = 3, j.name = "Плечо", j.a = 2, j.b = 1;
    j.anchorA = {-0.2_r, 0, 0}, j.anchorB = {0, 0.55_r, 0};
    j.frameA = j.frameB = {0, -90, 0};
    j.dof[DofRx].motion = JointMotion::Free;
    j.dof[DofRx].drive.enabled = true;
    j.dof[DofRx].drive.stiffness = 60, j.dof[DofRx].drive.damping = 4, j.dof[DofRx].drive.maxForce = 100;
    g.joints.push_back(j);
    SensorObject imu;
    imu.id = 7, imu.name = "IMU", imu.parent = 2, imu.kind = SensorKind::Imu, imu.seed = 7;
    g.sensors.push_back(imu);
    ControllerObject panel;
    panel.id = 8, panel.name = "Пульт";
    ControllerAction jog;
    jog.name = "jog", jog.value = ControlValue::Axis, jog.drive = ControlDrive::Jog, jog.target = 3, jog.speed = 0.5_r;
    panel.actions = {jog};
    g.controllers.push_back(panel);
    return g;
}

void build(Simulation& s, int frames = 0) {
    startWorld(s, smallWorld());
    for (int k = 0; k < frames; ++k) s.stepFrame();
}

Vector3 velocityOf(const Simulation& s, uint32_t id) { return s.physics()->state(s.robot()->bodyOf(id)).velocity; }

const Value* shapeOf(const Value& state, int64_t id) {
    for (const Value& shape : state["shapes"].items())
        if (shape["id"].number() == double(id)) return &shape;
    return nullptr;
}

std::string withoutTimers(std::string json) {
    const size_t at = json.find("\"step_ms\":");
    return at == std::string::npos ? json : json.erase(at, json.find('}', at) - at + 1);
}

} // namespace

void testWorldOpsNeedAWorld() {
    const struct { const char* op; const char* args; const char* text; } cases[] = {
        {"controller_input", "{\"values\":{\"jog\":1}}", "the scene is not running: ▶ or sim_step first"},
        {"sensor_read", "{}", "the scene is not running: ▶ or sim_step first - sensors read a running world"},
        {"robot_command", "{\"commands\":{\"1\":{\"force\":[0,1,0]}}}", "the scene is not running: ▶ or sim_step first - commands drive a running world"},
        {"sim_impulse", "{\"body\":1,\"impulse\":[0,1,0]}", "the scene is not running: play or step it first"},
        {"force_fire", "{\"id\":1}", "the scene is not running: play or step it first"},
        {"sim_measure", "{\"scene\":true}", "the scene is not running: play or step it first"},
    };
    for (const auto& c : cases) {
        const Result r = call(nullptr, c.op, c.args);
        CHECK(!r.ok && r.error == c.text, "%s with no world says '%s'", c.op, r.error.c_str());
    }
    CHECK(worldOps().size() == 6, "six world operations, not %zu", worldOps().size());
    Simulation empty;
    CHECK(call(&empty, "controller_input", "{\"values\":{\"jog\":1}}", 8).error == "the scene is not built", "a world that holds no scene");
}

void testWorldOpsFramesArg() {
    const struct { const char* json; int frames; } cases[] = {
        {"{\"frames\":-5}", 0}, {"{\"frames\":-1}", 0}, {"{\"frames\":0}", 0}, {"{\"frames\":1}", 1}, {"{\"frames\":4.0}", 4},
        {"{\"frames\":6000}", 6000}, {"{\"frames\":100000}", 6000}, {"{\"frames\":1e10}", 1}, {"{}", 1},
        {"{\"frames\":2147483647}", 6000}, {"{\"frames\":2147483648}", 1}, {"{\"frames\":-2147483648}", 0}, {"{\"frames\":-2147483649}", 1},
        {"{\"frames\":2.5}", 1}, {"{\"frames\":\"3\"}", 1}, {"{\"frames\":null}", 1}, {"{\"frames\":true}", 1}, {"{\"frames\":[2]}", 1},
    };
    for (const auto& c : cases) CHECK(framesArg(parseJson(c.json)) == c.frames, "%s: %d, not %d", c.json, framesArg(parseJson(c.json)), c.frames);
    Value hand = Value::map();
    hand.set("frames", std::numeric_limits<double>::quiet_NaN());
    CHECK(framesArg(hand) == 1, "a number that is not a number is the default");
}

void testWorldOpsRobotCommand() {
    Simulation s;
    build(s);
    CHECK(call(&s, "robot_command", "{\"commands\":{\"3\":{\"target\":0.3}}}").ok, "a servo's target is taken");
    for (int k = 0; k < 240; ++k) s.stepFrame();
    const Value state = liveState(s);
    double angle = -9;
    for (const Value& joint : state["joints"].items())
        if (joint["id"].number() == 3) angle = joint["angle"].number();
    CHECK(std::fabs(std::fabs(angle) - 0.3) < 0.05, "the servo went to its target: angle %f", angle);

    const double before = velocityOf(s, 4).x;
    CHECK(call(&s, "robot_command", "{\"commands\":{\"4\":{\"force\":[200,0,0]}}}").ok, "a force on a body");
    s.stepFrame();
    CHECK(velocityOf(s, 4).x > before + 0.01_r, "the force moved the body: %f -> %f", before, double(velocityOf(s, 4).x));

    CHECK(call(&s, "robot_command", "{\"commands\":{\"9999\":{\"force\":[0,1,0]}}}").error == "entity 9999 has no moving body", "a bad id");
    CHECK(call(&s, "robot_command", "{\"commands\":{\"2\":{\"wat\":0.5}}}").error == "a command is target, velocity, duty or strength, not wat", "a bad word");
    CHECK(call(&s, "robot_command", "{\"commands\":{\"2\":{}}}").error == "2: {target | velocity | duty | strength: number} or {force, torque: [x, y, z]}", "an empty entry");
    CHECK(call(&s, "robot_command", "{\"commands\":{}}").error == "commands: {id: {target | velocity | duty | strength: number}}", "no commands");
    CHECK(call(&s, "robot_command", "{\"commands\":[1]}").error == "commands: {id: {target | velocity | duty | strength: number}}", "commands that are no object");

    // The joined text is in the order of the keys as text, whatever order the caller built the Value in.
    Value two = Value::map(), commands = Value::map(), nine = Value::map(), ten = Value::map();
    nine.set("bogus", 1), ten.set("wat", 2);
    commands.set("9", nine), commands.set("10", ten);
    two.set("commands", commands);
    CHECK(call(&s, "robot_command", two).error == "a command is target, velocity, duty or strength, not wat; a command is target, velocity, duty or strength, not bogus",
          "'10' before '9': %s", call(&s, "robot_command", two).error.c_str());
    Value inner = Value::map();
    inner.set("zz", 1), inner.set("aa", 2);
    Value byWord = Value::map(), byWordCommands = Value::map();
    byWordCommands.set("2", inner), byWord.set("commands", byWordCommands);
    CHECK(call(&s, "robot_command", byWord).error == "a command is target, velocity, duty or strength, not aa; a command is target, velocity, duty or strength, not zz", "the words of one entry sorted too");

    // An object's key is an id as QString::toUInt reads it: blanks around and a plus sign allowed, anything else is 0.
    for (const char* key : {"4", " 4", "4 ", "+4", "\\t4", "004"}) {
        const std::string json = std::string("{\"commands\":{\"") + key + "\":{\"force\":[0,1,0]}}}";
        CHECK(call(&s, "robot_command", json.c_str()).ok, "the key '%s' is 4", key);
    }
    for (const char* key : {"7x", "-1", "4 4", "+ 4", "0x4", "4294967296", "", "+", "-0"})
        CHECK(call(&s, "robot_command", (std::string("{\"commands\":{\"") + key + "\":{\"force\":[0,1,0]}}}").c_str()).error == "entity 0 has no moving body", "the key '%s' is 0", key);
    // A missing item of a vector is 0, a non-number too: the force is {5, 0, 0}.
    Simulation a, b;
    build(a), build(b);
    CHECK(call(&a, "robot_command", "{\"commands\":{\"4\":{\"force\":[5,\"x\"]}}}").ok, "a short vector");
    CHECK(call(&b, "robot_command", "{\"commands\":{\"4\":{\"force\":[5,0,0]}}}").ok, "the whole vector");
    a.stepFrame(), b.stepFrame();
    CHECK(velocityOf(a, 4).x == velocityOf(b, 4).x && velocityOf(a, 4).y == velocityOf(b, 4).y && velocityOf(a, 4).z == velocityOf(b, 4).z,
          "a short vector is the vector padded with 0");

    // Reading of a key stops at a NUL, as QString::toUInt does.
    Value nul = Value::map(), nulCommands = Value::map(), force = Value::map(), v = Value::list();
    v.push(0.0), v.push(1.0), v.push(0.0);
    force.set("force", v), nulCommands.set(std::string("4\0z", 3), force), nul.set("commands", nulCommands);
    CHECK(call(&s, "robot_command", nul).ok, "the key '4' NUL 'z' is 4");

    // Keys sort by UTF-16 code units: U+1F600 (a surrogate pair) before U+FFFF.
    Value wide = Value::map(), wideCommands = Value::map(), aa = Value::map(), bb = Value::map();
    aa.set("aa", 1), bb.set("bb", 1);
    wideCommands.set("\xEF\xBF\xBF", bb), wideCommands.set("\xF0\x9F\x98\x80", aa), wide.set("commands", wideCommands);
    CHECK(call(&s, "robot_command", wide).error == "a command is target, velocity, duty or strength, not aa; a command is target, velocity, duty or strength, not bb",
          "a pair of surrogates sorts below U+FFFF: %s", call(&s, "robot_command", wide).error.c_str());
}

void testWorldOpsImpulse() {
    Simulation s;
    build(s);
    const char* body = "\"body\":6,\"impulse\":[0.4,0,0]";
    // The order of the checks: the world, an unknown field (in key order), the body, the impulse, the point.
    CHECK(call(&s, "sim_impulse", "{\"zeta\":1,\"_alpha\":2,\"body\":0}").error == "unknown sim_impulse field: _alpha", "an unknown field comes first, the smallest key");
    CHECK(call(&s, "sim_impulse", "{\"_x\":1,\"body\":0,\"impulse\":1}").error == "unknown sim_impulse field: _x", "an unknown field before the body");
    CHECK(call(&s, "sim_impulse", "{\"body\":0,\"impulse\":1,\"point\":1}").error == "body is a positive scene entity id", "the body before the impulse");
    CHECK(call(&s, "sim_impulse", "{\"body\":6,\"impulse\":[1,2],\"point\":1}").error == "impulse is three finite N s values", "the impulse before the point");
    CHECK(call(&s, "sim_impulse", "{\"body\":6,\"impulse\":[1,2,3],\"point\":[1]}").error == "point is three finite world metres", "the point");
    for (const char* wrong : {"{\"body\":1.5,\"impulse\":[0,0,0]}", "{\"body\":\"6\",\"impulse\":[0,0,0]}", "{\"body\":4294967296,\"impulse\":[0,0,0]}", "{\"body\":-3,\"impulse\":[0,0,0]}"})
        CHECK(call(&s, "sim_impulse", wrong).error == "body is a positive scene entity id", "%s", wrong);
    for (const char* wrong : {"[1,2,\"3\"]", "[1,2,null]", "[1,2,3,4]", "{}", "[1e300,0,0]"})
        CHECK(call(&s, "sim_impulse", (std::string("{\"body\":6,\"impulse\":") + wrong + "}").c_str()).error == "impulse is three finite N s values", "impulse %s", wrong);

    const Real mass = s.physics()->state(s.robot()->bodyOf(6)).mass;
    const Result taken = call(&s, "sim_impulse", (std::string("{") + body + "}").c_str());
    CHECK(taken.ok && taken.answer["accepted"].truth() && taken.answer["body"].number() == 6, "an impulse on a dynamic body is accepted");
    CHECK(std::fabs(velocityOf(s, 6).x - 0.4_r / mass) < 1e-4_r * (0.4_r / mass), "v = J / m: %f, not %f", double(velocityOf(s, 6).x), double(0.4_r / mass));
    CHECK(call(&s, "sim_impulse", "{\"body\":6,\"impulse\":[0,1,0],\"point\":[6,0.1,0]}").ok, "an impulse at a point");

    CHECK(call(&s, "sim_impulse", "{\"body\":1,\"impulse\":[0,1,0]}").error == "body must be dynamic (a fixed one does not move)", "a static body is refused");
    CHECK(call(&s, "sim_impulse", "{\"body\":5,\"impulse\":[0,1,0]}").error == "body must be dynamic (a fixed one does not move)", "a kinematic one too");
    CHECK(call(&s, "sim_impulse", "{\"body\":99,\"impulse\":[0,1,0]}").error == "body entity has no live rigid body", "a body that is not there");
}

// The order of the checks: the world, an unknown field, the id, then what the world refuses. The answer is acceptance only.
void testWorldOpsForceFire() {
    SceneGraph g = smallWorld();
    Force push, constant;
    push.id = 20, push.position = {6, 0.1_r, 0}, push.strength = 2;
    constant = push;
    constant.id = 21, constant.mode = ForceMode::Constant, constant.start = 0;
    push.start = 3600;
    g.forces = {push, constant};
    Simulation s;
    startWorld(s, g);
    CHECK(call(&s, "force_fire", "{\"zeta\":1,\"_a\":2,\"id\":0}").error == "unknown force_fire field: _a", "an unknown field comes first");
    for (const char* wrong : {"{\"id\":0}", "{\"id\":1.5}", "{\"id\":\"20\"}", "{}"})
        CHECK(call(&s, "force_fire", wrong).error == "id is a positive scene object id", "%s", wrong);
    CHECK(call(&s, "force_fire", "{\"id\":99}").error == "no force with id 99 in the scene", "no such force");
    CHECK(call(&s, "force_fire", "{\"id\":21}").error == "a constant force acts on its own: only an impulse is fired", "a constant force is refused");
    const Result taken = call(&s, "force_fire", "{\"id\":20}");
    CHECK(taken.ok && taken.answer["accepted"].truth() && taken.answer["id"].number() == 20 && !taken.answer.has("body") && !taken.answer.has("velocity"),
          "an impulse is accepted, the answer has no body data");
    const Vector3 v = velocityOf(s, 6);
    CHECK(std::fabs(length(v) - 2) < 2e-4_r, "a field of 2 m/s gives the body 2 m/s, whatever it weighs: %f", double(length(v)));
}

void testWorldOpsSensorsAndControllers() {
    Simulation s;
    build(s, 30);
    const Result one = call(&s, "sensor_read", "{\"ids\":[7]}");
    const Result all = call(&s, "sensor_read", "{}");
    CHECK(one.ok && all.ok && one.answer["sensors"].items().size() == 1 && all.answer["sensors"].items().size() == 1, "one sensor, all of them");
    const Value& imu = all.answer["sensors"].items()[0];
    CHECK(imu["id"].number() == 7 && imu["kind"].text() == "imu" && imu["accel"].items().size() == 3 && imu["gyro"].items().size() == 3 && !imu.has("truth"),
          "an IMU as the robot reads it: accel, gyro, no truth");
    CHECK(all.answer["time"].number() == double(s.time()), "the world's time");
    CHECK(call(&s, "sensor_read", "{\"ids\":[7,99,98]}").error == "no sensor 99, 98 (scene_list names the sensors)", "unknown ids, in the order asked");
    CHECK(call(&s, "sensor_read", "{\"ids\":[99]}").error == "no sensor 99 (scene_list names the sensors)", "an unknown id");

    CHECK(call(&s, "controller_input", "{\"id\":8,\"values\":{\"jog\":0.5}}").answer["id"].number() == 8, "a controller by its id");
    CHECK(call(&s, "controller_input", "{\"values\":{\"jog\":0.5}}", 8).answer["id"].number() == 8, "no id: the first controller");
    CHECK(call(&s, "controller_input", "{\"values\":{\"jog\":0.5}}").error == "no controller 0", "no id and no controller: %s",
          call(&s, "controller_input", "{\"values\":{\"jog\":0.5}}").error.c_str());
    CHECK(call(&s, "controller_input", "{\"id\":8,\"values\":{\"fly\":1}}", 8).error.find("fly") != std::string::npos, "a bad action is named: %s",
          call(&s, "controller_input", "{\"id\":8,\"values\":{\"fly\":1}}", 8).error.c_str());
    CHECK(call(&s, "controller_input", "{\"id\":8,\"values\":{}}", 8).error == "values: {action: value}, e.g. {\"move\": [0, 1]}", "no values");
    CHECK(call(&s, "controller_input", "{\"id\":8,\"values\":[1]}", 8).error == "values: {action: value}, e.g. {\"move\": [0, 1]}", "values that are no object");
    CHECK(call(&s, "controller_input", "{\"id\":8,\"values\":{\"jog\":[1,\"x\"]}}", 8).ok, "a vector for an axis: its first number");
}

void testWorldOpsMeasure() {
    Simulation s;
    build(s, 20);
    const Result r = call(&s, "sim_measure", "{\"objects\":[4]}");
    CHECK(r.ok && r.answer["time"].number() == double(s.time()), "a body measured");
    const std::vector<Measurement> own = measureAll(s, [] {
        ChannelRequests q;
        q.objects.push_back({4, "4"});
        return q;
    }());
    CHECK(r.answer["channels"].items().size() == own.size() && !own.empty(), "the channels asked, the same count");
    for (size_t k = 0; k < own.size() && k < r.answer["channels"].items().size(); ++k) {
        const Value& c = r.answer["channels"].items()[k];
        CHECK(c["id"].text() == own[k].info.id && c["value"].number() == own[k].value && c["unit"].text() == own[k].info.unit && c["name"].text() == own[k].info.name,
              "channel %zu is the engine's own", k);
    }
    CHECK(call(&s, "sim_measure", "{\"objects\":[99]}").ok, "an unknown id is measured as empty channels");
    CHECK(call(&s, "sim_measure", "{}").error == "nothing asked: scene, objects, group, joints or goals", "nothing asked");
    CHECK(call(&s, "sim_measure", "{\"group\":[]}").ok, "an empty group is still asked");
    CHECK(call(&s, "sim_measure", "{\"scene\":true,\"joints\":[3],\"goals\":[1]}").ok, "the scene, a joint, a goal");
}

void testWorldOpsLiveState() {
    Simulation s;
    build(s, 5);
    const Value state = liveState(s);
    CHECK(state["time"].number() == double(s.time()) && state["frame"].number() == double(s.frame()) && !state.has("ok"), "time and frame are the Simulation's");
    const Value* falling = shapeOf(state, 4);
    const Vector3 at = s.physics()->state(s.robot()->bodyOf(4)).position;
    CHECK(falling && (*falling)["position"].items()[1].number() == double(at.y) && (*falling)["position"].items()[0].number() == double(at.x), "the falling box is where the engine has it");
    CHECK(falling && !falling->has("fixed") && !falling->has("kinematic") && !falling->has("asleep"), "a falling box has no flag");
    const Value* post = shapeOf(state, 1);
    CHECK(post && (*post)["fixed"].truth() && !post->has("kinematic"), "a static body is fixed");
    const Value* led = shapeOf(state, 5);
    CHECK(led && (*led)["kinematic"].truth() && !led->has("fixed"), "a body led by its velocity is kinematic");
    CHECK(state["joints"].items().size() == 1 && state["joints"].items()[0]["built"].truth() && state["sensors"].items().size() == 1 &&
              state["sensors"].items()[0].has("truth") && state["contacts"].type() == Value::Type::List && state["step_ms"].type() == Value::Type::Map,
          "joints, sensors with their truth, contacts and timers");

    for (int k = 0; k < 600 && s.physics()->state(s.robot()->bodyOf(6)).awake; ++k) s.stepFrame();
    const Value rested = liveState(s); // kept: shapeOf points into it
    const Value* asleep = shapeOf(rested, 6);
    CHECK(asleep && (*asleep)["asleep"].truth(), "a body at rest that was put to sleep says so");

    const Value step = stepAnswer(s, 7, 3);
    CHECK(step["requested_frames"].number() == 7 && step["completed_frames"].number() == 3 && step["cancelled"].truth() && step["time"].number() == double(s.time()),
          "stepAnswer adds the frames asked and done");
    CHECK(!stepAnswer(s, 3, 3)["cancelled"].truth(), "a step that was done is not cancelled");

    Simulation none;
    const Value bare = liveState(none);
    CHECK(bare.keys().size() == 2 && bare.has("time") && bare.has("frame"), "no world: the time and the frame alone");

    // Two fresh worlds, the same calls: the same text, but the timers (wall clock).
    Simulation a, b;
    build(a, 40), build(b, 40);
    for (Simulation* w : {&a, &b}) call(w, "robot_command", "{\"commands\":{\"3\":{\"target\":0.2},\"4\":{\"force\":[1,2,3]}}}");
    a.stepFrame(), b.stepFrame();
    CHECK(withoutTimers(toJson(liveState(a))) == withoutTimers(toJson(liveState(b))), "the same calls on two worlds print the same");
    CHECK(toJson(sensorReadings(a)) == toJson(sensorReadings(b)) && toJson(bodyStates(a)) == toJson(bodyStates(b)), "sensor readings and body states too");
    CHECK(bodyStates(a).has("4") && !bodyStates(a).has("1") && bodyStates(a)["4"]["mass"].number() > 0, "the moving bodies by id, no static one");
}
