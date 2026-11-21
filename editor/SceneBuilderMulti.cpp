// Several objects selected in the scene builder (see SceneBuilder.h): the inspector shows the active
// object, "—" in every field whose value differs between the selected (a checkbox half-checked), and
// the component cards of what all of them have. An edit is what differs from what the widgets showed
// when they were filled: only the field that was touched goes to every selected object, the others
// keep their own values. The shape and the components of an instance are its master's, so an edit
// there reaches the master and all its instances.
#include "SceneBuilder.h"
#include "TexturePanel.h"

#include "RoleBar.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>

#include <algorithm>

using namespace ke;

namespace {

// Copies what changed between `before` and `after` into `target`: the one field that was edited.
void take(Real before, Real after, Real& target) {
    if (after != before) target = after;
}
void take(bool before, bool after, bool& target) {
    if (after != before) target = after;
}
void take(const Vector3& before, const Vector3& after, Vector3& target) {
    for (int k = 0; k < 3; ++k) take(before[k], after[k], target[k]);
}
void take(const std::string& before, const std::string& after, std::string& target) {
    if (after != before) target = after;
}

void takeObject(const SceneObject& before, const SceneObject& after, SceneObject& target) {
    take(before.name, after.name, target.name);
    take(before.visible, after.visible, target.visible);
    take(before.locked, after.locked, target.locked);
    take(before.color, after.color, target.color);
    take(before.position, after.position, target.position);
    take(before.rotationDeg, after.rotationDeg, target.rotationDeg);
    if (after.tags != before.tags) target.tags = after.tags;
}

// The geometry and its look (the components' numbers are their cards', which write the one field edited themselves).
void takeShape(const Entity& before, const Entity& after, Entity& t) {
    if (after.shape != before.shape) t.shape = after.shape;
    if (before.texture.kind != after.texture.kind) t.texture.kind = after.texture.kind;
    if (before.texture.file != after.texture.file) t.texture.file = after.texture.file;
    take(before.texture.repeats, after.texture.repeats, t.texture.repeats);
    take(before.texture.bumpHeight, after.texture.bumpHeight, t.texture.bumpHeight);
    if (before.texture.heightFile != after.texture.heightFile) t.texture.heightFile = after.texture.heightFile;
    take(before.size, after.size, t.size);
    take(before.opacity, after.opacity, t.opacity);
}

// "—" in a number of the three when the selected differ in it.
void markVector(Vec3Row* r, const std::vector<Entity>& all, Vector3 (*get)(const Entity&)) {
    for (int k = 0; k < 3; ++k)
        for (const Entity& e : all)
            if (get(e)[k] != get(all.front())[k]) {
                showMixed(r->spin(k));
                break;
            }
}
} // namespace

std::vector<Entity*> SceneBuilder::selectedEntities() {
    std::vector<Entity*> out;
    for (uint32_t id : selection_)
        if (Entity* e = entityById(id)) out.push_back(e);
    return out;
}

std::vector<RoleIcon> SceneBuilder::commonRoles() const {
    std::vector<RoleIcon> roles;
    std::vector<Entity> shapes;
    for (uint32_t id : selection_)
        if (indexOf(id) >= 0) shapes.push_back(resolveInstance(graph_, graph_.entities[size_t(indexOf(id))]));
    if (shapes.empty() || shapes.size() != selection_.size()) return roles;
    for (int k = 0; k < int(RoleIcon::Count); ++k)
        if (std::all_of(shapes.begin(), shapes.end(), [k](const Entity& e) { return roleEnabled(e, RoleIcon(k)); })) roles.push_back(RoleIcon(k));
    return roles;
}

Entity SceneBuilder::readWidgets() const {
    Entity e = shown_; // the id and whatever no widget shows
    object_->writeTo(e);
    readDetails(e);
    texture_->writeTo(e.texture);
    return e;
}

// Called after the widgets were filled from the active object.
void SceneBuilder::markMixed() {
    std::vector<const SceneObject*> objects;
    for (uint32_t id : selection_)
        if (const SceneObject* o = findObject(graph_, id)) objects.push_back(o); // a joint or a battery among them has no place of its own
    if (objects.empty()) return;
    int position = 0, rotation = 0;
    bool name = false;
    for (const SceneObject* o : objects) {
        name = name || o->name != objects.front()->name;
        for (int k = 0; k < 3; ++k) {
            position |= o->position[k] != objects.front()->position[k] ? 1 << k : 0;
            rotation |= o->rotationDeg[k] != objects.front()->rotationDeg[k] ? 1 << k : 0;
        }
    }
    object_->showMixed(name, position, rotation);
    std::vector<Entity> all;
    for (uint32_t id : selection_)
        if (const Entity* e = entityById(id)) all.push_back(resolveInstance(graph_, *e));
    if (all.size() != selection_.size()) return; // not all shapes: only the object's part is shown
    if (std::any_of(all.begin(), all.end(), [&](const Entity& e) { return e.shape != all.front().shape; })) shape_->setCurrentIndex(-1);
    markVector(size_, all, [](const Entity& e) { return e.size; });
}

// A widget changed: what differs from what was shown goes to every selected object - its own part
// (name, pose, eye, lock) to the object, the shape's part to the shape's master.
void SceneBuilder::applyWidgetEdit(bool details) {
    (void)details; // the object card and the details are compared alike
    if (filling_ || selection_.empty()) return;
    const Entity after = readWidgets();
    remember(true);
    for (uint32_t id : selection_) {
        SceneObject* o = objectById(id);
        if (!o) continue;
        takeObject(shown_, after, *o);
        if (entityById(id)) takeShape(shown_, after, *entityById(masterOf(id)));
    }
    shown_ = after;
    refreshList();
    if (selection_.size() > 1) fillInspector(); // a field all now share shows its number again
    else refreshHeader();
    applyEdit(selectedId_);
}
