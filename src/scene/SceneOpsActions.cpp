// An editor's actions as operations of the core (SceneOps.h, the work itself in SceneActions.h): add a
// shape, a light, a camera; delete, duplicate, group, ungroup, make an array, show everything; give or
// take a role; move, turn and scale. Each answers what a window selects after it - the new shape, the
// copies, the group - so a client with a window shows the same as after the button.
#include "scene/SceneOps.h"

#include "core/Format.h"
#include "scene/Collider.h"
#include "scene/Kinematics.h"
#include "scene/SceneActions.h"

#include <algorithm>

namespace ke {

namespace {

const char* kShapes[] = {"box", "sphere", "cylinder", "cone", "plane"};
const char* kLights[] = {"sun", "point", "spot"};

template <size_t N>
int wordIndex(const std::string& w, const char* (&words)[N]) {
    for (size_t k = 0; k < N; ++k)
        if (w == words[k]) return int(k);
    return -1;
}

bool device(const SceneGraph& g, uint32_t id) {
    return std::any_of(g.batteries.begin(), g.batteries.end(), [id](const BatteryObject& b) { return b.id == id; }) ||
           std::any_of(g.drivers.begin(), g.drivers.end(), [id](const DriverObject& d) { return d.id == id; }) ||
           std::any_of(g.controllers.begin(), g.controllers.end(), [id](const ControllerObject& c) { return c.id == id; });
}

// The objects named, all of them known: why not ("" - they are).
std::string unknown(const SceneGraph& g, const std::vector<uint32_t>& ids) {
    for (uint32_t id : ids)
        if (!findObject(g, id) && !device(g, id)) return "no object " + std::to_string(id);
    return ids.empty() ? "no objects to act on: pass ids" : "";
}

// An optional [x, y, z]: absent, or three numbers (a malformed one is refused, not ignored).
bool optionalVector(const Value& a, const char* key, Vector3& out, bool& given, std::string& e) {
    given = vectorArg(a, key, out);
    if (!given && a.has(key)) return e = std::string(key) + " is [x, y, z]", false;
    return true;
}

Value answerOf(const std::vector<uint32_t>& ids) { return selected(ids, ids.empty() ? 0 : ids.back()); }

void merge(Value& into, const Value& from) {
    for (size_t k = 0; k < from.keys().size(); ++k) into.set(from.keys()[k], from.at(k));
}

bool addShapeOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const int shape = wordIndex(a["shape"].text("box"), kShapes);
    if (shape < 0) return e = "shape is box, sphere, cylinder, cone or plane", false;
    Vector3 position, size;
    bool at = false, sized = false;
    if (!optionalVector(a, "position", position, at, e) || !optionalVector(a, "size", size, sized, e)) return false;
    if (sized && !(size.x > 0 && size.y > 0 && size.z > 0)) return e = "size is [x, y, z], each above 0 m", false;
    const uint32_t id = addShape(g, ShapeKind(shape));
    Entity& x = *std::find_if(g.entities.begin(), g.entities.end(), [id](const Entity& o) { return o.id == id; });
    if (sized && !at) x.position.y += 0.5_r * (size.y - x.size.y); // still standing as high over the floor
    if (sized) x.size = size;
    if (at) x.position = position;
    if (a.has("name")) x.name = a["name"].text("");
    Role role = Role::Rigid;
    const bool fixed = a["fixed"].truth(false), kinematic = a["kinematic"].truth(false);
    if (fixed && kinematic) return e = "a body is fixed or kinematic, not both", false;
    const bool roled = a.has("role") || fixed || kinematic; // a fixed or a kinematic shape is a rigid one
    if (roled && !roleOfWord(a["role"].text("rigid"), role)) return e = "role is rigid or collider", false;
    const std::string note = roled ? setRoles(g, {id}, role, true) : std::string();
    for (Entity& made : g.entities)
        if (made.id == id) made.rigid.fixed = fixed, made.rigid.kinematic = kinematic;
    merge(r, answerOf({id}));
    if (!note.empty()) r.set("note", note);
    return true;
}

bool addLightOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const int kind = wordIndex(a["kind"].text("sun"), kLights);
    if (kind < 0) return e = "kind is sun, point or spot", false;
    const AABB box = sceneBounds(g);
    Vector3 at = box.valid() ? box.center() : Vector3(0.0_r), given;
    bool there = false;
    if (!optionalVector(a, "at", given, there, e)) return false;
    if (there) at = given;
    Vector3 position, target = at;
    bool placed = false, aimed = false;
    if (!optionalVector(a, "position", position, placed, e) || !optionalVector(a, "target", target, aimed, e)) return false;
    if (!aimed) target = at;
    return merge(r, answerOf({placed ? placeLight(g, LightKind(kind), position, target) : addLight(g, LightKind(kind), at)})), true;
}

