// Plugins by hand (see SelfTest.h; the host: PluginHost.h, the Python side: clients/python/kinetica/plugin.py): a plugin.py in a
// folder of KE_PLUGINS starts and its commands come - a button of «Плагины» with its key; called, it lifts two selected
// cubes with two edits and one Ctrl+Z takes both back; its button opens the form of its field and runs with the value
// given; its panel - a tab of the inspector - runs it with the panel's field and shows the value it set; an error in a command answers with the author's file and line; the file saved, the plugin is back within a
// few seconds with its new words; a plugin that falls over at its line 3 says so on the status bar - and the editor goes on.
// A field that is an object of the scene - in a command's form, on a component's card -, and the controller's template.
// A chart of a plugin's panel: its lines from what on_tick shows, a point a frame.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "PlotPanel.h"
#include "PluginHost.h"
#include "Resources.h"
#include "PluginPanel.h"
#include "TrackChart.h"
#include "UiLanguage.h"

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QMouseEvent>
#include <QSettings>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QApplication>
#include <QKeyEvent>
#include <QTabBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QToolButton>
#include <QBoxLayout>
#include "scene/Assembly.h"
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

using namespace ke;
using namespace selftest;

namespace {

const char* kLifter = R"py(from kinetica.plugin import command, field, show, ui


@command(label="WORDS", key="Ctrl+Shift+U")
def lift(scene, height: float = field(0.1, unit="м", range=(-1.0, 1.0), label="На сколько")):
    ids = scene.ed.selection_get()["ids"]
    for i in ids:  # an edit an object: still one undo step
        scene.ed.scene_move(ids=[i], by=[0, height, 0])
    show(moved=len(ids))
    return {"moved": len(ids)}


@command(label="Ошибка")
def broken(scene):
    scene.ed.scene_add(shape="box")  # half built when it falls: the call taken back whole
    return 1 / 0


ui.panel("Подъём", ui.section("Выделенное", ui.number("height", 0.3, unit="м", range=(-1.0, 1.0), label="На сколько"),
                              ui.button(lift, "Поднять")), ui.value("moved", "Поднято", 0))
)py";

void write(const QString& file, const QString& text) {
    QFile f(file);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(text.toUtf8());
}

QAction* action(QMainWindow& w, const QString& name) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->objectName() == name) return a;
    return nullptr;
}


