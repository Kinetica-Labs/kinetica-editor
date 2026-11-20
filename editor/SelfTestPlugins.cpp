// Plugins by hand (see SelfTest.h; the host: PluginHost.h, the Python side: python/kinetica/plugin.py): a plugin.py in a
// folder of KE_PLUGINS starts and its commands come - a button of «Плагины» with its key; called, it lifts two selected
// cubes with two edits and one Ctrl+Z takes both back; its button opens the form of its field and runs with the value
// given; its panel - a tab of the inspector - runs it with the panel's field and shows the value it set; an error in a command answers with the author's file and line; the file saved, the plugin is back within a
// few seconds with its new words; a plugin that falls over at its line 3 says so on the status bar - and the editor goes on.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "PluginHost.h"
#include "Resources.h"

#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QApplication>
#include <QKeyEvent>
#include <QToolBar>
#include "scene/Assembly.h"
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

using namespace ke;
using namespace selftest;

namespace {

const char* kLifter = R"py(from kinetica.plugin import command, field, run, show, ui


@command(label="WORDS", key="Ctrl+Shift+U")
def lift(scene, height: float = field(0.1, unit="м", range=(-1.0, 1.0), label="На сколько")):
    ids = scene.client.call("selection_get")["ids"]
    for i in ids:  # an edit an object: still one undo step
        scene.client.call("scene_move", ids=[i], by=[0, height, 0])
    show(moved=len(ids))
    return {"moved": len(ids)}


@command(label="Ошибка")
def broken(scene):
    scene.client.call("scene_add", shape="box")  # half built when it falls: the call taken back whole
    return 1 / 0


ui.panel("Подъём", ui.section("Выделенное", ui.number("height", 0.3, unit="м", range=(-1.0, 1.0), label="На сколько"),
                              ui.button(lift, "Поднять")), ui.value("moved", "Поднято", 0))
run()
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
// lockstep), sees its encoder's reading and no truth; Stop leaves the scene as authored and the journal without a line
// of what the ticks did.
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
    write(file, QString(R"py(from kinetica.plugin import on_play, on_stop, on_tick

ticks = 0


@on_play
def start(scene):
    global ticks
    ticks = 0


@on_tick(world=True)
def hold(scene, dt, bodies):
    return {CUBE: {"force": [0, bodies[CUBE]["mass"] * 9.81, 0]}}


@on_tick
def control(scene, dt, sensors):
    global ticks
    ticks += 1
    if ticks == 5:
        kinds = sorted(s["kind"] for s in sensors.values())
        print("seen", kinds, "truth" in str(sensors))
    return {JOINT: {"target": 0.5}}


@on_stop
def stop(scene):
    print("ticks", ticks)
)py").replace("CUBE", QString::number(cube)).replace("JOINT", QString::number(joint)));
    QStringList printed;
    const auto heard = QObject::connect(host, &PluginHost::output, [&printed](const QString&, const QString& line) { printed << line; });
    host->start(true);
    waitUntil([&] { return host->ticking(); }, 10000);
    const auto journal = [ops] { return ops->runNow("ops_journal", {{"last", 500}}).value("lines").toArray().size(); };
    const qsizetype lines0 = journal();
    const float y0 = findObject(b.graph(), cube)->position.y;
    QJsonObject stepped;
    ops->run("sim_step", {{"frames", 60}}, [&stepped](const QJsonObject& r) { stepped = r; }, "ui");
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
    const bool told = waitUntil([&] { return printed.filter("ticks").size() > 0; }, 5000);
    QObject::disconnect(heard);
    const QString seen = printed.filter("seen").value(0), counted = printed.filter("ticks").value(0);
    const qsizetype added = journal() - lines0;
    const bool lines = added <= 2; // at most sim_stop (sim_step is the window's time): nothing of the ticks
    const float after = float(findJoint(b.graph(), joint)->dof[DofRx].drive.target);
    std::printf("  60 frames with the run's hooks: the cube %.4f -> %.4f m (held), the joint at %.3f rad (commanded 0.5); %s; %s; "
                "the journal +%lld lines; the servo's authored target %.3f -> %.3f\n", double(y0), double(y), double(angle),
                qPrintable(seen), qPrintable(counted), static_cast<long long>(added), double(authored), double(after));
    c.check(std::fabs(y - y0) < 0.01f, "a physics plugin's on_tick(world=True) pushes a body every frame: m g up holds a cube in the air");
    c.check(std::fabs(angle - 0.5f) < 0.05f && seen.contains("encoder") && seen.endsWith("False"),
            "a controller's on_tick sees its encoder's reading, not the truth, and turns the servo by its command");
    c.check(told && counted == "ticks 61", "the lockstep: one tick a frame - the starting one too -, before the next is stepped");
    c.check(lines && std::fabs(after - authored) < 1e-6f, "Stop: the scene as authored, no journal line of what the ticks did");
    QDir(folder + "/ticker").removeRecursively();
}

// A plugin's component as a built-in one (Cinema 4D's orange handles): it comes in «+ Добавить компонент», is added with its
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
    const size_t steps = b.undoSteps();
    if (add) add->trigger();
    pump(100);
    const Entity* e = findEntity(b.graph(), cube);
    const bool added = add && e && e->components.size() == 1 && e->components[0].type == "turbines.turbine" && b.undoSteps() == steps + 1;
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
    const size_t before = b.undoSteps();
    const auto setLines = [ops] {
        int n = 0;
        for (const QJsonValue& l : ops->runNow("ops_journal", {{"last", 500}}).value("lines").toArray()) n += l.toObject().value("op").toString() == "component_set";
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
                b.undoSteps() - before, lines);
    c.check(std::fabs(now - 2.0) < 0.03 && b.undoSteps() == before + 1 && lines == 1,
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
    const bool kept = line != std::string::npos && text.find(" radius 2", line) < text.find('\n', line) && back.load(text, error) && back.save() == text;
    std::printf("  the plugin gone: «%s»; the scene keeps its words %s\n", qPrintable(note.left(80)), kept ? "yes" : "no");
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
    const bool onShelf = tool && shelf && shelf->actions().contains(tool) && !tool->icon().isNull();
    b.newScene();
    const uint32_t cube = uint32_t(ops->runNow("scene_add", {{"shape", "box"}, {"position", QJsonArray{0.0, 0.5, 0.0}}, {"size", QJsonArray{1.0, 1.0, 1.0}}, {"role", "collider"}}).value("id").toInt());
    b.select(0);
    v->setFocusBox(AABB(Vector3(-2, 0, -2), Vector3(2, 2, 2)));
    v->frameScene();
    pump(100);
    if (tool) tool->trigger();
    pump(50);
    const bool taken = v->pluginTool() == "marks_flag" && tool && tool->isChecked();
    const size_t steps = b.undoSteps();
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
                         b.undoSteps() == steps + 1;
    const bool drawn = v->hasDrawing("marks/флажок") && v->hasDrawing("marks/где");
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(v, &esc);
    const bool left = v->pluginTool().isEmpty() && !tool->isChecked();
    std::printf("  the tool: on the shelf %s, taken %s; a click on the cube's top -> «%s» at %.3f %.3f %.3f (%zu undo step); drawn %s; "
                "Esc leaves it %s\n", onShelf ? "yes" : "no", taken ? "yes" : "no", sphere ? sphere->name.c_str() : "-", double(at.x),
                double(at.y), double(at.z), b.undoSteps() - steps, drawn ? "yes" : "no", left ? "yes" : "no");
    c.check(onShelf && taken && clicked && drawn && left, "a plugin's tool: an orange button on the shelf, a click gives it the point, "
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
    const bool api = agent.value("ok").toBool() && !odd.value("ok").toBool() && ops->runNow("view_draw", {{"clear", true}, {"key", "агент/"}}).value("cleared").toInt() == 1;
    QDir(folder + "/marks").removeRecursively();
    const bool withPlugin = waitUntil([&] { return !v->hasDrawing("marks/флажок"); }, 5000);
    std::printf("  on_tick's picture while playing %s, gone at Stop %s (the tool's stays); view_draw from the API %s; the plugin gone, "
                "its pictures too %s\n", ticking ? "yes" : "no", gone ? "yes" : "no", api ? "yes" : "no", withPlugin ? "yes" : "no");
    c.check(ticking && gone && api && withPlugin, "on_tick draws while the scene plays, gone at Stop; view_draw for any client; a plugin's "
                                                  "pictures go with it");
}

// A robot from URDF (plugins/urdf, the editor's own plugin; examples/robot-arm.urdf): «Импорт…» lists the format, the
// import is one undo step - six links in a group, five joints, the root fixed, z up turned to y up (the upper arm's centre
// 0.4 m over the floor), the shoulder's limit written; Play, and the motors hold the arm up. A mesh found by package://
// stands where URDF puts its points: a 0.1 m cube from 0 to 0.1 on each axis has its centre at 0.05, 0.05, -0.05.
void testUrdf(Checker& c, QMainWindow& w, SceneBuilder& b, EditorOps* ops, PluginHost* host, const QString& folder) {
    QDir().mkpath(folder + "/urdf");
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
    const bool listed = host->formats().contains("URDF — робот (*.urdf)") && host->openerFor("/a/B.URDF") == "urdf_load";
    b.newScene();
    const size_t steps = b.undoSteps();
    const auto load = [&](const QString& path) { // a plugin's command answers when the plugin has
        QJsonObject answer;
        ops->run("urdf_load", {{"path", path}}, [&answer](const QJsonObject& r) { answer = r; }, "ui");
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
    for (const QJsonValue& v : ops->runNow("scene_get", {}).value("text").toString().split('\n'))
        if (v.toString().contains("limit ")) limits += v.toString().trimmed() + "; ";
    const bool built = made.value("links").toInt() == 6 && made.value("joints").toInt() == 5 && b.undoSteps() == steps + 1 && base &&
                       base->rigid.fixed && forearm && !forearm->rigid.fixed && std::fabs(at.y - 0.4f) < 1e-3f && std::fabs(at.x) < 1e-3f &&
                       std::fabs(at.z) < 1e-3f && limits.contains("limit rx lower -1.5 upper 1.5") && limits.contains("limit tx lower 0 upper 0.03");
    std::printf("  URDF: %s; listed %s; %d links, %d joints, %zu undo step; the upper arm at %.3f %.3f %.3f; %s\n",
                qPrintable(made.value("note").toString(made.value("error").toString())), listed ? "yes" : "no", made.value("links").toInt(),
                made.value("joints").toInt(), b.undoSteps() - steps, double(at.x), double(at.y), double(at.z), qPrintable(limits));
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
    QDir(folder + "/urdf").removeRecursively();
}

} // namespace

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
    write(second + "/plugin.py", "import threading\nfrom kinetica.plugin import command\n\nprint('привет из плагина')\n\n\n"
                                 "@command(label='Ничего')\ndef nothing(scene):\n    return {}\n\n\n"
                                 "def worker():\n    raise KeyError('нет такого')\n\n\nthreading.Timer(0.5, worker).start()\n");
    host->start(true);
    const bool named = waitUntil([&] { return action(w, "plugin:moy_plagin_nothing") != nullptr; }, 10000);
    const bool heardIt = waitUntil([&] { return printed.contains("moy_plagin: привет из плагина"); }, 3000);
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

    // Saved: back with its new words.
    clock.restart();
    write(file, QString(kLifter).replace("WORDS", "Поднять выше"));
    const bool reloaded = waitUntil([&] { QAction* x = action(w, "plugin:lifter_lift"); return x && x->text().startsWith("Поднять выше"); }, 8000);
    const qint64 reloadMs = clock.elapsed();
    std::printf("  saved: the button «Поднять выше…» back in %lld ms\n", static_cast<long long>(reloadMs));
    c.check(reloaded, "a saved plugin.py restarts its plugin with the new words");

    // Fallen over at its line 3: a banner says where; the editor goes on.
    write(file, "from kinetica.plugin import run\n\nraise RuntimeError(\"сломано\")\nrun()\n");
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
    testUrdf(c, w, b, ops, host, dir.path());
    qunsetenv("KE_PLUGINS");
    return c.failures;
}
