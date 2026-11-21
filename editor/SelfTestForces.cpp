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
    const Expect table[] = {{ForceShape::Directional, ForceMode::Impulse, 5, 0, ForceFalloff::None},
                            {ForceShape::Radial, ForceMode::Impulse, 10, 3, ForceFalloff::Linear},
                            {ForceShape::Directional, ForceMode::Constant, 10, 0, ForceFalloff::None},
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
    c.check(defaults && selected, "each of the four makes its kind with the menu's defaults (5 / 10 / 10 / -10, radius 0 / 3 / 0 / 5), selected");
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
    if (fire) fire->click();
    newFrames(v, 20);
    const float moved = boxX() - rest;
    pump(100);
    b.stop();
    pump(200);
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

int runForceTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString&) {
    Checker c;
    testButtonAndDefaults(c, w, b, v);
    testPickAndGizmo(c, w, b, v);
    testCard(c, w, b);
    testFirePlayStop(c, w, b, v);
    return c.failures;
}
