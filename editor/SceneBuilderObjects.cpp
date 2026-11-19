// Many objects at once in the scene builder (see SceneBuilder.h): the lookups that treat a shape, a
// group and an array alike, the hierarchy (which group a thing is in, what a click selects), groups
// (Ctrl+G / Ctrl+Shift+G), arrays ("Сделать массивом…", "Разобрать на объекты"), instances
// ("Отвязать") and the selection helpers of the menus ("Выбрать все такие же", "Выбрать все с
// компонентом", "Инвертировать выбор"). The poses are kept in the world whatever the hierarchy
// does: grouping, ungrouping or exploding an array never moves anything on the screen.
#include "SceneBuilder.h"

#include "scene/SceneActions.h"
#include "scene/Assembly.h"

#include "RoleBar.h"

#include <QString>

#include <algorithm>
#include <set>

using namespace ke;

namespace {

// The components of a shape as bits (RoleIcon order), to compare two shapes at a glance.
int roleBits(const Entity& e) {
    int bits = 0;
    for (int k = 0; k < int(RoleIcon::Count); ++k) bits |= roleEnabled(e, RoleIcon(k)) ? 1 << k : 0;
    return bits;
}

} // namespace

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
SceneObject* SceneBuilder::objectById(uint32_t id) {
    if (Entity* e = entityById(id)) return e;
    if (Group* g = groupById(id)) return g;
    if (Light* l = lightById(id)) return l;
    if (Camera* c = cameraById(id)) return c;
    if (SensorObject* s = sensorById(id)) return s;
    return arrayById(id);
}

Entity* SceneBuilder::entityById(uint32_t id) {
    const int i = indexOf(id);
    return i >= 0 ? &graph_.entities[size_t(i)] : nullptr;
}

Group* SceneBuilder::groupById(uint32_t id) {
    for (Group& g : graph_.groups)
        if (id != 0 && g.id == id) return &g;
    return nullptr;
}

ArrayObject* SceneBuilder::arrayById(uint32_t id) {
    for (ArrayObject& a : graph_.arrays)
        if (id != 0 && a.id == id) return &a;
    return nullptr;
}

SceneObject* SceneBuilder::selectedObject() { return objectById(selectedId_); }

uint32_t SceneBuilder::ownerOf(uint32_t drawnId) const { return ke::ownerOf(graph_, drawnId); }

// The hierarchy as the core sees it (ke::SceneActions): one rule for the window and every client.
std::vector<uint32_t> SceneBuilder::chainOf(uint32_t id) const { return ke::ancestry(graph_, id); }
bool SceneBuilder::isInside(uint32_t id, uint32_t ancestor) const { return ke::isInside(graph_, id, ancestor); }
std::vector<uint32_t> SceneBuilder::childrenOf(uint32_t id) const { return ke::childrenOf(graph_, id); }
// What the gizmo, Delete and Ctrl+D work on: a member of a selected group goes with its group.
std::vector<uint32_t> SceneBuilder::selectionRoots() const { return ke::topmost(graph_, selection_); }

std::vector<uint32_t> SceneBuilder::topLevelObjects() const {
    std::vector<uint32_t> ids;
    auto add = [&](const SceneObject& o) {
        if (o.visible && !o.locked && !findObject(graph_, o.parent)) ids.push_back(o.id);
    };
    for (const Entity& e : graph_.entities) add(e);
    for (const Group& g : graph_.groups) add(g);
    for (const ArrayObject& a : graph_.arrays) add(a);
    for (const Light& l : graph_.lights) add(l);
    for (const Camera& c : graph_.cameras) add(c);
    return ids;
}

bool SceneBuilder::isHiddenTemplate(const Entity& e) const {
    return !e.visible && std::any_of(graph_.arrays.begin(), graph_.arrays.end(), [&](const ArrayObject& a) { return a.templateId == e.id; });
}

uint32_t SceneBuilder::masterOf(uint32_t entityId) const { return ke::masterOf(graph_, entityId); }

