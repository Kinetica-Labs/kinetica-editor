// The journal as a way back (EditorOps.h): a session of a hand's and an agent's edits - a cube by its
// button, a role, a bend over the API with no ids (the window fills them in), a field typed, a sphere, an
// undo, a camera at the view's eye, an edit while the scene plays, a stop, a cone - then the scene thrown
// away as a crash would, and the session's journal brings it back to the bit; brought back to a line in
// the middle it is the scene as it was then. A session without its last line and no window on its lock
// is found crashed; this one, alive, is not.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace ke;
using namespace selftest;

namespace {

int linesOf(EditorOps& ops, const QString& session) {
    for (const QJsonValue& v : ops.runNow("journal_sessions", {}).value("sessions").toArray())
        if (v.toObject().value("session").toString() == session) return v.toObject().value("lines").toInt();
    return 0;
}

// The session's edits; `middle` gets the scene and the line after the field typed.
void editSession(EditorOps& ops, SceneBuilder& b, std::string& middle, int& middleLine, const QString& session) {
    const uint32_t cube = made(b, b.createActions().front());
    b.toggleRole(RoleIcon::Rigid);
    ops.runNow("modifier_add", {{"kind", "bend"}, {"axis", "x"}, {"angle", 40.0}}); // no ids: the selection's
    b.editEntity(cube, [](Entity& e) { e.position.x += 0.3f; });
    for (int k = 0; k < 100 && b.graph().save() == middle; ++k) pump(50);
    pump(700); // the field's line comes when the edit rests
    middle = b.graph().save();
    middleLine = linesOf(ops, session) - 1;
    ops.runNow("scene_add", {{"shape", "sphere"}});
    b.undoAction()->trigger();
    ops.runNow("scene_add_camera", {}); // the eye from the view: the journal keeps it
    b.play();
    pump(300);
    b.editEntity(cube, [](Entity& e) { e.position.y += 1.0f; }); // while it plays: gone at the stop
    pump(700);
    b.stop();
    pump(200);
    ops.runNow("scene_add", {{"shape", "cone"}});
}

void testJournalRestore(Checker& c, QMainWindow& w, SceneBuilder& b) {
    EditorOps& ops = *dynamic_cast<MainWindow&>(w).operators();
    b.setCodeReporter([&ops](const QString& op, const QJsonObject& args) { ops.report("ui", op, args); }); // as the window has it (a test before may have taken it)
    QTemporaryDir dir;
    ops.setJournalFile(dir.path() + "/journal.jsonl");
    ops.setSnapshotEvery(3);
    b.newScene();
    ops.startSession();
    const QString session = ops.session();
    std::string middle = b.graph().save();
    int middleLine = 0;
    editSession(ops, b, middle, middleLine, session);
    const std::string last = b.graph().save();
    const int lines = linesOf(ops, session);
    QFile f(dir.path() + "/journal.jsonl");
    f.open(QIODevice::ReadOnly);
    const int snapshots = int(f.readAll().count("journal_snapshot"));
    f.close();
    b.newScene(); // the crash: the scene is gone
    const QJsonObject back = ops.runNow("journal_restore", {{"session", session}, {"line", lines - 1}});
    c.check(back.value("ok").toBool() && b.graph().save() == last,
            ("the session's journal brings the scene back to the bit after it was thrown away (" + std::to_string(lines) + " lines, " +
             std::to_string(snapshots) + " snapshots): " + back.value("error").toString().toStdString()).c_str());
    const QJsonObject then = ops.runNow("journal_restore", {{"session", session}, {"line", middleLine}});
    c.check(then.value("ok").toBool() && b.graph().save() == middle, ("brought back to line " + std::to_string(middleLine) + " it is the scene of then").c_str());
    c.check(snapshots >= 4, ("snapshots: at the start, every 3 core edits, after the undo and the stop (" + std::to_string(snapshots) + ")").c_str());
    if (f.open(QIODevice::Append | QIODevice::Text)) { // a window that died: its lines, no end, no lock
        f.write(R"({"args":{},"ok":true,"op":"session_start","s":"dead-1","src":"journal","t":"2026-10-30T10:00:00.000Z"})" "\n");
        f.write(R"({"args":{"shape":"box"},"ok":true,"op":"scene_add","s":"dead-1","src":"api","t":"2026-10-30T10:00:01.000Z"})" "\n");
        f.close();
    }
    bool dead = false, alive = true;
    for (const QJsonValue& v : ops.sessions()) {
        const QJsonObject s = v.toObject();
        if (s.value("session").toString() == "dead-1") dead = s.value("crashed").toBool() && s.value("edits").toInt() == 1;
        if (s.value("session").toString() == session) alive = !s.value("crashed").toBool();
    }
    c.check(dead && alive, "a session with no end and a free lock is crashed; the living one is not");
    // A film's journal (examples/films/): its sessions, one session's lines numbered as the restore counts them, its scene
    // at any line - read from that file, the window's own journal untouched; a film that is not there is refused by name.
    const QJsonArray films = ops.runNow("journal_sessions", {{"file", "pendulum-tower.jsonl"}}).value("sessions").toArray();
    const QString film = films.isEmpty() ? QString() : films.first().toObject().value("session").toString();
    const QJsonArray filmLines = ops.runNow("journal_lines", {{"file", "pendulum-tower.jsonl"}, {"session", film}}).value("lines").toArray();
    const QJsonObject filmed = ops.runNow("journal_restore", {{"file", "pendulum-tower.jsonl"}, {"session", film}});
    const QString built = QString::fromStdString(b.graph().save());
    const QJsonObject missing = ops.runNow("journal_sessions", {{"file", "no-such-film.jsonl"}});
    c.check(films.size() == 1 && filmLines.size() > 10 && filmed.value("ok").toBool() && built.contains("Брусок 5") && built.contains("Шарнир") &&
                !missing.value("ok").toBool() && missing.value("error").toString().contains("no-such-film"),
            ("a film's journal: its session, its lines and its scene at the end (" + std::to_string(filmLines.size()) + " lines): " +
             filmed.value("error").toString().toStdString()).c_str());
    ops.endSession();
    ops.setSnapshotEvery(100);
    ops.setJournalFile(qEnvironmentVariable("KE_JOURNAL"));
}

} // namespace

int runJournalTests(QMainWindow& w, SceneBuilder& b, Viewport*, const QString&) {
    Checker c;
    testJournalRestore(c, w, b);
    return c.failures;
}
