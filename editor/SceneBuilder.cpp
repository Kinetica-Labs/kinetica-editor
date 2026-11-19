// The scene builder's logic (see SceneBuilder.h): creating and removing things, turning components on
// and off together with what they need, the gizmo's edits, the undo history, the edit / play / stop
// modes and what the viewport shows in each. The panels are built in SceneBuilderPanels.cpp.
#include "SceneBuilder.h"
#include "core/Probe.h"
#include "scene/SensorSpec.h"
#include "scene/Assembly.h"
#include "scene/SceneActions.h"
#include "WorldPanel.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "RoleBar.h"

#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>

using namespace ke;

const char* shapeName(ShapeKind k) { return ke::shapeTitle(k); }

SceneBuilder::SceneBuilder(SimController* ctrl, QObject* parent) : QObject(parent), ctrl_(ctrl) {
    lastRemember_.start();
    buildActions();
    buildLightActions();
    buildSceneList();
    buildWorldPanel();
    connect(this, &SceneBuilder::modeChanged, this, [this] { worldPanel_->setEditable(editing()); });
    buildInspector();
    loadSensorCatalog();
    fillWorld();
    refreshList();
    fillInspector();
    updateHistoryActions();
}

// ---------------------------------------------------------------------------
// Every edit ends here
// ---------------------------------------------------------------------------
void SceneBuilder::applyEdit(uint32_t entityId) {
    if (editing()) refreshView();
    else applyEditDuringPlay(entityId);
    scheduleCode();
}

void SceneBuilder::afterCardEdit(uint32_t id, bool click) {
    refreshList();
    applyEdit(id);
    if (click) fillInspector();
}

namespace {
std::string arrayText(const SceneGraph& g) {
    SceneGraph arrays;
    arrays.arrays = g.arrays;
    return arrays.save();
}
std::string entitiesText(const SceneGraph& g) {
    SceneGraph entities;
    entities.entities = g.entities;
    return entities.save();
}
std::string worldText(const SceneGraph& g) {
    SceneGraph world;
    world.world = g.world;
    return world.save();
}
std::string lightsText(const SceneGraph& g) {
    SceneGraph lights;
    lights.lights = g.lights;
    lights.cameras = g.cameras;
    return lights.save();
}
std::string jointsText(const SceneGraph& g) {
    SceneGraph joints;
    joints.joints = g.joints;
    return joints.save();
}
std::string devicesText(const SceneGraph& g) {
    SceneGraph devices;
    devices.batteries = g.batteries;
    devices.drivers = g.drivers;
    devices.controllers = g.controllers;
    devices.sensors = g.sensors;
    devices.wheels = g.wheels;
    return devices.save();
}
std::string groupsText(const SceneGraph& g) {
    SceneGraph groups;
    groups.groups = g.groups;
    return groups.save();
}
} // namespace

