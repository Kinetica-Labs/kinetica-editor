// The window's own state as objects of the API (EditorOps.h) - what exists only because there is a
// screen and a hand: the selection (which objects an action without ids works on), the gizmo's tool
// and its axes, the view's camera. The scene does not change through them, so they are no undo step;
// a hand's change of the selection and of the tool is still said as its line (editor.select(...),
// editor.gizmo.tool = "rotate"), so a session reads back as the person did it.
#include "EditorOps.h"

#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>

#include <algorithm>
#include <QWidget>

using namespace ke;

namespace {

const char* kTools[] = {"select", "move", "rotate", "scale"}; // GizmoMode's order
const char* kToolActions[] = {"toolSelect", "toolMove", "toolRotate", "toolScale"};

QJsonArray json(const QVector3D& v) { return QJsonArray{double(v.x()), double(v.y()), double(v.z())}; }

bool vector(const QJsonValue& v, QVector3D& out) {
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return false;
    out = QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
    return true;
}

bool known(const SceneGraph& g, uint32_t id) {
    if (findObject(g, id)) return true;
    for (const JointObject& j : g.joints) if (j.id == id) return true;
    for (const BatteryObject& b : g.batteries) if (b.id == id) return true;
    for (const DriverObject& d : g.drivers) if (d.id == id) return true;
    for (const ControllerObject& c : g.controllers) if (c.id == id) return true;
    return false;
}

QAction* action(QWidget* window, const QString& name) {
    for (QAction* a : window->findChildren<QAction*>())
        if (a->objectName() == name) return a;
    return nullptr;
}

} // namespace

// The objects of `ids` selected, as a click would (no `ids`: the selection stays): why not ("" - done).
QString EditorOps::selectArgs(const QJsonObject& a) {
    if (!a.contains("ids")) return builder_->selection().empty() ? "no objects to act on: select them or pass ids" : "";
    std::vector<uint32_t> ids;
    for (const QJsonValue& v : a.value("ids").toArray()) {
        const uint32_t id = uint32_t(v.toInt());
        if (!known(builder_->graph(), id)) return QString("no object %1").arg(id);
        ids.push_back(id);
    }
    if (ids.empty()) return "ids is empty";
    builder_->selectMany(ids);
    return builder_->selection().empty() ? "the objects cannot be selected (locked?)" : "";
}

void EditorOps::addStateOps() {
    addSync("selection_get", "The selected objects, the active one last", "{}", [this](const QJsonObject&) {
        return ok({{"ids", jsonIds(builder_->orderedSelection())}, {"id", int(builder_->selectedId())}});
    });
    addSync("selection_set", "Select these objects as clicks would, the last one active (none: nothing)", "{ids: [int]}", [this](const QJsonObject& a) {
        if (a.value("ids").toArray().isEmpty()) return builder_->selectMany({}), ok({{"ids", QJsonArray()}});
        const QString why = selectArgs(a);
        return why.isEmpty() ? ok({{"ids", jsonIds(builder_->orderedSelection())}}) : error(why);
    });
    auto gizmo = [this] { return ok({{"tool", kTools[int(view_->gizmo().mode())]}, {"local", view_->gizmo().local()}}); };
    addSync("gizmo_get", "The gizmo: its tool (select, move, rotate, scale) and whether its axes are the object's own", "{}",
            [gizmo](const QJsonObject&) { return gizmo(); });
    addSync("gizmo_set", "The gizmo's tool and axes, as the tool shelf sets them (W E R Q, L)", "{tool?: select|move|rotate|scale, local?: bool}",
            [this, gizmo](const QJsonObject& a) {
        if (a.contains("tool")) {
            const int k = int(std::find(std::begin(kTools), std::end(kTools), a.value("tool").toString()) - std::begin(kTools));
            QAction* button = k < 4 ? action(window_, kToolActions[k]) : nullptr;
            if (!button) return error("tool is select, move, rotate or scale");
            button->trigger();
        }
        if (QAction* local = action(window_, "toolLocal"); local && a.contains("local")) local->setChecked(a.value("local").toBool());
        return gizmo();
    });
    addSync("view_get", "The view's camera: the eye, the point it orbits, the distance, the vertical field of view", "{}", [this](const QJsonObject&) {
        const OrbitCamera& c = view_->camera();
        return ok({{"eye", json(c.eye())}, {"target", json(c.target())}, {"distance", double(c.distance())}, {"fov", double(c.fovDeg())}, {"follow_body",int(view_->followedBody())}, {"through", int(builder_->lookingThrough())}});
    });
    addSync("view_look", "The view's camera at an eye, looking at a target (the orbit goes on around the target)", "{eye: [x,y,z], target: [x,y,z]}",
            [this](const QJsonObject& a) {
        QVector3D eye, target;
        if (!vector(a.value("eye"), eye) || !vector(a.value("target"), target) || (eye - target).length() < 1e-4f)
            return error("eye and target are two different points [x, y, z]");
        view_->setFollow(0);
        view_->camera().lookAt(eye, target);
        view_->update();
        return ok();
    });
    addSync("view_follow", "The view's camera follows a body's centre of mass whenever the world plays - now, or from the next ▶ - and orbits "
                           "it as the mouse turns it (Вид → «Следить за выделенным», Shift+F); body 0: the editor's own view again. "
                           "chase: a chase camera as games make it - no jump, it starts where it looks; going forward, a second after the "
                           "mouse last turned it, it swings behind the motion (1.2 s, slower under 2 m/s), and aims 0.3 s ahead of the "
                           "body (Вид → «Камера преследования»); not given: as it was. view_look, view_through, F and Home end it",
            "{body: int, chase?: bool}", [this](const QJsonObject& a) {
        const uint32_t id = uint32_t(a.value("body").toInt());
        if (id && !ke::findEntity(builder_->graph(), id)) return error(QString("no body %1 in the scene").arg(id));
        if (a.contains("chase")) view_->setChase(a.value("chase").toBool());
        view_->setFollow(id);
        return ok({{"follow_body", int(view_->followWish())}, {"chase", view_->chase()}});
    });
    addSync("view_through", "The view looks through a camera of the scene - its eye, its turn and its lens, wherever its keys or the running world take "
                            "it - as «Смотрю через» in the view's corner; no camera (or 0): the editor's own view again, as it was",
            "{camera?: int}", [this](const QJsonObject& a) {
        const uint32_t id = uint32_t(a.value("camera").toInt());
        const std::vector<Camera>& cameras = builder_->graph().cameras;
        if (id && std::none_of(cameras.begin(), cameras.end(), [id](const Camera& c) { return c.id == id; })) return error(QString("no camera %1").arg(id));
        view_->setFollow(0);
        builder_->lookThrough(id);
        return ok({{"through", int(builder_->lookingThrough())}});
    });
}
