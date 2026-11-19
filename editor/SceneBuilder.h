#pragma once
// The scene builder ("Конструктор"): a scene made without code, in two modes, as in Unity, Unreal or
// Blender.
//   ПРАВКА (edit, the default) - a plain 3D editor. The big buttons create GEOMETRY ONLY: a shape
//     with no role that stands where it was put; nothing moves, nothing falls, nothing is simulated.
//     Objects are selected with the mouse (again on the same spot: the one behind), moved, turned
//     and scaled with the gizmo or the keyboard, and given components - a rigid body, a collider,
//     joints, drives, sensors. In edit mode a component only marks the object.
//   ▶ ПУСК (play) - the robot's world is built from the graph once and runs; ⏸ pauses it; ■ СТОП
//     throws it away and shows the scene exactly as it was before Play (the arm back in its pose,
//     the cube back in place). As in Unity, edits made while playing are not kept after Stop.
//
// The scene has three layers: SOURCES (geometry: never destroyed by physics), OBJECTS (a source with
// a pose and roles - what the gizmo and the inspector edit) and META-OBJECTS (the bodies and joints
// of the physics engine a role makes when the scene plays, ke::RobotWorld). While the scene plays the
// inspector stays live where the running world can take an edit in place: every edit goes through
// applyEditDuringPlay(), which compares the graph with what the simulation was last given - a joint's
// drive (a servo's target, an IK goal dragged) goes to the running joints at once, the lights to the
// picture; anything else the world is built of (a body, a collider, the world itself) needs the
// scene built again: then the builder stops with the edit kept and says why. On pause the gizmo works
// on the object where it is now.
//
// The builder owns an ke::SceneGraph (saved as *.kscene) and shows two tabs, as Unity's hierarchy
// and inspector: "Сцена" (what is there, the eye and the lock of each thing, and the world) and
// "Объект" (the selected thing, top to bottom: a quick palette of components, the "Объект" card
// every thing has - name, visibility, lock, colour, position, rotation, size -, the "Геометрия" card
// - what it looks like, which physics never changes -, then only the COMPONENTS it has, each a card
// with an ✕, and "+ Добавить компонент"). The collider is a component of its own: alone it makes a
// fixed obstacle, with "Твёрдое тело" a moving body; adding "Твёрдое тело" adds a collider "Авто",
// as Unity's primitives come with theirs. The collider is drawn as a thin green wireframe over the
// geometry (Houdini's collision guide): always for the selected object, for all with "Коллайдеры".
//
// Many objects at once: the selection may hold several objects, and a thing in
// the scene is an ke::SceneObject - a shape (Entity), a group (Ctrl+G: its members move with it) or
// an array (one object standing for a row, a grid or a circle of copies of a template shape). An
// instance shares the geometry and components of its master: editing one edits all. Shift held
// when a gizmo drag starts clones the selection (SceneBuilderClone.cpp); the inspector edits every
// selected object at once, a number that differs between them showing "—" (SceneBuilderMulti.cpp).
//
// Lights and cameras (SceneBuilderLights.cpp) are objects too, with no geometry and no physics: the
// toolbar's "Свет ▾" (Солнце, Лампа, Прожектор) and "Камера" make them, the viewport draws them as
// wireframes (SceneMarkers.h) that the mouse picks and the gizmo moves and turns (a scale does
// nothing to them), the "Свет" and "Камера" cards edit them, and "Смотреть через камеру" makes the
// view that camera: moving the view then moves the camera (Blender's "lock camera to view").
#include "ColliderGuides.h"
#include "EditView.h"
#include "Gizmo.h"
#include "Icons.h"
#include "SceneMarkers.h"
#include "scene/Kinematics.h"
#include "SimController.h"
#include "scene/SceneGraph.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRectF>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ComponentCard;
class GroupPanel;
class InlineBanner;
class ObjectInspector;
class QAction;
class IkSolverView;
class QCheckBox;
class TexturePanel;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QTableWidget;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class RoleBar;
class Vec3Row;

class SceneBuilder : public QObject {
    Q_OBJECT
public:
    enum class Mode { Edit, Playing, Paused };
    // What a Shift + gizmo drag makes (the "Клонировать" popover): independent copies, instances of
    // the originals (one geometry, one set of components), or one array per original.
    enum class CloneKind { Copy, Instance, Array };

    explicit SceneBuilder(SimController* ctrl, QObject* parent = nullptr);

    QWidget* sceneListPanel() const { return listPanel_; }
    QWidget* inspectorPanel() const { return inspectorPanel_; }
    QWidget* worldPanel() const;
    const std::vector<QAction*>& createActions() const { return createActions_; } // Куб, Сфера ...
    QAction* undoAction() const { return undoAct_; }
    QAction* redoAction() const { return redoAct_; }
    QAction* deselectAction() const { return deselectAct_; }