// An edit while the robot's world runs. What the world can take in place goes to it between frames:
// the lights and cameras (the physics never sees them, the next frame shows them) and the joints' drives
// - a servo's target typed, an IK goal dragged (RobotWorld::setJoints). What the world is built of - the
// bodies and their colliders, the arrays and groups, the world itself, the devices - needs it built
// again: the builder stops with the edit kept and says why. An IK goal is no body: dragging it while
// the scene plays moves the goal's mark in the running world (RobotWorld::setGoals) and its arm's targets.
void SceneBuilder::applyEditDuringPlay(uint32_t entityId) {
    (void)entityId; // every change is found by comparing, whichever edit made it
    if (worldText(graph_) != worldText(simGraph_)) return reloadNeeded("Тяжесть, размер мира и настройки физики меняют только перезапуском сцены");
    if (groupsText(graph_) != groupsText(simGraph_)) return reloadNeeded("Группы меняют только перезапуском сцены");
    if (arrayText(graph_) != arrayText(simGraph_)) return reloadNeeded("Массивы меняют только перезапуском сцены");
    if (devicesText(graph_) != devicesText(simGraph_)) return reloadNeeded("Датчики, колёса, батареи и пульты меняют только перезапуском сцены");
    SceneGraph goalsAside = graph_, before = simGraph_; // the goals are handles: no body of the world
    for (SceneGraph* g : {&goalsAside, &before})
        g->entities.erase(std::remove_if(g->entities.begin(), g->entities.end(), [](const Entity& e) { return isHandle(e); }), g->entities.end());
    if (entitiesText(goalsAside) != entitiesText(before)) return reloadNeeded("Тела и коллайдеры меняют только перезапуском сцены");
    refreshLightsAndCameras();
    if (lightsText(graph_) != lightsText(simGraph_)) {
        const std::vector<Light> lights = graph_.lights;
        const std::vector<Camera> cameras = graph_.cameras;
        ctrl_->post([lights, cameras](Simulation& s) {
            if (s.robot()) s.robot()->setLightsAndCameras(lights, cameras);
        }, "lights");
    }
    std::vector<Entity> goals, goalsBefore; // made, moved or removed: the running world's marks and goal channels follow
    for (const Entity& e : graph_.entities) // the handles: IK goals and cameras' targets
        if (isHandle(e)) goals.push_back(e);
    for (const Entity& e : simGraph_.entities)
        if (isHandle(e)) goalsBefore.push_back(e);
    SceneGraph a, z;
    a.entities = goals, z.entities = goalsBefore;
    if (a.save() != z.save())
        ctrl_->post([goals](Simulation& s) {
            if (s.robot()) s.robot()->setGoals(goals);
        }, "goals");
    if (jointsText(graph_) != jointsText(simGraph_)) {
        const std::vector<JointObject> joints = graph_.joints;
        QPointer<SceneBuilder> self(this);
        ctrl_->post([joints, self](Simulation& s) {
            if (!s.robot() || s.robot()->setJoints(*s.physics(), joints)) return;
            QMetaObject::invokeMethod(self, [self] {
                if (self && !self->editing()) self->reloadNeeded("Вид, звенья, рамки и пределы сустава меняют только перезапуском сцены");
            }, Qt::QueuedConnection);
        }, "joints");
    }
    simGraph_ = graph_;
}

// A change the running simulation cannot take: stop with the edit kept, and say what to do.
void SceneBuilder::reloadNeeded(const QString& why) {
    returnToEdit(true);
    showBanner(why + ". Правка сохранена.", "▶ Пуск", [this] { play(); });
    stickyBanner_ = true;
}

void SceneBuilder::showBanner(const QString& text, const QString& buttonText, std::function<void()> fix) {
    banner_->showMessage(text, buttonText, std::move(fix));
}

// ---------------------------------------------------------------------------
// Creating and removing: a new shape is geometry only, standing on the floor
// ---------------------------------------------------------------------------
void SceneBuilder::addEntity(ShapeKind shape, const std::function<void(Entity&)>& adjust) {
    flushCode();
    remember();
    const uint32_t id = coreEdit([&](SceneGraph& g) { return ke::addShape(g, shape); });
    if (adjust) adjust(*entityById(id));
    setSelected(id);
    refreshList();
    applyEdit(id);
    frameObjects();
    emit firstAction();
    codeAction("scene_add", QString("scene.add(\"%1\")").arg(shapeWord(shape)), {{"shape", shapeWord(shape)}}, false);
}

bool SceneBuilder::importModel(const QString& path, QString& error) {
    Entity probe;
    probe.shape = ShapeKind::Mesh;
    probe.meshFile = path.toStdString();
    probe.size = Vector3(1.0f);
    std::string err;
    const TriMesh m = entityLocalMesh(probe, graph_.baseDirectory, &err);
    if (m.triangles.empty()) {
        error = err.empty() ? QString("в файле нет треугольников") : QString::fromStdString(err);
        return false;
    }
    const Vector3 extent = m.bounds().extent();
    remember();
    const uint32_t id = coreEdit([&](SceneGraph& g) { return ke::addShape(g, ShapeKind::Mesh); }); // a free spot, as any new shape
    Entity& e = *entityById(id);
    e.meshFile = probe.meshFile;
    e.size = extent / std::max(maxComp(extent), 1e-6f); // its largest side 1 m, proportions kept
    e.name = QFileInfo(path).completeBaseName().toStdString();
    e.position.y = 0.5f * e.size.y; // standing on the floor
    setSelected(id);
    refreshList();
    applyEdit(id);
    frameObjects();
    return true;
}


