// The scene as a document (Document.h): the history, the ids, the transaction, the new scene, the floor, and the answer
// of an undo.
#include "scene/Document.h"

#include "scene/Assembly.h"
#include "scene/SceneEdits.h"

#include <algorithm>
#include <set>

namespace ke {

void History::push(const SceneGraph& before) {
    past_.push_back(before);
    if (past_.size() > 100) past_.erase(past_.begin()), ++forgotten_;
    redo_.clear();
}

bool History::undo(SceneGraph& current) {
    if (past_.empty()) return false;
    redo_.push_back(std::move(current));
    current = std::move(past_.back());
    past_.pop_back();
    return true;
}

bool History::redo(SceneGraph& current) {
    if (redo_.empty()) return false;
    past_.push_back(std::move(current));
    current = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

SceneGraph History::pop() {
    if (past_.empty()) return SceneGraph();
    SceneGraph last = std::move(past_.back());
    past_.pop_back();
    return last;
}

void History::cut(size_t keep) {
    past_.resize(std::min(past_.size(), keep));
    redo_.clear();
}

SceneGraph floorScene(uint32_t floorId) {
    SceneGraph g;
    Entity floor;
    floor.id = floorId;
    floor.name = "Пол";
    floor.shape = ShapeKind::Plane;
    floor.size = Vector3(g.world.size.x, 0.02_r, g.world.size.z);
    floor.position = Vector3(0.0_r, 0.01_r, 0.0_r);
    floor.color = Vector3(0.52_r, 0.55_r, 0.60_r);
    floor.collider.enabled = true;
    floor.locked = true;
    g.entities.push_back(floor);
    return g;
}

Value historyAnswer(const SceneGraph& before, const SceneGraph& after, bool undo, bool done, size_t steps) {
    std::vector<uint32_t> ids;
    const auto note = [&](uint32_t id) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    };
    bool world = false;
    for (const GraphEdit& e : diffEdits(before, after)) {
        if (e.kind == GraphEdit::Kind::World) world = true;
        else note(e.id);
    }
    for (const SceneGraph* g : {&before, &after})
        for (const JointObject& j : g->joints)
            if (!findJoint(before, j.id) != !findJoint(after, j.id)) note(j.id);
    Value changed = Value::list();
    for (const uint32_t id : ids) changed.push(id);
    Value answer = Value::map();
    answer.set(undo ? "undone" : "redone", done);
    answer.set("changed", changed);
    answer.set("world", world);
    answer.set("undo_steps", int64_t(steps));
    return answer;
}

// Files written by hand or by an older editor may have no ids, or the same id twice.
void Document::adoptIds() {
    next_ = std::max(next_, nextId(graph)); // groups and arrays too
    std::set<uint32_t> seen;
    for (Entity& e : graph.entities) {
        if (e.id == 0 || seen.count(e.id)) e.id = newId();
        seen.insert(e.id);
    }
}

bool Document::take(const Remember& remember) {
    if (remember) return remember();
    history.push(graph);
    return true;
}

bool Document::transact(const std::function<bool(SceneGraph&, std::string&)>& change, std::string& error, const Remember& remember) {
    SceneGraph before = graph;
    const bool taken = take(remember);
    graph.idFloor = next_; // the core's new ids never take one this session gave out before
    if (!change(graph, error)) {
        graph = std::move(before);
        if (taken) history.pop();
        return false;
    }
    adoptIds();
    return true;
}

void Document::newScene(const Remember& remember) {
    if (!graph.entities.empty() || !graph.groups.empty() || !graph.arrays.empty() || !graph.lights.empty() || !graph.cameras.empty()) take(remember);
    graph = floorScene(newId());
}

void Document::load(SceneGraph g, const Remember& remember) {
    take(remember);
    graph = std::move(g);
    adoptIds();
}

} // namespace ke