bool addCameraOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const AABB box = sceneBounds(g);
    const Vector3 centre = box.valid() ? box.center() : Vector3(0.0_r, 0.5_r, 0.0_r);
    Vector3 eye = centre + Vector3(2.5_r, 1.5_r, 2.5_r), given;
    bool there = false;
    if (!optionalVector(a, "eye", given, there, e)) return false;
    if (there) eye = given;
    Vector3 target = centre;
    bool aimed = false;
    if (!optionalVector(a, "target", target, aimed, e)) return false;
    if (length(eye - target) < 0.05_r) eye = target + Vector3(0.0_r, 0.5_r, 2.0_r); // an eye at its target looks nowhere
    const std::string kind = a["kind"].text("free");
    if (kind != "free" && kind != "target") return e = "kind is free or target", false;
    const uint32_t at = uint32_t(a["at"].integer(0));
    if (at && !findObject(g, at)) return e = "no object " + std::to_string(at) + " to look at", false;
    if (const SceneObject* o = at ? findObject(g, at) : nullptr) {
        Quaternion turn;
        worldPose(g, *o, target, turn);
    }
    const uint32_t id = addCamera(g, eye, target);
    if (kind == "target" || at) aimCamera(g, id, at, &target);
    return merge(r, answerOf({id})), true;
}

// camera_aim: a target camera at an object or a point, or a free camera again.
bool aimCameraOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const uint32_t camera = uint32_t(a["camera"].integer(0));
    if (std::none_of(g.cameras.begin(), g.cameras.end(), [camera](const Camera& c) { return c.id == camera; }))
        return e = "no camera " + std::to_string(camera), false;
    if (a["free"].truth(false)) return freeCamera(g, camera), r.set("id", camera), true;
    Vector3 point;
    bool given = false;
    if (!optionalVector(a, "point", point, given, e)) return false;
    const uint32_t at = uint32_t(a["at"].integer(0));
    if (!aimCamera(g, camera, at, given ? &point : nullptr)) return e = "no object " + std::to_string(at) + " to look at", false;
    return r.set("id", camera), true;
}

// Delete, duplicate, group, ungroup, make an array: on the topmost of `ids` (a member goes with its group).
SceneOp treeOp(const char* name, const char* help, const char* args = "{ids}") {
    return {name, help, args, {}, [op = std::string(name)](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        const std::vector<uint32_t> named = idsArg(a), roots = topmost(g, named);
        if (!(e = unknown(g, named)).empty()) return false;
        Vector3 step;
        bool stepped = false;
        const int count = a["count"].integer(5);
        if (!optionalVector(a, "step", step, stepped, e)) return false;
        if (count < 1 || count > 1000) return e = "count is 1..1000 copies", false;
        std::vector<uint32_t> made;
        if (op == "scene_delete") {
            std::vector<uint32_t> gone = roots;
            for (uint32_t id : named) if (device(g, id)) gone.push_back(id); // a battery or a driver: no root
            deleteObjects(g, gone);
        } else if (op == "scene_ungroup") {
            made = ungroupObjects(g, named);
            if (made.empty()) return e = "no group among the ids", false;
            return merge(r, selected(made, made.front())), true; // the window makes the first member active
        } else if (roots.empty()) {
            return e = "nothing to act on (locked?)", false;
        } else {
            made = op == "scene_duplicate" ? duplicateObjects(g, roots) : op == "scene_group" ? std::vector<uint32_t>{groupObjects(g, roots)}
                                                                                               : arrayObjects(g, roots);
            if (made.empty()) return e = "no shape to make an array of", false;
            if (op == "scene_array")
                for (ArrayObject& row : g.arrays)
                    if (std::find(made.begin(), made.end(), row.id) != made.end()) row.count[0] = count, row.step = stepped ? step : row.step;
        }
        return merge(r, answerOf(made)), true;
    }, true};
}