// The eye and the lock of any object (the list, the context menu).
void SceneBuilder::toggleVisible(uint32_t id) {
    if (!objectById(id)) return;
    remember();
    objectById(id)->visible = !objectById(id)->visible;
    refreshList();
    fillInspector();
    applyEdit(id);
}

void SceneBuilder::toggleLocked(uint32_t id) {
    if (!objectById(id)) return;
    remember();
    objectById(id)->locked = !objectById(id)->locked;
    refreshList();
    if (objectById(id)->locked && isSelected(id)) setSelected(0); // a locked thing is not edited
    else fillInspector();
    applyEdit(id);
}

void SceneBuilder::frameSelected() {
    AABB box;
    for (uint32_t id : selection_) box.expand(boundsOf(id));
    if (!box.valid()) return frameObjects();
    const Vector3 margin(std::max(0.15f, 0.4f * maxComp(box.extent())));
    emit frameRequested(AABB(box.lo - margin, box.hi + margin));
}

void SceneBuilder::editEntity(uint32_t id, const std::function<void(Entity&)>& change) {
    if (indexOf(id) < 0) return;
    remember();
    change(graph_.entities[size_t(indexOf(id))]);
    refreshList();
    if (id == selectedId_) fillInspector();
    applyEdit(id);
}

// ---------------------------------------------------------------------------
// Roles: one click turns the shape into a model; the role switches on what it needs
// ---------------------------------------------------------------------------
// On every selected shape at once (an instance: its master, so all its instances): added to all
// unless all have it, then taken from all.
void SceneBuilder::toggleRole(RoleIcon role) {
    bool all = !selectedEntities().empty();
    for (Entity* e : selectedEntities()) all = all && roleEnabled(*entityById(masterOf(e->id)), role);
    setRoleOfSelected(role, !all);
}

void SceneBuilder::setRoleOfSelected(RoleIcon role, bool on) {
    if (selectedEntities().empty()) {
        emit statusMessage("Сначала выберите объект — или создайте его кнопкой сверху");
        return;
    }
    std::vector<uint32_t> ids;
    QStringList lines;
    for (Entity* e : selectedEntities()) {
        ids.push_back(e->id);
        lines << codeRefs({e->id}) + QString(".roles.%1(\"%2\")").arg(on ? "add" : "remove", roleWord(role));
    }
    flushCode();
    remember();
    const std::string note = ke::setRoles(graph_, ids, ke::Role(int(role)), on);
    if (!note.empty()) emit statusMessage(QString::fromStdString(note));
    fillInspector();
    refreshList();
    applyEdit(selectedId_);
    emit firstAction();
    codeAction("scene_role", lines.join("; "), {{"ids", jsonIds(ids)}, {"role", roleWord(role)}, {"on", on}}, false);
}


// ---------------------------------------------------------------------------
// Edits from the widgets
// ---------------------------------------------------------------------------
void SceneBuilder::onObjectEdited() { applyWidgetEdit(false); }
void SceneBuilder::onDetailsEdited() { applyWidgetEdit(true); }

void SceneBuilder::onWorldEdited(const WorldSettings& settings) {
    if (filling_) return;
    remember(); graph_.world = settings; fillWorld(); applyEdit(0);
}

QWidget* SceneBuilder::worldPanel() const { return worldPanel_; }

