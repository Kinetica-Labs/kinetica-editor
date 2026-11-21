// Forces, pressed the way a person presses them (see SelfTest.h): "Сила ▾" makes the four kinds with their
// defaults, each selected, listed and drawn; a click on a force's wireframe selects it and the gizmo moves it
// (a scale leaves it alone); its card shows the right fields, a switch to radial lands a radius before the shape
// (the lines the hand says, run again, are accepted by the core), a typed strength is one undo step; in play
// «Сработать» fires an impulse (the box moves along the force's axis, the journal has force_fire) and Stop
// forgets it; a force added while playing does not fire by itself.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "UiLanguage.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTreeWidget>

#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

const Force* forceOf(const SceneBuilder& b, uint32_t id) {
    for (const Force& f : b.graph().forces)
        if (f.id == id) return &f;
    return nullptr;
}

QAction* addAction(QMainWindow& w, int kind) {
    static const char* names[] = {"addForcePush", "addForceBlast", "addForceThrust", "addForcePull"};
    return w.findChild<QAction*>(names[kind]);
}

bool listed(QMainWindow& w, const std::string& name) {
    auto* list = w.findChild<QTreeWidget*>("sceneList");
    return list && !list->findItems(QString::fromStdString(name), Qt::MatchContains | Qt::MatchRecursive).isEmpty();
}

void testButtonAndDefaults(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    auto* button = w.findChild<QToolButton*>("forcesButton");
    bool four = button && button->menu();
    for (int k = 0; k < 4; ++k) four = four && addAction(w, k) && button->menu()->actions().contains(addAction(w, k));
    QMenu* sub = nullptr;
    for (QAction* top : w.menuBar()->actions())
        for (QAction* item : top->menu() ? top->menu()->actions() : QList<QAction*>())
            if (item->menu() && item->text() == uiText("Kinetica", "Сила")) sub = item->menu();
    if (sub) emit sub->aboutToShow();
    c.check(four && sub && sub->actions().size() == 4, "«Сила ▾» holds Толчок, Взрыв, Тяга and Притяжение, and so does «Сцена» → «Добавить» → «Сила»");

    struct Expect { ForceShape shape; ForceMode mode; float strength, radius; ForceFalloff falloff; };
    const Expect table[] = {{ForceShape::Directional, ForceMode::Impulse, 5, 1.5f, ForceFalloff::None},
                            {ForceShape::Radial, ForceMode::Impulse, 10, 3, ForceFalloff::Linear},
                            {ForceShape::Directional, ForceMode::Constant, 10, 1.5f, ForceFalloff::None},
                            {ForceShape::Radial, ForceMode::Constant, -10, 5, ForceFalloff::Linear}};
    b.newScene();
    b.frameAll();
    pump(150);
    bool defaults = true, selected = true, rows = true, marks = true;
    for (int k = 0; k < 4; ++k) {
        const size_t forces = b.graph().forces.size(), markers = v->sceneMarkers().size();
        const uint32_t id = made(b, addAction(w, k));
        const Force* f = forceOf(b, id);
        selected = selected && f && b.selectedId() == id && b.graph().forces.size() == forces + 1;
        defaults = defaults && f && f->shape == table[k].shape && f->mode == table[k].mode && f->strength == table[k].strength &&
                   f->radius == table[k].radius && f->falloff == table[k].falloff && f->start == 0 && f->tag.empty();
        rows = rows && f && listed(w, f->name);
        marks = marks && v->sceneMarkers().size() == markers + 1;
    }
    c.check(defaults && selected, "each of the four makes its kind with the menu's defaults (5 / 10 / 10 / -10, radius 1.5 / 3 / 1.5 / 5), selected");
    c.check(rows && marks, "each is a row of the scene list and one more wireframe in the view");
}

