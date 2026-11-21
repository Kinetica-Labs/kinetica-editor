// The scene builder under the API (EditorOps): an edit made of the core's edits (scene/SceneEdits.h) as one undo
// step, and the shape under the mouse for the magnet.
#include "SceneBuilder.h"

#include <algorithm>

using namespace ke;

bool SceneBuilder::transact(const std::function<bool(SceneGraph&, std::string&)>& change, std::string& error) {
    if (!doc_.transact(change, error, rememberer())) {
        if (rememberedNow_ && undoGroup_ > 0) undoGroupTaken_ = false; // a group's first edit failed: the next one takes the step
        updateHistoryActions();
        return false;
    }
    pruneSelection();
    refreshList();
    fillInspector();
    applyEdit(0);
    return true;
}

bool SceneBuilder::pickShape(const Ray& ray, uint32_t& id, Vector3& point, Vector3* normal) {
    if (!editing()) return false;
    const std::vector<PickHit> hits = editView_.pickAll(graph_, ray);
    if (hits.empty()) return false;
    const PickHit& nearest = *std::min_element(hits.begin(), hits.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    id = nearest.id;
    point = nearest.point;
    if (normal) *normal = nearest.normal;
    return true;
}

bool SceneBuilder::loadPaused() {
    if (!editing()) return true;
    if (!loadIntoSimulation()) return false;
    setMode(Mode::Paused);
    return true;
}
