// What is selected in the scene builder (see SceneBuilder.h), and what works on all of it: one object
// is active - the inspector and the gizmo show it - and Shift / Ctrl + click or the selection box add
// more (Shift and Ctrl both toggle, so any habit works). An object is a shape, a
// group or an array alike. Delete, duplicate, hide and the gizmo act on every selected object - a
// member of a selected group goes with its group -; a gizmo drag is one undo step and Esc puts them
// back. The gizmo works in the world: an object in a group is moved where the mouse takes it, its
// pose written back relative to the group.
#include "SceneBuilder.h"
#include "UiLanguage.h"
#include "scene/Collider.h"

#include "IkSolverView.h"

#include "scene/Assembly.h"
#include "scene/Kinematics.h"

#include "scene/SceneActions.h"
#include "scene/SceneOps.h"

#include <QJsonArray>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>

using namespace ke;

bool SceneBuilder::isSelected(uint32_t id) const {
    return id != 0 && std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void SceneBuilder::setSelected(uint32_t id) { setSelection(id ? std::vector<uint32_t>{id} : std::vector<uint32_t>{}, id); }

// The one place the selection changes: unknown ids and repeats are dropped, the active one is kept
// if it is still selected, else the last one added becomes active.
void SceneBuilder::setSelection(std::vector<uint32_t> ids, uint32_t active) {
    std::vector<uint32_t> kept;
    for (uint32_t id : ids)
        if ((findObject(graph_, id) || isDevice(id)) && std::find(kept.begin(), kept.end(), id) == kept.end()) kept.push_back(id);
    selection_ = std::move(kept);
    selectedId_ = isSelected(active) ? active : (selection_.empty() ? 0 : selection_.back());
    if (colliderEdit_ && (selection_.size() != 1 || selectedId_ != colliderEdit_)) setColliderEdit(false); // another thing: the body's gizmo again
    syncListSelection();
    fillInspector();
    refreshIkView();
    sendViewportFlags();
    updateGizmoTarget();
    refreshColliderGuides();
    refreshMarkers();
    emit selectionChanged(selectedId_);
    scheduleCode(); // a click's selection is a line too: editor.select(...)
}

void SceneBuilder::syncListSelection() {
    const QSignalBlocker quiet(list_);
    for (QTreeWidgetItemIterator it(list_); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const uint32_t id = item->data(0, Qt::UserRole).toUInt();
        item->setSelected(isSelected(id));
        if (id == selectedId_) list_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
}

// Shift / Ctrl + click: a thing inside a selected group takes the group away; else it is added.
void SceneBuilder::toggleSelected(uint32_t id) {
    const SceneObject* o = findObject(graph_, id);
    if (!o || o->locked) return;
    std::vector<uint32_t> ids = selection_;
    for (uint32_t up : chainOf(id)) {
        if (!isSelected(up)) continue;
        ids.erase(std::remove(ids.begin(), ids.end(), up), ids.end());
        return setSelection(ids, selectedId_ == up ? 0 : selectedId_);
    }
    ids.push_back(id);
    setSelection(ids, id);
}

void SceneBuilder::selectAll() { setSelection(topLevelObjects(), selectedId_); }

// The box: every visible, unlocked thing whose outline on screen (the corners of its bounds,
// projected) touches the rectangle selects its topmost group (or its array). Shift adds to the
// selection, Ctrl takes away (Blender's rule).
void SceneBuilder::selectInRect(const QRectF& rect, const GizmoView& view, bool add, bool remove) {
    std::vector<uint32_t> ids = add || remove ? selection_ : std::vector<uint32_t>{};
    for (const Entity& w : worldEntities(graph_)) {
        if (!w.visible || w.locked) continue;
        const std::vector<uint32_t> chain = chainOf(ownerOf(w.id));
        if (chain.empty()) continue;
        const AABB box = editView_.worldBounds(graph_, w);
        QRectF onScreen;
        bool behind = false;
        for (int c = 0; c < 8; ++c) {
            const Vector3 p((c & 1) ? box.hi.x : box.lo.x, (c & 2) ? box.hi.y : box.lo.y, (c & 4) ? box.hi.z : box.lo.z);
            if (dot(p - view.eye(), view.forward()) <= 0.0f) behind = true;
            const Vector2 s = view.project(p);
            onScreen = c == 0 ? QRectF(s.x, s.y, 0, 0) : onScreen.united(QRectF(s.x, s.y, 0, 0));
        }
        if (behind || !onScreen.adjusted(-1, -1, 1, 1).intersects(rect)) continue;
        const uint32_t target = chain.back();
        if (remove) ids.erase(std::remove(ids.begin(), ids.end(), target), ids.end());
        else if (std::find(ids.begin(), ids.end(), target) == ids.end()) ids.push_back(target);
    }
    setSelection(ids, selectedId_);
}

void SceneBuilder::removeSelected() {
    std::vector<uint32_t> gone = selectionRoots();
    for (uint32_t id : selection_)
        if (isDevice(id)) gone.push_back(id); // a battery or a driver: no SceneObject, no root
    if (gone.empty()) return;
    const QString py = codeCall("delete", gone);
    flushCode();
    remember();
    ke::deleteObjects(graph_, gone);
    selection_.clear();
    refreshList();
    setSelection({}, 0);
    applyEdit(0);
    codeAction("scene_delete", py, {{"ids", jsonIds(gone)}}, false);
}

// Copies in place (Ctrl+D), a little above the originals; the copies become the selection.
void SceneBuilder::duplicateSelected() {
    const std::vector<uint32_t> roots = selectionRoots();
    if (roots.empty()) return;
    const QString py = codeCall("duplicate", roots);
    flushCode();
    remember();
    const std::vector<uint32_t> copies = coreEdit([&](SceneGraph& g) { return ke::duplicateObjects(g, roots); });
    refreshList();
    setSelection(copies, copies.back());
    applyEdit(0);
    frameObjects();
    codeAction("scene_duplicate", py, {{"ids", jsonIds(roots)}}, false);
}

void SceneBuilder::hideSelected() {
    const std::vector<uint32_t> roots = selectionRoots();
    if (roots.empty()) return;
    remember();
    for (uint32_t id : roots) objectById(id)->visible = false;
    refreshList();
    setSelection({}, 0);
    applyEdit(0);
}

// Everything is shown again, except the hidden templates of arrays (the arrays draw them).
void SceneBuilder::unhideAll() {
    flushCode();
    remember();
    ke::showAll(graph_);
    refreshList();
    fillInspector();
    applyEdit(0);
    codeAction("scene_show_all", "scene.show_all()", {}, false);
}

// ---------------------------------------------------------------------------
// The gizmo: a drag is one undo step; Esc puts the objects back as they were
// ---------------------------------------------------------------------------
void SceneBuilder::onGizmoStarted() {
    if (colliderEdit_) return beginColliderDrag();
    flushCode(); // what the hand did before the drag is said before it
    if (clonePending_) cancelClone(); // a popover left open: its clone is dropped
    SceneObject* active = selectedObject();
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    cloneDrag_ = false;
    if (!active || !gizmoOnSelection()) return;
    remember();
    takeGoalFromPath(active->id);
    if (!editing() && liveTargetValid_ && entityById(active->id)) { // on pause: from where it is now
        Vector3 p;
        Quaternion q;
        worldPose(graph_, *active, p, q);
        const Vector3 keep = active->rotationDeg;
        setWorldPose(graph_, *active, liveTarget_, q);
        active->rotationDeg = keep;
    }
    gizmoGraph_ = graph_;
    lastPose_ = GizmoPose();
    for (uint32_t id : selectionRoots()) {
        const SceneObject* o = findObject(graph_, id);
        if (!effectivelyVisible(graph_, *o)) continue;
        GizmoStart s;
        worldPose(graph_, *o, s.position, s.rotation);
        gizmoStarts_[id] = s;
    }
    if (gizmoStarts_.empty()) return;
    gizmoEntity_ = active->id;
    worldPose(graph_, *active, gizmoFrom_.position, gizmoFrom_.rotation);
    gizmoPoses_ = graph_;
    gizmoStep_ = GizmoStep();
}

// Every dragged object follows the gizmo in the world (ke::moveObjects, turnObjects, scaleObjects from the
// poses when the drag began): moved by the same step, turned about the gizmo's centre by the same turn,
// scaled about its own centre by the same factors.
void SceneBuilder::onGizmoMoved(const GizmoPose& pose) {
    if (colliderDrag_) { // the collider mode: the gizmo's place and turn are the collider's on the body; its factors, the kind's own numbers
        ColliderRole c = colliderStart_;
        const Quaternion back = colliderBodyTurn_.conjugate();
        const auto most = [](float a, float b) { return std::fabs(a - 1.0f) > std::fabs(b - 1.0f) ? a : b; }; // the factor the hand moved
        if (pose.mode == GizmoMode::Translate) c.offset = back.rotate(pose.position - colliderBodyAt_);
        else if (pose.mode == GizmoMode::Rotate) c.rotationDeg = eulerDegrees((back * pose.rotation).normalized());
        else if (pose.mode == GizmoMode::Scale && c.kind == ColliderKind::Box) c.size = vmax(c.size * pose.scale, Vector3(0.005f));
        else if (pose.mode == GizmoMode::Scale && c.kind == ColliderKind::Sphere) c.size = Vector3(std::max(0.005f, c.size.x * most(pose.scale.x, most(pose.scale.y, pose.scale.z))));
        else if (pose.mode == GizmoMode::Scale) {
            c.size.x = c.size.z = std::max(0.005f, c.size.x * most(pose.scale.x, pose.scale.z));
            c.size.y = std::max(0.005f, c.size.y * pose.scale.y);
        }
        return showCollider(c);
    }
    if (!gizmoEntity_) return;
    lastPose_ = pose;
    applyGizmo(stepOf(pose));
}

// The pose the mouse made, as the numbers of the line: the step exactly; the turn as an axis and an
// angle, a world axis named when the axis is one to 1e-6 - the hand then turns by exactly that.
SceneBuilder::GizmoStep SceneBuilder::stepOf(const GizmoPose& pose) const {
    GizmoStep s;
    s.mode = pose.mode;
    s.by = pose.position - gizmoFrom_.position;
    s.scale = pose.scale;
    Quaternion q = (pose.rotation * gizmoFrom_.rotation.conjugate()).normalized();
    if (q.w < 0) q = Quaternion{-q.w, -q.x, -q.y, -q.z};
    const Vector3 v(q.x, q.y, q.z);
    const float n = length(v);
    s.axis = n > 1e-12f ? v / n : Vector3(0.0f, 1.0f, 0.0f);
    // To a ten-thousandth of a degree: a quaternion of floats gives a quarter turn back as 90.00001 or 89.99999, and the
    // line and the scene would carry that for ever; the hand applies the rounded angle, so the line still replays it.
    s.degrees = float(std::round(2.0 * std::atan2(double(n), double(q.w)) * (180.0 / 3.14159265358979323846) * 1e4) / 1e4);
    for (int k = 0; k < 3; ++k)
        for (float sign : {1.0f, -1.0f})
            if (std::fabs(s.axis[k] * sign - 1.0f) < 1e-6f) s.axis = Vector3(0.0f), s.axis[k] = 1.0f, s.degrees *= sign;
    return s;
}

std::vector<uint32_t> SceneBuilder::gizmoIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, start] : gizmoStarts_)
        if (id != gizmoEntity_) ids.push_back(id);
    if (gizmoStarts_.count(gizmoEntity_)) ids.push_back(gizmoEntity_);
    return ids;
}