void testPickAndGizmo(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.frameAll();
    pump(150);
    const uint32_t id = made(b, addAction(w, 1));
    const Force before = *forceOf(b, id);
    b.select(0);
    pump(60);
    trigger(w, "toolSelect");
    const QPointF at = toPoint(v->gizmoView().project(before.position));
    sendMouse(v, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    pump(60);
    c.check(b.selectedId() == id, "a click on a force's wireframe selects it");
    trigger(w, "toolMove"); // the tool the window starts with: the groups after this one drag the gizmo's arrows
    b.select(id);
    b.onGizmoStarted();
    GizmoPose pose;
    pose.mode = GizmoMode::Translate;
    pose.position = before.position + Vector3(0.4f, 0, 0);
    b.onGizmoMoved(pose);
    b.onGizmoFinished();
    const Vector3 moved = forceOf(b, id)->position;
    b.onGizmoStarted();
    pose.mode = GizmoMode::Scale;
    pose.position = moved;
    pose.scale = Vector3(3.0f);
    b.onGizmoMoved(pose);
    b.onGizmoFinished();
    const Force* f = forceOf(b, id);
    c.check(std::fabs(moved.x - before.position.x - 0.4f) < 1e-5f && same(f->position, moved) && f->radius == before.radius && f->strength == before.strength,
            "the gizmo moves a force; a scale leaves it as it was");
}

void testCard(Checker& c, QMainWindow& w, SceneBuilder& b) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    b.newScene();
    pump(100);
    const int panelWidth = b.inspectorPanel()->width();
    const uint32_t push = made(b, addAction(w, 0));
    if (ops) ops->runNow("scene_set", {{"id", int(push)}, {"line", "effect"}, {"key", "radius"}, {"values", QJsonArray{0}}}); // every body: no reach of its own
    pump(100);
    const auto field = [&](const char* name) { return w.findChild<QWidget*>(name); };
    const auto shows = [&](const char* name) { return field(name) && field(name)->isVisibleTo(b.inspectorPanel()); };
    auto* card = w.findChild<QWidget*>("forceCard");
    c.check(card && card->isVisibleTo(b.inspectorPanel()) && shows("forceShape") && shows("forceMode") && shows("forceStrength") && shows("forceRadius") &&
                shows("forceFalloff") && shows("forceStart") && shows("forceTag") && shows("forceFire"),
            "a selected impulse force shows its card: shape, mode, strength, radius, falloff, moment, tag and «Сработать»");
    c.check(b.inspectorPanel()->width() == panelWidth, ("the force's header line does not widen the inspector: " + std::to_string(panelWidth) + " -> " +
                                                         std::to_string(b.inspectorPanel()->width()) + " px").c_str());

    std::vector<std::pair<QString, QJsonObject>> lines; // what the hand says about the edit, in order
    b.setCodeReporter([&lines](const QString& op, const QJsonObject& args) { lines.push_back({op, args}); });
    auto* shape = w.findChild<QComboBox*>("forceShape");
    if (!shape || !ops) return c.check(false, "the card has its shape field");
    shape->setCurrentIndex(1);
    pump(700); // the hand's line comes when the edit rests
    b.setCodeReporter([ops](const QString& op, const QJsonObject& args) { ops->report("ui", op, args); });
    const Force* f = forceOf(b, push);
    c.check(f && f->shape == ForceShape::Radial && f->radius == 3, "a switch to radial on a force with no radius puts radius 3 with it");
    // The whole span from the radius to the shape is one scene_set, radius first, so the core reads it as one line.
    QStringList said;
    for (const auto& [op, a] : lines)
        if (op == "scene_set" && a.value("line").toString() == "effect") {
            said << a.value("key").toString();
            for (const QJsonValue& x : a.value("values").toArray()) said << (x.isString() ? x.toString() : QString::number(x.toDouble()));
        }
    c.check(said.join(" ") == "radius 3 shape radial", ("the hand says radius before shape, in one line: " + said.join(" ").toStdString()).c_str());
    b.undoAction()->trigger();
    pump(100);
    bool accepted = forceOf(b, push) && forceOf(b, push)->shape == ForceShape::Directional && forceOf(b, push)->radius == 0;
    for (const auto& [op, a] : lines)
        if (op == "scene_set") accepted = ops->runNow(op, a).value("ok").toBool() && accepted;
    f = forceOf(b, push);
    c.check(accepted && f && f->shape == ForceShape::Radial && f->radius == 3, "those lines, run again as operations on the directional force, are accepted by the core");

    auto* strength = w.findChild<QDoubleSpinBox*>("forceStrength");
    const float was = forceOf(b, push)->strength;
    if (strength) strength->setValue(7.5);
    pump(100);
    const bool typed = forceOf(b, push)->strength == 7.5f;
    b.undoAction()->trigger();
    pump(100);
    c.check(typed && forceOf(b, push)->strength == was && forceOf(b, push)->shape == ForceShape::Radial, "a typed strength reaches the graph and one Ctrl+Z takes it back");

    const uint32_t thrust = made(b, addAction(w, 2));
    c.check(thrust && !shows("forceStart") && !shows("forceFire") && shows("forceStrength"), "a constant force has no moment and no «Сработать»");
}

