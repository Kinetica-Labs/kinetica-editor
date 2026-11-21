// The list of the core's operations (SceneOps.h) and its first piece: the scene's list and blocks, the
// core edits of the scene file (put a block, set a line, remove an object, the world's lines) and a
// batch of edits as one step - the inverse-able edits of SceneEdits.h under their API names.
#include "scene/SceneOps.h"

#include "core/Format.h"
#include "scene/Assembly.h"
#include "scene/Describe.h"
#include "scene/SceneEdits.h"
#include "scene/SensorSpec.h"

#include <algorithm>

namespace ke {

namespace {

const char* kindOf(const SceneGraph& g, uint32_t id) {
    auto in = [id](const auto& v) { return std::any_of(v.begin(), v.end(), [id](const auto& o) { return o.id == id; }); };
    return in(g.entities) ? "entity" : in(g.groups) ? "group" : in(g.arrays) ? "array" : in(g.lights) ? "light" : in(g.cameras) ? "camera"
         : in(g.batteries) ? "battery" : in(g.drivers) ? "driver" : in(g.sensors) ? "sensor" : in(g.wheels) ? "wheel"
         : in(g.controllers) ? "controller" : findJoint(g, id) ? "joint" : "";
}

// The words scene_list's `kind` takes: a row's kind, a sensor's own ("encoder", "imu"...: SensorSpec.h), and "body" - an
// entity with the rigid role, what physics moves.
const char* const kListKinds[] = {"entity", "body", "group", "array", "light", "camera", "joint", "battery", "driver", "sensor", "wheel", "controller"};

bool isOfKind(const SceneGraph& g, uint32_t id, const std::string& word) {
    if (word == kindOf(g, id)) return true;
    for (const SensorObject& s : g.sensors)
        if (s.id == id) return word == sensorKindKey(s.kind);
    for (const Entity& e : g.entities)
        if (e.id == id) return word == "body" && resolveInstance(g, e).rigid.enabled;
    return false;
}

// Empty word: every object. A word no object can be: false, and the words there are.
bool objectList(const SceneGraph& g, const std::string& word, Value& out, std::string& error) {
    out = Value::list();
    bool known = word.empty() || std::any_of(std::begin(kListKinds), std::end(kListKinds), [&](const char* k) { return word == k; });
    for (int k = 0; k <= int(SensorKind::Stereo) && !known; ++k) known = word == sensorKindKey(SensorKind(k));
    if (!known) {
        error = "kind '" + word + "' is not one of:";
        for (const char* k : kListKinds) error += std::string(" ") + k;
        for (int k = 0; k <= int(SensorKind::Stereo); ++k) error += std::string(" ") + sensorKindKey(SensorKind(k));
        return false;
    }
    auto add = [&](const auto& objects) {
        for (const auto& o : objects) {
            if (!word.empty() && !isOfKind(g, o.id, word)) continue;
            Value row = Value::map();
            row.set("id", o.id), row.set("kind", kindOf(g, o.id)), row.set("name", o.name);
            out.push(row);
        }
    };
    add(g.entities), add(g.groups), add(g.arrays), add(g.lights), add(g.cameras), add(g.joints), add(g.batteries), add(g.drivers);
    add(g.sensors), add(g.wheels), add(g.controllers);
    return true;
}

SceneOp query(const char* name, const char* help, const char* args, SceneOp::Query run) { return {name, help, args, std::move(run), {}, false}; }
SceneOp edit(const char* name, const char* help, const char* args, SceneOp::Edit run) { return {name, help, args, {}, std::move(run), false}; }

// A block's words as an answer holds them: a number as a number, a quoted word as text whatever it reads as.
Value wordValues(const std::vector<BlockWord>& words) {
    Value list = Value::list();
    double number = 0;
    for (const BlockWord& w : words) list.push(!w.quoted && parseNumber(w.text, number) ? Value(number) : Value(w.text));
    return list;
}

// scene_get's and world_get's {line, key?, count?} of a text: the values there, or null.
Value fieldValues(const std::string& block, const Value& a) {
    const std::vector<BlockWord> words = getField(block, a["line"].text(""), a["key"].text(""), size_t(std::max(a["count"].integer(1), 1)));
    return words.empty() ? Value() : wordValues(words);
}

std::vector<SceneOp> readOps() {
    return {
        query("scene_list", "The objects of the scene: id, kind, name. With kind - only those: a row's kind (entity, group, array, light, "
              "camera, joint, battery, driver, sensor, wheel, controller), a sensor's own (encoder, imu, sonar, touch, wrench, effort, "
              "stereo), or body - an entity with the rigid role. What a plugin's field of a scene object chooses from",
              "{kind?: str}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
            Value objects;
            if (!objectList(g, a["kind"].text(""), objects, e)) return false;
            return r.set("objects", objects), true;
        }),
        query("scene_get", "One object's block of the scene file: text, and the same read - kind, name, lines (each line as its words, a "
              "number as a number); the whole file without an id. With line: values - the count words (1) after key on the object's "
              "line that starts with the words of line, as scene_set names them (no key: right after the line's own; null: not there)",
              "{id?: int, line?: str, key?: str, count?: int}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
            if (!a.has("id")) return a.has("line") ? (e = "a line is an object's: give its id", false) : (r.set("text", g.save()), true);
            const std::string block = getObject(g, uint32_t(a["id"].integer()));
            if (block.empty()) return e = "no object " + std::to_string(a["id"].integer()), false;
            if (a.has("line")) return r.set("values", fieldValues(block, a)), true;
            const std::vector<std::vector<BlockWord>> lines = blockLines(block);
            Value all = Value::list();
            for (const std::vector<BlockWord>& words : lines) all.push(wordValues(words));
            r.set("text", block), r.set("kind", lines[0][0].text), r.set("name", lines[0].size() > 1 ? lines[0][1].text : "");
            return r.set("lines", all), true;
        }),
        query("scene_describe", "The scene in words: the bodies as a tree of joints with their shapes, masses, roles and drives; sensors and where "
              "they look; the batteries - for a person or a language model building by text", "{}",
              [](const SceneGraph& g, const Value&, Value& r, std::string&) { return r.set("text", describeScene(g)), true; }),
        query("world_get", "The world's 'world' and 'physics' lines of the scene file: text. With line (world, physics): values - the count "
              "words (1) after key on that line, as scene_get reads an object's (null: not there)",
              "{line?: str, key?: str, count?: int}", [](const SceneGraph& g, const Value& a, Value& r, std::string&) {
            return a.has("line") ? r.set("values", fieldValues(getWorld(g), a)) : r.set("text", getWorld(g)), true;
        }),
    };
}