// A tag put on the objects or taken off them - of any kind: a shape, a joint, a sensor, a battery, a sequence.
bool tagOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const std::vector<uint32_t> ids = idsArg(a);
    const std::string word = a["tag"].text("");
    if (!tagWord(word)) return e = "a tag is one word of up to 48 bytes, no spaces or quotes", false;
    if (ids.empty()) return e = "no objects to tag: pass ids", false;
    std::vector<std::vector<std::string>*> all;
    for (uint32_t id : ids) {
        all.push_back(objectTags(g, id));
        if (!all.back()) return e = "no object " + std::to_string(id), false;
    }
    int changed = 0;
    for (std::vector<std::string>* tags : all) {
        const auto at = std::find(tags->begin(), tags->end(), word);
        const bool on = a.has("on") ? a["on"].truth() : at == tags->end();
        if (on && at == tags->end()) tags->push_back(word), ++changed;
        if (!on && at != tags->end()) tags->erase(at), ++changed;
    }
    return r.set("changed", changed), true; // not the ids: a tag is no reason to change what is selected
}

bool roleOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    Role role;
    const std::vector<uint32_t> named = idsArg(a);
    if (!(e = unknown(g, named)).empty()) return false;
    if (!roleOfWord(a["role"].text(""), role)) return e = "role is rigid or collider", false;
    std::vector<uint32_t> entities;
    for (uint32_t id : named)
        if (std::any_of(g.entities.begin(), g.entities.end(), [id](const Entity& x) { return x.id == id; })) entities.push_back(id);
    if (entities.empty()) return e = "no shape among the ids", false;
    bool all = true; // no `on`: switched as a click - added unless all have it already
    for (uint32_t id : entities)
        for (const Entity& x : g.entities) all = all && (x.id != masterOf(g, id) || hasRole(x, role));
    const bool on = a.has("on") ? a["on"].truth() : !all;
    // An IK goal is intent, not matter (the observability contract, CLAUDE.md): a body or a collider would fall and push the arm.
    for (uint32_t id : entities)
        for (const Entity& x : g.entities)
            if (on && x.id == id && isHandle(x)) return e = "«" + x.name + "» - цель: намерение, а не тело, ролей у неё нет", false;
    const std::string note = setRoles(g, entities, role, on);
    merge(r, answerOf(named));
    if (!note.empty()) r.set("note", note);
    return true;
}

// The collider by numbers (scene/Collider.h): the kind, a box's edges or a round one's diameter and height, its centre and
// turn on the object. What is not given stays as the collider is built now - a fitted one's numbers taken from the
// geometry - so one number changes and the rest do not jump; the fit to the geometry is off after it. Each kind takes
// its own numbers: a sphere's three equal edges were three fields of which one worked (a newcomer's question).
bool colliderOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    static const char* kinds[] = {"box", "sphere", "capsule", "cylinder"};
    static const ColliderKind kindOf[] = {ColliderKind::Box, ColliderKind::Sphere, ColliderKind::Capsule, ColliderKind::Cylinder};
    const uint32_t id = masterOf(g, uint32_t(a["id"].integer())); // an instance collides as its master
    Entity* body = nullptr;
    for (Entity& x : g.entities) body = x.id == id ? &x : body;
    if (!body) return e = "no shape " + std::to_string(a["id"].integer()), false;
    if (isHandle(*body)) return e = "«" + body->name + "» - цель: намерение, а не тело, коллайдера у неё нет", false;
    ColliderRole c = heldCollider(*body, entityLocalMesh(*body, g.baseDirectory));
    if (a.has("kind")) {
        const int k = wordIndex(a["kind"].text(""), kinds);
        if (k < 0) return e = "kind is box, sphere, capsule or cylinder (a hull follows the geometry: scene_set collider kind hull)", false;
        c.size = kindForm(kindOf[k], c.size), c.kind = kindOf[k];
    }
    if (!sizedByNumbers(c.kind)) return e = "the collider is the geometry's hull: give kind box, sphere, capsule or cylinder to size it", false;
    const std::string word = kinds[std::find(kindOf, kindOf + 4, c.kind) - kindOf];
    const bool round = c.kind != ColliderKind::Box, tall = c.kind == ColliderKind::Capsule || c.kind == ColliderKind::Cylinder;
    if (a.has("size") && round) return e = std::string("a ") + word + " is sized by diameter" + (tall ? " and height" : "") + ", not size", false;
    if (a.has("diameter") && !round) return e = "a box is sized by size [x, y, z], not diameter", false;
    if (a.has("height") && !tall) return e = std::string("a ") + word + " has no height" + (round ? ": diameter" : ": size [x, y, z]"), false;
    Vector3 v;
    if (a.has("size") && !vectorArg(a, "size", v)) return e = "size is [x, y, z], metres", false;
    if (a.has("size")) c.size = v;
    if (a.has("diameter")) c.size.x = c.size.z = Real(a["diameter"].number()), c.size.y = tall ? c.size.y : c.size.x;
    if (a.has("height")) c.size.y = Real(a["height"].number());
    if (!(minComp(c.size) >= 0.001_r)) return e = "every size is at least 0.001 m", false;
    if (a.has("offset") && !vectorArg(a, "offset", c.offset)) return e = "offset is [x, y, z], metres in the object's frame", false;
    if (a.has("rotation") && !vectorArg(a, "rotation", c.rotationDeg)) return e = "rotation is [x, y, z], degrees in the object's frame", false;
    c.enabled = true;
    body->collider = c;
    r.set("id", body->id), r.set("kind", word), r.set("offset", vectorValue(c.offset)), r.set("rotation", vectorValue(c.rotationDeg));
    if (round) r.set("diameter", c.size.x);
    if (tall) r.set("height", c.size.y);
    if (!round) r.set("size", vectorValue(c.size));
    return true;
}