QString SceneBuilder::objectTitle(uint32_t id) const {
    const SceneObject* o = findObject(graph_, id);
    if (!o) return QString();
    for (const ArrayObject& a : graph_.arrays) {
        if (a.id != id) continue;
        const SceneObject* t = findObject(graph_, a.templateId);
        const int n = a.pattern == ArrayPattern::Grid ? a.count[0] * a.count[1] * a.count[2] : a.count[0];
        return QString("Массив: %1 × %2").arg(n).arg(t ? QString::fromStdString(t->name) : QString("?"));
    }
    return QString::fromStdString(o->name);
}

// ---------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------
AABB SceneBuilder::boundsOf(uint32_t id) {
    return objectBounds(graph_, id, [this](const Entity& w) { return editView_.worldBounds(graph_, w); });
}

Vector3 SceneBuilder::worldPositionOf(uint32_t id) const {
    const SceneObject* o = findObject(graph_, id);
    Vector3 p(0.0f);
    Quaternion q;
    if (o) worldPose(graph_, *o, p, q);
    return p;
}

// The first click on a grouped thing selects its topmost group; the next click on it, the group one
// level down, and so on to the thing itself (Unity's and Blender's rule). An array is one thing.
uint32_t SceneBuilder::clickTarget(uint32_t drawnId) const {
    const std::vector<uint32_t> chain = chainOf(ownerOf(drawnId));
    if (chain.empty()) return 0;
    for (size_t k = 1; k < chain.size(); ++k)
        if (isSelected(chain[k])) return chain[k - 1];
    return isSelected(chain.front()) ? chain.front() : chain.back();
}

