// Plugins by hand (see SelfTest.h; the host: PluginHost.h, the Python side: python/kinetica/plugin.py): a plugin.py in a
// folder of KE_PLUGINS starts and its commands come - a button of «Плагины» with its key; called, it lifts two selected
// cubes with two edits and one Ctrl+Z takes both back; its button opens the form of its field and runs with the value
// given; its panel - a tab of the inspector - runs it with the panel's field and shows the value it set; an error in a command answers with the author's file and line; the file saved, the plugin is back within a
// few seconds with its new words; a plugin that falls over at its line 3 says so on the status bar - and the editor goes on.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "PluginHost.h"

#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
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


} // namespace

int runPluginTests(QMainWindow& w, SceneBuilder& b, Viewport*, const QString&) {
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
    qunsetenv("KE_PLUGINS");
    return c.failures;
}