// A plugin's components (SceneGraph.h PluginComponent): a value as one word of the scene file - a number, a vector
// "x,y,z", a flag 1/0, a word - and back. The core keeps them word for word; the plugin's schema says what they mean.
std::string componentWord(const Value& v) {
    if (v.type() == Value::Type::List) {
        std::string out;
        for (const Value& x : v.items()) out += (out.empty() ? "" : ",") + numberText(x.number());
        return out;
    }
    if (v.type() == Value::Type::Bool) return v.truth() ? "1" : "0";
    if (v.type() == Value::Type::Number) return numberText(v.number());
    return v.text("");
}

Value componentValue(const std::string& word) {
    Real x = 0;
    if (parseNumber(word, x)) return Value(double(x));
    if (word.find(',') != std::string::npos) {
        Value list = Value::list();
        size_t at = 0;
        while (at <= word.size()) {
            const size_t comma = std::min(word.find(',', at), word.size());
            Real part = 0;
            if (!parseNumber(word.substr(at, comma - at), part)) return Value(word);
            list.push(double(part));
            at = comma + 1;
        }
        return list;
    }
    return Value(word);
}

Entity* entityById(SceneGraph& g, uint32_t id) {
    for (Entity& e : g.entities)
        if (e.id == id) return &e;
    return nullptr;
}

// The components of the object an operation names: an instance's are its master's, as its roles are.
std::vector<PluginComponent>* componentsOf(SceneGraph& g, const Value& a, uint32_t& id, std::string& e) {
    id = uint32_t(a["id"].integer());
    if (entityById(g, id)) id = masterOf(g, id);
    std::vector<PluginComponent>* found = objectComponents(g, id);
    if (!found) e = "no object " + std::to_string(a["id"].integer());
    return found;
}

bool componentSetOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    uint32_t id = 0;
    std::vector<PluginComponent>* components = componentsOf(g, a, id, e);
    const std::string type = a["type"].text("");
    if (!components) return false;
    if (type.find('.') == std::string::npos || type.find(' ') != std::string::npos)
        return e = "type is <plugin>.<component>, e.g. turbines.turbine", false;
    auto it = std::find_if(components->begin(), components->end(), [&](const PluginComponent& c) { return c.type == type; });
    if (it == components->end()) components->push_back({type, {}}), it = components->end() - 1;
    const Value& given = a["values"];
    for (size_t k = 0; k < given.keys().size(); ++k) {
        const std::string& key = given.keys()[k];
        const Value& v = given.at(k);
        if (key.empty() || key.find(' ') != std::string::npos) return e = "a value's key is one word: " + key, false;
        auto at = std::find_if(it->values.begin(), it->values.end(), [&](const auto& kv) { return kv.first == key; });
        if (at == it->values.end()) it->values.push_back({key, componentWord(v)});
        else at->second = componentWord(v);
    }
    r.set("id", id), r.set("type", type);
    return true;
}