void SceneBuilder::applyGizmo(const GizmoStep& step) {
    gizmoStep_ = step;
    const std::vector<uint32_t> ids = gizmoIds();
    switch (step.mode) {
    case GizmoMode::Translate: ke::moveObjects(graph_, gizmoPoses_, ids, step.by); break;
    case GizmoMode::Rotate: ke::turnObjects(graph_, gizmoPoses_, ids, gizmoFrom_.position, ke::turnOf(step.axis, step.degrees)); break;
    case GizmoMode::Scale: ke::scaleObjects(graph_, gizmoPoses_, ids, step.scale); break;
    case GizmoMode::Select: break;
    }
    followGoals(ids); // an IK goal dragged: its arm follows
    if (editing()) refreshView(); // on pause the simulation takes the new pose on release
    else updateGizmoTarget(), refreshLightsAndCameras();
    if (const SceneObject* active = selectedObject()) showTransform(*active);
}

void SceneBuilder::onGizmoFinished() {
    if (colliderDrag_) return finishColliderDrag(false);
    const bool moved = gizmoEntity_ != 0;
    QString op;
    QJsonObject args;
    const QString py = moved ? gizmoLine(op, args) : QString();
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    if (moved && cloneDrag_) { // the copy stands where it was dragged: the popover asks how many (MainWindow)
        cloneDrag_ = false;
        clonePending_ = true;
        return;
    }
    fillInspector();
    if (!editing() && moved) applyEdit(0);
    if (moved && !op.isEmpty()) codeAction(op, py, args, false);
}