// A run's hooks (on_tick): a plugin that extends the physics holds a cube in the air - m g up over every frame -, a robot's
// controller turns a servo to 0.5 rad by robot_command's path; it ticks once a frame, before the next is stepped (the
// lockstep), sees its encoder's reading and no truth; the ticks are answered while the window's thread sleeps - the
// lockstep is the simulation's and the plugin's, over their own wire (RunHooks.h); Stop leaves the scene as authored and
// the journal without a line of what the ticks did.
void testRunHooks(Checker& c, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    b.newScene();
    const auto id = [ops](const char* op, QJsonObject args) { return uint32_t(ops->runNow(op, args).value("id").toInt()); };
    const uint32_t cube = id("scene_add", {{"shape", "box"}, {"position", QJsonArray{2.0, 1.5, 0.0}}, {"size", QJsonArray{0.2, 0.2, 0.2}}, {"role", "rigid"}});
    const uint32_t base = id("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}, {"size", QJsonArray{0.2, 0.2, 0.2}},
                                           {"role", "rigid"}, {"fixed", true}});
    const uint32_t arm = id("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.3, 0.5, 0.0}}, {"size", QJsonArray{0.4, 0.05, 0.05}}, {"role", "rigid"}});
    const uint32_t joint = id("assembly_actuator", {{"kind", "rotary"}, {"a", int(arm)}, {"b", int(base)}, {"axis", QJsonArray{0.0, 0.0, 1.0}}});
    ops->runNow("assembly_encoder", {{"joint", int(joint)}, {"reference", 0.0}});
    const float authored = float(findJoint(b.graph(), joint) ? findJoint(b.graph(), joint)->dof[DofRx].drive.target : -1);
    const QString file = folder + "/ticker/plugin.py";
    QDir().mkpath(folder + "/ticker");
    write(file, QString(R"py(import time

from kinetica.plugin import on_play, on_stop, on_tick

ticks = 0
last = 0  # when the last tick was answered, ms of the wall clock
frames = []  # the frame each tick's encoder reading is of (the frame the run starts from, none yet: its -1 ns rounds to 0)


@on_play
def start(scene):
    global ticks
    ticks = 0
    frames.clear()


turned = []


@on_tick(world=True)
def hold(scene, dt, bodies):
    turned.append(len(bodies[CUBE].get("rotation", [])) == 3)
    return {CUBE: {"force": [0, bodies[CUBE]["mass"] * 9.81, 0]}}


@on_tick
def control(scene, dt, sensors):
    global ticks, last
    ticks += 1
    last = int(time.time() * 1000)
    frames.extend(round(s["sample_ns"] * 60e-9) for s in sensors.values() if s["kind"] == "encoder")
    if ticks == 5:
        kinds = sorted(s["kind"] for s in sensors.values())
        print("seen", kinds, "truth" in str(sensors))
    return {JOINT: {"target": 0.5}}


@on_stop
def stop(scene):
    print("ticks", ticks)
    print("rotation", all(turned) and len(turned) > 0)
    print("frames", frames[:6], frames == list(range(len(frames))) and len(frames) > 10)
    print("last", last)
)py").replace("CUBE", QString::number(cube)).replace("JOINT", QString::number(joint)));
    QStringList printed;
    const auto heard = QObject::connect(host, &PluginHost::output, [&printed](const QString&, const QString& line) { printed << line; });
    host->start(true);
    waitUntil([&] { return host->ticking(); }, 10000);
    const auto journal = [ops] { return ops->runNow("journal_lines", {{"last", 500}}).value("lines").toArray(); };
    const QJsonArray journalWas = journal();
    const float y0 = findObject(b.graph(), cube)->position.y;
    QJsonObject stepped;
    ops->run("sim_step", {{"frames", 60}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
    QThread::msleep(800); // the window's thread asleep, as in a paint that waits for the screen: the frames go on without it
    const qint64 awake = QDateTime::currentMSecsSinceEpoch();
    waitUntil([&] { return !stepped.isEmpty(); }, 20000);
    QJsonObject state;
    ops->run("sim_state", {}, [&state](const QJsonObject& r) { state = r; }, "ui");
    waitUntil([&] { return !state.isEmpty(); }, 5000);
    float y = 0, angle = 0;
    for (const QJsonValue& v : state.value("shapes").toArray())
        if (uint32_t(v.toObject().value("id").toInt()) == cube) y = float(v.toObject().value("position").toArray().at(1).toDouble());
    for (const QJsonValue& v : state.value("joints").toArray())
        if (uint32_t(v.toObject().value("id").toInt()) == joint) angle = float(v.toObject().value("angle").toDouble());
    b.stop();
    const bool told = waitUntil([&] { return printed.filter("last ").size() > 0; }, 5000);
    QObject::disconnect(heard);
    const QString seen = printed.filter("seen").value(0), counted = printed.filter("ticks").value(0), frames = printed.filter("frames").value(0);
    const qint64 last = printed.filter("last ").value(0).mid(5).toLongLong();
    // The lines the run added - those after the last one seen before it (the journal shows its last 500, so their
    // number says nothing once it is full): the run's own time, the window's selection and the journal's own snapshot
    // after a stop - nothing of the ticks.
    const QJsonArray journalNow = journal();
    qsizetype from = journalWas.isEmpty() ? 0 : journalNow.size();
    while (from > 0 && journalNow.at(from - 1) != journalWas.last()) --from;
    QStringList added;
    for (qsizetype k = from; k < journalNow.size(); ++k) added << journalNow.at(k).toObject().value("op").toString();
    const bool lines = std::all_of(added.begin(), added.end(), [](const QString& op) { return op == "sim_step" || op == "sim_stop" || op.startsWith("selection_") || op.startsWith("journal_"); });
    const float after = float(findJoint(b.graph(), joint)->dof[DofRx].drive.target);
    std::printf("  60 frames with the run's hooks: the cube %.4f -> %.4f m (held), the joint at %.3f rad (commanded 0.5); %s; %s; "
                "the last tick answered %lld ms before the window's thread woke; the journal + %s; the servo's authored "
                "target %.3f -> %.3f\n", double(y0), double(y), double(angle), qPrintable(seen), qPrintable(counted),
                static_cast<long long>(awake - last), qPrintable(added.join(", ")), double(authored), double(after));
    c.check(std::fabs(y - y0) < 0.01f, "a physics plugin's on_tick(world=True) pushes a body every frame: m g up holds a cube in the air");
    c.check(printed.filter("rotation").value(0) == "rotation True", "...and sees each body's rotation, to push it along its own axes");
    c.check(std::fabs(angle - 0.5f) < 0.05f && seen.contains("encoder") && seen.endsWith("False"),
            "a controller's on_tick sees its encoder's reading, not the truth, and turns the servo by its command");
    c.check(told && counted == "ticks 61", "the lockstep: one tick a frame - the starting one too -, before the next is stepped");
    c.check(frames.endsWith("True"), "sim_step's run: the ticks see the frame it starts from, then each frame once, in order");
    // ▶ as a person presses it: the same - the frame it starts from first, no frame twice, none passed (the lockstep is set
    // before the simulation's thread is let run: a run repeats exactly)
    printed.clear();
    const auto heardPlay = QObject::connect(host, &PluginHost::output, [&printed](const QString&, const QString& line) { printed << line; });
    b.play();
    pump(500);
    b.stop();
    const bool toldPlay = waitUntil([&] { return printed.filter("frames").size() > 0; }, 5000);
    QObject::disconnect(heardPlay);
    std::printf("  ▶ with the run's hooks: %s\n", qPrintable(printed.filter("frames").value(0)));
    c.check(toldPlay && printed.filter("frames").value(0).endsWith("True"), "▶: the ticks see the frame the run starts from, then each frame once, in order");
    c.check(last > 0 && last <= awake, "the lockstep does not pass through the window's thread: every tick answered while it slept");
    c.check(lines && std::fabs(after - authored) < 1e-6f, "Stop: the scene as authored, no journal line of what the ticks did");
    QDir(folder + "/ticker").removeRecursively();
}

// A plugin's component as a built-in one: it comes in «+ Добавить компонент», is added with its
// defaults in one undo step, draws its card from the plugin's fields and shows its handled fields as orange points on the
// shape; the radius point dragged by the mouse to 2 m sets the value - one edit, one journal line; the plugin gone, the
// card says so and the scene keeps the component's words to the bit.
void testComponents(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/turbines");
    write(folder + "/turbines/plugin.py", R"py(from kinetica.plugin import component, field


@component(label="Ветряк")
class Turbine:
    radius = field(1.0, unit="м", range=(0.1, 5.0), label="Радиус", handle="radius")
    hub = field((0.0, 0.5, 0.0), label="Ступица", handle="point")
    power = field(100.0, unit="Вт", range=(0.0, 1000.0), label="Мощность")
)py");
    host->start(true);
    const bool declared = waitUntil([&] { return !b.componentType("turbines.turbine").isEmpty(); }, 10000);
    b.newScene();
    const uint32_t cube = uint32_t(ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}}).value("id").toInt());
    b.select(cube);
    pump(50);
    QMenu menu;
    b.fillAddMenu(&menu);
    QAction* add = nullptr;
    for (QAction* a : menu.actions())
        if (a->text() == "Ветряк") add = a;
    const size_t steps = b.undoTaken();
    if (add) add->trigger();
    pump(100);
    const Entity* e = findEntity(b.graph(), cube);
    const bool added = add && e && e->components.size() == 1 && e->components[0].type == "turbines.turbine" && b.undoTaken() == steps + 1;
    auto* card = w.findChild<QWidget*>("pluginComponent:turbines.turbine");
    std::vector<ShapeHandle> points = v->shapeHandles();
    int radius = -1;
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i].plugin && points[i].label == "Радиус") radius = int(i);
    const bool shown = card && card->isVisibleTo(&w) && points.size() == 2 && radius >= 0;
    c.check(declared && added && shown, "a plugin's component comes in «+ Добавить компонент», is added in one undo step, draws its card "
                                        "and shows its handled fields as orange points");
    if (radius < 0) return;
    v->setFocusBox(AABB(Vector3(-3, -1, -3), Vector3(3, 2, 3)));
    v->frameScene();
    pump(100);
    points = v->shapeHandles();
    const ShapeHandle h = points[size_t(radius)];
    const QPointF from = v->pixelOf(h.centre + h.axis * h.reach), to = v->pixelOf(h.centre + h.axis * 2.0f);
    const size_t before = b.undoTaken();
    const auto setLines = [ops] {
        int n = 0;
        for (const QJsonValue& l : ops->runNow("journal_lines", {{"last", 500}}).value("lines").toArray()) n += l.toObject().value("op").toString() == "component_set";
        return n;
    };
    const int lines0 = setLines();
    sendMouse(v, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i) sendMouse(v, QEvent::MouseMove, from + (to - from) * (i / 12.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    pump(100);
    const int lines = setLines() - lines0;
    const QJsonObject listed = ops->runNow("component_list", {{"type", "turbines.turbine"}});
    const double now = listed.value("components").toArray().at(0).toObject().value("values").toObject().value("radius").toDouble();
    std::printf("  the orange radius point dragged to 2 m: radius %.3f m, %zu undo step(s), %d component_set line(s)\n", now,
                b.undoTaken() - before, lines);
    c.check(std::fabs(now - 2.0) < 0.03 && b.undoTaken() == before + 1 && lines == 1,
            "an orange point dragged sets its field: one edit, one undo step, one journal line");
    QDir(folder + "/turbines").removeRecursively();
    waitUntil([&] { return b.componentType("turbines.turbine").isEmpty(); }, 5000);
    b.select(0), b.select(cube);
    pump(100);
    QString note;
    if (auto* gone = w.findChild<QWidget*>("pluginComponent:turbines.turbine"))
        for (QLabel* l : gone->findChildren<QLabel*>()) note += l->text();
    const std::string text = b.graph().save();
    ke::SceneGraph back;
    std::string error;
    const size_t line = text.find("component turbines.turbine ");
    // Its line as written, the radius the mouse set (to the pixel it was dragged to), and the scene read back the same.
    const std::string words = line == std::string::npos ? std::string() : text.substr(line, text.find('\n', line) - line);
    const size_t at = words.find(" radius ");
    const double written = at == std::string::npos ? 0 : QByteArray::fromStdString(words.substr(at + 8)).split(' ').first().toDouble();
    const bool kept = std::fabs(written - 2.0) < 0.03 && back.load(text, error) && back.save() == text;
    std::printf("  the plugin gone: «%s»; the scene keeps its words %s: «%s»\n", qPrintable(note.left(80)), kept ? "yes" : "no", words.c_str());
    c.check(note.contains("не запущен") && kept, "its plugin gone, the card says so and the scene keeps the component to the bit");
}

// A plugin's tool and its pictures: «@command(tool=True)» is an orange button on the shelf; pressed, a click on the cube's
// top calls it with the point, the face's normal and the cube's id - its sphere stands 0.1 m over the face, one undo step;
// what it drew stays over the view, Esc leaves the tool. What on_tick draws is there while the scene plays, gone at Stop.
void testDrawAndTool(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/marks");
    write(folder + "/marks/plugin.py", R"py(from kinetica.plugin import command, draw, field, on_tick


@command(label="Флажок", tool=True)
def flag(scene, point, normal, object, height=field(0.1, unit="м")):
    top = [point[i] + normal[i] * height for i in range(3)]
    scene.ed.scene_add(shape="sphere", name=f"флажок {object}", position=top, size=[0.05, 0.05, 0.05], role="collider")
    draw.line("флажок", [point, top])
    draw.label("где", top, f"объект {object}")


@on_tick
def mark(scene, dt, sensors):
    draw.points("такт", [(0, 2, 0)], size=10)
)py");
    host->start(true);
    QAction* tool = nullptr;
    waitUntil([&] { return (tool = w.findChild<QAction*>("plugin:marks_flag")) != nullptr; }, 10000);
    auto* shelf = w.findChild<QToolBar*>("toolShelf");
    const QList<QAction*> shelved = shelf ? shelf->actions() : QList<QAction*>();
    const qsizetype line = std::find_if(shelved.begin(), shelved.end(), [](QAction* a) { return a->objectName() == "pluginToolsLine"; }) - shelved.begin();
    const bool onShelf = tool && shelved.contains(tool) && !tool->icon().isNull() && line + 1 < shelved.size() && shelved[line]->isSeparator() &&
                         shelved[line + 1]->objectName().startsWith("plugin:") && shelved.indexOf(tool) > line; // a line parts them from the gizmo's tools
    b.newScene();
    const uint32_t cube = uint32_t(ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}, {"size", QJsonArray{1.0, 1.0, 1.0}}, {"role", "collider"}}).value("id").toInt());
    b.select(0);
    v->setFocusBox(AABB(Vector3(-2, 0, -2), Vector3(2, 2, 2)));
    v->frameScene();
    pump(100);
    if (tool) tool->trigger();
    pump(50);
    const bool taken = v->pluginTool() == "marks_flag" && tool && tool->isChecked();
    const size_t steps = b.undoTaken();
    const QPointF top = v->pixelOf(Vector3(0.1f, 1.0f, 0.1f));
    sendMouse(v, QEvent::MouseMove, top, Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, top, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, top, Qt::LeftButton, Qt::NoButton);
    const Entity* sphere = nullptr;
    waitUntil([&] {
        for (const Entity& e : b.graph().entities)
            if (e.name.rfind("флажок", 0) == 0) sphere = &e;
        return sphere != nullptr;
    }, 10000);
    pump(100);
    const SceneObject* placed = sphere ? findObject(b.graph(), sphere->id) : nullptr;
    const Vector3 at = placed ? placed->position : Vector3(0.0f);
    const bool clicked = placed && std::fabs(at.y - 1.1f) < 0.01f && std::fabs(at.x - 0.1f) < 0.03f && sphere->name == "флажок " + std::to_string(cube) &&
                         b.undoTaken() == steps + 1;
    const bool drawn = v->hasDrawing("marks/флажок") && v->hasDrawing("marks/где");
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(v, &esc);
    const bool left = v->pluginTool().isEmpty() && !tool->isChecked();
    std::printf("  the tool: on the shelf %s, taken %s; a click on the cube's top -> «%s» at %.3f %.3f %.3f (%zu undo step); drawn %s; "
                "Esc leaves it %s\n", onShelf ? "yes" : "no", taken ? "yes" : "no", sphere ? sphere->name.c_str() : "-", double(at.x),
                double(at.y), double(at.z), b.undoTaken() - steps, drawn ? "yes" : "no", left ? "yes" : "no");
    c.check(onShelf && taken && clicked && drawn && left, "a plugin's tool: an orange button on the shelf under a line, a click gives it the point, "
                                                          "the face's normal and the object - one undo step; its picture stays; Esc leaves it");
    waitUntil([&] { return host->ticking(); }, 5000);
    QJsonObject stepped;
    ops->run("sim_step", {{"frames", 5}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
    waitUntil([&] { return !stepped.isEmpty(); }, 20000);
    pump(50);
    const bool ticking = v->hasDrawing("marks/такт");
    b.stop();
    pump(100);
    const bool gone = !v->hasDrawing("marks/такт") && v->hasDrawing("marks/флажок");
    const QJsonObject agent = ops->runNow("view_draw", {{"key", "агент/путь"}, {"line", QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{1, 1, 1}}}});
    const QJsonObject odd = ops->runNow("view_draw", {{"key", "агент/пары"}, {"segments", QJsonArray{QJsonArray{0, 0, 0}}}});
    // A ring and a box's edges are worked out by the operation itself - any client gets them, a plugin's draw.circle only
    // passes the words on: 48 sides closed at 2 m about y at the centre's height; twelve edges, a pair of points each.
    ops->runNow("view_draw", {{"key", "агент/кольцо"}, {"circle", QJsonObject{{"centre", QJsonArray{0, 1, 0}}, {"radius", 2.0}}}});
    ops->runNow("view_draw", {{"key", "агент/габарит"}, {"box", QJsonObject{{"centre", QJsonArray{0, 0.5, 0}}, {"size", QJsonArray{1, 1, 2}}}}});
    const Viewport::Drawing* ring = v->drawing("агент/кольцо");
    const Viewport::Drawing* edges = v->drawing("агент/габарит");
    bool round = ring && ring->strip.size() == 49 * 4;
    for (size_t k = 0; round && k < ring->strip.size(); k += 4)
        round = std::fabs(ring->strip[k + 1] - 1.0f) < 1e-5f && std::fabs(std::hypot(ring->strip[k], ring->strip[k + 2]) - 2.0f) < 1e-4f;
    bool boxed = edges && edges->segments.size() == 24 * 4;
    for (size_t k = 0; boxed && k < edges->segments.size(); k += 4)
        boxed = std::fabs(std::fabs(edges->segments[k]) - 0.5f) < 1e-6f && std::fabs(std::fabs(edges->segments[k + 1] - 0.5f) - 0.5f) < 1e-6f &&
                std::fabs(std::fabs(edges->segments[k + 2]) - 1.0f) < 1e-6f;
    const bool flat = !ops->runNow("view_draw", {{"key", "агент/ноль"}, {"circle", QJsonObject{{"centre", QJsonArray{0, 0, 0}}, {"radius", 0.0}}}}).value("ok").toBool();
    c.check(round && boxed && flat, "view_draw works out a ring (circle) and a box's twelve edges itself; a circle of no radius is refused");
    const bool api = agent.value("ok").toBool() && !odd.value("ok").toBool() && ops->runNow("view_draw", {{"clear", true}, {"key", "агент/"}}).value("cleared").toInt() == 3;
    QDir(folder + "/marks").removeRecursively();
    const bool withPlugin = waitUntil([&] { return !v->hasDrawing("marks/флажок"); }, 5000);
    std::printf("  on_tick's picture while playing %s, gone at Stop %s (the tool's stays); view_draw from the API %s; the plugin gone, "
                "its pictures too %s\n", ticking ? "yes" : "no", gone ? "yes" : "no", api ? "yes" : "no", withPlugin ? "yes" : "no");
    c.check(ticking && gone && api && withPlugin, "on_tick draws while the scene plays, gone at Stop; view_draw for any client; a plugin's "
                                                  "pictures go with it");
}

// A plugin's chart as the plots draw it - the only place it is drawn; asked anew each time, as the plots build their views
// again with the layout and the selection.
TrackChart* plotted(QMainWindow& w, const QString& section, size_t k = 0) {
    if (auto* plots = w.findChild<PlotPanel*>())
        for (const auto& [title, views] : plots->pluginSections())
            if (title == section && k < views.size()) return views[k];
    return nullptr;
}

// A chart's row in the plots' «Что показывать»: under its section's row (by the section's title), the k-th; nullptr: none.
QTreeWidgetItem* treeRow(PlotPanel* plots, const QString& section, int k = 0) {
    auto* tree = plots->findChild<QTreeWidget*>("plotTree");
    for (int i = 0; tree && i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0) == section && k < tree->topLevelItem(i)->childCount()) return tree->topLevelItem(i)->child(k);
    return nullptr;
}