bool componentRemoveOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    uint32_t id = 0;
    std::vector<PluginComponent>* components = componentsOf(g, a, id, e);
    const std::string type = a["type"].text("");
    if (!components) return false;
    const auto before = components->size();
    components->erase(std::remove_if(components->begin(), components->end(), [&](const PluginComponent& c) { return c.type == type; }), components->end());
    if (components->size() == before) return e = "object " + std::to_string(id) + " has no component " + type, false;
    r.set("id", id);
    return true;
}

bool componentListOp(const SceneGraph& g, const Value& a, Value& r, std::string&) {
    const std::string type = a["type"].text("");
    Value list = Value::list();
    const auto each = [&](const auto& objects) {
        for (const auto& x : objects)
            for (const PluginComponent& c : x.components) {
                if (!type.empty() && c.type != type) continue;
                Value values = Value::map();
                for (const auto& [key, word] : c.values) values.set(key, componentValue(word));
                Value row = Value::map();
                row.set("id", x.id), row.set("type", c.type), row.set("values", values);
                list.push(row);
            }
    };
    each(g.entities), each(g.groups), each(g.arrays), each(g.lights), each(g.cameras), each(g.joints), each(g.batteries), each(g.drivers);
    each(g.sensors), each(g.wheels), each(g.controllers), each(g.sequences);
    r.set("components", list);
    return true;
}

// An inverse kinematics goal among `ids` moved or turned: its arm's servos get the new coordinates (Kinematics.h).
void followGoals(SceneGraph& g, const std::vector<uint32_t>& ids) {
    for (uint32_t id : ids)
        if (std::any_of(g.entities.begin(), g.entities.end(), [id](const Entity& x) { return x.id == id && x.goalOf != 0; })) reachGoal(g, id);
}

// Move, turn, scale: from the poses before, on the topmost of `ids`.
bool transformOp(const std::string& op, SceneGraph& g, const Value& a, std::string& e) {
    const std::vector<uint32_t> ids = topmost(g, idsArg(a));
    if (!(e = unknown(g, idsArg(a))).empty()) return false;
    if (ids.empty()) return e = "nothing to act on (locked?)", false;
    const SceneGraph start = g;
    Vector3 v;
    if (op == "scene_move") return vectorArg(a, "by", v) ? (moveObjects(g, start, ids, v), followGoals(g, ids), true) : (e = "by is [x, y, z], metres", false);
    if (op == "scene_scale") {
        if (!vectorArg(a, "by", v)) v = Vector3(Real(a["by"].number(1.0)));
        return scaleObjects(g, start, ids, v), true;
    }
    const std::string name = a["axis"].text("");
    if (name == "x" || name == "y" || name == "z") v = Vector3(0.0_r), v[name[0] - 'x'] = 1.0_r;
    else if (!vectorArg(a, "axis", v) || length(v) <= 0) return e = "axis is x, y, z or [x, y, z]", false;
    Vector3 pivot;
    Quaternion q;
    if (!vectorArg(a, "pivot", pivot)) worldPose(g, *findObject(g, ids.back()), pivot, q); // the last one's centre
    turnObjects(g, start, ids, pivot, turnOf(v, Real(a["degrees"].number())));
    followGoals(g, ids);
    return true;
}

SceneOp transform(const char* name, const char* help, const char* args) {
    return {name, help, args, {}, [op = std::string(name)](SceneGraph& g, const Value& a, Value&, std::string& e) { return transformOp(op, g, a, e); }, true};
}

} // namespace