int firedLines(EditorOps& ops) {
    QFile f(ops.journalFile());
    return f.open(QIODevice::ReadOnly) ? int(f.readAll().count("force_fire")) : 0;
}

void testFirePlayStop(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    EditorOps& ops = *dynamic_cast<MainWindow&>(w).operators();
    b.newScene();
    pump(100);
    ops.runNow("world_set", {{"line", "world"}, {"key", "gravity"}, {"values", QJsonArray{0, 0, 0}}});
    const uint32_t box = made(b, b.createActions()[0]);
    b.editEntity(box, [](Entity& e) { e.position = Vector3(0, 2, 0), e.rigid.enabled = e.collider.enabled = true; });
    const uint32_t id = made(b, addAction(w, 0));
    const auto set = [&](const char* line, const char* key, QJsonArray values) {
        ops.runNow("scene_set", {{"id", int(id)}, {"line", line}, {"key", key}, {"values", values}});
    };
    set("pose", "position", {0, 2, 0});
    set("pose", "rotation", {0, 0, -90}); // its +y axis points along +x
    set("effect", "start", {3600});
    b.select(id);
    pump(100);
    auto* fire = w.findChild<QPushButton*>("forceFire");
    const bool offInEdit = fire && !fire->isEnabled();
    b.play();
    const bool ran = waitFrames(v, 5, 15000);
    pump(100);
    const bool onInPlay = fire && fire->isEnabled();
    const auto boxX = [&] {
        for (const Frame::Body& body : v->snapshot()->bodies)
            if (body.object == box) return body.pos.x;
        return 0.0f;
    };
    const float rest = boxX();
    const int lines = firedLines(ops);
    const auto drawn = [&] { // the force's arrow among the view's marks
        for (const SceneMarker& m : v->sceneMarkers())
            if (m.id == id) return true;
        return false;
    };
    const bool before = drawn();
    if (fire) fire->click();
    waitUntil([&] { return boxX() - rest > 0.05f; }, 5000); // the press goes to the world's thread: wait for the box, not for a count of frames
    const float moved = boxX() - rest;
    const bool spent = waitUntil([&] { return !drawn(); }, 3000);
    pump(100);
    b.stop();
    pump(200);
    c.check(before && spent && drawn(), "the arrow of an impulse is drawn till it fires, gone once it is given, back after Stop");
    c.check(offInEdit && onInPlay, "«Сработать» is off while editing and on while the scene plays");
    c.check(ran && moved > 0.05f, "pressed, it fires the impulse: the box moves along the force's axis");
    c.check(firedLines(ops) == lines + 1, "the press is a force_fire line in the journal");
    c.check(b.editing() && entity(b, box)->position.x == 0 && entity(b, box)->position.y == 2, "Stop forgets it: the box is where it was");

    // A force made while the scene plays has its moment already past: it does not fire by itself.
    b.play();
    waitFrames(v, 5, 15000);
    const uint32_t late = made(b, addAction(w, 0));
    const float y0 = [&] {
        for (const Frame::Body& body : v->snapshot()->bodies)
            if (body.object == box) return body.pos.y;
        return 0.0f;
    }();
    newFrames(v, 20);
    float y1 = y0;
    for (const Frame::Body& body : v->snapshot()->bodies)
        if (body.object == box) y1 = body.pos.y;
    b.stop();
    pump(200);
    c.check(late && std::fabs(y1 - y0) < 0.01f, "a force added while playing does not fire by itself");
}

} // namespace

