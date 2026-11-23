// The scene with no window (api/Scene.h). The bar is the recording of the window: every line of
// tests/data/window-transcript.jsonl that the window alone does not answer goes through a Scene and must come out as the
// window said it (the machine's timing and the operating system's words masked). Around it, what the recording cannot
// say: the operations list, a world that takes an edit and one that does not, the words of this host's own refusals, its
// files, and that two scenes given one script say one thing.
#include "TestRunner.h"

#include "api/Scene.h"
#include "core/Json.h"
#include "scene/SceneOps.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {

namespace fs = std::filesystem;

std::string slurp(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

Value fixture(const char* name) { return parseJson(slurp(std::string(KE_TEST_DATA) + "/" + name)); }

// A copy of v without the keys `drop` names, at any depth, and with every text passed through `text`.
Value rebuild(const Value& v, const std::function<bool(const std::string&)>& drop, const std::function<std::string(const std::string&)>& text) {
    switch (v.type()) {
    case Value::Type::Text: return Value(text(v.text()));
    case Value::Type::List: {
        Value out = Value::list();
        for (const Value& item : v.items()) out.push(rebuild(item, drop, text));
        return out;
    }
    case Value::Type::Map: {
        Value out = Value::map();
        for (const std::string& key : v.keys())
            if (!drop(key)) out.set(key, rebuild(v[key], drop, text));
        return out;
    }
    default: return v;
    }
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

// What the machine said about itself: the key and its whole value go.
bool timing(const std::string& key) { return key == "step_ms" || key == "frame_timing"; }
std::string plain(const Value& v) { return toJson(rebuild(v, timing, [](const std::string& s) { return s; })); }

// A joint's shape edited while built: the window answers until_stop and stops a frame later, the host stops at once.
Value stoppedInPlace(const Value& answer) {
    if (!answer.has("until_stop")) return answer;
    Value out = Value::map();
    for (const std::string& key : answer.keys())
        if (key != "until_stop") out.set(key, answer[key]);
    out.set("play_stopped", true);
    out.set("reload", "joints");
    return out;
}

// The keys of a map named in `keys` hold "<masked>": the operating system's words, which no host says alike.
// Two answers of a built world, recorded on one machine and computed on another: the same shape, words and whole
// numbers, and every measured number within a hundredth (of itself, and 0.01 near zero). The engine's last bits depend
// on the compiler (seen between GCC 11 and GCC 15: the eighth digit of an angle after a frame, and after 6000 frames
// the residual spin of bodies at rest, 0.003 rad/s, in its first) - one machine's own repeatability is the physics
// tests' matter (PhysicsTests.cpp), not this recording's.
bool closeAnswers(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
    case Value::Type::Number: {
        const double x = a.number(), y = b.number();
        return x == y || std::fabs(x - y) <= 1e-2 + 1e-2 * std::max(std::fabs(x), std::fabs(y));
    }
    case Value::Type::List:
    case Value::Type::Map: {
        if (a.size() != b.size() || a.keys() != b.keys()) return false;
        for (size_t i = 0; i < a.items().size(); ++i)
            if (!closeAnswers(a.items()[i], b.items()[i])) return false;
        return true;
    }
    default: return toJson(a) == toJson(b);
    }
}

Value maskKeys(const Value& v, const Value& keys) {
    Value out = Value::map();
    for (const std::string& k : v.keys()) {
        bool hit = false;
        for (const Value& masked : keys.items()) hit = hit || masked.text() == k;
        out.set(k, hit ? Value("<masked>") : v[k]);
    }
    return out;
}

// Where the window stood the light (over the point it took: 1.5 m below) or the camera (at its eye) that row `made` added
// and a later scene_get shows: the argument the window filled in.
Value placeOf(const std::vector<Value>& rows, size_t made, bool light) {
    for (size_t k = made + 1; k < rows.size(); ++k)
        if (rows[k]["request"]["op"].text() == "scene_get" && rows[k]["request"]["args"]["id"].integer() == rows[made]["answer"]["id"].integer())
            for (const Value& entry : rows[k]["answer"]["lines"].items())
                if (entry.items().size() > 4 && entry.items()[0].text() == "pose")
                    return vectorValue(Vector3(Real(entry.items()[2].number()), Real(entry.items()[3].number() - (light ? 1.5 : 0.0)), Real(entry.items()[4].number())));
    CHECK(false, "no scene_get after line %d shows where the window put it", rows[made]["n"].integer());
    return Value();
}

struct Folder {
    fs::path path;
    Folder() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("ke-scene-api-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path, ec);
    }
    ~Folder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string text() const { return path.generic_u8string(); } // forward slashes: it goes into JSON and is compared with answers
};

Value call(Scene& scene, const char* op, const char* args = "{}") { return scene.call(op, parseJson(args)); }

} // namespace