    // The window's actions as code (SceneBuilderCode.cpp), as Blender's Info shows the operator a button
    // ran: an action as the one call of the object model that does it (scene.add("box"), a gizmo drag as
    // .move(...)), a click's selection and the tool shelf as editor.select(...) / editor.gizmo.tool, an
    // edit of a field a moment after it as the core edits it was (ke::diffEdits) - executable, replayed
    // they make the same scene - and play, stop, undo as the operations they are. The reporter
    // (MainWindow: EditorOps::report) puts them in the journal and the API console. An edit the API made
    // is already its own line: between apiCallStarted and apiCallFinished nothing is reported twice.
    void setCodeReporter(std::function<void(const QString& op, const QJsonObject& args)> report) { reportCode_ = std::move(report); }
    void apiCallStarted();
    void apiCallFinished();

    void newScene();                         // a floor and nothing else, in edit mode
    void assignImage(uint32_t id, const QString& file);
    bool openFile(const QString& path, QString& error);
    bool saveFile(const QString& path, QString& error) const;
    bool importModel(const QString& path, QString& error); // an OBJ / STL model as geometry
    // The scene as a snapshot of the journal (EditorJournal.cpp): the scene file, its folder, the next id the builder gives out - all a replay needs to give the same ids; and back, as one
    // undo step (a restore can be undone).
    QString snapshotText() const;
    uint32_t nextIdToGive() const { return nextId_; }
    bool restoreSnapshot(const QString& text, const QString& baseDirectory, uint32_t nextId, QString& error);
    const ke::SceneGraph& graph() const { return graph_; }
    static std::string sceneName() { return "Конструктор"; } // the simulation's name of the builder's scene

    // Play / pause / stop / one step.
    Mode mode() const { return mode_; }
    bool editing() const { return mode_ == Mode::Edit; }
    bool gizmoAllowed() const { return mode_ != Mode::Playing; } // edit mode, or paused
    // IK goals alone are selected: handles, as MuJoCo's mocap bodies - the simulation never moves them, so their
    // gizmo works while the scene plays too, and the arm follows by its motors (followGoals).
    bool goalSelected() const;
    bool gizmoOnSelection() const { return gizmoAllowed() || goalSelected(); }
    int bodyOfEntity(uint32_t id) const;   // the first drawn body of an object (-1: none)
    uint32_t entityOfBody(int body) const; // the drawn id of a body: an entity, an array's copy... (0: none)
    // Play mode: the simulation's newest picture (the live pose of the selected object on pause).
    void onSimulationSnapshot(const ke::Frame& s);
    void play();
    void pause();
    void stop();
    void step();
    // K in play or on pause (SceneBuilderKeep.cpp, Unreal's "Keep Simulation Changes"): the live poses
    // of the selected objects (all, if none is selected) stay after Stop; one undo step after Stop.
    void keepSimulationPoses();

    // Edit mode, from the viewport: what the mouse ray passes through, clicks, hover, the gizmo.
    // A hit's id is what a click there selects (clickTarget): the topmost group of the thing first;
    // with that group selected, the next level down, and so on to the thing itself.
    std::vector<PickHit> pickAll(const Ray& ray);
    uint32_t clickTarget(uint32_t drawnId) const;
    bool drawnSelected(uint32_t drawnId) const; // the thing, or a group or array it belongs to, is selected
    void onEntityClicked(uint32_t id);
    void onEntityHovered(uint32_t id);
    void onGizmoStarted();
    void onGizmoMoved(const GizmoPose& pose);
    // A controller of the scene by id (SceneBuilderControllers.cpp; null: none).
    const ke::ControllerObject* controllerById(uint32_t id) const;
    void onGizmoFinished();
    void onGizmoCancelled();
    // A line of the window said for what lives outside the builder (the tool shelf: the gizmo's tool).
    void sayAction(const QString& op, const QString& py, QJsonObject args = {}) { codeAction(op, py, std::move(args)); }
    void onCloneDragStarted(); // Shift was held when the drag began: the drag moves copies (SceneBuilderClone.cpp)
    void selectBody(int body); // play mode: a click on a simulated body selects its object

    // What the context menu does to the selected object.
    void removeSelected();
    void duplicateSelected();
    void toggleRole(RoleIcon role);
    void toggleVisible(uint32_t id);
    void toggleLocked(uint32_t id);
    void frameSelected(); // the camera shows the selected objects (F); nothing selected: all
    void frameAll() { frameObjects(); } // Home
    // The collider wireframes of all objects (the toolbar's "Коллайдеры"); the selected one always.
    void setShowAllColliders(bool on);
    // The first-minute hint and the like: one sentence and a button that fixes the cause.
    void showBanner(const QString& text, const QString& buttonText = {}, std::function<void()> fix = {});