bool SceneBuilder::drawnSelected(uint32_t drawnId) const {
    const std::vector<uint32_t> chain = chainOf(ownerOf(drawnId));
    return std::any_of(chain.begin(), chain.end(), [this](uint32_t id) { return isSelected(id); });
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
// The group stands at the centre of what it holds; the members keep their places in the world, their
// poses now relative to the group. Members of one parent group stay inside it.
void SceneBuilder::groupSelected() {
    const std::vector<uint32_t> roots = selectionRoots();
    if (roots.empty()) return emit statusMessage("Выберите объекты, которые объединить в группу");
    const QString py = "scene.group(" + codeRefs(roots) + ")";
    flushCode();
    remember();
    const uint32_t group = coreEdit([&](SceneGraph& g) { return ke::groupObjects(g, roots); });
    refreshList();
    setSelection({group}, group);
    applyEdit(0);
    emit statusMessage("Группа: щелчок по ней выбирает всю группу, ещё щелчок — объект внутри");
    codeAction("scene_group", py, {{"ids", jsonIds(roots)}}, false);
}

void SceneBuilder::ungroupSelected() {
    std::vector<uint32_t> groups;
    for (uint32_t id : selection_)
        if (groupById(id)) groups.push_back(id);
    if (groups.empty()) return emit statusMessage("Выберите группу, чтобы разгруппировать");
    const QString py = codeCall("ungroup", groups);
    flushCode();
    remember();
    const std::vector<uint32_t> freed = ke::ungroupObjects(graph_, groups);
    refreshList();
    setSelection(freed, freed.empty() ? 0 : freed.front());
    applyEdit(0);
    codeAction("scene_ungroup", py, {{"ids", jsonIds(groups)}}, false);
}

void SceneBuilder::setGlued(uint32_t groupId, bool glued) {
    Group* g = groupById(groupId);
    if (!g || g->glued == glued) return;
    remember();
    groupById(groupId)->glued = glued;
    refreshList();
    fillInspector();
    applyEdit(0);
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------
// Each selected shape becomes the hidden template of a row of 5 copies along X, a little more than
// its width apart; the array stands where the shape stood, so copy 0 is the shape itself.
void SceneBuilder::makeArrayOfSelected() {
    std::vector<uint32_t> shapes;
    for (uint32_t id : selectionRoots())
        if (const Entity* e = entityById(id); e && !isHiddenTemplate(*e)) shapes.push_back(id);
    if (shapes.empty()) return emit statusMessage("Массив делается из формы: выберите куб, шар…");
    const QString py = "scene.make_array(" + codeRefs(shapes) + ")";
    flushCode();
    remember();
    const std::vector<uint32_t> arrays = coreEdit([&](SceneGraph& g) { return ke::arrayObjects(g, shapes); });
    refreshList();
    setSelection(arrays, arrays.back());
    applyEdit(0);
    frameObjects();
    codeAction("scene_array", py, {{"ids", jsonIds(shapes)}}, false);
}

// Every copy becomes a shape of its own where it stands. The template becomes the first copy when
// no other array uses it; else it stays the other array's hidden template.
void SceneBuilder::explodeArray(uint32_t id) {
    const ArrayObject* found = arrayById(id);
    if (!found) return;
    remember();
    const ArrayObject array = *found;
    const std::vector<Entity> copies = expandArray(graph_, array);
    const bool shared = std::count_if(graph_.arrays.begin(), graph_.arrays.end(), [&](const ArrayObject& a) { return a.templateId == array.templateId; }) > 1;
    const std::string name = entityById(array.templateId) ? entityById(array.templateId)->name : array.name;
    std::vector<uint32_t> made;
    for (size_t n = 0; n < copies.size(); ++n) {
        Entity c = copies[n]; // world pose, the template's geometry and components
        const Vector3 p = c.position;
        const Quaternion q = objectRotation(c);
        c.parent = array.parent;
        setWorldPose(graph_, c, p, q);
        if (n == 0 && !shared && entityById(array.templateId)) { // the template itself takes the first place
            Entity& t = *entityById(array.templateId);
            static_cast<SceneObject&>(t) = static_cast<const SceneObject&>(c);
            t.id = array.templateId;
            t.name = name;
            made.push_back(t.id);
            continue;
        }
        c.id = newId();
        c.name = name + " " + std::to_string(n + 1);
        graph_.entities.push_back(c);
        made.push_back(c.id);
    }
    graph_.arrays.erase(std::remove_if(graph_.arrays.begin(), graph_.arrays.end(), [id](const ArrayObject& a) { return a.id == id; }),
                        graph_.arrays.end());
    refreshList();
    setSelection(made, made.empty() ? 0 : made.front());
    applyEdit(0);
}

// ---------------------------------------------------------------------------
// Instances
// ---------------------------------------------------------------------------
void SceneBuilder::unlinkInstance(uint32_t id) {
    const Entity* e = entityById(id);
    if (!e || e->instanceOf == 0) return;
    const Entity own = resolveInstance(graph_, *e);
    editEntity(id, [own](Entity& x) {
        x = own;
        x.instanceOf = 0;
    });
}

// ---------------------------------------------------------------------------
// The selection helpers of the menus
// ---------------------------------------------------------------------------
// "Такие же": for a shape, the shapes of the same form with the same components, and every instance
// of the same master; for a group every group, for an array every array.
void SceneBuilder::selectSimilar() {
    const SceneObject* active = selectedObject();
    if (!active) return;
    std::vector<uint32_t> ids;
    if (groupById(active->id)) {
        for (const Group& g : graph_.groups)
            if (g.visible && !g.locked) ids.push_back(g.id);
    } else if (arrayById(active->id)) {
        for (const ArrayObject& a : graph_.arrays)
            if (a.visible && !a.locked) ids.push_back(a.id);
    } else {
        const Entity like = resolveInstance(graph_, *entityById(active->id));
        const uint32_t master = masterOf(active->id);
        for (const Entity& e : graph_.entities) {
            if (!e.visible || e.locked) continue;
            const Entity r = resolveInstance(graph_, e);
            if (masterOf(e.id) == master || (r.shape == like.shape && roleBits(r) == roleBits(like))) ids.push_back(e.id);
        }
    }
    setSelection(ids, active->id);
}

void SceneBuilder::selectWithRole(RoleIcon role) {
    std::vector<uint32_t> ids;
    for (const Entity& e : graph_.entities)
        if (e.visible && !e.locked && roleEnabled(resolveInstance(graph_, e), role)) ids.push_back(e.id);
    if (ids.empty()) emit statusMessage("Ни у одного объекта нет компонента «" + roleTitle(role) + "»");
    setSelection(ids, selectedId_);
}

void SceneBuilder::invertSelection() {
    std::vector<uint32_t> ids;
    for (uint32_t id : topLevelObjects())
        if (!drawnSelected(id)) ids.push_back(id);
    setSelection(ids, 0);
}