void testSceneApiTranscript() {
    Folder dir;
    Scene scene(KE_DATA);
    Value selection = Value::list(); // what the window held selected, by its selection_set lines
    int compared = 0, windowOnly = 0, raw = 0, written = 0;
    const auto inDir = [&](const std::string& s) { return replaceAll(s, "{DIR}", dir.text()); };
    const auto keep = [](const std::string&) { return false; };
    std::vector<Value> rows;
    std::istringstream lines(slurp(std::string(KE_TEST_DATA) + "/window-transcript.jsonl"));
    std::string line;
    std::getline(lines, line); // the first line explains the fields
    while (std::getline(lines, line)) rows.push_back(parseJson(line));
    for (size_t at = 0; at < rows.size(); ++at) {
        const Value& row = rows[at];
        const int n = row["n"].integer();
        if (row.has("write_file")) {
            std::ofstream(fs::u8path(inDir(row["write_file"]["path"].text())), std::ios::binary) << inDir(row["write_file"]["text"].text());
            ++written;
            continue;
        }
        if (row.has("raw")) { // a line sent as it is: its answer is the wire's, not this host's - what it does to the scene is
            ++raw;
            Value sent;
            std::string why;
            if (parseJson(row["raw"].text(), sent, why) && sent.type() == Value::Type::Map && sent["op"].type() == Value::Type::Text) scene.call(sent["op"].text(), sent["args"]);
            continue;
        }
        const Value request = row["request"];
        Value args = rebuild(request["args"], keep, inDir);
        const SceneOp* core = findSceneOp(request["op"].text());
        if (request["op"].text() == "selection_set") selection = args["ids"];
        for (const Value& key : row["filled"].items()) // where the window put what it made: read off its own scene_get of it
            if (request["op"].text().rfind("scene_add_", 0) == 0) args.set(key.text(), placeOf(rows, at, key.text() == "at"));
        if (core && core->takesIds && args.type() == Value::Type::Map && !args.has("ids") && row["filled"].type() == Value::Type::List) args.set("ids", selection); // the window's selection
        const Value got = scene.call(request["op"].text(), args);
        if (row.has("window_only")) { // run for what it does to the lines after it; its answer is the window's
            ++windowOnly;
            continue;
        }
        Value want = rebuild(row["answer"], keep, inDir);
        Value mine = got;
        if (row.has("mask")) {
            want = maskKeys(want, row["mask"]);
            mine = maskKeys(mine, row["mask"]);
        } else if (row.has("by_design")) {
            want = stoppedInPlace(want);
        }
        ++compared;
        const bool measured = scene.simulation() != nullptr; // the engine's numbers: equal to the last bit only on the machine that recorded them
        CHECK(plain(want) == plain(mine) || (measured && closeAnswers(parseJson(plain(want)), parseJson(plain(mine)))), "line %d (%s)\n    request: %s\n    window:  %s\n    host:    %s", n, row["note"].text().c_str(),
              toJson(request).c_str(), plain(want).c_str(), plain(mine).c_str());
    }
    std::printf("  transcript: %d answers compared, %d run with the answer left to the window (a selection, a view, a clock, the list of its own "
                "operations), %d raw lines run for their effect (their answers are the wire's), %d files written\n", compared, windowOnly, raw, written);
    CHECK(compared > 150, "most of the recording is compared (%d)", compared);
}