// The edits a batch may hold: every edit of the list but the batch itself.
bool batch(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    Value results = Value::list();
    for (const Value& step : a["ops"].items()) {
        const std::string name = step["op"].text("");
        const SceneOp* op = findSceneOp(name);
        if (!op || !op->edit || name == "scene_batch") return e = "'" + name + "' is no edit a batch can hold", false;
        Value answer = Value::map();
        if (!op->edit(g, step["args"], answer, e)) return e = name + ": " + e, false;
        results.push(answer);
    }
    return r.set("results", results), true;
}

} // namespace

std::vector<SceneOp> coreSceneOps() {
    std::vector<SceneOp> ops = readOps();
    ops.push_back(edit("scene_put", "A block of the scene file in: a new object (id 0 or unused), or the one of its id replaced", "{block: str}",
                       [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        const uint32_t id = putObject(g, a["block"].text(""), e);
        return r.set("id", id), id != 0;
    }));
    ops.push_back(edit("world_set", "The world's 'world' and 'physics' lines, as world_get gives them (gravity, box, physics engine; a missing physics "
                       "line: defaults) - or one 'line key values' of them, as scene_set sets an object's: line physics, key substeps",
                       "{text?: str, line?: str, key?: str, values?: [number|str]}", [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        if (!a.has("line")) return a.has("text") ? putWorld(g, a["text"].text(""), e) : (e = "give the text, or a line with its key and values", false);
        std::vector<std::string> values;
        for (const Value& v : a["values"].items()) values.push_back(fileWord(v));
        return setWorldField(g, a["line"].text(""), a["key"].text(""), values, e);
    }));
    ops.push_back(edit("scene_set", "One 'line key values' of an object's block, as in the scene file (a missing role line is added)",
                       "{id: int, line: str, key?: str, values?: [number|str]}", [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        std::vector<std::string> values;
        for (const Value& v : a["values"].items()) values.push_back(fileWord(v));
        const uint32_t id = setField(g, uint32_t(a["id"].integer()), a["line"].text(""), a["key"].text(""), values, e);
        return r.set("id", id), id != 0;
    }));
    ops.push_back(edit("scene_remove", "One object out, with its joints; its children keep their place in the world (scene_delete: with everything under it)", "{id: int}",
                       [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        return removeObject(g, uint32_t(a["id"].integer())) || (e = "no object " + std::to_string(a["id"].integer()), false);
    }));
    ops.push_back(edit("scene_batch", "Several edits as one undo step, all or none", "{ops: [{op: str, args: {}}]}", batch));
    return ops;
}

const std::vector<SceneOp>& sceneOps() {
    static const std::vector<SceneOp> all = [] {
        std::vector<SceneOp> ops = coreSceneOps();
        for (std::vector<SceneOp> part : {actionSceneOps(), assemblySceneOps(), kinematicSceneOps(), controllerSceneOps(), animationSceneOps()})
            ops.insert(ops.end(), part.begin(), part.end());
        return ops;
    }();
    return all;
}

const SceneOp* findSceneOp(const std::string& name) {
    const std::vector<SceneOp>& ops = sceneOps();
    const auto it = std::find_if(ops.begin(), ops.end(), [&](const SceneOp& o) { return o.name == name; });
    return it != ops.end() ? &*it : nullptr;
}

bool vectorArg(const Value& args, const char* key, Vector3& out) {
    const Value& v = args[key];
    if (v.type() != Value::Type::List || v.size() != 3) return false;
    out = Vector3(Real(v.items()[0].number()), Real(v.items()[1].number()), Real(v.items()[2].number()));
    return true;
}

std::vector<uint32_t> idsArg(const Value& args) {
    std::vector<uint32_t> ids;
    for (const Value& v : args["ids"].items()) ids.push_back(uint32_t(v.integer()));
    return ids;
}

Value vectorValue(const Vector3& v) {
    Value out = Value::list();
    out.push(double(v.x)), out.push(double(v.y)), out.push(double(v.z));
    return out;
}

Value idsValue(const std::vector<uint32_t>& ids) {
    Value out = Value::list();
    for (uint32_t id : ids) out.push(id);
    return out;
}

Value selected(const std::vector<uint32_t>& ids, uint32_t active) {
    Value out = Value::map();
    out.set("ids", idsValue(ids)), out.set("id", active);
    return out;
}

} // namespace ke