void SceneBuilder::onGizmoCancelled() {
    if (colliderDrag_) return finishColliderDrag(true);
    if (!gizmoEntity_) return;
    graph_ = gizmoGraph_;
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    if (!undo_.empty()) undo_.pop_back(); // nothing happened: no undo step either
    updateHistoryActions();
    if (cloneDrag_) { // the moving copies go; the originals are the selection again
        cloneDrag_ = false;
        std::vector<uint32_t> originals;
        for (const auto& [original, copy] : cloneOf_) originals.push_back(original);
        cloneOf_.clear();
        refreshList();
        setSelection(originals, originals.empty() ? 0 : originals.front());
    }
    fillInspector();
    if (editing()) refreshView();
    else updateGizmoTarget();
}

// A goal dragged moves only the goal: an arm moves by its motors, and without the physics running there are
// none - in edit mode the arm stands where it is put. While the scene plays the running world leads the hand
// to the goal in a straight line (RobotWorld::leadGoals). The card shows the inverse kinematics as a forecast:
// a solve on a copy - whether the goal is reached and how (the observability contract: IK is a prediction,
// not proof of execution).
void SceneBuilder::followGoals(const std::vector<uint32_t>& moved) {
    bool any = false;
    for (uint32_t id : moved)
        for (const Entity& e : graph_.entities) {
            if (e.id != id || e.goalOf == 0) continue;
            any = true;
            SceneGraph copy = graph_;
            lastIk_ = ke::reachGoal(copy, id);
            lastIkGoal_ = id;
            break;
        }
    if (any && !editing()) applyEditDuringPlay(0); // the goal's new pose into the running world
    if (any) refreshIkView();
}