void testSceneApiOperationsList() {
    Scene scene(KE_DATA);
    const Value all = call(scene, "ops_list");
    CHECK(all["ok"].truth(), "ops_list answers");
    CHECK(all["protocol_version"].integer() == kProtocolVersion, "ops_list says the protocol's version: what a client built for it may call");
    std::map<std::string, Value> window;
    const Value recorded = fixture("ops-list.json");
    for (const Value& op : recorded["ops"].items()) window[op["name"].text()] = op;
    for (const Value& op : all["ops"].items()) {
        const std::string name = op["name"].text();
        CHECK(window.count(name) && toJson(op) == toJson(window[name]), "the entry of %s is the window's", name.c_str());
    }
    CHECK(all["ops"].items().size() == sceneOps().size() + worldOps().size() + hostOps().size(),
          "it lists the core's, the world's and the host's own operations, no other (%zu)", all["ops"].items().size());
    for (const char* absent : {"sim_play", "sim_pause", "sim_record", "selection_get", "view_screenshot"})
        CHECK(toJson(all).find(std::string("\"name\":\"") + absent + "\"") == std::string::npos, "%s is the window's alone", absent);

    const Value sim = call(scene, "ops_list", R"({"families": ["sim"]})");
    std::vector<std::string> names;
    for (const Value& op : sim["ops"].items()) names.push_back(op["name"].text());
    CHECK((names == std::vector<std::string>{"sim_impulse", "sim_measure", "sim_state", "sim_step", "sim_stop"}), "the sim family is five operations (%zu)", names.size());
    CHECK(call(scene, "ops_list", R"({"families": ["edit", "scene_x"]})")["ops"].items().size() == 2, "a family is the text before the first underscore: edit_undo, edit_redo");
}