void tick(PlotPanel* plots, const QString& section, bool on, int k = 0) {
    if (QTreeWidgetItem* row = treeRow(plots, section, k)) row->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    pump(50);
}

// The window's first level (docs/contracts/ui.md): its docked panels by name, each open (+) or closed (-).
QStringList firstLevel(QMainWindow& w) {
    QStringList out;
    for (QDockWidget* d : w.findChildren<QDockWidget*>()) out << d->objectName() + (d->isHidden() ? "-" : "+");
    out.sort();
    return out;
}

// A plugin's chart (ui.chart): lines over the run's time, drawn by the editor from what the plugin shows in on_tick - a
// point a frame at that frame's time, with the frame's answer; the same value is a word of the panel; a new run is a new
// history; the plugin gone, its chart too. The panel asked for the bottom, and is a tab of the inspector all the same:
// the plugin, started, started again and gone, leaves the window's panels as they were; the chart is drawn in the plots
// only, the panel's «Графики ▸» opens them.
void testChart(Checker& c, QMainWindow& w, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    const QStringList before = firstLevel(w);
    QDir().mkpath(folder + "/meter");
    const QString file = R"py(from kinetica.plugin import on_play, on_tick, show, ui

clock = 0.0

ui.panel("Счётчик", ui.chart("Время кадра", {"clock": "время", "half": "половина"}, unit="с"), ui.value("clock", "Сейчас"), place="bottom")


@on_play
def start(scene):
    global clock
    clock = 0.0


@on_tick
def count(scene, dt, sensors):
    global clock
    clock += dt
    show(clock=clock, half=clock / 2)
)py";
    write(folder + "/meter/plugin.py", file);
    host->start(true);
    TrackChart* chart = nullptr;
    waitUntil([&] { return (chart = plotted(w, "Счётчик")) != nullptr && host->ticking(); }, 10000);
    if (!chart) return c.check(false, "a plugin's chart comes with its panel, in the plots"), void();
    const QWidget* first = w.findChild<QWidget*>("pluginPanel");
    write(folder + "/meter/plugin.py", file + "\n");
    waitUntil([&] { return host->ticking() && plotted(w, "Счётчик") && w.findChild<QWidget*>("pluginPanel") != first; }, 10000); // started again
    pump(300);
    auto* panel = w.findChild<QWidget*>("pluginPanel");
    QWidget* dock = panel;
    while (dock && !qobject_cast<QDockWidget*>(dock)) dock = dock->parentWidget();
    auto* button = w.findChild<QPushButton*>("pluginChartsButton");
    std::printf("  the window's panels with the plugin: %s (before: %s)\n", qPrintable(firstLevel(w).join(' ')), qPrintable(before.join(' ')));
    c.check(firstLevel(w) == before && dock && dock->objectName() == "inspectorDock",
            "a plugin's panel - asked for the bottom too - is a tab of the inspector: the window's panels stay as they were");
    c.check(panel && panel->findChildren<TrackChart*>().isEmpty() && button && button->text().contains("Время кадра"),
            "the panel draws no chart: one «Графики ▸» button names its charts");
    if (button) button->click();
    pump(50);
    auto* results = w.findChild<QDockWidget*>("resultsDock");
    c.check(results && results->isVisible(), "the panel's «Графики ▸» opens the plots");
    int tabs = 0, iconed = 0; // the docks' tabs carry their panel's icon beside its words
    for (QTabBar* bar : w.findChildren<QTabBar*>(Qt::FindDirectChildrenOnly))
        for (int i = 0; i < bar->count(); ++i) ++tabs, iconed += !bar->tabIcon(i).isNull();
    c.check(tabs > 0 && iconed == tabs, "every tab of the window's panels has its icon beside its words");
    auto* wide = w.findChild<QToolButton*>("actionBottomWidePlots");
    c.check(wide && wide->defaultAction() && wide->defaultAction()->objectName() == "actionBottomWide",
            "the plots' bar has the bottom panel's ⇔ (the window's whole width), as the Sequencer's has");
    chart = plotted(w, "Счётчик");
    b.newScene();
    ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 1.0, 0.0}}, {"role", "rigid"}});
    const PlotSeries* line = chart->tracks().size() == 2 ? chart->tracks()[0].series : nullptr;
    const PlotSeries* half = line ? chart->tracks()[1].series : nullptr;
    const auto run = [&](int frames) {
        QJsonObject stepped;
        ops->run("sim_step", {{"frames", frames}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
        waitUntil([&] { return !stepped.isEmpty(); }, 20000);
        waitUntil([&] { return line && line->size() == size_t(frames) + 1; }, 3000); // the answers come after the fact
        pump(100);                                                                    // and the panel follows at 25 a second
    };
    run(30);
    // A point a frame, the starting one too, at the frame's time: the plugin's clock runs one step ahead of it (the
    // simulation's time is a float's sum of steps: the two agree to its rounding).
    bool timed = line && line->size() == 31 && half->size() == 31 && line->time(0) == 0;
    for (size_t k = 1; timed && k < line->size(); ++k)
        timed = line->time(k) > line->time(k - 1) && std::fabs((line->value(k) - line->value(k - 1)) - (line->time(k) - line->time(k - 1))) < 1e-6 &&
                std::fabs(half->value(k) * 2 - line->value(k)) < 1e-12;
    auto* word = w.findChild<QLabel*>("pluginValue_clock");
    const QString said = word ? word->text() : QString();
    const bool worded = line && line->size() > 0 && said == "Сейчас: " + QString::number(line->latest(), 'g', 6);
    chart = plotted(w, "Счётчик");
    if (!chart) return c.check(false, "the plugin's chart stays in the plots"), void();
    const auto [from, to] = chart->timeRange();
    const bool framed = line && line->size() > 0 && from == 0 && std::fabs(to - line->time(line->size() - 1)) < 1e-9 && chart->title() == "Время кадра, с";
    std::printf("  a plugin's chart «%s»: %zu lines, %zu points over %.3f s after 30 frames, in step with the frames %s; the word «%s»\n",
                qPrintable(chart->title()), chart->tracks().size(), line ? line->size() : 0, to, timed ? "yes" : "no", qPrintable(said));
    c.check(timed && framed, "a plugin's chart: what on_tick shows is a point of its line at that frame's time, a frame each");
    c.check(worded, "the value a chart draws is a word of the panel too: one show() for both");
    b.stop();
    pump(100);
    run(10);
    c.check(line && line->size() == 11 && line->time(0) == 0, "a new run is a new history of the chart");
    b.stop();
    QDir(folder + "/meter").removeRecursively();
    c.check(waitUntil([&] { return plotted(w, "Счётчик") == nullptr && !w.findChild<QWidget*>("pluginPanel"); }, 5000),
            "the plugin gone, its chart goes with its panel");
    if (results && before.contains("resultsDock-")) results->hide(); // as it was: the button opened it
    pump(50);
    c.check(firstLevel(w) == before, "the plugin gone, the window's panels are as they were before it");
}

// A plugin's words in the window's language (docs/contracts/editor-plugin.md, «Перевод»): its catalog
// i18n/<language>.json beside plugin.py; the editor translates what the plugin describes - the panel's title, a button, a
// command's label, a choice's words (the value it gets stays the plugin's) -, tr() what it makes up; the catalog saved,
// the plugin starts again in its new words.
void testWords(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/speaker/i18n");
    const QString catalog = folder + "/speaker/i18n/" + UiLanguage::active() + ".json";
    write(catalog, R"({"Слова": "Words", "Сказать": "Say", "да": "yes", "нет": "no", "привет, {who}": "hello, {who}", "тишина": "silence"})");
    write(folder + "/speaker/plugin.py", R"py(from kinetica.plugin import command, field, tr, ui


@command(label="Сказать")
def say(scene, answer: str = field("да", choices=("да", "нет"), label="Ответ")):
    return {"said": tr("привет, {who}").format(who=answer)}


ui.panel("Слова", ui.choice("answer", ("да", "нет"), label="Ответ"), ui.button(say), ui.value("state", default="тишина"))
)py");
    host->start(true);
    QPushButton* button = nullptr;
    waitUntil([&] { return (button = w.findChild<QPushButton*>("pluginButton_say")) != nullptr; }, 10000);
    QAction* act = action(w, "plugin:speaker_say");
    auto* choice = button ? button->parentWidget()->findChild<QComboBox*>() : nullptr;
    QJsonObject said;
    ops->run("speaker_say", {{"answer", "нет"}}, [&said](const QJsonObject& r) { said = r; }, "ui");
    waitUntil([&] { return !said.isEmpty(); }, 5000);
    std::printf("  a plugin's words in «%s»: button «%s», action «%s», choice «%s», said «%s»\n", qPrintable(UiLanguage::active()),
                button ? qPrintable(button->text()) : "-", act ? qPrintable(act->text()) : "-", choice ? qPrintable(choice->itemText(0)) : "-",
                qPrintable(said.value("said").toString()));
    c.check(button && button->text() == "Say" && act && act->text().startsWith("Say") && choice && choice->itemText(0) == "yes",
            "the editor shows a plugin's labels and choices in its catalog's words");
    auto* state = w.findChild<QLabel*>("pluginValue_state");
    c.check(state && state->text() == "silence", "a value's first words are the catalog's too");
    c.check(said.value("said").toString() == "hello, нет", "tr() in the plugin gives its catalog's word; the value a choice sends stays the plugin's");
    write(catalog, R"({"Сказать": "Speak"})");
    c.check(waitUntil([&] { return (button = w.findChild<QPushButton*>("pluginButton_say")) && button->text() == "Speak"; }, 10000),
            "the catalog saved, the plugin starts again in its new words");
    QDir(folder + "/speaker").removeRecursively();
    waitUntil([&] { return !w.findChild<QPushButton*>("pluginButton_say"); }, 5000);
}

// A picture in the panel (ui.svg): an SVG the plugin sends through show() from a command is drawn by the editor, scaled to
// the panel with the aspect kept; an invalid one does not crash and leaves the placeholder.
void testSvg(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/painter");
    write(folder + "/painter/plugin.py", R"py(from kinetica.plugin import command, show, ui

ui.panel("Рисунок", ui.svg("pic", height=120), place="bottom")


@command(label="Рисовать")
def draw(scene, good: bool = True):
    show(pic='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100"><rect width="200" height="100" fill="#ff0000"/></svg>' if good else "not an svg")
)py");
    host->start(true);
    SvgView* view = nullptr;
    waitUntil([&] { return (view = w.findChild<SvgView*>("pluginSvg_pic")) != nullptr && action(w, "plugin:painter_draw") != nullptr; }, 10000);
    if (!view) return c.check(false, "a picture comes with its panel"), void();
    const auto look = [&](bool good) {
        ops->run("painter_draw", {{"good", good}}, [](const QJsonObject&) {}, "ui");
        waitUntil([&] { return view->valid() == good; }, 5000);
        pump(150);
    };
    look(true);
    view->resize(300, 200);
    const QImage red = view->grab().toImage();
    int painted = 0;
    for (int y = 0; y < red.height(); ++y)
        for (int x = 0; x < red.width(); ++x) painted += qRed(red.pixel(x, y)) > 200 && qGreen(red.pixel(x, y)) < 60;
    const bool fitted = view->heightForWidth(300) == 120 && view->heightForWidth(100) == 50; // aspect 2:1, at most 120 px
    std::printf("  a picture «pluginSvg_pic»: valid %s, %d red pixels in 300x200, heightForWidth(300)=%d\n", view->valid() ? "yes" : "no", painted, view->heightForWidth(300));
    c.check(view->valid() && painted > 5000, "an SVG the plugin shows is drawn in the panel (a valid renderer, a painted picture)");
    c.check(fitted, "the picture takes the panel's width with its aspect kept, at most the given height");
    const QPointF middle(150, 60);
    QWheelEvent wheel(middle, view->mapToGlobal(middle), {}, {0, 480}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view, &wheel);
    const double zoomed = view->zoom();
    const QPointF dragTo = middle + QPointF(40, 0);
    QMouseEvent press(QEvent::MouseButtonPress, middle, view->mapToGlobal(middle), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, dragTo, view->mapToGlobal(dragTo), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, dragTo, view->mapToGlobal(dragTo), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    const QPointF before = view->pan();
    QApplication::sendEvent(view, &press), QApplication::sendEvent(view, &move), QApplication::sendEvent(view, &release);
    const bool moved = std::abs(view->pan().x() - before.x() - 40) < 0.5;
    QMouseEvent twice(QEvent::MouseButtonDblClick, middle, view->mapToGlobal(middle), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view, &twice);
    std::printf("  the wheel over it: zoom %.2f; a double click: %.2f\n", zoomed, view->zoom());
    c.check(zoomed > 1.5 && view->zoom() == 1.0, "the wheel zooms the picture in, a double click shows it whole again");
    c.check(moved, "the wheel held moves the zoomed picture, as in the 3D view");
    look(false);
    const QImage placeholder = view->grab().toImage();
    c.check(!view->valid() && placeholder.width() > 0, "an invalid SVG does not crash: the quiet placeholder");
    QDir(folder + "/painter").removeRecursively();
    c.check(waitUntil([&] { return w.findChild<SvgView*>("pluginSvg_pic") == nullptr; }, 5000), "the plugin gone, its picture goes with its panel");
}

// A training's chart (ui.chart(..., x="step")): the axis is a step, not the run's time, and the points come from a thread
// of the plugin through show() - no run, no on_tick. They are in the plots' section of the plugin, «Что показывать»
// chooses the charts drawn; a step going back is a new training; the plugin gone, the section goes with its panel.
void testStepChart(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/trainer");
    write(folder + "/trainer/plugin.py", R"py(import threading

from kinetica.plugin import command, show, ui

ui.panel("Обучение", ui.chart("Награда", {"best": "лучший", "mean": "средний"}, unit="м", x={"step": "поколение"}), place="bottom")


@command(label="Учить")
def train(scene, generations: int = 5, first: int = 0):
    def loop():
        for g in range(first, first + generations):
            show(step=g, best=10.0 * g, mean=g)
    threading.Thread(target=loop).start()
)py");
    host->start(true);
    TrackChart* chart = nullptr;
    waitUntil([&] { return (chart = plotted(w, "Обучение")) != nullptr && action(w, "plugin:trainer_train") != nullptr; }, 10000);
    auto* plots = w.findChild<PlotPanel*>();
    if (!chart || !plots) return c.check(false, "a step chart comes with its panel, in the plots"), void();
    const PlotSeries* best = chart->tracks().size() == 2 ? chart->tracks()[0].series : nullptr;
    const auto train = [&](int n, size_t points, int first = 0) {
        ops->run("trainer_train", {{"generations", n}, {"first", first}}, [](const QJsonObject&) {}, "ui");
        waitUntil([&] { return best && best->size() == points; }, 5000);
        pump(150);
    };
    train(5, 5);
    chart = plotted(w, "Обучение");
    bool stepped = chart && best && best->size() == 5 && chart->axisWords() == "поколение";
    for (size_t k = 0; stepped && k < best->size(); ++k) stepped = best->time(k) == double(k) && best->value(k) == 10.0 * double(k);
    const bool shown = chart && chart->title() == "Награда, м" && chart->timeRange().second == 4;
    std::printf("  a step chart «%s» in the plots' section «Обучение»: %zu points by generation from a thread's show()\n",
                chart ? qPrintable(chart->title()) : "-", best ? best->size() : 0);
    c.check(stepped, "a step chart takes a point per line whenever one show() carries the x and the line's value, from any thread");
    c.check(shown, "the plugin's chart is in the plots, in a section headed with the panel's title, all its points in sight");
    // «Что показывать»: a chart unticked is not drawn - its section neither, none of its charts ticked, the others take
    // the room; ticked again, it is back. The scene's charts the same; the find line keeps the rows that have its words.
    const bool listed = treeRow(plots, "Обучение") && treeRow(plots, "Обучение")->text(0) == "Награда, м";
    tick(plots, "Обучение", false);
    const bool gone = !plotted(w, "Обучение") && treeRow(plots, "Обучение");
    tick(plots, "Обучение", true);
    c.check(listed && gone && plotted(w, "Обучение"), "«Что показывать» lists the plugin's charts: unticked - not drawn, its section neither; ticked - back");
    const auto sceneCharts = [&] {
        int n = 0;
        for (TrackChart* v : plots->findChildren<TrackChart*>("trackChart")) n += v->isVisibleTo(plots);
        return n;
    };
    const int energy = sceneCharts();
    tick(plots, uiText("Kinetica", "Сцена"), false);
    const int without = sceneCharts();
    tick(plots, uiText("Kinetica", "Сцена"), true);
    c.check(energy > 0 && without == energy - 1 && sceneCharts() == energy, "the scene's charts are ticked there too: the energy goes and comes back");
    auto* find = plots->findChild<QLineEdit*>("plotFind");
    if (find) find->setText("Наград");
    const bool found = treeRow(plots, "Обучение") && !treeRow(plots, "Обучение")->isHidden() && treeRow(plots, uiText("Kinetica", "Сцена")) &&
                       treeRow(plots, uiText("Kinetica", "Сцена"))->isHidden();
    if (find) find->clear();
    c.check(found, "its find line keeps the charts whose words have it, hides the others");
    // The wheel over a chart: its time closer about the mouse, frozen while it is the reader's; a drag moves it; ↺ - all
    // of it again. Shift + the wheel: the values.
    if (TrackChart* v = plotted(w, "Обучение")) {
        const auto [a0, a1] = v->timeRange();
        const QRectF r = v->plotArea();
        const QPointF mid = r.center();
        QWheelEvent in(mid, v->mapToGlobal(mid), {}, {0, 240}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v, &in);
        const auto [b0, b1] = v->timeRange();
        QMouseEvent press(QEvent::MouseButtonPress, mid, v->mapToGlobal(mid), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        const QPointF to = mid + QPointF(r.width() / 4, 0);
        QMouseEvent move(QEvent::MouseMove, to, v->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, to, v->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v, &press), QApplication::sendEvent(v, &move), QApplication::sendEvent(v, &release);
        const auto [c0, c1] = v->timeRange();
        pump(100); // the panel's own window keeps coming: the reader's stays
        const bool kept = v->timeRange().first == c0;
        const QPointF corner(r.right() - 12, r.top() + 10);
        QMouseEvent back(QEvent::MouseButtonPress, corner, v->mapToGlobal(corner), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &back);
        std::printf("  the wheel: %.2f..%.2f -> %.2f..%.2f; dragged a quarter: %.2f..%.2f, kept %s; ↺: %.2f..%.2f\n", a0, a1, b0, b1, c0, c1,
                    kept ? "yes" : "no", v->timeRange().first, v->timeRange().second);
        c.check(b1 - b0 < (a1 - a0) * 0.8 && b0 > a0 && b1 < a1, "the wheel over a chart brings its time closer, about the mouse");
        c.check(c0 < b0 && std::fabs((c1 - c0) - (b1 - b0)) < 1e-9 && kept, "a drag moves the window; the reader's window stays while the data come");
        c.check(!v->zoomed() && v->timeRange() == std::make_pair(a0, a1), "↺ in the plot's corner gives all of it back");
    } else c.check(false, "the step chart is there to zoom");
    // The plots are a state machine: the view is one value; what is not plain is said under the charts with one way back;
    // the value is saved by name and set again through plots_set, read through plots_get - the same as the hands make it.
    {
        const auto call = [&](const QString& op, const QJsonObject& args) {
            QJsonObject answer;
            ops->run(op, args, [&answer](const QJsonObject& r) { answer = r; }, "ui");
            waitUntil([&] { return !answer.isEmpty(); }, 3000);
            return answer;
        };
        auto* words = w.findChild<QLabel*>("plotStateWords"); // on the bar under the charts: the window's
        auto* state = w.findChild<QWidget*>("plotState");
        tick(plots, "Обучение", false);
        const QString said = words ? words->text() : QString();
        const bool shown = state && state->isVisibleTo(&w);
        const QJsonObject saved = call("plots_set", {{"save", "selftest"}});
        if (auto* reset = w.findChild<QToolButton*>("plotResetView")) reset->click();
        pump(50);
        const bool plain = plots->view().hidden.empty() && plotted(w, "Обучение") && state && !state->isVisibleTo(&w);
        const QJsonObject back = call("plots_set", {{"preset", "selftest"}});
        const bool restored = plots->view().hidden.size() == 1 && !plotted(w, "Обучение") && back.value("view").toObject() == saved.value("view").toObject();
        const QJsonObject got = call("plots_get", {});
        const QJsonObject gone = call("plots_set", {{"delete", "selftest"}, {"reset", true}});
        std::printf("  the view's words: «%s», shown %s; reset plain %s; the preset restored %s; plots_get: %d charts, presets after delete %d\n",
                    qPrintable(said), shown ? "yes" : "no", plain ? "yes" : "no", restored ? "yes" : "no", int(got.value("charts").toArray().size()),
                    int(gone.value("presets").toArray().size()));
        c.check(!said.isEmpty() && shown && plain, "what is not the plain view is said under the charts, and «Сбросить вид» gives the plain one");
        c.check(saved.value("ok").toBool() && restored && got.value("view").toObject() == saved.value("view").toObject(),
                "the view is one value: saved by name (plots_set save), set again (preset), read (plots_get) the same");
        c.check(gone.value("ok").toBool() && !gone.value("presets").toArray().contains("selftest") && plotted(w, "Обучение"),
                "plots_set delete removes the preset, reset - every chart back");
        // What «Ещё величины…», «Очистить» and «Сохранить CSV» do, an agent does too (MCP offers every operation).
        const auto shownOf = [](const QJsonObject& r, const QString& id) {
            for (const QJsonValue& ch : r.value("channels").toArray())
                if (ch.toObject().value("id").toString() == id) return ch.toObject().value("shown").toBool();
            return false;
        };
        // A quantity on no chart (a step measures them all), put on and taken off again: the scene's charts are left as
        // they were - the plots keep them by the scene's name for the next tests.
        call("sim_step", {{"frames", 3}});
        QString energy;
        for (const QJsonValue& ch : call("plots_get", {}).value("channels").toArray())
            if (!ch.toObject().value("shown").toBool() && energy.isEmpty()) energy = ch.toObject().value("id").toString();
        const QJsonObject on = call("plots_show", {{"channels", QJsonArray{energy}}});
        const QJsonObject off = call("plots_show", {{"channels", QJsonArray{energy}}, {"on", false}});
        const QJsonObject unknown = call("plots_show", {{"channels", QJsonArray{"no/such"}}});
        const QString csv = QDir::temp().filePath("kinetica-selftest-plots.csv");
        const QJsonObject saved2 = call("plots_save", {{"file", csv}});
        const QJsonObject cleared = call("plots_clear", {});
        call("sim_stop", {});
        std::printf("  plots_show %s off: shown %s, on: shown %s; an unknown one: %s; plots_save %s; plots_clear %s\n", qPrintable(energy),
                    shownOf(off, energy) ? "yes" : "no", shownOf(on, energy) ? "yes" : "no", qPrintable(unknown.value("error").toString()),
                    QFile::exists(csv) ? "wrote the file" : "-", cleared.value("ok").toBool() ? "ok" : "-");
        c.check(!energy.isEmpty() && !shownOf(off, energy) && shownOf(on, energy) && !unknown.value("ok").toBool(),
                "plots_show puts a quantity on its chart and takes it off; an unknown one is refused");
        c.check(saved2.value("ok").toBool() && QFile::exists(csv) && cleared.value("ok").toBool(), "plots_save writes the CSV, plots_clear erases the history");
        QFile::remove(csv);
    }
    // A double click on a chart: it alone in the whole width, a way back over it; again - every chart back.
    const auto visibleCharts = [&] {
        int n = 0;
        for (TrackChart* v : plots->findChildren<TrackChart*>()) n += v->isVisibleTo(plots);
        return n;
    };
    const int all = visibleCharts();
    const auto twice = [&](TrackChart* v) {
        if (!v) return;
        const QPointF mid(v->width() / 2.0, v->height() * 0.6);
        QMouseEvent e(QEvent::MouseButtonDblClick, mid, v->mapToGlobal(mid), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &e);
        pump(50);
    };
    twice(plotted(w, "Обучение"));
    const int alone = visibleCharts();
    const bool back = plots->findChild<QToolButton*>("plotFocusBack") != nullptr;
    twice(plotted(w, "Обучение"));
    std::printf("  charts in the plots: %d; one double-clicked: %d, a way back %s; again: %d\n", all, alone, back ? "yes" : "no", visibleCharts());
    c.check(all > 1 && alone == 1 && back && visibleCharts() == all && !plots->findChild<QToolButton*>("plotFocusBack"),
            "a double click shows the chart alone in the whole width of the plots, another brings every chart back");
    train(3, 3); // the step went back: a new training, a new history
    c.check(best && best->size() == 3 && best->time(0) == 0, "the step going back starts a new history");
    train(2, 2, 2); // a new training starts again at the generation the last one ended on: still a new history
    c.check(best && best->size() == 2 && best->time(0) == 2, "a step not advancing (equal to the last) starts a new history too");
    chart = plotted(w, "Обучение");
    if (!chart) return c.check(false, "the step chart stays in the plots"), void();
    const QString tip = plotHoverText(chart->tracks()[0], 2.4, chart->axisWords());
    c.check(tip.contains("поколение 2") && !tip.contains("2,4"), "a step chart's tooltip says the generation of the nearest point, not the mouse's x");
    for (PlotPanel::Arrangement a : {PlotPanel::Arrangement::Normalized, PlotPanel::Arrangement::Lanes, PlotPanel::Arrangement::Overlay}) {
        plots->setArrangement(a);
        pump(50);
        c.check(plots->pluginSections().size() == 1 && plots->pluginSections()[0].second.size() == 1, "the plugin's section of the plots is in every layout");
        c.check(treeRow(plots, uiText("Kinetica", "Сцена")) != nullptr, "«Что показывать» is there in every layout, «Нормировать» too");
    }
    QDir(folder + "/trainer").removeRecursively();
    c.check(waitUntil([&] { return plots->pluginSections().empty(); }, 5000),
            "the plugin gone, its section of the plots goes with its panel");
}

// Where an object is in the world, its groups composed (worldPose).
Vector3 worldPosition(const SceneGraph& g, uint32_t id) {
    for (const Entity& e : g.entities)
        if (e.id == id) {
            Vector3 p;
            Quaternion q;
            worldPose(g, e, p, q);
            return p;
        }
    return Vector3(0.0f);
}

// A robot from URDF (plugins/io_formats, the editor's own plugin; examples/robot-arm.urdf): «Импорт…» lists the format, the
// import is one undo step - six links in a group, five joints, the root fixed, z up turned to y up (the upper arm's centre
// 0.4 m over the floor), the shoulder's limit written; Play, and the motors hold the arm up. A mesh found by package://
// stands where URDF puts its points: a 0.1 m cube from 0 to 0.1 on each axis has its centre at 0.05, 0.05, -0.05.
void testUrdf(Checker& c, QMainWindow& w, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/io_formats");
    for (const char* file : {"plugin.py", "urdf.py", "gltf.py"}) QFile::copy(resourceDir("plugins") + "/io_formats/" + file, folder + "/io_formats/" + file);
    QDir().mkpath(folder + "/urdf"); // the old name, obsolete: it answers, it is no format and no button
    QFile::copy(resourceDir("plugins") + "/urdf/plugin.py", folder + "/urdf/plugin.py");
    QDir().mkpath(folder + "/kpkg/meshes");
    { // a binary STL: a cube from 0 to 0.1 m, twelve triangles
        QFile f(folder + "/kpkg/meshes/block.stl");
        f.open(QIODevice::WriteOnly);
        QByteArray head(80, 0);
        f.write(head);
        const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
        const int faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {0, 4, 7}, {0, 7, 3}};
        const quint32 n = 12;
        f.write(reinterpret_cast<const char*>(&n), 4);
        for (const auto& t : faces) {
            float v[12] = {0};
            for (int i = 0; i < 3; ++i)
                for (int k = 0; k < 3; ++k) v[3 + 3 * i + k] = 0.1f * float(corner[t[i]][k]);
            f.write(reinterpret_cast<const char*>(v), 48);
            f.write("\0\0", 2);
        }
    }
    write(folder + "/kpkg/block.urdf", R"(<robot name="block"><link name="block"><visual><geometry><mesh filename="package://kpkg/meshes/block.stl"/></geometry></visual><inertial><mass value="1"/></inertial></link></robot>)");
    host->start(true);
    waitUntil([&] { return !host->openerFor("x.urdf").isEmpty(); }, 10000);
    const bool listed = host->formats().contains("URDF — робот (*.urdf)") && host->openerFor("/a/B.URDF") == "io_formats_urdf_load" &&
                        host->formats().count("URDF — робот (*.urdf)") == 1;
    b.newScene();
    const size_t steps = b.undoTaken();
    const auto load = [&](const QString& path) { // a plugin's command answers when the plugin has
        QJsonObject answer;
        ops->run("io_formats_urdf_load", {{"path", path}}, [&answer](const QJsonObject& r) { answer = r; }, "ui");
        waitUntil([&] { return !answer.isEmpty(); }, 20000);
        return answer;
    };
    const QJsonObject made = load(resourceDir("examples") + "/robot-arm.urdf");
    const auto named = [&](const char* n) -> const Entity* {
        for (const Entity& e : b.graph().entities)
            if (e.name == n) return &e;
        return nullptr;
    };
    const Entity *base = named("base_link"), *upper = named("upper_arm"), *forearm = named("forearm");
    const SceneObject* up = upper ? findObject(b.graph(), upper->id) : nullptr;
    const Vector3 at = up ? up->position : Vector3(-9.0f);
    QString limits;
    for (const QString& line : ops->runNow("scene_get", {}).value("text").toString().split('\n'))
        if (line.contains("limit ")) limits += line.trimmed() + "; ";
    const bool built = made.value("links").toInt() == 6 && made.value("joints").toInt() == 5 && b.undoTaken() == steps + 1 && base &&
                       base->rigid.fixed && forearm && !forearm->rigid.fixed && std::fabs(at.y - 0.4f) < 1e-3f && std::fabs(at.x) < 1e-3f &&
                       std::fabs(at.z) < 1e-3f && limits.contains("limit rx lower -1.5 upper 1.5") && limits.contains("limit tx lower 0 upper 0.03");
    std::printf("  URDF: %s; listed %s; %d links, %d joints, %zu undo step; the upper arm at %.3f %.3f %.3f; %s\n",
                qPrintable(made.value("note").toString(made.value("error").toString())), listed ? "yes" : "no", made.value("links").toInt(),
                made.value("joints").toInt(), b.undoTaken() - steps, double(at.x), double(at.y), double(at.z), qPrintable(limits));
    c.check(listed && built, "a URDF arm: «Импорт…» lists the format; one undo step - six links in a group, five joints, the root fixed, "
                             "z up turned to y up, the limits written");
    QJsonObject stepped;
    ops->run("sim_step", {{"frames", 120}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
    waitUntil([&] { return !stepped.isEmpty(); }, 30000);
    QJsonObject state;
    ops->run("sim_state", {}, [&state](const QJsonObject& r) { state = r; }, "ui");
    waitUntil([&] { return !state.isEmpty(); }, 5000);
    float held = -1;
    for (const QJsonValue& v : state.value("shapes").toArray())
        if (forearm && uint32_t(v.toObject().value("id").toInt()) == forearm->id) held = float(v.toObject().value("position").toArray().at(1).toDouble());
    b.stop();
    pump(100);
    std::printf("  2 s of Play: the forearm's centre %.3f m (made at 0.750)\n", double(held));
    c.check(std::fabs(held - 0.75f) < 0.02f, "the imported arm holds itself up: its motors keep the pose it was made in");
    b.newScene();
    const QJsonObject block = load(folder + "/kpkg/block.urdf");
    const Entity* cube = named("block");
    const SceneObject* o = cube ? findObject(b.graph(), cube->id) : nullptr;
    const Vector3 p = o ? o->position : Vector3(-9.0f);
    std::printf("  a mesh by package://: %s; its centre %.3f %.3f %.3f, size %.3f\n", qPrintable(block.value("note").toString(block.value("error").toString())),
                double(p.x), double(p.y), double(p.z), cube ? double(cube->size.x) : -1.0);
    c.check(o && std::fabs(p.x - 0.05f) < 1e-3f && std::fabs(p.y - 0.05f) < 1e-3f && std::fabs(p.z + 0.05f) < 1e-3f && std::fabs(cube->size.x - 0.1f) < 1e-3f,
            "a URDF mesh (package://, binary STL) stands where its points are, at its own size");
    // Out and in again - a metamorphic check of the pair: the arm written to URDF and read back is the same arm, link by
    // link (the place of each, its joints and limits); «Экспорт…» lists the format; the scene is not changed by writing.
    {
        b.newScene();
        load(resourceDir("examples") + "/robot-arm.urdf");
        std::map<std::string, Vector3> first;
        for (const Entity& e : b.graph().entities)
            if (e.rigid.enabled) first[e.name] = worldPosition(b.graph(), e.id);
        const size_t stepsBefore = b.undoTaken();
        const QString out = folder + "/out/arm.urdf";
        QDir().mkpath(folder + "/out");
        QJsonObject saved;
        ops->run("io_formats_urdf_save", {{"path", out}}, [&saved](const QJsonObject& r) { saved = r; }, "ui");
        waitUntil([&] { return !saved.isEmpty(); }, 20000);
        const bool untouched = b.undoTaken() == stepsBefore;
        b.newScene();
        const QJsonObject again = load(out);
        double worst = 0;
        int matched = 0;
        for (const Entity& e : b.graph().entities)
            if (const auto it = first.find(e.name); e.rigid.enabled && it != first.end())
                worst = std::max(worst, double(length(worldPosition(b.graph(), e.id) - it->second))), ++matched;
        std::printf("  URDF out and in: %s; again %d links, %d joints; %d links matched, the worst place %.2g m; listed for «Экспорт…» %s\n",
                    qPrintable(saved.value("note").toString()), again.value("links").toInt(), again.value("joints").toInt(), matched, worst,
                    host->formats(true).contains("URDF — робот (*.urdf)") ? "yes" : "no");
        c.check(saved.value("ok").toBool() && untouched && host->formats(true).contains("URDF — робот (*.urdf)") &&
                    host->openerFor("x.URDF", true) == "io_formats_urdf_save",
                "«Экспорт…» lists URDF; writing it changes nothing in the scene");
        c.check(again.value("links").toInt() == 6 && again.value("joints").toInt() == 5 && matched == 6 && worst < 1e-3,
                "URDF out and in again is the same arm: six links each where it was (under 1 mm), five joints");
    }

    // The old name still answers - forwarded, the same robot -, and it is no menu item.
    b.newScene();
    QJsonObject old;
    ops->run("urdf_load", {{"path", resourceDir("examples") + "/robot-arm.urdf"}}, [&old](const QJsonObject& r) { old = r; }, "ui");
    waitUntil([&] { return !old.isEmpty(); }, 20000);
    std::printf("  the obsolete urdf_load answered: %s\n", QJsonDocument(old).toJson(QJsonDocument::Compact).left(300).constData());
    c.check(old.value("links").toInt() == 6 && !action(w, "plugin:urdf_load"), "the obsolete urdf_load still answers, forwarding to io_formats; no button");
    QDir(folder + "/urdf").removeRecursively();
    QDir(folder + "/io_formats").removeRecursively();
}

} // namespace

// A field that is an object of the scene (field(ref="joint")): the tool an agent gets says the kind and that the field must
// be said; a call without it, or with an object of another kind, is refused - the second naming the joints there are;
// with a joint's id the command gets the joint itself; the button's form offers the scene's joints by name. On a
// component such a field is a list on its card: the encoder chosen there is kept as its id, one undo step.
// And «Контроллер робота» as «Новый плагин…» gives it: a servo with an encoder follows its wave by what the encoder reads.
void testSceneFields(Checker& c, QMainWindow& w, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    b.newScene();
    const auto id = [ops](const char* op, QJsonObject args) { return uint32_t(ops->runNow(op, args).value("id").toInt()); };
    const uint32_t base = id("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}, {"size", QJsonArray{0.2, 0.2, 0.2}},
                                           {"role", "rigid"}, {"fixed", true}});
    const uint32_t arm = id("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.3, 0.5, 0.0}}, {"size", QJsonArray{0.4, 0.05, 0.05}}, {"role", "rigid"}});
    const uint32_t joint = id("assembly_actuator", {{"kind", "rotary"}, {"a", int(arm)}, {"b", int(base)}, {"axis", QJsonArray{0.0, 0.0, 1.0}}});
    const uint32_t encoder = id("assembly_encoder", {{"joint", int(joint)}, {"reference", 0.0}});
    const QString jointName = QString::fromStdString(findJoint(b.graph(), joint)->name);
    QDir().mkpath(folder + "/picker");
    write(folder + "/picker/plugin.py", R"py(from kinetica.plugin import command, component, field


@command(label="Чей сустав")
def whose(scene, joint=field(ref="joint", label="Сустав"), by: float = field(0.2, unit="рад")):
    """Says what it was given."""
    return {"given": type(joint).__name__, "id": joint.id, "servo": joint.servo}


@component(label="Следит")
class Watcher:
    encoder = field(ref="encoder", label="Энкодер")
)py");
    host->start(true);
    const bool came = waitUntil([&] { return action(w, "plugin:picker_whose") != nullptr && !b.componentType("picker.watcher").isEmpty(); }, 10000);
    QJsonObject schema;
    for (const QJsonValue& o : ops->runNow("ops_list", {}).value("ops").toArray())
        if (o.toObject().value("name") == "picker_whose") schema = o.toObject().value("schema").toObject();
    const QJsonObject said = schema.value("properties").toObject().value("joint").toObject();
    const bool told = said.value("type") == "integer" && !said.contains("default") && said.value("description").toString().contains("kind joint") &&
                      schema.value("required").toArray().contains("joint");
    const auto call = [&](const QJsonObject& args) {
        QJsonObject r;
        ops->run("picker_whose", args, [&r](const QJsonObject& a) { r = a; }, "ui");
        waitUntil([&] { return !r.isEmpty(); }, 5000);
        return r;
    };
    const QString none = call({}).value("error").toString(), other = call({{"joint", int(arm)}}).value("error").toString();
    const QJsonObject right = call({{"joint", int(joint)}});
    QString offered;
    int chosen = 0;
    QTimer::singleShot(400, [&w, &offered, &chosen] {
        for (QDialog* d : w.findChildren<QDialog*>("pluginForm")) {
            if (auto* box = d->findChild<QComboBox*>("pluginField_joint")) offered = QString("%1 of %2").arg(box->currentText()).arg(box->count()), chosen = box->currentData().toInt();
            d->reject();
        }
    });
    if (QAction* button = action(w, "plugin:picker_whose")) button->trigger();
    std::printf("  a field of a scene object: the tool says «%s», required %s; no joint: %s; a body for a joint: %s; the joint: %s %d; "
                "the form offers «%s»\n", qPrintable(said.value("description").toString()), told ? "yes" : "no", qPrintable(none), qPrintable(other),
                qPrintable(right.value("given").toString()), right.value("id").toInt(), qPrintable(offered));
    c.check(came && told && none.contains("joint") && other.contains("не «joint»") && other.contains(jointName) && right.value("ok").toBool() &&
            right.value("given").toString() == "Joint" && right.value("id").toInt() == int(joint) && right.value("servo").toBool(),
            "a field of a scene object: its kind in the tool's schema, a call without it or with another kind refused naming the joints, the command gets the joint itself");
    c.check(offered == jointName + " of 1" && chosen == int(joint), "its form offers the scene's joints by name and sends the chosen one's id");

    // On a component: a list on the card, «—» until one is chosen; the encoder chosen is kept as its id.
    b.select(arm);
    ops->runNow("component_set", {{"id", int(arm)}, {"type", "picker.watcher"}});
    b.select(arm);
    pump(100);
    auto* card = w.findChild<QWidget*>("pluginComponent:picker.watcher");
    auto* list = card ? card->findChild<QComboBox*>() : nullptr;
    const QString items = list ? QString("%1 | %2").arg(list->itemText(0), list->count() > 1 ? list->itemText(1) : QString()) : QString("no list");
    const size_t steps = b.undoTaken();
    if (list && list->count() > 1) list->setCurrentIndex(1);
    pump(150);
    const QJsonObject kept = ops->runNow("component_list", {{"type", "picker.watcher"}}).value("components").toArray().at(0).toObject().value("values").toObject();
    std::printf("  on a component's card: «%s»; chosen: encoder %d kept (the scene's is %u), %zu undo step(s)\n", qPrintable(items),
                kept.value("encoder").toInt(), encoder, b.undoTaken() - steps);
    c.check(list && list->count() == 2 && list->itemText(0) == "—" && kept.value("encoder").toInt() == int(encoder) && b.undoTaken() == steps + 1,
            "a component's field of a scene object is a list of them on its card; the one chosen is kept as its id, one undo step");
    QDir(folder + "/picker").removeRecursively();

    // The controller's template, as plugin_new writes it: the joint follows 0.3 sin(t) by its encoder.
    QFile source(resourceDir("clients/python") + "/kinetica/plugin_template_controller.py");
    source.open(QIODevice::ReadOnly);
    QDir().mkpath(folder + "/wave");
    write(folder + "/wave/plugin.py", QString::fromUtf8(source.readAll()).replace("PLUGIN_NAME", "wave"));
    host->start(true);
    const bool ticks = waitUntil([&] { return host->ticking(); }, 10000);
    QJsonObject stepped;
    ops->run("sim_step", {{"frames", 180}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
    waitUntil([&] { return !stepped.isEmpty(); }, 20000);
    double angle = 0;
    for (const QJsonValue& j : stepped.value("joints").toArray())
        if (uint32_t(j.toObject().value("id").toInt()) == joint) angle = j.toObject().value("angle").toDouble();
    const double wave = 0.3 * std::sin(stepped.value("time").toDouble());
    b.stop();
    std::printf("  the controller's template, 3 s: the joint at %.3f rad, its wave at %.3f\n", angle, wave);
    c.check(ticks && std::fabs(angle - wave) < 0.05, "«Контроллер робота» as it is made: a servo with an encoder follows its wave by what the encoder reads");
    QDir(folder + "/wave").removeRecursively();
}

// A plugin's folder made while the editor runs is not seen by itself: plugin_scan starts it. With a folder - a plugins'
// repository - that folder is added to the ones looked through and remembered (the test puts the settings back);
// a folder there is not is refused. plugin_list says where plugins are looked for.
// A filled picture (view_draw's fill), a component's typed line and @on_change, as a plugin of signs or of formulas uses
// them. The picture belongs to its object: painted over it at its place, moved with it, and a click on it takes the
// object; one lies in the world in the view's perspective, one stands in the frame. A line typed in the component's card
// is one edit, and the plugin, told that its components changed, draws anew - as it does after an undo and when the
// component is taken off.
void testFillAndChange(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/signs");
    write(folder + "/signs/plugin.py", R"py(from kinetica.plugin import component, draw, field, on_change

drawn = set()


@component(label="Знак")
class Sign:
    word = field("a", label="Слово")
    size = field(5.0, unit="%", range=(1.0, 50.0), label="Размер")


@on_change
def redraw(scene):
    keys = set()
    for id, values in Sign.instances(scene):
        key = f"{id}/{values['word']}"
        draw.fill(key, ["M -1 -0.5 L 1 -0.5 L 1 0.5 L -1 0.5 Z"], object=id, at=(0, 0.5, 0), space="label", scale=values["size"] / 100)
        keys.add(key)
    for gone in drawn - keys:
        draw.clear(gone)
    drawn.clear()
    drawn.update(keys)
)py");
    host->start(true);
    const bool declared = waitUntil([&] { return !b.componentType("signs.sign").isEmpty(); }, 10000);
    b.newScene();
    const uint32_t cube = uint32_t(ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}}).value("id").toInt());
    v->setFocusBox(AABB(Vector3(-3, -1, -3), Vector3(3, 2, 3)));
    v->frameScene();
    b.select(0);
    pump(100);
    const QJsonArray plate{"M -1 -0.5 L 1 -0.5 C 1 0 1 0 1 0.5 L -1 0.5 Z"}; // two units wide, one high
    const auto centre = [](const QPolygonF& box) { return box.boundingRect().center(); };
    const auto near = [](const QPointF& a, const QPointF& b, double px = 1.5) { return std::hypot(a.x() - b.x(), a.y() - b.y()) < px; };
    const bool put = ops->runNow("view_draw", {{"key", "агент/знак"}, {"fill", plate}, {"object", int(cube)}, {"at", QJsonArray{0, 0.5, 0}}, {"space", "label"}, {"scale", 0.05}})
                         .value("ok").toBool();
    pump(100);
    const QPolygonF label = v->fillBox("агент/знак");
    const bool over = put && !label.isEmpty() && near(centre(label), v->pixelOf(Vector3(0, 1.0f, 0))) &&
                      std::fabs(label.boundingRect().height() - 0.05 * v->height()) < 1.5 && std::fabs(label.boundingRect().width() - 0.1 * v->height()) < 1.5;
    ops->runNow("scene_move", {{"ids", QJsonArray{int(cube)}}, {"by", QJsonArray{0.5, 0.0, 0.0}}});
    pump(100);
    const QPolygonF moved = v->fillBox("агент/знак");
    const bool went = !moved.isEmpty() && near(centre(moved), v->pixelOf(Vector3(0.5f, 1.0f, 0)));
    sendMouse(v, QEvent::MouseButtonPress, centre(moved), Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, centre(moved), Qt::LeftButton, Qt::NoButton);
    pump(100);
    const bool taken = b.selectedId() == cube;
    std::printf("  a label two units wide at 5 %% of a view %d px high: %.0f x %.0f px over its cube %s; the cube moved, it went along %s; a click on it took the cube %s\n",
                v->height(), label.boundingRect().width(), label.boundingRect().height(), over ? "yes" : "NO", went ? "yes" : "NO", taken ? "yes" : "NO");
    c.check(over && went && taken, "a filled picture that belongs to an object: painted over it at its size of the view, moved with it, and a click on it takes the object");

    ops->runNow("view_draw", {{"key", "агент/мир"}, {"fill", plate}, {"at", QJsonArray{0, 1.5, 0}}, {"scale", 0.5}});
    ops->runNow("view_draw", {{"key", "агент/кадр"}, {"fill", plate}, {"space", "screen"}, {"place", QJsonArray{0.25, 0.75}}, {"scale", 0.1}});
    pump(100);
    const QPolygonF world = v->fillBox("агент/мир"), frame = v->fillBox("агент/кадр");
    // In the world a unit is 0.5 m: its box's corners are where the view sees the plane's own, from its left top round.
    const bool lies = world.size() == 4 && near(world[0], v->pixelOf(Vector3(-0.5f, 1.75f, 0))) && near(world[1], v->pixelOf(Vector3(0.5f, 1.75f, 0))) &&
                      near(world[2], v->pixelOf(Vector3(0.5f, 1.25f, 0))) && near(world[3], v->pixelOf(Vector3(-0.5f, 1.25f, 0)));
    const bool stands = !frame.isEmpty() && near(centre(frame), QPointF(0.25 * v->width(), 0.75 * v->height())) && std::fabs(frame.boundingRect().height() - 0.1 * v->height()) < 1.5;
    const auto refused = [&](QJsonObject call) {
        call.insert("key", "агент/нет");
        return !ops->runNow("view_draw", call).value("ok").toBool();
    };
    const bool words = refused({{"fill", QJsonArray{"L 0 0 Z"}}}) && refused({{"fill", plate}, {"space", "sky"}}) && refused({{"fill", plate}, {"object", 99999}}) &&
                       refused({{"fill", plate}, {"scale", 0}}) && !v->hasDrawing("агент/нет");
    // An edge: the same plate in the frame, white, with a red edge a fifth of its unit wide - 0.02 of the view's height
    // outside it, where there was only the scene before.
    ops->runNow("view_draw", {{"key", "агент/кадр"}, {"fill", plate}, {"space", "screen"}, {"place", QJsonArray{0.25, 0.75}}, {"scale", 0.1},
                              {"colour", QJsonArray{1, 1, 1}}, {"stroke", QJsonArray{1, 0, 0}}, {"stroke_width", 0.2}});
    pump(100);
    const QImage edged = v->grabFramebuffer();
    const QRectF plateBox = v->fillBox("агент/кадр").boundingRect();
    const QColor outside = edged.pixelColor(QPointF(plateBox.center().x(), plateBox.top() - 0.01 * v->height()).toPoint()),
                 inside = edged.pixelColor(plateBox.center().toPoint());
    const bool edge = outside.red() > 200 && outside.green() < 60 && inside.green() > 200;
    const bool cleared = ops->runNow("view_draw", {{"clear", true}, {"key", "агент/"}}).value("cleared").toInt() == 3;
    pump(60);
    c.check(lies && stands && words && cleared && v->fillBox("агент/мир").isEmpty(),
            "one in the world lies on its plane in the view's perspective, one on the screen stands at its place of the frame; a row that is no outline, a space "
            "that is none, an object that is not there and a scale of 0 are refused");
    c.check(edge, "a stroke is an edge of its colour round the outline, as wide as asked, under the fill");

    ops->runNow("component_set", {{"id", int(cube)}, {"type", "signs.sign"}});
    const QString first = QString("signs/%1/a").arg(cube), second = QString("signs/%1/bc").arg(cube);
    const bool drew = declared && waitUntil([&] { return v->hasDrawing(first); }, 6000);
    b.select(0), b.select(cube);
    pump(150);
    auto* line = w.findChild<QLineEdit*>("pluginField:word");
    const size_t steps = b.undoTaken();
    if (line) {
        line->setText("bc");
        line->setModified(true); // as typed
        emit line->editingFinished();
    }
    const bool typed = line && waitUntil([&] { return v->hasDrawing(second) && !v->hasDrawing(first); }, 6000);
    const bool one = b.undoTaken() == steps + 1 && ops->runNow("component_list", {{"type", "signs.sign"}}).value("components").toArray().at(0).toObject()
                                                           .value("values").toObject().value("word").toString() == "bc";
    b.undoAction()->trigger();
    const bool undone = waitUntil([&] { return v->hasDrawing(first) && !v->hasDrawing(second); }, 6000);
    ops->runNow("component_remove", {{"id", int(cube)}, {"type", "signs.sign"}});
    const bool off = waitUntil([&] { return !v->hasDrawing(first); }, 6000);
    std::printf("  @on_change: drawn at its start %s; a line typed in the card - redrawn %s, one edit %s; after an undo %s; the component off, its picture off %s\n",
                drew ? "yes" : "NO", typed ? "yes" : "NO", one ? "yes" : "NO", undone ? "yes" : "NO", off ? "yes" : "NO");
    c.check(drew && typed && one && undone && off, "a line typed in a plugin component's card is one edit, and the plugin's @on_change draws anew after it, "
                                                   "after an undo and when the component is taken off");
    QDir(folder + "/signs").removeRecursively();
    waitUntil([&] { return b.componentType("signs.sign").isEmpty(); }, 5000);
}

void testScan(Checker& c, QMainWindow& w, EditorOps* ops, PluginHost* host, const QString& folder) {
    const char* hello = "from kinetica.plugin import command\n\n\n@command(label=\"Привет\")\ndef hello(scene):\n    return {\"said\": True}\n";
    QDir().mkpath(folder + "/late");
    write(folder + "/late/plugin.py", hello);
    pump(600);
    const bool unseen = action(w, "plugin:late_hello") == nullptr;
    const QJsonObject looked = ops->runNow("plugin_scan", {});
    const bool came = waitUntil([&] { return action(w, "plugin:late_hello") != nullptr; }, 10000);
    QTemporaryDir repository;
    QDir().mkpath(repository.path() + "/extra");
    write(repository.path() + "/extra/plugin.py", hello);
    const QVariant remembered = QSettings().value("plugins/folders");
    const QJsonObject added = ops->runNow("plugin_scan", {{"folder", repository.path()}});
    const bool extra = waitUntil([&] { return action(w, "plugin:extra_hello") != nullptr; }, 10000);
    const bool kept = host->folders().contains(repository.path());
    if (remembered.isValid()) QSettings().setValue("plugins/folders", remembered); // the person's own folders, as they were
    else QSettings().remove("plugins/folders");
    const QString none = ops->runNow("plugin_scan", {{"folder", repository.path() + "/нет"}}).value("error").toString();
    const QJsonArray where = ops->runNow("plugin_list", {}).value("folders").toArray();
    std::printf("  a folder made while running: seen by itself %s; plugin_scan started %s; a repository added: started %s, remembered %s; "
                "no such folder: %s; looked through: %lld folder(s)\n", unseen ? "no" : "yes",
                qPrintable(QJsonDocument(looked.value("started").toArray()).toJson(QJsonDocument::Compact)),
                qPrintable(QJsonDocument(added.value("started").toArray()).toJson(QJsonDocument::Compact)), kept ? "yes" : "no", qPrintable(none),
                static_cast<long long>(where.size()));
    c.check(unseen && came && looked.value("started").toArray().contains("late"), "plugin_scan starts a plugin whose folder was made while the editor runs");
    c.check(extra && kept && added.value("started").toArray().contains("extra") && none.contains("нет папки") && where.contains(folder),
            "plugin_scan with a folder adds a plugins' repository and remembers it; a folder there is not is refused; plugin_list says where plugins are looked for");
    QDir(folder + "/late").removeRecursively();
}

// A plugin's gizmos: a component's angle is an orange point on a circle around the shape
// (a dial), its size three points on the shape's faces (a box) - each dragged sets its field, one undo step; a tool whose
// command takes `to` is dragged from a point to a point and gets both.
void testGizmos(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/gizmos");
    write(folder + "/gizmos/plugin.py", R"py(from kinetica.plugin import command, component, field


@component(label="Заслонка")
class Flap:
    angle = field(30.0, unit="°", range=(0.0, 360.0), label="Угол", handle="dial")
    size = field((0.4, 0.2, 0.6), label="Габарит", handle="box")


@command(label="Отрезок", tool=True)
def span(scene, point, to, object, to_object):
    """A bar from the point pressed to the point the drag is let go at."""
    length = sum((b - a) ** 2 for a, b in zip(point, to)) ** 0.5
    middle = [(a + b) / 2 for a, b in zip(point, to)]
    scene.add("box", name=f"отрезок {object}-{to_object}", position=middle, size=(max(length, 0.01), 0.02, 0.02))
    return {"length": length}
)py");
    host->start(true);
    const bool came = waitUntil([&] { return action(w, "plugin:gizmos_span") != nullptr && !b.componentType("gizmos.flap").isEmpty(); }, 10000);
    b.newScene();
    const uint32_t cube = uint32_t(ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}, {"size", QJsonArray{0.4, 0.4, 0.4}}}).value("id").toInt());
    ops->runNow("component_set", {{"id", int(cube)}, {"type", "gizmos.flap"}});
    b.select(cube);
    v->setFocusBox(AABB(Vector3(-2, -1, -2), Vector3(2, 2, 2)));
    v->frameScene();
    pump(150);
    const auto kept = [&](const char* field) {
        return ops->runNow("component_list", {{"type", "gizmos.flap"}}).value("components").toArray().at(0).toObject().value("values").toObject().value(field);
    };
    const auto drag = [&](const QPointF& from, const QPointF& to) {
        sendMouse(v, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 12; ++i) sendMouse(v, QEvent::MouseMove, from + (to - from) * (i / 12.0), Qt::NoButton, Qt::LeftButton);
        sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        pump(120);
    };
    std::vector<ShapeHandle> points = v->shapeHandles();
    const size_t shown = points.size();
    size_t steps = b.undoTaken();
    double angle = 0, wide = 0;
    size_t dialSteps = 0, boxSteps = 0;
    if (shown == 4 && points[0].guide.size() == 49) {
        drag(v->pixelOf(points[0].centre), v->pixelOf(points[0].guide[12])); // a quarter of its circle on: 90 degrees
        angle = kept("angle").toDouble(), dialSteps = b.undoTaken() - steps, steps = b.undoTaken();
        points = v->shapeHandles();
        const ShapeHandle& face = points[1]; // the size's x
        drag(v->pixelOf(face.centre + face.axis * face.reach), v->pixelOf(face.centre + face.axis * 0.5f));
        wide = kept("size").toArray().at(0).toDouble(), boxSteps = b.undoTaken() - steps;
    }
    const QJsonArray size = kept("size").toArray();
    std::printf("  a component's gizmos: %zu orange points (a dial and a box's three); the dial dragged a quarter round: %.1f deg, %zu undo "
                "step(s); the box's x face dragged to 0.5 m: size %.3f %.3f %.3f, %zu undo step(s)\n", shown, angle, dialSteps, wide,
                size.at(1).toDouble(), size.at(2).toDouble(), boxSteps);
    c.check(came && shown == 4 && std::fabs(angle - 90.0) < 2.0 && dialSteps == 1,
            "a dial: a component's angle is an orange point on a circle around the shape; dragged round, it sets the field in the field's degrees - one undo step");
    c.check(std::fabs(wide - 1.0) < 0.03 && std::fabs(size.at(1).toDouble() - 0.2) < 1e-6 && std::fabs(size.at(2).toDouble() - 0.6) < 1e-6 && boxSteps == 1,
            "a box: a component's size is a point on each face; one dragged sets that size, the other two stay - one undo step");

    // The tool: pressed on the floor at one point, let go at another - a bar between them, as long as the drag.
    b.select(0);
    QAction* tool = action(w, "plugin:gizmos_span");
    if (tool) tool->trigger();
    pump(50);
    const bool drags = v->pluginTool() == "gizmos_span" && v->pluginToolDrags();
    steps = b.undoTaken();
    drag(v->pixelOf(Vector3(1.0f, 0.0f, 1.0f)), v->pixelOf(Vector3(1.6f, 0.0f, 1.0f)));
    const Entity* bar = nullptr;
    waitUntil([&] {
        for (const Entity& e : b.graph().entities)
            if (e.name.rfind("отрезок", 0) == 0) bar = &e;
        return bar != nullptr;
    }, 10000);
    pump(100);
    const SceneObject* placed = bar ? findObject(b.graph(), bar->id) : nullptr;
    const float length = bar ? resolveInstance(b.graph(), *bar).size.x : 0;
    std::printf("  a drag tool: taken %s; dragged 0.6 m over the floor -> «%s», %.3f m long, its middle at x %.3f (%zu undo step)\n", drags ? "yes" : "no",
                bar ? bar->name.c_str() : "-", double(length), placed ? double(placed->position.x) : 0.0, b.undoTaken() - steps);
    c.check(drags && bar && bar->name == "отрезок 0-0" && std::fabs(length - 0.6f) < 0.02f && placed && std::fabs(placed->position.x - 1.3f) < 0.02f &&
            b.undoTaken() == steps + 1,
            "a plugin's tool with `to` is dragged: the point pressed and the point let go both reach its command - one undo step");
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(v, &esc);
    QDir(folder + "/gizmos").removeRecursively();
}

int runPluginTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString&) {
    Checker c;
    auto* host = w.findChild<PluginHost*>();
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    QTemporaryDir dir;
    if (!host || !ops || !dir.isValid()) return c.check(false, "the window hosts plugins"), c.failures;
    QDir().mkpath(dir.path() + "/lifter");
    const QString file = dir.path() + "/lifter/plugin.py";
    write(file, QString(kLifter).replace("WORDS", "Поднять"));
    qputenv("KE_PLUGINS", dir.path().toUtf8());
    QElapsedTimer clock;
    clock.start();
    host->start(true);
    const bool came = waitUntil([&] { return action(w, "plugin:lifter_lift") != nullptr; }, 10000);
    const qint64 startMs = clock.elapsed();
    QAction* lift = action(w, "plugin:lifter_lift");
    std::printf("  a plugin.py started: its command in %lld ms, the button «%s» %s\n", static_cast<long long>(startMs),
                lift ? qPrintable(lift->text()) : "-", lift ? qPrintable(lift->shortcut().toString()) : "");
    c.check(came && lift->shortcut().toString() == "Ctrl+Shift+U", "a plugin's command comes as a button of «Плагины» with its key");
    if (!came) return qunsetenv("KE_PLUGINS"), c.failures;

    // Called: two cubes up by its default, two edits - one Ctrl+Z.
    b.newScene();
    const uint32_t a = made(b, b.createActions()[0]), bb = made(b, b.createActions()[0]);
    b.selectMany({a, bb});
    const auto y = [&b](uint32_t id) { return findObject(b.graph(), id)->position.y; };
    const float a0 = y(a), b0 = y(bb);
    QJsonObject answer;
    ops->run("lifter_lift", {}, [&answer](const QJsonObject& r) { answer = r; }, "ui");
    waitUntil([&] { return !answer.isEmpty(); }, 5000);
    const float a1 = y(a), b1 = y(bb);
    b.undoAction()->trigger();
    pump(50);
    std::printf("  lifter_lift: %s, the cubes up %.3f and %.3f m; one Ctrl+Z: %.3f and %.3f m\n", answer.value("ok").toBool() ? "ok" : "failed",
                double(a1 - a0), double(b1 - b0), double(y(a) - a0), double(y(bb) - b0));
    c.check(std::fabs(a1 - a0 - 0.1f) < 1e-4f && std::fabs(b1 - b0 - 0.1f) < 1e-4f && y(a) == a0 && y(bb) == b0,
            "the command lifts both cubes; its two edits are one undo step");

    // Its button: the form of its field, a value typed, the call with it.
    QTimer::singleShot(400, [&w] {
        for (QDialog* d : w.findChildren<QDialog*>("pluginForm")) {
            if (auto* box = d->findChild<QDoubleSpinBox*>("pluginField_height")) box->setValue(0.25);
            d->accept();
        }
    });
    lift->trigger();
    waitUntil([&] { return std::fabs(y(a) - a0 - 0.25f) < 1e-4f; }, 5000);
    c.check(std::fabs(y(a) - a0 - 0.25f) < 1e-4f, "its button opens the form of its field and runs with the value given");

    // Its panel: a tab of the inspector; its field's 0.3, its button: the cubes up by that; the value it shows set.
    QWidget* panel = nullptr;
    for (QWidget* x : w.findChildren<QWidget*>("pluginPanel")) panel = x;
    auto* press = panel ? panel->findChild<QPushButton*>("pluginButton_lift") : nullptr;
    const float before = y(a);
    if (press) press->click();
    waitUntil([&] { return std::fabs(y(a) - before - 0.3f) < 1e-4f; }, 5000);
    auto* shown = panel ? panel->findChild<QLabel*>("pluginValue_moved") : nullptr;
    waitUntil([&] { return shown && shown->text() == "Поднято: 2"; }, 3000);
    std::printf("  its panel: %s; «Поднять» - the cube up %.3f m (its field 0.3); the value says «%s»\n", panel ? "a tab" : "none",
                double(y(a) - before), shown ? qPrintable(shown->text()) : "-");
    c.check(press && std::fabs(y(a) - before - 0.3f) < 1e-4f && shown && shown->text() == "Поднято: 2",
            "a plugin's panel: its button runs the command with the panel's field, its value shows what the plugin said");

    // An error in a command: the author's file and line in the answer, and what it had built taken back.
    const size_t objects = b.graph().entities.size();
    QJsonObject failed;
    ops->run("lifter_broken", {}, [&failed](const QJsonObject& r) { failed = r; }, "ui");
    waitUntil([&] { return !failed.isEmpty(); }, 5000);
    const QString why = failed.value("error").toString();
    std::printf("  lifter_broken: %s\n", qPrintable(why));
    c.check(!failed.value("ok").toBool() && why.contains("plugin.py:16") && why.contains("ZeroDivisionError"),
            "an error in a command answers with the author's file and line");
    c.check(b.graph().entities.size() == objects && why.contains("отменены"),
            "a command that fails changes nothing: the cube it had added is taken back, and the answer says so");

    // A second plugin in a folder named in Russian: its name spelt in Latin (it was «___»); its print() in the console as it
    // comes (it was seen only after a fall); a worker thread of it falling said on the status bar while it lives on; its
    // folder deleted - the editor forgets it (it stayed «запускается» for ever).
    QStringList printed;
    const auto heard = QObject::connect(host, &PluginHost::output, [&printed](const QString& p, const QString& line) { printed << p + ": " + line; });
    const QString second = dir.path() + "/Мой плагин";
    QDir().mkpath(second);
    write(second + "/plugin.py", "import sys\nimport threading\nfrom kinetica.plugin import command\n\nprint('привет из плагина')\n"
                                 "sys.stdout.write('строка Windows\\r\\n')\n\n\n"
                                 "@command(label='Ничего')\ndef nothing(scene):\n    return {}\n\n\n"
                                 "def worker():\n    raise KeyError('нет такого')\n\n\nthreading.Timer(0.5, worker).start()\n");
    host->start(true);
    const bool named = waitUntil([&] { return action(w, "plugin:moy_plagin_nothing") != nullptr; }, 10000);
    const bool heardIt = waitUntil([&] { return printed.contains("moy_plagin: привет из плагина") && printed.contains("moy_plagin: строка Windows"); }, 3000);
    QString background;
    waitUntil([&] {
        if (auto* x = w.findChild<QLabel*>("pluginNoticeText"); x && x->isVisible() && x->text().contains("в фоне")) background = x->text();
        return !background.isEmpty();
    }, 6000);
    QDir(second).removeRecursively();
    const bool forgotten = waitUntil([&] { return !host->names().contains("moy_plagin"); }, 5000);
    QObject::disconnect(heard);
    std::printf("  «Мой плагин»: %s; its print heard %s; a worker falling: «%s»; its folder deleted, forgotten %s\n",
                named ? "moy_plagin_nothing" : "not named", heardIt ? "yes" : "no", qPrintable(background), forgotten ? "yes" : "no");
    c.check(named && heardIt, "a plugin in a folder named in Russian is moy_plagin; its print() reaches the API console as it comes");
    c.check(background.contains("KeyError") && forgotten, "a worker thread falling is said on the status bar; its folder deleted, the editor forgets the plugin");

    // Saved: back with its new words - they come from a module of it, words.py; saved there, it is back with those.
    clock.restart();
    const QString module = QFileInfo(file).absolutePath() + "/words.py";
    write(module, "WORDS = 'Поднять выше'\n");
    write(file, "import words\n" + QString(kLifter).replace("\"WORDS\"", "words.WORDS"));
    const bool reloaded = waitUntil([&] { QAction* x = action(w, "plugin:lifter_lift"); return x && x->text().startsWith("Поднять выше"); }, 8000);
    const qint64 reloadMs = clock.elapsed();
    write(module, "WORDS = 'Поднять ещё'\n");
    const bool moduleReloaded = waitUntil([&] { QAction* x = action(w, "plugin:lifter_lift"); return x && x->text().startsWith("Поднять ещё"); }, 8000);
    std::printf("  saved: the button «Поднять выше…» back in %lld ms; its module saved: «Поднять ещё…» %s\n", static_cast<long long>(reloadMs),
                moduleReloaded ? "back" : "not back");
    c.check(reloaded, "a saved plugin.py restarts its plugin with the new words");
    c.check(moduleReloaded, "a saved module beside plugin.py restarts its plugin too");

    // Fallen over at its line 3: a banner says where; the editor goes on.
    write(file, "from kinetica.plugin import command\n\nraise RuntimeError(\"сломано\")\n");
    QString banner;
    const bool said = waitUntil([&] {
        if (auto* x = w.findChild<QLabel*>("pluginNoticeText"); x && x->isVisible() && x->text().contains("plugin.py:3")) banner = x->text();
        return !banner.isEmpty();
    }, 8000);
    std::printf("  fallen: «%s»; the lifter's button gone %s\n", qPrintable(banner), action(w, "plugin:lifter_lift") ? "no" : "yes");
    c.check(said && banner.contains("сломано") && !action(w, "plugin:lifter_lift"), "a plugin that falls over says its file and line on the status bar");
    const QString gone = ops->runNow("lifter_lift", {}).value("error").toString();
    std::printf("  its command called now: %s\n", qPrintable(gone));
    c.check(gone.contains("упал") && gone.contains("plugin.py:3"), "a fallen plugin's command, called, says the plugin fell and where - not «no operation»");
    testRunHooks(c, b, ops, host, dir.path());
    testComponents(c, w, b, v, ops, host, dir.path());
    testDrawAndTool(c, w, b, v, ops, host, dir.path());
    testChart(c, w, b, ops, host, dir.path());
    testStepChart(c, w, ops, host, dir.path());
    testSvg(c, w, ops, host, dir.path());
    testWords(c, w, ops, host, dir.path());
    testUrdf(c, w, b, ops, host, dir.path());
    testSceneFields(c, w, b, ops, host, dir.path());
    testGizmos(c, w, b, v, ops, host, dir.path());
    testFillAndChange(c, w, b, v, ops, host, dir.path());
    testScan(c, w, ops, host, dir.path());
    qunsetenv("KE_PLUGINS");
    return c.failures;
}