void SceneBuilder::noteGoalSolves(const SceneGraph& before) {
    for (const Entity& e : graph_.entities) {
        if (e.goalOf == 0) continue;
        SceneGraph copy = before;
        for (Entity& was : copy.entities)
            if (was.id == e.id && (length(was.position - e.position) > 0 || length(was.rotationDeg - e.rotationDeg) > 0)) {
                was.position = e.position, was.rotationDeg = e.rotationDeg;
                lastIk_ = ke::reachGoal(copy, e.id), lastIkGoal_ = e.id;
            }
    }
    refreshIkView();
}

// Each goal's path from where the goal stands through its waypoints (and back, looped), a small cross at each point.
void SceneBuilder::refreshPaths() {
    std::vector<float> lines;
    auto segment = [&lines](const Vector3& a, const Vector3& b) { lines.insert(lines.end(), {a.x, a.y, a.z, 0, b.x, b.y, b.z, 0}); };
    for (const Entity& e : graph_.entities) {
        if (e.goalOf == 0 || e.path.empty()) continue;
        Vector3 at;
        Quaternion q;
        worldPose(graph_, e, at, q);
        std::vector<Vector3> points{at};
        points.insert(points.end(), e.path.begin(), e.path.end());
        if (e.pathLoop) points.push_back(at);
        const float total = float(ke::pathLength(e));
        const int samples = e.pathSmooth && e.path.size() >= 2 ? std::max(2, int(total / 0.01f)) : 0; // a flight: the spline itself
        for (int i = 1; i <= samples; ++i) {
            Vector3 a, b, axis;
            Quaternion qa, qb;
            ke::pathPose(e, total * float(i - 1) / float(samples) / e.pathSpeed, a, qa);
            ke::pathPose(e, total * float(i) / float(samples) / e.pathSpeed, b, qb);
            segment(a, b);
            for (int k = 0; i % 6 == 0 && k < 3; ++k) // a frame every 6 cm: how the goal is turned there, its y long
                axis = Vector3(0.0f), axis[k] = k == 1 ? 0.03f : 0.01f, segment(b, b + qb.rotate(axis));
        }
        for (size_t k = 1; samples == 0 && k < points.size(); ++k) segment(points[k - 1], points[k]);
        for (size_t k = 0; k < e.path.size(); ++k) { // a cross turned as the goal will be there, its y long: where the hand points
            SceneObject o;
            o.rotationDeg = k < e.pathTurns.size() ? e.pathTurns[k] : e.rotationDeg;
            const Quaternion turn = objectRotation(o);
            for (int axis = 0; axis < 3; ++axis) {
                Vector3 d(0.0f);
                d[axis] = 0.012f;
                segment(e.path[k] - turn.rotate(d), e.path[k] + turn.rotate(axis == 1 ? d * 3.5f : d));
            }
        }
    }
    emit pathLines(std::move(lines));
}