void testSceneApiBuiltWorld() {
    Scene scene(KE_DATA);
    const Value light = call(scene, "scene_add_light", R"({"kind": "point", "at": [0, 1, 0]})");
    const Value box = call(scene, "scene_add", R"({"shape": "box", "role": "rigid", "position": [0, 1, 0]})");
    CHECK(light["ok"].truth() && box["ok"].truth() && scene.mode() == PlayMode::Edit && !scene.simulation(), "a scene of a light and a box, not built");
    const std::string id = std::to_string(box["id"].integer());
    const std::string lightId = std::to_string(light["id"].integer());

    const Value stepped = call(scene, "sim_step", R"({"frames": 3})");
    CHECK(stepped["ok"].truth() && stepped["frame"].integer() == 3 && scene.mode() == PlayMode::Paused && scene.simulation(), "a step builds the world and steps it in place");
    const Value state = call(scene, "sim_state");
    CHECK(state["mode"].text() == "paused" && state["frame"].integer() == 3 && !state.has("frame_timing"), "sim_state says its mode and keeps no frame timing");

    const double was = scene.graph().lights.front().intensity;
    const Value taken = call(scene, "scene_set", ("{\"id\": " + lightId + ", \"line\": \"kind\", \"key\": \"intensity\", \"values\": [7]}").c_str());
    CHECK(taken["ok"].truth() && taken["until_stop"].text() == untilStopNote() && !taken.has("play_stopped") && scene.simulation() &&
              scene.graph().lights.front().intensity == 7,
          "a light is taken by the world on the way: until_stop, still built");
    CHECK(call(scene, "sim_step", R"({"frames": 1})")["frame"].integer() == 4, "and the world goes on");
    CHECK(call(scene, "sim_stop")["mode"].text() == "edit" && scene.graph().lights.front().intensity == was && !scene.simulation(),
          "Stop brings the scene back: what the world took is gone");
    CHECK(call(scene, "sim_stop")["mode"].text() == "edit", "Stop in edit mode answers the mode too");

    call(scene, "sim_step", R"({"frames": 3})");
    const Value moved = call(scene, "scene_move", ("{\"ids\": [" + id + "], \"by\": [0, 0.1, 0]}").c_str());
    CHECK(moved["ok"].truth() && moved["play_stopped"].truth() && moved["reload"].text() == "bodies" && !moved.has("until_stop") &&
              scene.mode() == PlayMode::Edit && !scene.simulation(),
          "a body moved while built stops the world and keeps the edit: play_stopped, reload bodies");
    CHECK(call(scene, "sim_state")["mode"].text() == "edit", "the mode is edit");
    CHECK(call(scene, "sim_step", R"({"frames": 1})")["frame"].integer() == 1, "the next step builds a new world at frame 0");
    const Value undone = call(scene, "edit_undo");
    CHECK(undone["ok"].truth() && undone["play_stopped"].truth() && undone["reload"].text() == "bodies", "an undo while built says it the same way");
    CHECK(call(scene, "sim_state")["mode"].text() == "edit", "and the world is dropped");

    const Value force = call(scene, "scene_add_force", R"({"shape": "directional", "mode": "constant", "at": [0, 3, 0]})");
    call(scene, "sim_step", R"({"frames": 2})");
    const Value stronger = call(scene, "scene_set", ("{\"id\": " + std::to_string(force["id"].integer()) + ", \"line\": \"effect\", \"key\": \"strength\", \"values\": [20]}").c_str());
    CHECK(stronger["ok"].truth() && !stronger.has("play_stopped") && scene.simulation() && scene.mode() == PlayMode::Paused &&
              scene.simulation()->robot()->graph().forces.front().strength == 20,
          "a force's strength is taken by the running world, not rebuilt");
    CHECK(call(scene, "force_fire", ("{\"id\": " + std::to_string(force["id"].integer()) + "}").c_str())["error"].text().find("constant") != std::string::npos,
          "force_fire of a constant force is refused");

    call(scene, "sim_step", R"({"frames": 0})");
    CHECK(scene.simulation() && call(scene, "scene_new")["ok"].truth() && scene.mode() == PlayMode::Edit && scene.graph().entities.size() == 1,
          "a new scene stops a built world");
    CHECK(!call(scene, "sim_measure", R"({"scene": true})")["error"].text().empty(), "the world's own operations answer in edit mode with no world: in words");
}

void testSceneApiRefusals() {
    Scene scene(KE_DATA);
    const Value play = call(scene, "sim_play");
    CHECK(!play["ok"].truth() && play["error"].text() == "нет операции «sim_play» в kinetica-core: часов нет, время идёт по sim_step (scene.run, scene.step)",
          "sim_play says there is no clock (%s)", play["error"].text().c_str());
    CHECK(call(scene, "sim_pause")["error"].text().find("часов нет") != std::string::npos, "so does sim_pause");
    const Value other = call(scene, "selection_get");
    CHECK(other["error"].text() == "нет операции «selection_get» в kinetica-core (ops_list называет все её операции; выбор, вид, консоль, плагины и журнал есть только у окна)",
          "an operation of the window alone is named so (%s)", other["error"].text().c_str());
    CHECK(call(scene, "scene_list", R"({"nope": 1})")["error"].text().find("nope") != std::string::npos, "an unknown field is refused by the schema");
    CHECK(scene.call("scene_list", parseJson("[1]"))["ok"].truth(), "arguments that are no object act as none");
    CHECK(call(scene, "scene_delete")["error"].text() == "no objects to act on: pass ids", "no selection to fill ids from: the operation's own words");
    bool thrown = false;
    try {
        Scene gone("/no/such/folder/for/ke");
    } catch (const std::runtime_error& e) {
        thrown = std::string(e.what()).find("нет каталога data/sensors") == 0;
    }
    CHECK(thrown, "a data folder that is not there throws, in words");
    Scene none("");
    CHECK(call(none, "scene_list")["ok"].truth(), "an empty name is no tables, on purpose");
}

