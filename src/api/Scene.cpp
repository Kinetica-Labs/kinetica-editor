// A scene with no window (Scene.h): the table of operations - the core's, the world's and the host's own twelve - their
// call in the order the window keeps, the world built and stepped in place, what a running world takes of an edit, and
// the scene's files for this host.
#include "api/Scene.h"

#include "core/Json.h"
#include "scene/ArgsSchema.h"
#include "scene/SceneFiles.h"
#include "scene/SensorSpec.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ke {

namespace {

namespace fs = std::filesystem;

Value failure(const std::string& why) {
    Value a = Value::map();
    a.set("ok", false);
    a.set("error", why);
    return a;
}

std::string unknown(const std::string& name) {
    if (name == "sim_play" || name == "sim_pause") return "нет операции «" + name + "» в kinetica-core: часов нет, время идёт по sim_step (scene.run, scene.step)";
    return "нет операции «" + name + "» в kinetica-core (ops_list называет все её операции; выбор, вид, консоль, плагины и журнал есть только у окна)";
}

} // namespace

const std::vector<HostOp>& hostOps() {
    static const std::vector<HostOp> ops = {
        {"scene_new", "A new scene: the floor and nothing else, the play range 0..10 s (the world's duration)", "{}", "edit"},
        {"scene_open", "A scene file opened (one undo step back to the scene before)", "{file: str}", "edit"},
        {"scene_save", "The scene saved to a file", "{file: str}", "action"},
        {"assembly_list", "The assemblies (сборки) - parts of scenes kept as scene files, a car, a gripper, a stand: the program's own and the "
                          "user's (assembly_save), each its name and file", "{}", "query"},
        {"assembly_add", "An assembly put into the scene in one step: by its name (assembly_list), a file, or text - blocks of the "
                         "scene file as scene_get gives them, with ids of their own (unused ones are given: whatever refers to whatever "
                         "inside keeps doing so) - so a whole robot of shapes, joints, drives, sensors and a controller is one call. Under a "
                         "group of its name standing at position (none: the origin) - the group moves and turns it whole. Answers the "
                         "group's id and each id of the text with the one it got. overrides: its parts changed as it goes in, each a scene_set by "
                         "the part's name - {name: \"Батарея 1\", line: cells, key: parallel, values: [2]} - so a ready robot takes "
                         "another battery, motor or panel in the same call", "{name?: str, file?: str, text?: str, position?: [x,y,z], "
                         "overrides?: [{name: str, line: str, key?: str, values?: []}]}", "edit"},
        {"assembly_describe", "An assembly studied before one's own is built - how a ready car, arm or stand is made: its bodies as a "
                              "tree of joints with shapes, masses and roles, the drives, motors, sensors and batteries, in words as "
                              "scene_describe gives the scene (by its name, assembly_list, or a file); nothing is put into the scene",
         "{name?: str, file?: str}", "query"},
        {"assembly_save", "The objects ids with all under them and what belongs to them - joints between them, their sensors, drivers and "
                          "batteries, the controllers that drive only them - kept as the user's assembly of this name (a scene file "
                          "standing about the origin on the floor). Answers its file", "{ids: [int], name: str}", "action"},
        {"edit_undo", "Undo the last step. Answers what it changed: the objects' ids, whether the world did; undone false: nothing left to undo", "{}", "edit"},
        {"edit_redo", "Redo the step undone last. Answers what it changed, as edit_undo", "{}", "edit"},
        {"sim_stop", "Stop: the scene as it was before play", "{}", "window"},
        {"sim_step", "Step N frames (1/60 s each) on pause and answer the live state, as sim_state gives it (frames 0: the scene built as play "
                     "builds it, its state at time 0); state false: only the time, the frame and the frames stepped - for a client that "
                     "steps often and reads what it needs by sensor_read or sim_measure", "{frames?: int, state?: bool (true)}", "window"},
        {"sim_state",
         "The live state: time and frame, every body's pose (position of its frame, rotation: Euler degrees [x, y, z], the "
         "turn R = Ry Rx Rz - as the scene file), velocity and spin (asleep: at rest, put to sleep; fixed: a static body; kinematic: one led by its own velocity or by a sequence), every "
         "joint's coordinates, main angle or travel, its drive's effort (N m or N) and a DC motor's current and power, every sensor's reading "
         "as the robot reads it (its datasheet's device: an IMU's accel and gyro, a sonar's distance, an encoder's count, a wrench's force and "
         "torque, a pad's force) with its probe's truth beside it, which entities "
         "touch (contacts: a, b - 0 the floor -, points, normal force N, deepest overlap m), what the world could not build (notes); "
         "frame_timing: the last 120 frames' mean wall ms in posted commands, the wait for the plugins' on_tick (controllers_ms), the step "
         "and the snapshot, and the window's painting - its "
         "frames a second, one frame's ms and the renderer, a GPU or llvmpipe (the scene runs slower than "
         "the wall clock when they pass frame_dt_ms). The physics engine's truth, for checks - not a robot's sensors", "{}", "query"},
        {"ops_list", "Every operation of the editor: name, help, arguments, their JSON Schema, and kind - query (reads), edit (the "
                     "scene, one undo step), window (the selection, the view, play and pause: never the authored scene) or "
                     "action (files, plugins, the program). families: only the operations whose names begin so - "
                     "[\"scene\", \"sim\"] gives scene_* and sim_*; protocol_version - what a client built for it may call: a "
                     "client of N works with every program of N and later", "{families?: [str]}", "query"},
    };
    return ops;
}