// The fingers of a hand: the sliders on it that move (as assembly_grip finds them); closed when every one is
// driven to its far end.
static bool handClosed(const SceneGraph& g, uint32_t hand, bool& fingers) {
    bool closed = true;
    fingers = false;
    for (const JointObject& j : g.joints) {
        const int dof = mainFreedom(j);
        if (dof < 0 || dof > 2 || (j.a != hand && j.b != hand) || j.dof[dof].motion == JointMotion::Locked) continue;
        fingers = true;
        closed = closed && j.dof[dof].drive.enabled && j.dof[dof].drive.target >= j.dof[dof].upper - 1e-6_r;
    }
    return fingers && closed;
}

void SceneBuilder::refreshIkView() {
    // On an arm's link the button makes its IK goal; on any other thing, with an arm in the scene, it sends the arm there
    // - a newcomer asked it to "reach here" for a cube and was told to pick an arm's link.
    // Neither on a goal itself (its own button would lift it over itself) nor where no arm can take it.
    const Entity* selected = entityById(selectedId_);
    const bool link = selected && !ke::ikChain(graph_, selectedId_).empty();
    const bool movable = selected && selected->rigid.enabled && !selected->rigid.fixed; // a thing to take, not the floor or a table
    const bool reach = selected && !link && !selected->goalOf && movable && goalNear(selectedId_) != 0;
    goalButton_->setVisible(link || reach);
    goalButton_->setText(reach ? uiText("Kinetica", "Рука — к этому предмету") : uiText("Kinetica", "Вести звено мышью (цель)")); // it said «рука тянется сюда» and did not
    goalButton_->setToolTip(reach ? uiText("Kinetica", "Цель руки встаёт на 10 см над предметом, рука идёт к ней. Дальше: опустить — F, сомкнуть схват — пробел "
                                    "(в «Пуске»; в правке — кнопка «Схват: сжать» у цели)")
                                  : uiText("Kinetica", "Для последнего звена руки: прозрачный куб-цель в его позе. Тяните и вращайте куб гизмо — "
                                    "инверсная кинематика ведёт сочленения (в «Пуске» — сервоприводами)"));
    const Entity* goal = nullptr;
    for (const Entity& e : graph_.entities)
        if (e.id == selectedId_ && e.goalOf != 0 && selection_.size() == 1) goal = &e;
    bool fingers = false;
    const bool closed = goal && handClosed(graph_, goal->goalOf, fingers);
    grip_->setVisible(goal && fingers);
    grip_->setText(closed ? uiText("Kinetica", "Схват: разжать") : uiText("Kinetica", "Схват: сжать"));
    ikMore_->setVisible(goal != nullptr);
    ikView_->setVisible(goal && ikMore_->isChecked()); // folded under «Подробно» unless opened
    if (!goal) return;
    if (lastIkGoal_ != goal->id) { // not dragged yet: a solve from where its servos are sent, on a copy
        SceneGraph copy = graph_;
        lastIk_ = ke::reachGoal(copy, goal->id), lastIkGoal_ = goal->id;
    }
    ikView_->show(lastIk_, int(ke::ikChain(graph_, goal->goalOf).size()));
}

