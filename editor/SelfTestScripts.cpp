// Scripts by hand (see SelfTest.h; ScriptRunner.h): script_run runs a Python file that builds the scene - three boxes
// come, its print() is heard and is the answer's output, and one Ctrl+Z takes all three back; a script that falls after
// building says its file and line and leaves nothing; a file that is not there is refused; every example of
// examples/scripts runs to its end on a new scene - the row of cubes, the UR5e with a pyramid, the cart with a range finder.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "ScriptRunner.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace selftest;

int runScriptTests(QMainWindow& w, SceneBuilder& b, Viewport*, const QString&) {
    Checker c;
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    auto* runner = w.findChild<ScriptRunner*>();
    QTemporaryDir dir;
    if (!ops || !runner || !dir.isValid()) return c.check(false, "the window runs scripts"), c.failures;
    const auto write = [&dir](const char* name, const char* text) {
        QFile f(dir.path() + "/" + name);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(text);
        return f.fileName();
    };
    const auto run = [&](const QString& file) {
        QJsonObject answer;
        ops->run("script_run", {{"file", file}}, [&answer](const QJsonObject& r) { answer = r; }, "ui");
        waitUntil([&] { return !answer.isEmpty(); }, 30000);
        return answer;
    };
    QStringList heard;
    const auto listening = QObject::connect(runner, &ScriptRunner::output, [&heard](const QString& script, const QString& line) { heard << script + ": " + line; });

    b.newScene();
    const size_t before = b.graph().entities.size(), steps = b.undoTaken();
    const QJsonObject built = run(write("three.py", "import kinetica as ke\n\nscene = ke.connect()\nfor k in range(3):\n"
                                                    "    scene.add(\"box\", position=(k * 0.3, 0.1, 0))\nprint(\"готово\", 3)\n"));
    const size_t made = b.graph().entities.size() - before, taken = b.undoTaken() - steps;
    const bool said = heard.contains("three.py: готово 3") && built.value("output").toArray().contains("готово 3");
    b.undoAction()->trigger();
    pump(100);
    const size_t left = b.graph().entities.size() - before;
    std::printf("  three.py: %s, %zu boxes, %zu undo step(s), its print heard %s; one Ctrl+Z leaves %zu\n", built.value("ok").toBool() ? "ok" : "failed", made,
                taken, said ? "yes" : "no", left);
    c.check(built.value("ok").toBool() && made == 3 && said, "script_run runs a Python file that builds the scene; its print() is the answer's output");
    c.check(taken == 1 && left == 0, "everything a script did is one undo step");

    const QJsonObject fell = run(write("falls.py", "import kinetica as ke\n\nscene = ke.connect()\nscene.add(\"box\")\nprint(1 / 0)\n"));
    const QString why = fell.value("error").toString();
    std::printf("  falls.py: %s\n", qPrintable(why));
    c.check(!fell.value("ok").toBool() && why.contains("falls.py:5") && why.contains("ZeroDivisionError"), "a script that falls says its file and line");
    c.check(b.graph().entities.size() == before && why.contains("отменены"), "a script that falls changes nothing: the box it had added is taken back");
    c.check(run(dir.path() + "/none.py").value("error").toString().contains("no such script"), "a file that is not there is refused");

    b.newScene();
    const size_t empty = b.graph().entities.size();
    const QJsonObject example = run(MainWindow::examplesDir() + "/scripts/cubes_row.py");
    std::printf("  examples/scripts/cubes_row.py: %s, %zu cubes\n", example.value("ok").toBool() ? "ok" : qPrintable(example.value("error").toString()),
                b.graph().entities.size() - empty);
    c.check(example.value("ok").toBool() && b.graph().entities.size() - empty == 5, "the editor's own example script puts its row of cubes");
    // Every example: on a new scene, to its end, saying something.
    const QDir examples(MainWindow::examplesDir() + "/scripts");
    QStringList ran, broken;
    for (const QString& name : examples.entryList({"*.py"}, QDir::Files, QDir::Name)) {
        b.newScene();
        const QJsonObject r = run(examples.filePath(name));
        const QString last = r.value("output").toArray().last().toString();
        std::printf("  %s: %s\n", qPrintable(name), r.value("ok").toBool() ? qPrintable(last) : qPrintable(r.value("error").toString()));
        (r.value("ok").toBool() && !last.isEmpty() ? ran : broken) << name;
    }
    c.check(ran.size() >= 3 && broken.isEmpty(), "every script of examples/scripts runs to its end on a new scene and says what it did");
    b.stop();
    b.newScene();
    QObject::disconnect(listening);
    return c.failures;
}