void testSceneApiFiles() {
    Folder dir;
    Scene scene(KE_DATA);
    const std::string base = dir.text();
    call(scene, "scene_add", R"({"shape": "box"})");
    const Value saved = scene.call("scene_save", parseJson("{\"file\": \"" + base + "/a.kscene\"}"));
    CHECK(saved["ok"].truth() && saved["file"].text() == base + "/a.kscene", "a save answers the absolute path");
    CHECK(slurp(base + "/a.kscene") == scene.graph().save() && !fs::exists(fs::path(base) / "a.kscene.part"), "the file is the scene's text, and no part is left");

    Scene other(KE_DATA);
    CHECK(other.call("scene_open", parseJson("{\"file\": \"" + base + "/a.kscene\"}"))["ok"].truth() && other.graph().save() == scene.graph().save(),
          "it opens in another host as the same scene");
    CHECK(call(other, "edit_undo")["undone"].truth() && other.graph().entities.size() == 1 + 0, "an open is one step: its undo brings the floor back");

    const auto refused = [&](const char* op, const std::string& file) {
        Value args = Value::map();
        args.set("file", file);
        return scene.call(op, args)["error"].text();
    };
    CHECK(refused("scene_open", "") == "не назван файл", "an open of no name");
    CHECK(refused("scene_save", "") == "не назван файл", "a save of no name");
    CHECK(refused("scene_open", base + "/none.kscene") == "нет файла «" + base + "/none.kscene»", "an open of a file that is not there");
    CHECK(refused("scene_open", "none-here.kscene").find("нет файла «none-here.kscene» (искали: ") == 0, "a relative name says where it looked");
    CHECK(refused("scene_open", base) == "«" + base + "» - каталог, а не файл", "an open of a folder");
    CHECK(refused("scene_save", base + "/no/x.kscene") == "нет каталога «" + base + "/no» для файла «" + base + "/no/x.kscene»", "a save into a folder that is not there");
    std::ofstream(fs::path(base) / "bad.kscene") << "this is not a scene\nfoo bar\n";
    CHECK(refused("scene_open", base + "/bad.kscene") == "line 1: cannot read 'this'", "a scene that does not parse keeps the core's words");
    CHECK(scene.graph().entities.size() == 2, "a failed open changes nothing");

    std::ofstream(fs::path(base) / "latin.kscene", std::ios::binary) << "# Kinetica scene\nentity \"A\xE9\"\n  shape box size 1 1 1\nend\n";
    CHECK(other.call("scene_open", parseJson("{\"file\": \"" + base + "/latin.kscene\"}"))["ok"].truth() && toJson(call(other, "scene_list")).find("\\ufffd") == std::string::npos &&
              validUtf8(other.graph().entities.back().name) == other.graph().entities.back().name,
          "a name with a byte that is no UTF-8 is repaired on reading");
}

void testSceneApiTwoScenesAgree() {
    const auto script = [] {
        Scene scene(KE_DATA);
        std::string out;
        for (const auto& [op, args] : std::vector<std::pair<const char*, const char*>>{
                 {"scene_add", R"({"shape": "box", "role": "rigid", "position": [0, 1, 0]})"},
                 {"scene_add", R"({"shape": "sphere", "role": "rigid", "position": [0.3, 2, 0]})"},
                 {"sim_step", R"({"frames": 60})"},
                 {"scene_add", R"({"shape": "cone", "role": "rigid", "position": [-1, 1, 0]})"},
                 {"sim_step", R"({"frames": 30})"},
                 {"sim_state", "{}"},
                 {"scene_get", "{}"}})
            out += plain(call(scene, op, args)) + "\n";
        return out;
    };
    const std::string first = script(), second = script();
    CHECK(first == second && first.size() > 2000, "the same script on two fresh scenes says the same text, but for the machine's timing (%zu)", first.size());
}