    // The selection: one object is active (the inspector and the gizmo show it); Shift / Ctrl + click
    // and the selection box add more. Delete, duplicate, hide and the gizmo act on all of them
    // (SceneBuilderSelection.cpp).
    const std::vector<uint32_t>& selection() const { return selection_; }
    bool isSelected(uint32_t id) const;
    void toggleSelected(uint32_t id); // Shift / Ctrl + click
    void selectAll();                 // Ctrl+A: every visible, unlocked object
    void selectInRect(const QRectF& rect, const GizmoView& view, bool add, bool remove); // the box
    void hideSelected();              // H
    void unhideAll();                 // Alt+H
    void selectSimilar();             // "Выбрать все такие же": same shape and components, or the same master
    void selectWithRole(RoleIcon role); // "Выбрать все с компонентом"
    void invertSelection();           // "Инвертировать выбор"

    // Many objects at once (SceneBuilderObjects.cpp). Each is one undo step.
    void groupSelected();             // Ctrl+G: a group at the centre of the selection, poses kept in the world
    void ungroupSelected();           // Ctrl+Shift+G
    void setGlued(uint32_t groupId, bool glued); // "Склеить в одно тело": the members become one rigid body
    void makeArrayOfSelected();       // "Сделать массивом…": each selected shape becomes the template of a row of 5
    void explodeArray(uint32_t id);   // "Разобрать на объекты": every copy an object of its own
    void editArray(uint32_t id, const std::function<void(ke::ArrayObject&)>& change);
    void unlinkInstance(uint32_t id); // "Отвязать": the instance gets its own geometry and components
    const ke::SceneObject* object(uint32_t id) const { return ke::findObject(graph_, id); }
    QString objectTitle(uint32_t id) const; // "Куб 1", "Группа 1", "Массив: 5 × Куб 1"
    uint32_t masterOf(uint32_t entityId) const; // the entity whose geometry an instance shows (itself if none)

    // Shift + gizmo = clone (SceneBuilderClone.cpp). After the drag the copy stands where it was
    // dragged; the popover asks how many and of what kind, and then the originals stay where they
    // were and the copies repeat the same step: moved d -> d, 2d, 3d...; turned a -> a, 2a... about
    // the gizmo's centre; scaled s -> s, s², s³... The whole clone is one undo step.
    bool clonePending() const { return clonePending_; }
    bool cloneAllows(CloneKind kind) const; // an array or an instance cannot repeat a scale
    void finishClone(int copies, CloneKind kind);
    void cancelClone();

    // Lights and cameras (SceneBuilderLights.cpp). A new light or camera is one undo step.
    const std::vector<QAction*>& lightActions() const { return lightActions_; } // Солнце, Лампа, Прожектор
    QAction* cameraAction() const { return cameraAct_; }
    // «Датчик ▾» of the top bar: the six kinds (ke::SensorKind order), each put on the selected shape or on the joint
    // it hangs on; updateSensorActions greys out what does not fit and says why.
    const std::vector<QAction*>& sensorActions();
    void updateSensorActions();
    // In the air over a free spot, named after its shape ("Куб 2"); `adjust` changes it before it joins.
    void addEntity(ke::ShapeKind shape, const std::function<void(ke::Entity&)>& adjust = {});
    void setRoleOfSelected(RoleIcon role, bool on); // the role bar's click with its outcome given
    // Inverse kinematics in the window (SceneBuilderSelection.cpp): «Цель IK» makes a goal cube at the selected
    // link (ke::addGoal); dragging a goal drives its arm (ke::reachGoal).
    void addGoalToSelected();
    void refreshIkView(); // an IK goal selected: its solver's last solve under its card, its gripper's button
    void toggleGrip();    // the selected goal's gripper closed or opened: assembly_grip
    void dragGoal(uint32_t id, ke::Vector3 position, ke::Quaternion turn, bool end); // the mouse drags a goal in play: assembly_goal_pose
    void refreshPaths();  // the goals' paths to the view
    void takeGoalFromPath(uint32_t id);
    // An operation moved goals (scene_move, scene_turn: the core solved them): the solve done again on the scene
    // before it, the goal at its new pose - the same bits, its rounds for the card.
    void noteGoalSolves(const ke::SceneGraph& before);
    IkSolverView* ikView_ = nullptr;
    QPushButton* grip_ = nullptr; // «Схват: сжать / разжать» under an IK goal whose hand has fingers
    ke::IkResult lastIk_;      // the last solve a goal's drag made, of goal lastIkGoal_
    uint32_t lastIkGoal_ = 0;
    void followGoals(const std::vector<uint32_t>& moved);
    void addLight(ke::LightKind kind, const ke::Vector3* at = nullptr); // over `at`, else the selection or the scene's centre
    ke::Vector3 spawnTarget();          // the selection's place, else the centre of the shapes
    void addCamera(const ke::Vector3* eye = nullptr); // at `eye`, else the editor's eye, looking at the scene's centre
    void setViewEye(std::function<ke::Vector3()> eye) { viewEye_ = std::move(eye); }
    void editLight(uint32_t id, const std::function<void(ke::Light&)>& change);
    void editCamera(uint32_t id, const std::function<void(ke::Camera&)>& change);
    // The view through a camera: it becomes the scene's active camera and the viewport shows what it
    // sees; 0 gives the editor's own view back. Not an undo step (the view is no edit of the scene).
    void lookThrough(uint32_t cameraId);
    void lookThroughActiveCamera(); // the active camera, else the first one (the command line's --camera)
    uint32_t lookingThrough() const { return throughId_; }
    // Looking through a camera the view moved: the camera goes where the view is now.
    void onViewMovedThroughCamera(const ke::Vector3& eye, const ke::Vector3& forward, const ke::Vector3& up);

