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
#include <map>
#include <type_traits>

namespace ke {

namespace {

const char* kindOf(const SceneGraph& g, uint32_t id) {
    auto in = [id](const auto& v) { return std::any_of(v.begin(), v.end(), [id](const auto& o) { return o.id == id; }); };
    return in(g.entities) ? "entity" : in(g.groups) ? "group" : in(g.arrays) ? "array" : in(g.lights) ? "light" : in(g.cameras) ? "camera"
         : in(g.batteries) ? "battery" : in(g.drivers) ? "driver" : in(g.sensors) ? "sensor" : in(g.forces) ? "force" : in(g.wheels) ? "wheel"
         : in(g.controllers) ? "controller" : in(g.sequences) ? "sequence" : findJoint(g, id) ? "joint" : "";
}

// The words scene_list's `kind` takes: a row's kind, a sensor's own ("encoder", "imu"...: SensorSpec.h), and "body" - an
// entity with the rigid role, what physics moves.
const char* const kListKinds[] = {"entity", "body", "group", "array", "light", "camera", "joint", "battery", "driver", "sensor", "force", "wheel", "controller",
                                  "sequence"};

bool isOfKind(const SceneGraph& g, uint32_t id, const std::string& word) {
    if (word == kindOf(g, id)) return true;
    for (const SensorObject& s : g.sensors)
        if (s.id == id) return word == sensorKindKey(s.kind);
    for (const Entity& e : g.entities)
        if (e.id == id) return word == "body" && resolveInstance(g, e).rigid.enabled;
    return false;
}

// Empty word: every object. A word no object can be: false, and the words there are.
// An object's tags as an answer holds them (empty: no tags, or no such object).
Value tagsValue(const SceneGraph& g, uint32_t id) {
    Value list = Value::list();
    if (const std::vector<std::string>* tags = objectTags(g, id))
        for (const std::string& t : *tags) list.push(t);
    return list;
}

bool objectList(const SceneGraph& g, const std::string& word, const std::string& tag, Value& out, std::string& error) {
    out = Value::list();
    bool known = word.empty() || std::any_of(std::begin(kListKinds), std::end(kListKinds), [&](const char* k) { return word == k; });
    for (int k = 0; k <= int(kLastSensorKind) && !known; ++k) known = word == sensorKindKey(SensorKind(k));
    if (!known) {
        error = "kind '" + word + "' is not one of:";
        for (const char* k : kListKinds) error += std::string(" ") + k;
        for (int k = 0; k <= int(kLastSensorKind); ++k) error += std::string(" ") + sensorKindKey(SensorKind(k));
        return false;
    }
    auto add = [&](const auto& objects) {
        for (const auto& o : objects) {
            if (!word.empty() && !isOfKind(g, o.id, word)) continue;
            if (!tag.empty() && std::find(o.tags.begin(), o.tags.end(), tag) == o.tags.end()) continue;
            Value row = Value::map();
            row.set("id", o.id), row.set("kind", kindOf(g, o.id)), row.set("name", o.name);
            if (!o.tags.empty()) row.set("tags", tagsValue(g, o.id));
            out.push(row);
        }
    };
    eachObjectList(g, add);
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

// The scene's links as one list, both ways (docs/contracts/names.md, the abstract layer): each a kind - the part and the
// key of the scene file that write it -, from the object that holds it, to the object it names (0: the world). Read off
// the graph itself: a link a new kind of object holds is added here, and the list knows it.
const char* const kRelationKinds[] = {"object.parent", "object.instance", "goal.effector", "target.camera", "target.object", "pattern.template",
                                      "link.a", "link.b", "bridge.joint", "bridge.battery", "encoder.against", "action.target", "track.object"};

bool relations(const SceneGraph& g, const Value& a, Value& out, std::string& e) {
    const std::string kind = a["kind"].text("");
    if (!kind.empty() && std::find_if(std::begin(kRelationKinds), std::end(kRelationKinds), [&](const char* k) { return kind == k; }) == std::end(kRelationKinds)) {
        e = "no relation kind '" + kind + "'; the kinds:";
        for (const char* k : kRelationKinds) e += std::string(" ") + k;
        return false;
    }
    out = Value::list();
    const auto add = [&](const char* k, uint32_t from, uint32_t to, const std::string& action = {}) {
        if ((a.has("from") && from != uint32_t(a["from"].integer())) || (a.has("to") && to != uint32_t(a["to"].integer())) || (!kind.empty() && kind != k)) return;
        Value row = Value::map();
        row.set("kind", k), row.set("from", from), row.set("to", to);
        if (!action.empty()) row.set("action", action);
        out.push(row);
    };
    eachObjectList(g, [&](const auto& list) {
        for (const auto& o : list)
            if constexpr (std::is_base_of_v<SceneObject, std::decay_t<decltype(o)>>)
                if (o.parent) add("object.parent", o.id, o.parent);
    });
    for (const Entity& x : g.entities) {
        if (x.instanceOf) add("object.instance", x.id, x.instanceOf);
        if (x.goalOf) add("goal.effector", x.id, x.goalOf);
        if (x.targetOf) add("target.camera", x.id, x.targetOf);
    }
    for (const Camera& c : g.cameras)
        if (c.target) add("target.object", c.id, c.target);
    for (const ArrayObject& r : g.arrays)
        if (r.templateId) add("pattern.template", r.id, r.templateId);
    for (const JointObject& j : g.joints) add("link.a", j.id, j.a), add("link.b", j.id, j.b); // b 0: the world
    for (const DriverObject& d : g.drivers) {
        if (d.joint) add("bridge.joint", d.id, d.joint);
        if (d.battery) add("bridge.battery", d.id, d.battery);
    }
    for (const SensorObject& s : g.sensors)
        if (s.against) add("encoder.against", s.id, s.against);
    for (const ControllerObject& c : g.controllers)
        for (const ControllerAction& x : c.actions)
            if (x.target) add("action.target", c.id, x.target, x.name);
    for (const SequenceObject& q : g.sequences)
        for (const SequenceTrack& t : q.tracks) add("track.object", q.id, t.object);
    return true;
}

// The format's tables as an answer (sceneSchema): a kind's lines, each with its lead and its keys - a unit and a lead only
// where there is one.
Value schemaParts(const SchemaKind& k) {
    Value parts = Value::list();
    for (const SchemaPart& p : k.parts) {
        Value part = Value::map(), keys = Value::list();
        part.set("word", p.word);
        if (!p.lead.empty()) part.set("lead", p.lead);
        for (const SchemaKey& key : p.keys) {
            Value row = Value::map();
            row.set("word", key.word), row.set("form", key.form);
            if (!key.unit.empty()) row.set("unit", key.unit);
            keys.push(row);
        }
        part.set("keys", keys);
        parts.push(part);
    }
    return parts;
}

std::vector<SceneOp> readOps() {
    return {
        query("scene_list", "The objects of the scene: id, kind, name. With kind - only those: a row's kind (entity, group, array, light, "
              "camera, joint, battery, driver, sensor, force, wheel, controller, sequence), a sensor's own (encoder, imu, sonar, touch, wrench, effort, "
              "stereo), or body - an entity with the rigid role. With tag - only those marked with it (scene_tag); a row says its "
              "tags when it has any. What a plugin's field of a scene object chooses from",
              "{kind?: str, tag?: str}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
            Value objects;
            if (!objectList(g, a["kind"].text(""), a["tag"].text(""), objects, e)) return false;
            return r.set("objects", objects), true;
        }),
        query("scene_get", "One object's block of the scene file: text, and the same read - kind, name, tags, lines (each line as its words, a "
              "number as a number); the whole file without an id, and folder - where its relative file paths (models, textures) are found "
              "(\"\": the program's working folder). With line: values - the count words (1) after key on the object's "
              "line that starts with the words of line, as scene_set names them (no key: right after the line's own; null: not there)",
              "{id?: int, line?: str, key?: str, count?: int}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
            if (!a.has("id")) return a.has("line") ? (e = "a line is an object's: give its id", false) : (r.set("text", g.save()), r.set("folder", g.baseDirectory), true);
            const std::string block = getObject(g, uint32_t(a["id"].integer()));
            if (block.empty()) return e = "no object " + std::to_string(a["id"].integer()), false;
            if (a.has("line")) return r.set("values", fieldValues(block, a)), true;
            const std::vector<std::vector<BlockWord>> lines = blockLines(block);
            Value all = Value::list();
            for (const std::vector<BlockWord>& words : lines) all.push(wordValues(words));
            r.set("text", block), r.set("kind", lines[0][0].text), r.set("name", lines[0].size() > 1 ? lines[0][1].text : "");
            r.set("tags", tagsValue(g, uint32_t(a["id"].integer())));
            return r.set("lines", all), true;
        }),
        query("schema_list", "The kinds of block the scene file holds - world, physics, entity, group, array, light, force, camera, joint, battery, "
              "driver, sensor, wheel, controller, sequence - each with the first words of its lines; schema_get tells one's keys",
              "{}", [](const SceneGraph&, const Value&, Value& r, std::string&) {
            Value kinds = Value::list();
            for (const SchemaKind& k : sceneSchema()) {
                Value row = Value::map(), parts = Value::list();
                for (const SchemaPart& p : k.parts) parts.push(p.word);
                row.set("kind", k.kind), row.set("parts", parts);
                kinds.push(row);
            }
            return r.set("kinds", kinds), true;
        }),
        query("schema_get", "One kind of block of the scene file as the reader reads it: its lines (parts) - each its first word, what stands "
              "between it and its keys (lead), its keys with the form of their values and their unit. What scene_get and scene_set read and write "
              "by line and key; the very tables the scene file is read by",
              "{kind: str}", [](const SceneGraph&, const Value& a, Value& r, std::string& e) {
            const std::string kind = a["kind"].text("");
            for (const SchemaKind& k : sceneSchema())
                if (k.kind == kind) return r.set("kind", k.kind), r.set("parts", schemaParts(k)), true;
            e = "no kind '" + kind + "'; the kinds:";
            for (const SchemaKind& k : sceneSchema()) e += " " + k.kind;
            return false;
        }),
        query("relation_list", "The scene's links as one list, both ways: each {kind, from, to} - kind is the part and the key of the scene "
              "file that write it (object.parent, object.instance, goal.effector, target.camera, target.object, pattern.template, link.a, "
              "link.b, bridge.joint, bridge.battery, encoder.against, action.target - with its action -, track.object), from the object that "
              "holds it, to the object it names (0: the world). from, to, kind: only those - to gives what refers to an object",
              "{from?: int, to?: int, kind?: str}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
            Value list;
            return relations(g, a, list, e) && (r.set("relations", list), true);
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

// An argument of a batch's step with what earlier steps answered in place of "$n" (the id step n made - from 0) or
// "$name" (the step named so by its `as`), "$n.key" / "$name.key" (that key of its answer), in lists and maps too; a
// reference to no such step or key: `e`.
Value referred(const Value& v, const Value& results, const std::map<std::string, size_t>& named, std::string& e) {
    if (v.type() == Value::Type::Text && v.text().size() > 1 && v.text()[0] == '$') {
        const std::string t = v.text().substr(1);
        const size_t dot = t.find('.');
        const std::string n = t.substr(0, dot), key = dot == std::string::npos ? "id" : t.substr(dot + 1);
        const bool whole = !n.empty() && n.size() < 6 && std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; });
        const auto name = named.find(n);
        const size_t step = whole ? size_t(std::stoi(n)) : name != named.end() ? name->second : results.items().size();
        if (step < results.items().size() && results.items()[step].has(key)) return results.items()[step][key];
        e = "'" + v.text() + "': no earlier step " + n + " answered " + key + " (a step's number from 0, or the name its 'as' gave it)";
        return Value();
    }
    if (v.type() == Value::Type::List) {
        Value out = Value::list();
        for (const Value& x : v.items()) out.push(referred(x, results, named, e));
        return out;
    }
    if (v.type() == Value::Type::Map) {
        Value out = Value::map();
        for (const std::string& k : v.keys()) out.set(k, referred(v[k], results, named, e));
        return out;
    }
    return v;
}

// The edits a batch may hold: every edit of the list but the batch itself.
bool batch(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    Value results = Value::list();
    std::map<std::string, size_t> named; // the steps' names (as) -> their places
    for (const Value& step : a["ops"].items()) {
        const std::string name = step["op"].text("");
        const SceneOp* op = findSceneOp(name);
        if (!op || !op->edit || name == "scene_batch") return e = "'" + name + "' is no edit a batch can hold", false;
        Value answer = Value::map();
        std::string unresolved; // its own: the caller's error may hold words of before
        const Value args = referred(step["args"], results, named, unresolved);
        if (!unresolved.empty()) return e = name + ": " + unresolved, false;
        if (!op->edit(g, args, answer, e)) return e = name + ": " + e, false;
        if (const std::string as = step["as"].text(""); !as.empty()) {
            if (named.count(as) || std::all_of(as.begin(), as.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return e = "as '" + as + "': a name for one step, not a number", false;
            named[as] = results.items().size();
        }
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
    ops.push_back(edit("world_set", "The world's 'world' and 'physics' lines, as world_get gives them (gravity, box, the play range's end 'duration' in s - 0: none -, the take's start 'record_from' in s, before it, physics engine; a missing physics "
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
    ops.push_back(edit("scene_batch", "Several edits as one undo step, all or none. A step named by as - {as: \"chassis\", op, args} - is "
                       "referred to as \"$chassis\" (the id it made) or \"$chassis.key\" (that key of its answer); \"$n\" is step n from 0. So "
                       "one batch builds a whole robot: shapes, then the joints, drives and sensors between them, e.g. [{as: body, op: "
                       "scene_add, args: {shape: box}}, {as: wheel, op: scene_add, args: {shape: cylinder}}, {op: assembly_joint, args: {a: "
                       "\"$wheel\", b: \"$body\", type: hinge}}]", "{ops: [{op: str, args: {}, as?: str}]}", batch));
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