// The button under a goal whose hand has fingers: closed - opened, opened - closed, one call of assembly_grip.
void SceneBuilder::toggleGrip() {
    const Entity* goal = entityById(selectedId_);
    if (!goal || goal->goalOf == 0) return;
    const uint32_t id = goal->id;
    bool fingers = false;
    const bool close = !handClosed(graph_, goal->goalOf, fingers);
    Value args = Value::map(), answer = Value::map();
    args.set("goal", Real(id)), args.set("close", close);
    flushCode();
    std::string error;
    if (!transact([&](SceneGraph& g, std::string& why) { return findSceneOp("assembly_grip")->edit(g, args, answer, why); }, error))
        return emit statusMessage(QString::fromStdString(error));
    codeAction("assembly_grip", QString("ed.assembly_grip(goal=%1, close=%2)").arg(id).arg(close ? "True" : "False"),
               QJsonObject{{"goal", int(id)}, {"close", close}}, false);
    refreshIkView();
}

// The mouse drags an IK goal while the scene plays (Viewport::goalDragged, GoalTool): every step is the core's
// assembly_goal_pose on the scene - the mouse moves a goal as a script does - the whole drag one undo step and, as it
// ends, one line of code that puts the goal where the mouse left it.
void SceneBuilder::dragGoal(uint32_t id, Vector3 position, Quaternion turn, bool end) {
    if (!goalDragging_) flushCode(), remember(), goalDragging_ = true;
    const Vector3 degrees = eulerDegrees(turn);
    Value args = Value::map(), answer;
    args.set("goal", Real(id)), args.set("position", vectorValue(position)), args.set("turn", vectorValue(degrees));
    std::string error;
    if (!findSceneOp("assembly_goal_pose")->edit(graph_, args, answer, error)) return emit statusMessage(QString::fromStdString(error));
    applyEdit(id);
    refreshIkView();
    if (!end) return;
    goalDragging_ = false;
    const auto three = [](const Vector3& v) { return QString("[%1, %2, %3]").arg(double(v.x), 0, 'g', 5).arg(double(v.y), 0, 'g', 5).arg(double(v.z), 0, 'g', 5); };
    const auto json = [](const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
    codeAction("assembly_goal_pose", QString("ed.assembly_goal_pose(goal=%1, position=%2, turn=%3)").arg(id).arg(three(position)).arg(three(degrees)),
               QJsonObject{{"goal", int(id)}, {"position", json(position)}, {"turn", json(degrees)}}, false);
}

// A goal on a path taken by the gizmo: the hand takes over, as a keyed object grabbed in an animation tool - the goal
// put where the path has walked it (its turn too), the path dropped; from here the gizmo drives the arm.
void SceneBuilder::takeGoalFromPath(uint32_t id) {
    Entity* goal = entityById(id);
    const auto live = goalLive_.find(id);
    if (!goal || goal->goalOf == 0 || goal->path.empty() || editing()) return;
    if (live != goalLive_.end()) goal->position = live->second.first, goal->rotationDeg = eulerDegrees(live->second.second);
    else if (!goal->pathLoop) {
        goal->position = goal->path.back();
        if (!goal->pathTurns.empty()) goal->rotationDeg = goal->pathTurns.back();
    }
    goal->path.clear(), goal->pathTurns.clear();
    emit statusMessage(uiText("Kinetica", "Цель снята с траектории: теперь её ведёт гизмо"));
}

bool SceneBuilder::goalSelected() const {
    const auto goal = [this](uint32_t id) {
        return std::any_of(graph_.entities.begin(), graph_.entities.end(), [id](const Entity& e) { return e.id == id && e.goalOf != 0; });
    };
    return !selection_.empty() && std::all_of(selection_.begin(), selection_.end(), goal);
}

uint32_t SceneBuilder::goalNear(uint32_t object) const {
    const ke::AABB box = ke::objectBounds(graph_, object);
    uint32_t best = 0;
    float nearest = std::numeric_limits<float>::infinity();
    for (const Entity& e : graph_.entities) {
        if (!e.goalOf) continue;
        const float d = box.valid() ? ke::length(e.position - box.center()) : 0.0f;
        if (d < nearest) nearest = d, best = e.id;
    }
    return best;
}

// «Цель IK»: a see-through cube at the selected link's pose; dragging it drives the arm (followGoals). On a thing that is no
// arm's link: «Рука — к этому предмету» - the nearest IK goal 10 cm over the thing's top, its turn kept (assembly_goal_pose).
void SceneBuilder::addGoalToSelected() {
    const uint32_t effector = selectedId_;
    if (const uint32_t goal = goalNear(effector); entityById(effector) && !entityById(effector)->goalOf && ke::ikChain(graph_, effector).empty() && goal) {
        const ke::AABB box = ke::objectBounds(graph_, effector);
        if (!box.valid()) return;
        const Vector3 over(box.center().x, box.hi.y + 0.10f, box.center().z);
        Value args = Value::map(), answer = Value::map(), at = Value::list();
        for (int k = 0; k < 3; ++k) at.push(over[k]);
        args.set("goal", goal), args.set("position", at);
        std::string error;
        flushCode();
        if (!transact([&](SceneGraph& g, std::string& e) { return ke::findSceneOp("assembly_goal_pose")->edit(g, args, answer, e); }, error))
            return emit statusMessage(uiText("Kinetica", "Рука: ") + QString::fromStdString(error));
        codeAction("assembly_goal_pose", QString("ed.assembly_goal_pose(goal=%1, position=[%2, %3, %4])").arg(goal).arg(over.x).arg(over.y).arg(over.z),
                   {{"goal", int(goal)}, {"position", QJsonArray{over.x, over.y, over.z}}}, false);
        return emit statusMessage(uiText("Kinetica", "Рука идёт к «") + QString::fromStdString(entityById(effector)->name) +
                                  uiText("Kinetica", "»: цель на 10 см над ним. Опустить — F, сомкнуть схват — пробел, поднять — R"));
    }
    if (!entityById(effector) || ke::ikChain(graph_, effector).empty()) return emit statusMessage(uiText("Kinetica", "Цель IK: выберите последнее звено руки — то, что висит на сочленении"));
    flushCode();
    remember();
    const uint32_t goal = coreEdit([effector](SceneGraph& g) { return ke::addGoal(g, effector); });
    refreshList();
    setSelection({goal}, goal);
    applyEdit(goal);
    codeAction("assembly_goal", codeRefs({effector}) + ".add_goal()", {{"effector", double(effector)}}, false);
    emit statusMessage(uiText("Kinetica", "Тяните и вращайте цель гизмо — рука идёт за ней (во время игры — моторами)"));
}