std::vector<SceneOp> actionSceneOps() {
    return {
        {"scene_add", "A shape as the toolbar's button adds it: in the air over a free spot, named after its shape; role: a moving body "
                      "(rigid, with its collider) or a static obstacle (collider), as scene_role gives it; fixed: a rigid body held still in the world; "
                      "kinematic: one that moves as its velocity and spin say and pushes what it meets, itself pushed by nothing. A cylinder's "
                      "axis is its own y",
         "{shape: box|sphere|cylinder|cone|plane, name?: str, position?: [x,y,z], size?: [x,y,z], role?: rigid|collider, fixed?: bool, kinematic?: bool}",
         {}, addShapeOp},
        {"scene_add_light", "A light as the menu adds it: the sun high at a slant, a lamp or a spot over `at` (else the scene's centre); "
                            "with `position` it stands there shining at `target` (else at `at`)",
         "{kind: sun|point|spot, at?: [x,y,z], position?: [x,y,z], target?: [x,y,z]}", {}, addLightOp},
        {"scene_add_camera", "A camera at `eye`, looking at `target` (else the scene's centre); kind target: a target camera that always "
                             "looks there - at object `at` (wherever it goes) or the point", "{eye?: [x,y,z], target?: [x,y,z], kind?: 'free'|'target', at?: int}",
         {}, addCameraOp},
        {"camera_aim", "A target camera (3ds Max's Target camera): it always looks at object `at` (wherever the running world takes it) or "
                       "at `point`, or (neither) at where it looks now; free: true - a free camera again, looking where it looked",
         "{camera: int, at?: int, point?: [x,y,z], free?: bool}", {}, aimCameraOp},
        treeOp("scene_delete", "Objects deleted as Del does: with everything under them, the joints and sensors they hold (scene_remove: one object, its children kept)"),
        treeOp("scene_duplicate", "Copies a little above the originals (Ctrl+D); the copies are the answer"),
        treeOp("scene_group", "A group at the centre of the objects, their poses kept in the world (Ctrl+G)"),
        treeOp("scene_ungroup", "The groups dissolved, their members kept where they stand (Ctrl+Shift+G)"),
        treeOp("scene_array", "Each shape becomes the hidden template of a row of copies: count of them (5), a step apart (its width and a gap, along "
               "x). The row is an object of its own - its pattern line holds the rest (a grid, a circle, a turn a copy: scene_set)",
               "{ids, count?: int, step?: [x,y,z]}"),
        {"scene_show_all", "Every hidden object shown again (Alt+H), not the templates of arrays", "{}", {},
         [](SceneGraph& g, const Value&, Value&, std::string&) { return showAll(g), true; }},
        {"scene_role", "A role given to the shapes or taken from them, as the role bar does; no `on`: switched as a click",
         "{ids, role: rigid|collider, on?: bool}", {}, roleOp, true},
        {"scene_tag", "A tag - one word, 'target', 'keep' - put on objects of any kind or taken off them; no `on`: switched. scene_list "
                      "{tag} finds by it, scene_get says an object's tags. For a plugin or a script to find what it is to act on",
         "{ids, tag: str, on?: bool}", {}, tagOp, true},
        {"component_set", "A plugin's component on an object of any kind - a shape, a joint, a light - added if it has none, with these values (a number, [x, y, z], a flag, a "
                          "word); the plugin's schema says what they mean and draws its card. One undo step",
         "{id: int, type: str, values?: {}}", {}, componentSetOp, true},
        {"component_remove", "A plugin's component taken off an object", "{id: int, type: str}", {}, componentRemoveOp, true},
        {"component_list", "Every plugin component in the scene (of one type: <plugin>.<component>): its object and values", "{type?: str}",
         componentListOp, {}, false},
        {"scene_collider", "A shape's collider by numbers, apart from what it looks like: the kind; a box's size, a sphere's diameter, a "
                           "capsule's or a cylinder's diameter and height; its centre (offset) and turn (rotation) on the shape, in the shape's "
                           "frame. What is not given stays as the collider is now (a fitted one's numbers from the geometry); fitting is off after",
         "{id: int, kind?: 'box'|'sphere'|'capsule'|'cylinder', size?: [x,y,z], diameter?: m, height?: m, offset?: [x,y,z], rotation?: [x,y,z]}",
         {}, colliderOp, true},
        transform("scene_move", "Objects moved by a step in the world, their angles kept (the gizmo's arrows)", "{ids, by: [x,y,z]}"),
        transform("scene_turn", "Objects turned about an axis by degrees around a pivot (no pivot: the last object's centre) - the rings",
                  "{ids, axis: x|y|z|[x,y,z], degrees: number, pivot?: [x,y,z]}"),
        transform("scene_scale", "Objects scaled by factors along their own axes, a group's spacing too (the cubes)", "{ids, by: [x,y,z] | number}"),
    };
}

} // namespace ke