// Hyper-operations: one scene_batch builds a robot - a box, a wheel, the hinge between them by "$1" and "$0" - and a
// reference to no step is refused with words; assembly_add puts the built-in sports car in, whole, in one call.
void testSceneApiHyperOperations() {
    Scene scene(KE_DATA);
    const Value built = call(scene, "scene_batch", R"({"ops": [
        {"op": "scene_add", "args": {"shape": "box", "position": [0, 0.3, 0], "size": [0.6, 0.05, 0.3], "role": "rigid"}},
        {"op": "scene_add", "args": {"shape": "cylinder", "position": [0, 0.3, 0.2], "size": [0.2, 0.06, 0.2], "role": "rigid"}},
        {"op": "assembly_joint", "args": {"a": "$1", "b": "$0", "type": "hinge", "anchor": [0, 0.3, 0.18], "axis": [0, 0, 1]}}]})");
    const auto& steps = built["results"].items();
    CHECK(built["ok"].truth() && steps.size() == 3 && scene.graph().joints.size() == 1 && scene.graph().joints[0].a == uint32_t(steps[1]["id"].integer()) &&
              scene.graph().joints[0].b == uint32_t(steps[0]["id"].integer()), "one batch: two shapes and the hinge between them: %s", built["error"].text("").c_str());
    const Value wrong = call(scene, "scene_batch", R"({"ops": [{"op": "assembly_joint", "args": {"a": "$4", "b": 0, "type": "hinge"}}]})");
    CHECK(!wrong["ok"].truth() && wrong["error"].text().find("no earlier step 4") != std::string::npos, "a reference to no step: %s", wrong["error"].text().c_str());
    const size_t before = scene.graph().entities.size();
    const Value car = call(scene, "assembly_add", R"({"name": "Спорткар", "position": [3, 0, 0]})");
    CHECK(car["ok"].truth() && scene.graph().entities.size() == before + 14 && scene.graph().drivers.size() == 2 && scene.graph().controllers.size() == 1,
          "the sports car in one call: %s", car["error"].text("").c_str());
    const Value bigger = call(scene, "assembly_add", R"({"name": "Спорткар", "position": [-3, 0, 0], "overrides": [{"name": "Батарея 1", "line": "cells", "key": "parallel", "values": [2]}]})");
    const auto pack = std::find_if(scene.graph().batteries.begin(), scene.graph().batteries.end(), [&](const BatteryObject& b) { return b.id == uint32_t(bigger["ids"]["31"].integer()); });
    CHECK(bigger["ok"].truth() && pack != scene.graph().batteries.end() && pack->parallel == 2, "an override changes its part as it goes in: %s", bigger["error"].text("").c_str());
    CHECK(!call(scene, "assembly_add", R"({"name": "Спорткар", "overrides": [{"name": "Нет такой", "line": "cells", "key": "parallel", "values": [2]}]})")["ok"].truth(),
          "an override of no part is refused, nothing put in");
    const std::string drivers = std::to_string(scene.graph().drivers[0].id);
    const Value notPack = scene.call("assembly_power", parseJson("{\"joint\": " + std::to_string(scene.graph().drivers[0].joint) + ", \"battery\": " + drivers + "}"));
    CHECK(!notPack["ok"].truth() && notPack["error"].text().find("is no battery") != std::string::npos, "a driver's id given for a battery is refused: %s",
          notPack["error"].text("").c_str());
    const Value listed = call(scene, "assembly_list");
    bool found = false;
    for (const Value& a : listed["assemblies"].items()) found = found || a["name"].text() == "Спорткар";
    CHECK(found, "assembly_list names the built-in sports car");
}