    // Scripted edits (the self-test, later a script console): one undo step each.
    void select(uint32_t id) { setSelected(id); }
    void selectMany(const std::vector<uint32_t>& ids) { setSelection(ids, ids.empty() ? 0 : ids.back()); }
    std::vector<uint32_t> orderedSelection() const; // the selection with the active object last, as selectMany takes it
    uint32_t selectedId() const { return selectedId_; }
    void editEntity(uint32_t id, const std::function<void(ke::Entity&)>& change);
    // The API (EditorOps, SceneBuilderApi.cpp): an edit made of the core's edits (ke::SceneEdits) is one undo
    // step; when `change` fails the graph is put back as it was and no step is left.
    bool transact(const std::function<bool(ke::SceneGraph&, std::string&)>& change, std::string& error);
    // The shape under a ray as drawn (not its group: the magnet needs the shape itself) and the point hit.
    bool pickShape(const Ray& ray, uint32_t& id, ke::Vector3& point, ke::Vector3* normal = nullptr);
    // Edit mode: the simulation built from the graph and left on pause, not one frame stepped (the
    // API steps exact frame counts from there). False when it cannot be built.
    bool loadPaused();
    void undoStep() { undo(); }
    void redoStep() { redo(); }

signals:
    void statusMessage(const QString& text);               // "Коллайдер добавлен: без него тело провалится"
    void simulationKept(int objects);                      // K took the live poses of this many objects
    void modeChanged();                                    // edit / playing / paused
    void editSnapshot(std::shared_ptr<const ke::Frame> s); // edit mode: what to draw
    void highlightBodies(const std::vector<int>& bodies);  // the bodies of the selected thing
    void hoverBodies(const std::vector<int>& bodies);      // the body under the mouse
    void ghostBodies(const std::vector<int>& bodies);      // geometry without a role (edit mode)
    void unpickableBodies(const std::vector<char>& flags); // per body: locked, the mouse passes through
    void gizmoTarget(bool visible, ke::Vector3 position, ke::Quaternion rotation);
    void frameRequested(const ke::AABB& box);              // show these objects (after a load or an add)
    void selectionChanged(uint32_t id);                    // the "Объект" tab shows it (0: nothing selected)
    void colliderGuides(std::vector<float> lines);         // green wireframes: pairs of points, x y z 0 each
    void pathLines(std::vector<float> lines);              // the IK goals' paths: segments and the waypoints' crosses
    void firstAction();                                    // the user did something: the first-start hint goes
    void sceneMarkers(std::vector<SceneMarker> markers);   // the lights and cameras to draw
    // The view through a camera (on) or the editor's own view (off).
    void cameraView(bool on, ke::Vector3 eye, ke::Vector3 forward, ke::Vector3 up, float fovDeg, float nearClip, float farClip);

private:
    // Building the panels
    void buildActions();
    void buildSceneList();
    void buildWorldPanel();
    void buildInspector();
    void buildHeader(QVBoxLayout* col);
    void buildObjectCard(QVBoxLayout* col);        // what every thing has, and the size
    void buildGeometryCard(QVBoxLayout* col);      // what it looks like
    void buildTextureCard(QVBoxLayout* col);       // how its surface looks
    void buildMaterialCards(QVBoxLayout* col);     // rigid (+ the no-collider warning)
    void buildColliderCard(QVBoxLayout* col);
    void buildAddComponent(QVBoxLayout* col);      // "+ Добавить компонент" and its menu
    void buildManyCards(QVBoxLayout* col);         // "Группа", "Массив" and the instance's link
    void fillAddMenu();                            // the components the object does not have yet
    ComponentCard* componentCard(QVBoxLayout* col, RoleIcon role);
    class FieldForm* shapeFields(ComponentCard* card, class FieldForm* SceneBuilder::*slot); // over the selected shapes' masters
    class FieldForm* arrayFields();
    Vec3Row* vectorField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals);

    // Editing
    void applyEdit(uint32_t entityId);          // every edit calls this after: edit mode redraws, play mode updates
    void applyEditDuringPlay(uint32_t entityId); // an edit while the simulation runs (see the top)
    void reloadNeeded(const QString& why);
    void onObjectEdited();
    void onDetailsEdited();
    void onWorldEdited(const ke::WorldSettings& settings);
    void onListClicked(QTreeWidgetItem* item, int column);
    void setSelected(uint32_t id);                              // just this one (0: nothing)
    void setSelection(std::vector<uint32_t> ids, uint32_t active); // every change of the selection goes here
    void syncListSelection();                                   // the scene list shows the selection

    // Graph <-> widgets
    void fillInspector();
    void fillManyCards(const ke::SceneObject& o); // the group, array and instance cards of the active object
    void fillDetails();
    void readDetails(ke::Entity& e) const;
    void fillWorld();
    void refreshList();
    void refreshHeader();
    void refreshSections();
    void refreshBanner();       // what does not work for the selected object, and the fix
    void showTransform(const ke::SceneObject& o); // the gizmo moved it: the numbers follow, not an edit

    // Several selected (SceneBuilderMulti.cpp): the widgets show the active object and "—" where
    // the selected differ; an edit is what differs from what the widgets showed, and goes to all.
    ke::Entity readWidgets() const;        // what the widgets show now, as an entity
    void markMixed();                      // "—" in every field whose value differs between the selected
    void applyWidgetEdit(bool details);    // the changed fields into every selected object
    std::vector<ke::Entity*> selectedEntities(); // the selected shapes (instances: their own objects)
    std::vector<RoleIcon> commonRoles() const;   // the components every selected shape has

    // Lights and cameras (SceneBuilderLights.cpp)
    void buildLightActions();
    void buildLightCards(QVBoxLayout* col);
    void fillLightCards(const ke::SceneObject& o);
    void addObjectDone(uint32_t id);            // select it, list it, show it
    void refreshLightsAndCameras();             // the wireframes and the view through a camera
    void refreshMarkers();
    void emitCameraView();
    std::vector<PickHit> pickMarkers(const Ray& ray) const;
    QIcon kindIcon(uint32_t id, int size) const; // a light's or a camera's icon (null: neither)
    ke::Light* lightById(uint32_t id);
    ke::Camera* cameraById(uint32_t id);

    // Sensors (SceneBuilderSensors.cpp): one put on the selected shape (an encoder on its joint, an IMU
    // flat on its top, a sonar out of the face toward the view), their card, and what they read while
    // the scene plays. The electrics (SceneBuilderDevices.cpp): batteries and their drivers as rows of
    // the scene list and their cards - a driver's duty set by hand while the car drives.
    void addSensor(ke::SensorKind kind);
    void fillSensorMenu(const ke::Entity& e);   // the "Датчики" part of "+ Добавить компонент"
    void fillJointMenu(const ke::Entity& e);    // its «Сочленение с приводом» part (SceneBuilderActuators.cpp)
    void addActuator(uint32_t link, uint32_t other, bool rotary, int axis); // assembly_actuator about / along a world axis
    void buildSensorCard(QVBoxLayout* col);
    void fillSensorCard(const ke::SceneObject& o);
    // A card's draft, edited (FieldForm), goes into the scene as one undo step - a typed number joins the next one typed;
    // a click (a choice, a tick) fills the inspector again, where other fields may show now.
    template <class T> void commitDraft(T* live, const T& draft, bool click) {
        if (filling_ || !live) return;
        remember(!click);
        *live = draft;
        afterCardEdit(live->id, click);
    }
    // A card over every selected thing of its kind (`find`: what an id stands for, else null): fill shows the active
    // one and "—" where they differ; an edit writes the one field into each of them, the rest of each stays its own.
    template <class Form, class T, class Find> void fillSelected(Form* form, T& draft, Find find) {
        std::vector<T*> all{find(selectedId_)};
        if (!all.front()) return;
        for (uint32_t id : selection_)
            if (T* x = find(id); x && id != selectedId_) all.push_back(x);
        form->fill(int(all.size()), [&](int i) { draft = *all[size_t(i)]; });
    }
    template <class Form, class T, class Find> void commitSelected(const Form* form, T& draft, Find find, bool click) {
        if (filling_ || !find(selectedId_)) return;
        remember(!click);
        for (uint32_t id : selection_)
            if (T* x = find(id)) draft = *x, form->repeat(), *x = draft;
        draft = *find(selectedId_);
        afterCardEdit(selectedId_, click);
    }
    ke::Entity* masterById(uint32_t id); // the shape an id shows (an instance's master), else null
    void afterCardEdit(uint32_t id, bool click);
    void pollLive();                            // the selected sensor's, battery's or driver's readings
    ke::SensorObject* sensorById(uint32_t id);
    void addDeviceRows();                       // into the scene list: the batteries, their drivers under them
    bool isDevice(uint32_t id) const;           // a battery or a driver (no SceneObject)
    void buildDeviceCards(QVBoxLayout* col);
    void fillDeviceCards();
    // Joints as meta-objects (SceneBuilderJoints.cpp): rows in the list between the bodies they tie, a card of their own.
    ke::JointObject* jointById(uint32_t id);
    void addJointRows(std::map<uint32_t, QTreeWidgetItem*>& rows, std::map<uint32_t, uint32_t>& under);
    void buildJointCard(QVBoxLayout* body);
    void fillJointCard();
    // The controller's card (SceneBuilderControllers.cpp): its actions with their speeds, its bindings with «Назначить» -
    // a press of the keys the binding is to take, as a game's settings rebind.
    struct BindCapture { uint32_t controller = 0; size_t action = 0, binding = 0; std::vector<std::string> got; QPushButton* button = nullptr; };
    BindCapture capture_;
    ComponentCard* controllerCard_ = nullptr;
    QLabel* controllerInfo_ = nullptr;
    QTableWidget *controllerActions_ = nullptr, *controllerBinds_ = nullptr;
    void buildControllerCard(QVBoxLayout* body);
    void fillControllerCard();
    void fillControllerBinds(const ke::ControllerObject& c);
    void onControllerSpeed(size_t action, double speed);
    void startCapture(size_t action, size_t binding, QPushButton* button);
    void captureControl(const std::string& control); // a key for the binding being set
    void finishCapture(bool keep);
    ke::ControllerObject* controllerIn(uint32_t id);
    bool eventFilter(QObject* watched, QEvent* event) override; // the keys while a binding is being set

    // History: every change can be undone (Ctrl+Z) and redone (Ctrl+Shift+Z)
    void remember(bool mergeWithLast = false);
    void undo();
    void redo();
    void restore(ke::SceneGraph g);
    void updateHistoryActions();

    // What the viewport shows
    void setMode(Mode m);
    void returnToEdit(bool keepEdits); // throw the simulation away, show the scene (as before Play unless kept)
    bool loadIntoSimulation();  // build the simulation from the graph (play, or a step from edit)
    void refreshView();         // edit mode: draw the graph as it is now
    std::string displayName() const;
    void frameObjects(); // asks the viewport to show the things of the scene, not the empty world
    void sendViewportFlags();
    void updateGizmoTarget();
    // The collider wireframes: from the graph in edit mode, at the bodies' poses while the scene plays.
    void refreshColliderGuides();
    void playColliderGuides(const ke::Frame& s);
    // The collider of an entity as the running world will build it (ke::colliderShapes), kept until its
    // geometry or collider changes; `key` gets what it depends on (the guides' cache key).
    const std::vector<ke::ShapeDesc>& colliderOf(const ke::Entity& e, std::string* key = nullptr);
    std::map<uint32_t, std::pair<std::string, std::vector<ke::ShapeDesc>>> colliders_;

    // Lookups (SceneBuilderObjects.cpp for the objects)
    int indexOf(uint32_t id) const;
    ke::Entity* selectedEntity();
    ke::SceneObject* selectedObject();
    ke::SceneObject* objectById(uint32_t id);
    ke::Entity* entityById(uint32_t id);
    ke::Group* groupById(uint32_t id);
    ke::ArrayObject* arrayById(uint32_t id);
    uint32_t ownerOf(uint32_t drawnId) const;        // an array's copy -> the array; anything else itself
    std::vector<uint32_t> chainOf(uint32_t id) const; // the object, its group, that group's group... to the top
    bool isInside(uint32_t id, uint32_t ancestor) const; // id is the ancestor or somewhere under it
    std::vector<uint32_t> childrenOf(uint32_t id) const;
    std::vector<uint32_t> selectionRoots() const;   // selected, unlocked, and no selected group above
    std::vector<uint32_t> topLevelObjects() const;  // visible, unlocked things not in a group
    bool isHiddenTemplate(const ke::Entity& e) const; // the hidden shape an array copies
    ke::AABB boundsOf(uint32_t id);                 // the world box of what an object draws
    ke::Vector3 worldPositionOf(uint32_t id) const;
    // A core action on the graph (ke::SceneActions): its new ids go on from the editor's, never one this
    // session gave out before (an undo may bring that object back).
    template <class F>
    auto coreEdit(F&& action) {
        graph_.idFloor = nextId_;
        auto result = action(graph_);
        nextId_ = std::max(nextId_, ke::nextId(graph_));
        return result;
    }
    // A Shift + gizmo drag's copies (SceneBuilderClone.cpp): copy k of an original, moved and turned
    // k times by the drag's step (from0 -> to), scaled by the factors to the k-th power.
    struct CloneStep {
        ke::Vector3 from, to;
        ke::Quaternion fromRotation, toRotation;
        ke::Vector3 scale{1.0f};
        bool turns = false;
    };
    uint32_t placeClone(uint32_t original, const CloneStep& step, int k, bool instance);
    uint32_t cloneAsArray(uint32_t original, const CloneStep& step, int copies);
    uint32_t newId();
    void pruneSelection(); // drops what is no longer in the graph
    void adoptIds(); // give ids to entities that came without one (old files); nextId_ past all

    SimController* ctrl_;
    ke::SceneGraph graph_;
    ke::SceneGraph playBackup_;    // the graph as it was when Play was pressed: Stop brings it back
    ke::SceneGraph simGraph_;      // what the simulation was last given (edits during play are diffed with it)
    bool hasPlayBackup_ = false;
    // K (SceneBuilderKeep.cpp): a live pose read on the simulation's thread, and the scene as it was
    // before the first K of this play - Stop puts it into the history as one undo step.
    struct KeptPose {
        uint32_t id = 0;
        ke::Vector3 position{0.0f};
        ke::Quaternion rotation;
    };
    void applyKeptPoses(const std::vector<KeptPose>& poses);
    ke::SceneGraph keptBefore_;
    bool hasKeptBefore_ = false;
    size_t undoAtPlay_ = 0;        // the history's length at Play: steps made while playing go on Stop
    ke::Vector3 liveTarget_{0.0f}; // on pause: where the selected object's body is now
    bool liveTargetValid_ = false;
    std::map<uint32_t, std::pair<ke::Vector3, ke::Quaternion>> goalLive_; // the goals on paths, where the scene has walked them
    Mode mode_ = Mode::Edit;
    EditView editView_;
    uint32_t nextId_ = 1;          // ids are never reused within a session
    uint32_t selectedId_ = 0;      // 0: nothing selected
    uint32_t hoverId_ = 0;         // the object under the mouse (edit mode)
    QString fileName_;
    bool filling_ = false;         // widgets are being filled from the graph: not an edit
    std::vector<uint32_t> bodyEntity_; // drawn body -> id of its entity (edit view, or the simulation)

    struct HistoryEntry { ke::SceneGraph graph; };
    std::vector<HistoryEntry> undo_, redo_;
    QElapsedTimer lastRemember_;
    bool goalDragging_ = false; // the mouse drags a goal (dragGoal): one undo step, its code said as it ends
    bool typing_ = false; // the last step was a typed number's: another typed within 0.7 s joins it, nothing else does

    ColliderGuides guides_;
    bool showAllColliders_ = false;

    // The gizmo: the active object it drags, every dragged object's world pose when the drag began,
    // and the whole graph then (Esc puts it back; a scale is always from the start sizes).
    struct GizmoStart {
        ke::Vector3 position;
        ke::Quaternion rotation;
    };
    uint32_t gizmoEntity_ = 0;
    std::map<uint32_t, GizmoStart> gizmoStarts_;
    GizmoStart gizmoFrom_;         // the active object's world pose when the drag began (the gizmo's own start)
    // A drag as the numbers of its line: a step in the world, a turn (a world axis named exactly when it is
    // one) in degrees, factors. The hand applies exactly these, so the line replays it to the bit.
    struct GizmoStep {
        GizmoMode mode = GizmoMode::Select;
        ke::Vector3 by{0.0f}, axis{0.0f, 1.0f, 0.0f}, scale{1.0f};
        float degrees = 0.0f;
    };
    GizmoStep gizmoStep_;
    GizmoStep stepOf(const GizmoPose& pose) const;
    void applyGizmo(const GizmoStep& step);
    std::vector<uint32_t> gizmoIds() const; // what the drag moves, the active object last
    QString gizmoLine(QString& op, QJsonObject& args) const; // the drag just done as the line of its operation
    ke::SceneGraph gizmoGraph_;
    ke::SceneGraph gizmoPoses_;    // the poses and sizes the drag starts from (a clone drag's copies among them)
    GizmoPose lastPose_;
    // A clone drag: each original and the copy of it the gizmo moves; after the drag, the popover.
    std::vector<std::pair<uint32_t, uint32_t>> cloneOf_;
    bool cloneDrag_ = false, clonePending_ = false;
    std::vector<uint32_t> selection_; // every selected object, the active one (selectedId_) among them
    ke::Entity shown_;                // the widgets as they were filled: an edit is what differs from it

    std::vector<QAction*> createActions_;
    std::vector<QAction*> lightActions_;
    std::vector<QAction*> sensorActions_;
    QString sensorMisfit(ke::SensorKind kind) const; // why the selection takes no such sensor ("": it does)
    QAction* cameraAct_ = nullptr;
    std::function<ke::Vector3()> viewEye_; // where the editor's view is (a new camera stands there)
    uint32_t throughId_ = 0;               // the camera the view looks through (0: the editor's view)
    QAction *undoAct_ = nullptr, *redoAct_ = nullptr, *deselectAct_ = nullptr;

    // Scene list panel
    QWidget* listPanel_ = nullptr;
    QTreeWidget* list_ = nullptr;
    class WorldPanel* worldPanel_ = nullptr;

    // Inspector panel
    QWidget* inspectorPanel_ = nullptr;
    QWidget* inspectorBody_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QLabel* playHint_ = nullptr;     // "Остановите ■, чтобы править"
    InlineBanner* banner_ = nullptr; // why something does not work, and the fix
    bool stickyBanner_ = false;      // a message about the whole scene (a restart needed): kept until ▶
    QLabel* headerIcon_ = nullptr;
    QLabel* headerName_ = nullptr;
    QWidget* headerChips_ = nullptr; // small role icons next to the name
    RoleBar* roleBar_ = nullptr;
    ObjectInspector* object_ = nullptr;
    QComboBox* shape_ = nullptr;
    QLabel* modelFile_ = nullptr;
    TexturePanel* texture_ = nullptr;
    Vec3Row* size_ = nullptr;
    ComponentCard* cards_[int(RoleIcon::Count)] = {};
    QWidget* sizeRow_ = nullptr;     // the size: a shape's only
    QWidget* shapePart_ = nullptr;   // "Геометрия", the components and "+ Добавить компонент"
    InlineBanner* instanceLink_ = nullptr; // "Экземпляр: связан с «Куб 1»" [Отвязать]
    ComponentCard *groupCard_ = nullptr, *arrayCard_ = nullptr;
    GroupPanel* groupPanel_ = nullptr;
    class FieldForm* arrayFields_ = nullptr;
    ke::ArrayObject arrayDraft_;
    QPushButton* addComponent_ = nullptr;
    QMenu* addMenu_ = nullptr;
    InlineBanner* noCollider_ = nullptr; // in the rigid card: a body without a collider falls through
    class FieldForm *rigidFields_ = nullptr, *colliderFields_ = nullptr;
    ke::Entity shapeDraft_; // the active shape's master, the rigid and collider cards' edits written here first
    // The "Свет" and "Камера" cards over every selected light or camera
    ComponentCard *lightCard_ = nullptr, *cameraCard_ = nullptr;
    class FieldForm *lightFields_ = nullptr, *cameraFields_ = nullptr;
    ke::Light lightDraft_;
    ke::Camera cameraDraft_;
    QPushButton* throughButton_ = nullptr;
    // The "Датчик" card; the electrics' cards, shown instead of the object's when one is selected
    ComponentCard* sensorCard_ = nullptr;
    QLabel *sensorMount_ = nullptr, *sensorLive_ = nullptr;
    class FieldForm* sensorFields_ = nullptr;
    ke::SensorObject sensorDraft_; // the sensor the card shows, its edits written here first
    QWidget* deviceBody_ = nullptr;
    ComponentCard *batteryCard_ = nullptr, *driverCard_ = nullptr;
    QLabel *batteryLive_ = nullptr, *driverMount_ = nullptr, *driverLive_ = nullptr;
    class FieldForm *batteryFields_ = nullptr, *driverFields_ = nullptr;
    ke::BatteryObject batteryDraft_; // the pack and the driver the cards show, their edits written here first
    ke::DriverObject driverDraft_;
    ComponentCard* jointCard_ = nullptr;
    QLabel *jointMount_ = nullptr, *jointLive_ = nullptr;
    class FieldForm* jointFields_ = nullptr;
    ke::JointObject jointDraft_; // the joint the card shows, its edits written here first
    QTimer* liveTimer_ = nullptr;
    // The window's actions as code (SceneBuilderCode.cpp)
    void scheduleCode();
    void flushCode();
    void codeAction(const QString& op, const QString& py, QJsonObject args = {}, bool flushFirst = true);
    // An action on objects as its line: `scene.objects["Куб 1"].method()` for one, `scene.method(a, b)`
    // for several. Made before the action runs (the names it removes are still there).
    QString codeCall(const QString& method, const std::vector<uint32_t>& ids) const;
    QString codeRefs(const std::vector<uint32_t>& ids) const; // scene.objects["Куб 1"], scene.objects["Шар 1"]
    ke::SceneGraph coded_;                      // the scene as the lines of code so far make it
    std::vector<uint32_t> codedSelection_;      // the selection as they make it (the active one last)
    QTimer* codeTimer_ = nullptr;
    int apiDepth_ = 0;
    std::function<void(const QString&, const QJsonObject&)> reportCode_;
};

// The names of the shapes, in the order of ke::ShapeKind: "Куб", "Сфера" ... "Модель".
const char* shapeName(ke::ShapeKind k);
// The names and icons of the kinds of light: "Солнце", "Лампа", "Прожектор".
const char* lightName(ke::LightKind k);
SceneIcon lightIcon(ke::LightKind k);
// The words of the API (SceneBuilderCode.cpp): "box", "sun", "rigid" - as the scene file and kinetica
// say them; and back (-1: no such word).
const char* shapeWord(ke::ShapeKind k);
const char* lightWord(ke::LightKind k);
const char* roleWord(RoleIcon r);
QString tuple(const ke::Vector3& v); // a point as Python writes it, numbers as the scene file does: (0.4, 0.1, -0.25)
QJsonArray jsonIds(const std::vector<uint32_t>& ids);