// What the hand does to a force in the view. A push made with a rigid cube selected is that cube's own: its child at
// the cube's centre, a metre of arrow, sized by the cube's mass. A click on its arrow away from its ring takes it. The
// orange point at the arrow's end is its whole vector: dragged, the force turns to push there and is as strong as the
// arrow is long. A blast made the same way stands at the cube's side; a field's second point is its radius.
void testHandles(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) { e.position = Vector3(0.0f, 0.5f, 0.0f), e.size = Vector3(1.0f), e.rigid.enabled = e.collider.enabled = true; });
    const auto over = [&](int kind) { // the menu's item pressed with the cube selected
        b.select(cube);
        pump(60);
        addAction(w, kind)->trigger();
        pump(60);
        return b.selectedId();
    };
    const uint32_t push = over(0), blast = over(1);
    if (!forceOf(b, push) || !forceOf(b, blast)) return void(c.check(false, "a force made with a cube selected is selected itself"));
    const float mass = float(bodyMass(*findEntity(b.graph(), cube)));
    Vector3 p;
    Quaternion q;
    worldPose(b.graph(), *forceOf(b, push), p, q);
    const Vector3 beside = forceOf(b, blast)->position;
    std::printf("  with a %.0f kg cube selected: the push its child %s, at (%.2f %.2f %.2f), %.0f N s, an arrow of %.2f m; the blast at (%.2f %.2f %.2f)\n", double(mass),
                forceOf(b, push)->parent == cube ? "yes" : "no", double(p.x), double(p.y), double(p.z), double(forceOf(b, push)->strength),
                double(forceArrow(b.graph(), *forceOf(b, push))), double(beside.x), double(beside.y), double(beside.z));
    c.check(forceOf(b, push)->parent == cube && forceBody(b.graph(), *forceOf(b, push)) && length(p - Vector3(0, 0.5f, 0)) < 1e-4f &&
                std::fabs(forceOf(b, push)->strength - 3 * mass) < 1e-2f && std::fabs(forceArrow(b.graph(), *forceOf(b, push)) - 1.0f) < 1e-4f,
            "a push made with a rigid cube selected is the cube's own: its child at its centre, a metre of arrow - 3 m/s of the cube");
    c.check(forceOf(b, blast)->parent == 0 && std::fabs(beside.x + 1.0f) < 1e-3f && std::fabs(beside.y - 0.5f) < 1e-3f, "a blast is a field: it stands 0.5 m off the cube's side");

    v->setFocusBox(AABB(Vector3(-3, -1, -3), Vector3(3, 3, 3)));
    v->frameScene();
    b.select(0);
    pump(100);
    trigger(w, "toolSelect");
    const QPointF shaft = toPoint(v->gizmoView().project(Vector3(0, 1.3f, 0))); // on the arrow, over the cube's top, far from its ring
    sendMouse(v, QEvent::MouseButtonPress, shaft, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, shaft, Qt::LeftButton, Qt::NoButton);
    pump(80);
    trigger(w, "toolMove");
    c.check(b.selectedId() == push, "a click on the arrow of a directional force, away from its ring, selects it");

    b.select(push);
    pump(100);
    std::vector<ShapeHandle> points = v->shapeHandles();
    const bool one = points.size() == 1 && points[0].free && length(points[0].centre - Vector3(0, 1.5f, 0)) < 1e-3f;
    c.check(one, "a body's own force shows one orange point, at the end of its arrow");
    if (!one) return;
    const auto dragTo = [&](const Vector3& from, const Vector3& to) {
        const QPointF a = v->pixelOf(from), z = v->pixelOf(to);
        sendMouse(v, QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
        sendMouse(v, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) sendMouse(v, QEvent::MouseMove, a + (z - a) * (i / 10.0), Qt::NoButton, Qt::LeftButton);
        sendMouse(v, QEvent::MouseButtonRelease, z, Qt::LeftButton, Qt::NoButton);
        pump(100);
    };
    // The point is dragged in the plane facing the eye: the force then points from its place at where it was let go.
    const size_t steps = b.undoTaken();
    const Vector3 aimed = points[0].centre + Vector3(1.2f, -0.7f, 0);
    const Vector3 target = v->handlePoint(points[0], v->pixelOf(aimed)); // where the mouse takes the point, in that plane
    dragTo(points[0].centre, aimed);
    Vector3 now;
    worldPose(b.graph(), *forceOf(b, push), now, q);
    const float along = dot(normalize(target - p), forceAxis(q)), metres = length(target - p), strength = forceOf(b, push)->strength;
    std::printf("  the arrow's end dragged %.2f m from the force: its axis %.3f of the way there, %.0f N s (%.0f a metre), its place kept %s, undo steps +%zu\n",
                double(metres), double(along), double(strength), double(3 * mass), length(now - p) < 1e-5f ? "yes" : "no", b.undoTaken() - steps);
    c.check(along > 0.995f && std::fabs(strength - metres * 3 * mass) < 0.02f * metres * 3 * mass && length(now - p) < 1e-5f && b.undoTaken() == steps + 1,
            "the orange point at the arrow's end is the whole vector: dragged, the force turns there and is as strong as the arrow is long - one undo step, its place kept");
    b.undoAction()->trigger();
    pump(80);
    c.check(std::fabs(forceOf(b, push)->strength - 3 * mass) < 1e-2f, "Ctrl+Z takes the drag back");

    // A field (a push made with nothing selected): the vector's point and one more, its radius.
    const uint32_t field = made(b, addAction(w, 0));
    points = v->shapeHandles();
    const bool two = forceOf(b, field) && !forceBody(b.graph(), *forceOf(b, field)) && points.size() == 2 && points[0].free && !points[1].free && std::fabs(points[1].reach - 1.5f) < 1e-4f;
    c.check(two, "a field shows two orange points: its vector and its reach");
    if (!two) return;
    dragTo(points[1].centre + points[1].axis * points[1].reach, points[1].centre + points[1].axis * 2.5f);
    const float radius = forceOf(b, field)->radius;
    b.undoAction()->trigger();
    pump(80);
    c.check(std::fabs(radius - 2.5f) < 0.05f && forceOf(b, field)->radius == 1.5f, ("the point on its reach, dragged out, is the radius; Ctrl+Z takes it back: " + std::to_string(radius)).c_str());
}