const HostOp* hostOp(const std::string& name) {
    for (const HostOp& op : hostOps())
        if (op.name == name) return &op;
    return nullptr;
}

Value opsListEntry(const std::string& name, const std::string& help, const std::string& args, const std::string& kind, const Value& schema) {
    Value entry = Value::map();
    entry.set("name", name);
    entry.set("help", help);
    entry.set("args", args);
    entry.set("kind", kind);
    entry.set("schema", schema);
    return entry;
}

bool inFamilies(const std::string& name, const Value& families) {
    if (families.type() != Value::Type::List || families.items().empty()) return true;
    const std::string family = name.substr(0, name.find('_'));
    for (const Value& f : families.items())
        if (f.type() == Value::Type::Text && f.text() == family) return true;
    return false;
}

bool editsGraph(const std::string& name) {
    const SceneOp* core = findSceneOp(name);
    return (core && core->edit) || name == "edit_undo" || name == "edit_redo";
}

const char* untilStopNote() { return "сцена играет: правка действует до ■ Стоп - sim_stop вернёт сцену, какой она была до ▶"; }

Scene::Scene(const std::string& dataDir) : dataDir_(dataDir) {
    sensorCatalog().clear();
    if (!dataDir.empty()) {
        const fs::path dir = fs::u8path(dataDir) / "sensors";
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) throw std::runtime_error("нет каталога data/sensors в «" + dataDir + "»");
        std::string problems;
        for (const SensorKind kind : kTableKinds) {
            const fs::path file = dir / (std::string(sensorKindKey(kind)) + ".csv");
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            std::ostringstream text;
            text << in.rdbuf();
            problems += sensorCatalog().read(kind, text.str(), file.u8string());
        }
        std::istringstream lines(problems);
        for (std::string line; std::getline(lines, line);)
            if (!line.empty()) std::fprintf(stderr, "kinetica-core: таблицы датчиков: %s\n", line.c_str());
    }
    doc_.newScene();
    for (const SceneOp& op : sceneOps()) {
        Op& entry = ops_[op.name];
        entry.core = &op;
        entry.help = op.help, entry.args = op.args, entry.kind = op.query ? "query" : "edit";
    }
    for (const WorldOp& op : worldOps()) {
        Op& entry = ops_[op.name];
        entry.world = &op;
        entry.help = op.help, entry.args = op.args, entry.kind = op.kind;
    }
    for (const HostOp& op : hostOps()) {
        Op& entry = ops_[op.name];
        entry.help = op.help, entry.args = op.args, entry.kind = op.kind;
    }
}

const Value& Scene::schemaOf(Op& op) {
    if (op.schema.isNull()) op.schema = argsSchema(op.args);
    return op.schema;
}

Value Scene::call(const std::string& name, const Value& given) {
    try {
        return run(name, given.type() == Value::Type::Map ? given : Value::map());
    } catch (const std::exception& e) {
        return failure(std::string("внутренняя ошибка: ") + e.what());
    } catch (...) {
        return failure("внутренняя ошибка: неизвестное исключение");
    }
}