// The eye and the lock are clicked right in the list; any other column selects the row.
void SceneBuilder::onListClicked(QTreeWidgetItem* item, int column) {
    const uint32_t id = item->data(0, Qt::UserRole).toUInt();
    if (isDevice(id)) return setSelected(id);
    if (!objectById(id)) return;
    if (column == 1) return toggleVisible(id);
    if (column == 2) {
        remember();
        objectById(id)->locked = !objectById(id)->locked;
        refreshList();
        fillInspector();
        applyEdit(id);
        return;
    }
    if (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) toggleSelected(id);
    else setSelected(id);
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
// Called before a change. Typing or dragging a number makes many small changes within a moment:
// those merge into one step, so one Ctrl+Z undoes the whole drag.
void SceneBuilder::remember(bool mergeWithLast) {
    const bool join = mergeWithLast && typing_ && lastRemember_.elapsed() < 700 && !undo_.empty();
    lastRemember_.restart();
    typing_ = mergeWithLast;
    if (join) return;
    undo_.push_back({graph_});
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
    updateHistoryActions();
}

void SceneBuilder::undo() {
    if (undo_.empty()) return;
    flushCode();
    typing_ = false;
    redo_.push_back({graph_});
    HistoryEntry entry = std::move(undo_.back());
    SceneGraph g = std::move(entry.graph);
    undo_.pop_back();
    restore(std::move(g));
    codeAction("edit_undo", "scene.undo()", {}, false); // the scene it brought back is the action, not an edit
}

void SceneBuilder::redo() {
    if (redo_.empty()) return;
    flushCode();
    typing_ = false;
    undo_.push_back({graph_});
    HistoryEntry entry = std::move(redo_.back());
    SceneGraph g = std::move(entry.graph);
    redo_.pop_back();
    restore(std::move(g));
    codeAction("edit_redo", "scene.redo()", {}, false); // the scene it brought back is the action, not an edit
}

void SceneBuilder::restore(SceneGraph g) {
    graph_ = std::move(g);
    pruneSelection();
    lastRemember_.restart();
    fillWorld();
    refreshList();
    fillInspector();
    refreshIkView(); // the goal's card: its solve and its gripper's button as the graph now has them
    updateHistoryActions();
    applyEdit(0);
}

void SceneBuilder::updateHistoryActions() {
    undoAct_->setEnabled(!undo_.empty());
    redoAct_->setEnabled(!redo_.empty());
}

// ---------------------------------------------------------------------------
// Edit / play / stop
// ---------------------------------------------------------------------------
void SceneBuilder::setMode(Mode m) {
    const Mode was = mode_;
    mode_ = m;
    if (m != was) // the buttons as the operations they are; Stop's restored scene is no edit of its own
        codeAction(m == Mode::Playing ? "sim_play" : m == Mode::Paused ? "sim_pause" : "sim_stop",
                   m == Mode::Playing ? "scene.play()" : m == Mode::Paused ? "scene.pause()" : "scene.stop()", {}, m != Mode::Edit);
    worldPanel_->setEditable(editing());
    liveTargetValid_ = false;
    playHint_->setVisible(!editing());
    playHint_->setText("Идёт симуляция: свет, камеры и приводы суставов меняются на лету, на паузе работает гизмо; "
                       "остальное — после ■ Стоп, который вернёт сцену, какой она была до ▶.");
    if (editing()) refreshView();
    else refreshColliderGuides(); // the play-mode lines come with the next snapshot
    refreshLightsAndCameras();
    refreshPaths();
    sendViewportFlags();
    updateGizmoTarget();
    emit modeChanged();
}

void SceneBuilder::play() {
    if (mode_ == Mode::Playing) return;
    stickyBanner_ = false;
    refreshBanner();
    emit firstAction();
    if (editing() && !loadIntoSimulation()) return;
    ctrl_->setRunning(true);
    setMode(Mode::Playing);
}

void SceneBuilder::pause() {
    if (mode_ != Mode::Playing) return;
    ctrl_->setRunning(false);
    setMode(Mode::Paused);
}

void SceneBuilder::stop() {
    if (mode_ != Mode::Edit) returnToEdit(false);
}

void SceneBuilder::step() {
    if (editing()) {
        if (!loadIntoSimulation()) return;
        setMode(Mode::Paused);
    } else if (mode_ == Mode::Playing) {
        pause();
    }
    ctrl_->requestStep();
}

// Stop: the simulation goes; the graph comes back as it was before Play, as in Unity (unless an
// edit the running scene could not take is being kept). The history loses the steps made while
// playing.
void SceneBuilder::returnToEdit(bool keepEdits) {
    flushCode(); // the edits made while it ran, before the scene goes back
    ctrl_->setRunning(false);
    ctrl_->post([](Simulation& s) { s.load(SceneGraph()); }, "stop"); // the meta-objects go: bodies, joints, drives
    const bool restore = hasPlayBackup_ && !keepEdits;
    if (restore) {
        const bool edited = playBackup_.save() != graph_.save();
        graph_ = playBackup_;
        undo_.resize(std::min(undo_.size(), undoAtPlay_));
        if (hasKeptBefore_) undo_.push_back({keptBefore_}); // K: one Ctrl+Z puts the objects back
        redo_.clear();
        pruneSelection();
        fillWorld();
        refreshList();
        fillInspector();
        updateHistoryActions();
        if (edited) emit statusMessage("Стоп: сцена вернулась к той, что была до ▶ (правки во время игры не сохраняются — как в Unity)");
    }
    hasPlayBackup_ = false;
    hasKeptBefore_ = false;
    setMode(Mode::Edit);
}

// The robot's world built from the graph on the simulation's thread; which drawn body is which object
// comes with every frame (onSimulationSnapshot).
bool SceneBuilder::loadIntoSimulation() {
    playBackup_ = simGraph_ = graph_;
    hasPlayBackup_ = true;
    undoAtPlay_ = undo_.size();
    ctrl_->setSceneName(displayName());
    const SceneGraph g = graph_;
    ctrl_->post([g](Simulation& s) {
        ke::Probe::clear(); // the channels of the scene before go with it
        s.load(g);
    }, "load");
    return true;
}

std::string SceneBuilder::displayName() const {
    return fileName_.isEmpty() ? std::string("Новая сцена") : fileName_.toStdString(); // what a person calls it, not «Конструктор»
}

void SceneBuilder::refreshView() {
    if (!editing()) return;
    refreshPaths();
    auto snap = editView_.snapshot(graph_, displayName(), bodyEntity_);
    snap->joints = ke::jointVisuals(graph_); // the joints as they will be built, before anything plays
    snap->sensors = ke::sensorVisuals(graph_);
    // A sonar's beam, before anything plays, to the first shape on its axis (the mouse's own picking)
    // - or faint to its full range.
    for (SensorVis& v : snap->sensors) {
        const SensorObject* s = sensorById(v.id);
        if (v.kind != int(SensorKind::Sonar) || !s) continue;
        const SonarSpec spec = sonarSpec(*s);
        float nearest = float(spec.maxM);
        for (const PickHit& h : editView_.pickAll(graph_, Ray{v.position, v.rotation.rotate(Vector3(1.0f, 0.0f, 0.0f))}))
            if (h.id != s->parent && h.t >= float(spec.minM) && h.t < nearest) nearest = h.t;
        v.reading = nearest < float(spec.maxM) ? nearest : -1.0f;
    }
    emit editSnapshot(snap);
    sendViewportFlags();
    updateGizmoTarget();
    refreshColliderGuides();
    refreshLightsAndCameras();
}

// The camera shows the things of the scene: the union of the visible, unlocked objects - lights and
// cameras too, at the size of their wireframes, or the gizmo could not reach them (the locked floor only
// when nothing else is there), with a margin - not the whole 4 x 3 x 4 m world box, in which small parts
// would be dots. After that the camera is free until the next load or add.
void SceneBuilder::frameObjects() {
    AABB mine, all;
    for (const Entity& w : worldEntities(graph_)) {
        if (!w.visible) continue;
        const AABB box = editView_.worldBounds(graph_, w);
        all.expand(box);
        if (!w.locked) mine.expand(box);
    }
    auto mark = [&](const SceneObject& o) {
        if (!effectivelyVisible(graph_, o) || o.locked) return;
        const Vector3 p = worldPositionOf(o.id);
        mine.expand(AABB(p - Vector3(0.3f), p + Vector3(0.3f)));
    };
    for (const Light& l : graph_.lights) mark(l);
    for (const Camera& c : graph_.cameras) mark(c);
    AABB box = mine.valid() ? mine : all;
    if (!box.valid()) return;
    box.lo.y = std::min(box.lo.y, 0.0f); // the floor under them is in the picture
    const Vector3 minimum(1.2f);          // a lone 20 cm cube is not a close-up
    const Vector3 grow = vmax(minimum - box.extent(), Vector3(0.0f)) * 0.5f;
    const Vector3 margin(std::max(0.1f, 0.08f * maxComp(box.extent())));
    box = AABB(box.lo - grow - margin, box.hi + grow + margin);
    emit frameRequested(box);
}

// The viewport outlines the selected thing's bodies (and in edit mode the one under the mouse and
// the geometry without a role) and lets the mouse pass through locked ones.
// A body belongs to what its drawn id says: a shape, an array's copy, a glued group (while playing);
// it is outlined when that, or a group or an array it is in, is selected.
void SceneBuilder::sendViewportFlags() {
    std::vector<int> selected, hovered, ghosts;
    std::vector<char> unpickable(bodyEntity_.size(), 0);
    std::map<uint32_t, const Entity*> drawn;
    const std::vector<Entity> world = worldEntities(graph_);
    for (const Entity& w : world) drawn[w.id] = &w;
    for (size_t b = 0; b < bodyEntity_.size(); ++b) {
        const uint32_t id = bodyEntity_[b];
        const SceneObject* owner = findObject(graph_, ownerOf(id));
        if (!owner) continue;
        if (drawnSelected(id)) selected.push_back(int(b));
        else if (editing() && isInside(ownerOf(id), hoverId_)) hovered.push_back(int(b));
        const auto w = drawn.find(id);
        if (editing() && w != drawn.end() && entityIsGeometryOnly(*w->second)) ghosts.push_back(int(b));
        unpickable[b] = w != drawn.end() ? w->second->locked : owner->locked;
    }
    emit highlightBodies(selected);
    emit hoverBodies(hovered);
    emit ghostBodies(ghosts);
    emit unpickableBodies(unpickable);
}

// The gizmo sits on the active object's world pose (a group: its centre, an array: its first copy).
void SceneBuilder::updateGizmoTarget() {
    const SceneObject* o = findObject(graph_, selectedId_);
    const SensorObject* sensor = sensorById(selectedId_);
    const bool onJoint = sensor && findJoint(graph_, sensor->parent); // its place is the joint's
    const bool editable = gizmoOnSelection() && o && effectivelyVisible(graph_, *o) && !o->locked && !onJoint;
    const bool live = !editing() && liveTargetValid_ && !gizmoEntity_;
    Vector3 p(0.0f);
    Quaternion q;
    if (o) worldPose(graph_, *o, p, q);
    const auto walking = goalLive_.find(selectedId_); // a goal on its path: the gizmo turned as the path has turned it
    for (const Entity& e : graph_.entities)
        if (live && e.id == selectedId_ && !e.path.empty() && walking != goalLive_.end()) q = walking->second.second;
    emit gizmoTarget(editable, live ? liveTarget_ : p, q);
}

// Which drawn body is which object (the frame says it for every body), and on pause the gizmo sits
// where the selected object's body is now, not where it was put.
void SceneBuilder::onSimulationSnapshot(const Frame& s) {
    if (editing()) return;
    if (const Camera* c = cameraById(throughId_); c && c->kind == CameraKind::Target && c->target) {
        for (const Frame::Body& b : s.bodies) liveObjectPositions_[b.object] = b.pos; // where the running world has its target
        for (const GoalVis& g : s.goals) liveObjectPositions_[g.id] = g.position;
        emitCameraView();
    }
    std::vector<uint32_t> bodyEntity(s.bodies.size());
    for (size_t b = 0; b < s.bodies.size(); ++b) { // an array's copy plays as the array (RobotWorld::objectIdOfBody)
        const uint32_t id = s.bodies[b].object;
        bodyEntity[b] = isArrayCopyId(id) ? arrayOfCopyId(id) : id;
    }
    if (bodyEntity != bodyEntity_) bodyEntity_ = std::move(bodyEntity), sendViewportFlags();
    playColliderGuides(s);
    if (gizmoEntity_) return;
    for (const GoalVis& g : s.goals) goalLive_[g.id] = {g.position, g.rotation};
    const int body = bodyOfEntity(selectedId_);
    bool valid = body >= 0 && body < int(s.bodies.size());
    Vector3 at = valid ? s.bodies[size_t(body)].pos : Vector3(0.0f);
    for (const SensorVis& v : s.sensors)
        if (v.id == selectedId_) valid = true, at = v.position; // a sensor: where its body carries it
    for (const GoalVis& g : s.goals)
        if (g.id == selectedId_) valid = true, at = g.position; // a goal on its path: where the path has taken it
    if (valid == liveTargetValid_ && (!valid || (at.x == liveTarget_.x && at.y == liveTarget_.y && at.z == liveTarget_.z))) return;
    liveTargetValid_ = valid;
    liveTarget_ = at;
    updateGizmoTarget();
}

int SceneBuilder::bodyOfEntity(uint32_t id) const {
    for (size_t b = 0; b < bodyEntity_.size(); ++b)
        if (id && bodyEntity_[b] == id) return int(b);
    return -1;
}

uint32_t SceneBuilder::entityOfBody(int body) const {
    return body >= 0 && body < int(bodyEntity_.size()) ? bodyEntity_[size_t(body)] : 0;
}

// ---------------------------------------------------------------------------
// The mouse in the viewport
// ---------------------------------------------------------------------------
// Each hit is what a click there selects (a group before its members; a sensor's glyph, the sensor:
// it is all of it there is to click, and its body is clicked on the body), each thing once, nearest first.
std::vector<PickHit> SceneBuilder::pickAll(const Ray& ray) {
    if (!editing()) return {};
    std::vector<PickHit> hits, all = editView_.pickAll(graph_, ray);
    for (const PickHit& h : pickMarkers(ray)) all.push_back(h); // lights and cameras: their wireframes
    std::sort(all.begin(), all.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    std::stable_partition(all.begin(), all.end(), [this](const PickHit& h) { return sensorById(h.id) != nullptr; }); // drawn over everything
    for (PickHit h : all) {
        h.id = sensorById(h.id) ? h.id : clickTarget(h.id);
        if (h.id == 0 || std::any_of(hits.begin(), hits.end(), [&](const PickHit& o) { return o.id == h.id; })) continue;
        h.name = objectTitle(h.id).toStdString();
        hits.push_back(h);
    }
    return hits;
}

void SceneBuilder::onEntityClicked(uint32_t id) { setSelected(id); }

void SceneBuilder::onEntityHovered(uint32_t id) {
    if (id == hoverId_) return;
    hoverId_ = id;
    sendViewportFlags();
}

void SceneBuilder::selectBody(int body) {
    if (body < 0 || body >= int(bodyEntity_.size())) return;
    const uint32_t id = clickTarget(bodyEntity_[size_t(body)]);
    const SceneObject* o = findObject(graph_, id);
    if (!o || o->locked) return;
    setSelected(id);
}

// ---------------------------------------------------------------------------
// Scenes and files
// ---------------------------------------------------------------------------
void SceneBuilder::newScene() {
    flushCode();
    if (!editing()) returnToEdit(false);
    if (!graph_.entities.empty() || !graph_.groups.empty() || !graph_.arrays.empty() || !graph_.lights.empty() || !graph_.cameras.empty()) remember();
    graph_ = SceneGraph();
    selection_.clear();
    fileName_.clear();
    throughId_ = 0;
    Entity floor;
    floor.id = newId();
    floor.name = "Пол";
    floor.shape = ShapeKind::Plane;
    floor.size = Vector3(graph_.world.size.x, 0.02f, graph_.world.size.z);
    floor.position = Vector3(0.0f, 0.01f, 0.0f);
    floor.color = Vector3(0.52f, 0.55f, 0.60f);
    floor.collider.enabled = true; // a collider alone: a fixed obstacle, as Unity's static colliders
    floor.locked = true;           // clicks on the floor select nothing; it is still in the list
    graph_.entities.push_back(floor);
    selectedId_ = 0;
    fillWorld();
    refreshList();
    fillInspector();
    refreshView();
    frameObjects();
    codeAction("scene_new", "scene.new()", {}, false);
}

bool SceneBuilder::openFile(const QString& path, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    SceneGraph g;
    std::string err;
    const QByteArray text = f.readAll();
    if (!g.load(QString::fromUtf8(text).toStdString(), err)) {
        error = QString::fromStdString(err);
        return false;
    }
    if (!editing()) returnToEdit(false);
    flushCode();
    remember();
    graph_ = std::move(g);
    graph_.baseDirectory = QFileInfo(path).absolutePath().toStdString(); // model files next to the scene
    adoptIds();
    fileName_ = QFileInfo(path).fileName();
    throughId_ = 0;
    selection_.clear();
    selectedId_ = 0;
    fillWorld();
    refreshList();
    fillInspector();
    refreshView();
    frameObjects();
    const QString file = QFileInfo(path).absoluteFilePath();
    codeAction("scene_open", "scene.open(" + QString::fromUtf8(QJsonDocument(QJsonArray{file}).toJson(QJsonDocument::Compact)).mid(1).chopped(1) + ")",
               {{"file", file}}, false);
    return true;
}

QString SceneBuilder::snapshotText() const { return QString::fromStdString(graph_.save()); }

bool SceneBuilder::restoreSnapshot(const QString& text, const QString& baseDirectory, uint32_t nextId, QString& error) {
    SceneGraph g;
    std::string err;
    if (!g.load(text.toStdString(), err)) return error = QString::fromStdString(err), false;
    if (!editing()) returnToEdit(false);
    flushCode();
    remember();
    graph_ = std::move(g);
    graph_.baseDirectory = baseDirectory.toStdString();
    nextId_ = std::max(nextId, ke::nextId(graph_));
    selection_.clear();
    selectedId_ = 0;
    fillWorld();
    refreshList();
    fillInspector();
    refreshView();
    coded_ = graph_; // the restored scene is said by the journal's line, not as edits by hand
    return true;
}

// Into another folder ("Save as"), the models' and textures' relative paths are written from there (rebaseFiles).
// Written whole or not at all: a failed save leaves the old file as it was.
bool SceneBuilder::saveFile(const QString& path, QString& error) const {
    SceneGraph saved = graph_;
    rebaseFiles(saved, QFileInfo(path).absolutePath().toStdString());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return error = f.errorString(), false;
    f.write(QString::fromStdString(saved.save()).toUtf8());
    if (!f.commit()) return error = f.errorString(), false;
    return true;
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
int SceneBuilder::indexOf(uint32_t id) const {
    if (id == 0) return -1;
    for (size_t i = 0; i < graph_.entities.size(); ++i)
        if (graph_.entities[i].id == id) return int(i);
    return -1;
}

Entity* SceneBuilder::selectedEntity() {
    const int i = indexOf(selectedId_);
    return i >= 0 ? &graph_.entities[size_t(i)] : nullptr;
}

uint32_t SceneBuilder::newId() { return nextId_++; }

// After an undo or a Stop: what is gone from the graph is gone from the selection too.
void SceneBuilder::pruneSelection() {
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [this](uint32_t id) { return !findObject(graph_, id) && !isDevice(id); }),
                     selection_.end());
    if (!isSelected(selectedId_)) selectedId_ = selection_.empty() ? 0 : selection_.back();
}

// Files written by hand or by an older editor may have no ids, or the same id twice.
void SceneBuilder::adoptIds() {
    nextId_ = std::max(nextId_, nextId(graph_)); // groups and arrays too
    std::set<uint32_t> seen;
    for (Entity& e : graph_.entities) {
        if (e.id == 0 || seen.count(e.id)) e.id = newId();
        seen.insert(e.id);
    }
}