// A push made on a shape that has no physics is still that shape's own - no field's sphere around it - but it pushes
// nothing: its arrow is pale, the status line and its card say why in a few words. The rigid role given, it is a
// force like any other; the body fixed, it is idle again.
void testIdle(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) { e.rigid.enabled = false; });
    QString said;
    const auto hears = QObject::connect(&b, &SceneBuilder::statusMessage, &b, [&said](const QString& text) { said = text; });
    b.select(cube);
    pump(60);
    addAction(w, 0)->trigger();
    pump(100);
    QObject::disconnect(hears);
    const uint32_t push = b.selectedId();
    const Force* f = forceOf(b, push);
    if (!f) return void(c.check(false, "a push made with a plain shape selected is selected"));
    const auto mark = [&]() -> const SceneMarker* {
        for (const SceneMarker& m : v->sceneMarkers())
            if (m.id == push) return &m;
        return nullptr;
    };
    auto* owner = w.findChild<QLabel*>("forceOwner");
    const QString none = uiText("Kinetica", "нет физики");
    const bool own = f->parent == cube && forceBody(b.graph(), *f) && forceIdle(b.graph(), *f);
    const bool pale = mark() && mark()->idle && mark()->range == 0 && v->shapeHandles().size() == 1;
    const bool told = owner && owner->isVisibleTo(b.inspectorPanel()) && owner->text().contains(none) && owner->text().size() < 40 && said.contains(none);
    c.check(own && pale && told, ("a push on a shape with no physics is the shape's own: no sphere, a pale arrow; the status line and a short line of its card say so: " +
                                  (owner ? owner->text().toStdString() : std::string())).c_str());
    b.editEntity(cube, [](Entity& e) { e.rigid.enabled = e.collider.enabled = true; });
    b.select(0), b.select(push);
    pump(100);
    const bool live = !forceIdle(b.graph(), *forceOf(b, push)) && mark() && !mark()->idle && owner && !owner->text().contains(none);
    b.editEntity(cube, [](Entity& e) { e.rigid.fixed = true; });
    b.select(0), b.select(push);
    pump(100);
    c.check(live && forceIdle(b.graph(), *forceOf(b, push)) && mark() && mark()->idle && owner && !owner->text().contains(none),
            "the rigid role given, the force is live and its card names the body; the body fixed, it is idle again");
}

int runForceTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString&) {
    Checker c;
    testButtonAndDefaults(c, w, b, v);
    testPickAndGizmo(c, w, b, v);
    testHandles(c, w, b, v);
    testIdle(c, w, b, v);
    testCard(c, w, b);
    testFirePlayStop(c, w, b, v);
    return c.failures;
}