Value Scene::run(const std::string& name, const Value& args) {
    const auto found = ops_.find(name);
    if (found == ops_.end()) {
        std::vector<std::string> names;
        for (const auto& [n, op] : ops_) names.push_back(n);
        const std::string near = nearestWord(name, names);
        return failure(unknown(name) + (near.empty() ? std::string() : "; может, «" + near + "»?"));
    }
    Op& op = found->second;
    if (const std::string why = argsProblem(args, schemaOf(op)); !why.empty()) return failure(why); // refused, not guessed
    const bool built = doc_.mode != PlayMode::Edit;
    const bool edits = editsGraph(name);
    Value answer = Value::map();
    std::string why;
    bool done;
    if (op.core && op.core->query) {
        done = op.core->query(doc_.graph, args, answer, why);
    } else if (op.core) {
        done = doc_.transact([&](SceneGraph& g, std::string& error) { return op.core->edit(g, args, answer, error); }, why);
    } else if (op.world) {
        const std::vector<ControllerObject>& controllers = doc_.graph.controllers;
        done = op.world->run(built ? sim_.get() : nullptr, controllers.empty() ? 0 : controllers.front().id, args, answer, why);
    } else {
        done = hostCall(name, args, answer, why);
    }
    if (!done) return failure(why);
    answer.set("ok", true);
    if (edits && built) { // decided after the edit: the one the world could not take stopped it, and is kept
        if (const LiveEdit::Reload reload = takeInWorld(); reload != LiveEdit::Reload::None) {
            answer.set("play_stopped", true);
            answer.set("reload", reloadWord(reload));
        } else {
            answer.set("until_stop", untilStopNote());
        }
    }
    return answer;
}

bool Scene::hostCall(const std::string& name, const Value& args, Value& answer, std::string& error) {
    if (name == "ops_list") {
        Value list = Value::list();
        for (auto& [opName, op] : ops_)
            if (inFamilies(opName, args["families"])) list.push(opsListEntry(opName, op.help, op.args, op.kind, schemaOf(op)));
        answer.set("ops", list);
        answer.set("protocol_version", int64_t(kProtocolVersion));
    } else if (name == "scene_new") {
        if (doc_.mode != PlayMode::Edit) stopWorld(false);
        doc_.newScene();
    } else if (name == "scene_open") {
        SceneGraph g;
        if (!readSceneFile(args["file"].text(), g, error)) return false; // parsed first: a failed open leaves a built world alone
        if (doc_.mode != PlayMode::Edit) stopWorld(false);
        doc_.load(std::move(g));
    } else if (name == "scene_save") {
        std::string file;
        if (!writeSceneFile(doc_.graph, args["file"].text(), file, error)) return false;
        answer.set("file", file);
    } else if (name.compare(0, 9, "assembly_") == 0) {
        if (name == "assembly_add" && doc_.mode != PlayMode::Edit) stopWorld(false);
        return assemblyCall(name, args, doc_.graph, dataDir_, [this](const auto& change, std::string& why) { return doc_.transact(change, why); }, answer, error);
    } else if (name == "edit_undo" || name == "edit_redo") {
        const bool undo = name == "edit_undo";
        const SceneGraph before = doc_.graph;
        const bool did = undo ? doc_.undo() : doc_.redo();
        answer = historyAnswer(before, doc_.graph, undo, did, doc_.history.steps());
    } else if (name == "sim_step") {
        if (doc_.mode == PlayMode::Edit) {
            if (!sim_) sim_ = std::make_unique<Simulation>();
            startWorld(*sim_, doc_.beginPlay());
        }
        const int frames = framesArg(args);
        for (int k = 0; k < frames; ++k) sim_->stepFrame();
        answer = stepAnswer(*sim_, frames, frames, args["state"].truth(true));
    } else if (name == "sim_stop") {
        if (doc_.mode != PlayMode::Edit) stopWorld(false);
        answer.set("mode", modeWord(doc_.mode));
    } else { // sim_state
        if (doc_.mode != PlayMode::Edit) answer = liveState(*sim_);
        answer.set("mode", modeWord(doc_.mode));
    }
    return true;
}

void Scene::stopWorld(bool keepEdits) {
    sim_->load(SceneGraph()); // the meta-objects go: bodies, joints, drives
    doc_.endPlay(keepEdits);
}

// What the world can take of the edit goes to it and is kept in `running`; what it cannot stops it with the edit kept.
LiveEdit::Reload Scene::takeInWorld() {
    const SceneGraph& g = doc_.graph;
    const LiveEdit le = liveEdit(doc_.running, g);
    LiveEdit::Reload reload = le.reload;
    if (RobotWorld* robot = sim_->robot(); robot && reload == LiveEdit::Reload::None) {
        if (le.duties) robot->setDuties(g.drivers);
        if (le.lights) robot->setLightsAndCameras(g.lights, g.cameras);
        if (le.forces) robot->setForces(g.forces);
        if (le.goals) { // the handles: IK goals and cameras' targets
            std::vector<Entity> goals;
            for (const Entity& e : g.entities)
                if (isHandle(e)) goals.push_back(e);
            robot->setGoals(goals);
        }
        if (le.joints && !robot->setJoints(*sim_->physics(), g.joints)) reload = LiveEdit::Reload::Joints;
    }
    if (reload != LiveEdit::Reload::None) stopWorld(true);
    else doc_.running = g;
    return reload;
}

} // namespace ke
