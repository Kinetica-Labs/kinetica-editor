// The Sequencer tab, pressed the way a person presses it (see SelfTest.h): a cube selected and «◆ Ключ» makes a
// sequence with its first key at 0 s; a click on the timeline's ruler puts the playhead at 2 s (one edit, sequence_seek);
// the gizmo moves the cube 1 m and «◆ Ключ» keys it there; a drag of the playhead to the middle shows the cube half way
// (the preview) and leaves it there on release, one undo step that takes it back; a click on the second diamond and
// Delete remove that key; Space over the timeline plays the sequence and stops it where it is; in play the playhead
// follows the world's time. «⏺ Запись» on a cube dropped from 1 m plays the
// scene and records it; pressed again, the scene stops - the cube back where it stood - and the take is a new active
// sequence «Запись 1» whose end has the cube on the floor; one undo takes the take away. «◈ Автоключ»: a hand's gizmo
// move or typed number keys the object at the playhead in its own undo step, the API and the preview key nothing. The play
// range «До»: a run pauses at its end, a take ends there, «Петля» starts the run over. With a directory, the pictures
// sequencer.png and sequencer-take.png.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "SceneBuilder.h"
#include "SequencerPanel.h"
#include "SequencerViews.h"
#include "scene/Animation.h"

#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QStatusBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>

#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

void mouse(QWidget* w, QEvent::Type type, const QPointF& p) {
    QMouseEvent e(type, p, w->mapToGlobal(p), Qt::LeftButton, type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

// Where the views draw time t (SequencerViews.h, the axis they share).
double xAt(QWidget* timeline, const SceneBuilder& b, double t) { // as the view has it: the play range's end in sight
    const SequencerPanel* panel = timeline->window()->findChild<SequencerPanel*>();
    return sequencer::axisOf(activeSequence(b.graph()), timeline->width(), panel ? panel->atLeast() : 0).x(t);
}

// ◈ Auto Key: a hand's move or turn keys the object at the playhead, in the same undo step; the API and the preview do not.
void autoKeyTests(QMainWindow& w, SceneBuilder& b, Checker& c) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    auto* autoKey = w.findChild<QToolButton*>("sequencerAutoKey");
    auto* panel = w.findChild<SequencerPanel*>();
    if (!autoKey || !panel) return c.check(false, "«◈ Автоключ» is a button of the Sequencer");
    const auto call = [&](const char* name, const QJsonObject& args) {
        bool ok = false;
        ops->run(name, args, [&ok](const QJsonObject& r) { ok = r["ok"].toBool(); }, "ui");
        return ok;
    };
    const auto keys = [&](uint32_t id) -> size_t {
        const SequenceObject* q = activeSequence(b.graph());
        if (q)
            for (const auto& t : q->tracks)
                if (t.object == id) return t.keys.size();
        return 0;
    };
    const auto at = [&](uint32_t id) { return findObject(b.graph(), id)->position.x; };
    const auto drag = [&](uint32_t id, float dx) {
        b.select(id);
        GizmoPose pose;
        Quaternion q;
        worldPose(b.graph(), *findObject(b.graph(), id), pose.position, q);
        pose.mode = GizmoMode::Translate, pose.position += Vector3(dx, 0.0f, 0.0f), pose.rotation = q;
        b.onGizmoStarted(), b.onGizmoMoved(pose), b.onGizmoFinished();
        pump(60);
    };
    b.newScene();
    const uint32_t box = made(b, b.createActions()[0]);
    pump(100);
    c.check(autoKey->isCheckable() && !autoKey->isChecked(), "Auto Key is a toggle, off at the start");

    int heard = 0;
    const auto listen = QObject::connect(&b, &SceneBuilder::handPlaced, &b, [&heard](const std::vector<uint32_t>&) { ++heard; });
    drag(box, 0.5f);
    c.check(b.graph().sequences.empty() && keys(box) == 0 && heard == 0, "Auto Key off: a gizmo move makes no key and nothing is worked out");

    autoKey->click();
    pump(30);
    const float start = at(box);
    drag(box, 0.5f);
    c.check(heard == 1, "Auto Key on: a hand's move is announced");
    QObject::disconnect(listen);
    const bool first = b.graph().sequences.size() == 1 && keys(box) == 1;
    b.undoStep();
    const bool firstUndone = b.graph().sequences.empty() && std::fabs(at(box) - start) < 1e-4f;
    b.redoStep();
    pump(30);
    c.check(first && firstUndone && keys(box) == 1 && std::fabs(at(box) - start - 0.5f) < 1e-4f,
            "Auto Key on, no sequence: the first move makes one with its key; one undo takes back the move, the key and the sequence, a redo all");

    call("sequence_seek", {{"time", 1.0}});
    const float home = at(box);
    drag(box, 0.5f);
    const SequenceObject* second = activeSequence(b.graph());
    const bool keyed = second && keys(box) == 2 && std::fabs(double(second->tracks[0].keys[1].time) - 1.0) < 1e-4 &&
                       std::fabs(second->tracks[0].keys[1].position.x - (home + 0.5f)) < 1e-4f;
    b.undoStep();
    const bool back = keys(box) == 1 && std::fabs(at(box) - home) < 1e-4f;
    b.redoStep();
    pump(30);
    c.check(keyed && back && keys(box) == 2 && std::fabs(at(box) - home - 0.5f) < 1e-4f,
            "a move at the playhead 1 s makes a key at 1 s; one undo takes back the move and the key together, a redo both");

    const size_t before = keys(box);
    const float moved = at(box);
    call("scene_move", {{"ids", QJsonArray{int(box)}}, {"by", QJsonArray{0.3, 0.0, 0.0}}});
    pump(500);
    c.check(keys(box) == before && std::fabs(at(box) - moved - 0.3f) < 1e-4f, "a scene_move through the API with Auto Key on makes no key");

    b.editEntity(box, [](Entity& e) { e.position.y = 2.0f; });
    pump(600);
    const SequenceObject* q = activeSequence(b.graph());
    const bool typed = q && keys(box) == 2 && std::fabs(q->tracks[0].keys[1].position.y - 2.0f) < 1e-4f;
    b.undoStep();
    q = activeSequence(b.graph());
    const bool undone = q && std::fabs(q->tracks[0].keys[1].position.y - 2.0f) > 1e-3f && std::fabs(at(box) - moved - 0.3f) < 1e-4f;
    b.redoStep();
    pump(30);
    q = activeSequence(b.graph());
    c.check(typed && undone && q && std::fabs(q->tracks[0].keys[1].position.y - 2.0f) < 1e-4f,
            "a number typed into the card keys too, one undo takes back the number and the key, a redo both");

    b.newScene();
    const uint32_t cam = made(b, b.createActions()[0]);
    b.removeSelected();
    b.addCamera();
    const uint32_t camera = b.selectedId();
    const float x0 = at(camera);
    call("sequence_seek", {{"time", 0.0}});
    drag(camera, 0.4f);
    call("sequence_seek", {{"time", 2.0}});
    drag(camera, 4.0f);
    call("sequence_seek", {{"time", 1.0}});
    pump(60);
    c.check(cam && keys(camera) == 2 && std::fabs(at(camera) - (x0 + 2.4f)) < 1e-3f, "a camera moved at 0 s and at 2 s stands halfway at 1 s after a seek");

    const float standing = at(camera);
    const size_t kept = keys(camera);
    panel->scrub(0.5);
    panel->endScrub();
    pump(60);
    c.check(keys(camera) == kept && std::fabs(at(camera) - (x0 + 1.4f)) < 1e-3f && standing > 0, "dragging the playhead with Auto Key on makes no key and loses nothing");
    autoKey->click();
}

// A keyed camera looked through while a take is recorded: the view goes where the camera's keys take it (the world moves
// the camera, the scene as written does not), and the take holds the camera's track beside the cube's.
void keyedCameraTests(QMainWindow& w, SceneBuilder& b, Viewport* v, Checker& c) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    const auto call = [&](const char* name, const QJsonObject& args) { ops->run(name, args, [](const QJsonObject&) {}, "ui"); };
    auto* record = w.findChild<QAction*>("actionRecord");
    auto* stop = w.findChild<QAction*>("actionStop");
    if (!record || !stop) return void(c.check(false, "the top bar has «Запись» and ■ Стоп"));
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) { e.position = Vector3(0.0f, 1.0f, 0.0f), e.rigid.enabled = e.collider.enabled = true; });
    const uint32_t cam = made(b, b.cameraAction());
    const QJsonArray ids{int(cam)};
    call("sequence_key", {{"ids", ids}, {"time", 0}});
    call("scene_move", {{"ids", ids}, {"by", QJsonArray{0, 0, 4}}});
    call("sequence_key", {{"ids", ids}, {"time", 1}});
    call("sequence_seek", {{"time", 0}});
    b.lookThrough(cam);
    pump(100);
    const QVector3D before = v->camera().eye();
    record->trigger();
    pump(700);
    const float went = (v->camera().eye() - before).length();
    stop->trigger();
    pump(400);
    const SequenceObject* take = activeSequence(b.graph());
    const bool held = take && take->name == "Запись 1" && trackOf(*take, cam) && trackOf(*take, cube);
    b.lookThrough(0);
    std::printf("  a camera keyed 4 m over 1 s, looked through, «Запись» for 0.7 s: the view went %.2f m; the take holds the camera's track %s\n",
                double(went), held ? "yes" : "no");
    c.check(went > 1.0f && went < 4.01f && held, "in play the view through a keyed camera goes with its keys, and the take brings its track along");
    // The same switch as an operation: through a camera of the scene, and back to the editor's own view.
    const bool in = ops->runNow("view_through", {{"camera", int(cam)}}).value("through").toInt() == int(cam) && b.lookingThrough() == cam &&
                    ops->runNow("view_get", {}).value("through").toInt() == int(cam);
    const bool out = ops->runNow("view_through", {}).value("through").toInt() == 0 && b.lookingThrough() == 0;
    c.check(in && out && !ops->runNow("view_through", {{"camera", 99999}}).value("ok").toBool(),
            "view_through looks through a camera of the scene and back; a camera that is not there is refused");
}

// The play range (the world's duration, the Sequencer's «До»): ▶ runs to its end and stays paused on that frame, ▶ again
// goes on; a take - from «От» - closes there and waits for ■ Стоп; with «Петля» the run starts over; the end's flag on the
// ruler is dragged to move it.
void playRangeTests(QMainWindow& w, SceneBuilder& b, Viewport* v, SequencerPanel* panel, QWidget* timeline, Checker& c) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    const auto call = [&](const char* name, const QJsonObject& args) { ops->run(name, args, [](const QJsonObject&) {}, "ui"); };
    auto* play = w.findChild<QAction*>("actionPlay");
    auto* record = w.findChild<QAction*>("actionRecord");
    auto* stop = w.findChild<QAction*>("actionStop");
    auto* end = w.findChild<QDoubleSpinBox*>("sequencerEnd");
    if (!play || !record || !stop || !end) return void(c.check(false, "the Sequencer has the play range's field «До»"));
    const auto worldTime = [v] { return v->snapshot() ? double(v->snapshot()->time) : -1.0; };
    b.newScene();
    c.check(b.graph().world.duration == 10 && b.graph().world.recordFrom == 0 && end->value() == 10,
            "a new scene has its play range from the start: «От» 0, «До» 10 s, on the ruler");
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) { e.position = Vector3(0.0f, 1.0f, 0.0f), e.rigid.enabled = e.collider.enabled = true; });
    end->setValue(0.5);
    pump(60);
    const bool set = std::fabs(double(b.graph().world.duration) - 0.5) < 1e-6;
    play->trigger();
    const bool paused = waitUntil([&] { return b.mode() == SceneBuilder::Mode::Paused; }, 4000);
    pump(200); // would go on, were it not stopped
    const double stood = worldTime();
    std::printf("  «До» 0.5 s and ▶: the run %s at %.3f s\n", paused ? "paused" : "DID NOT PAUSE", stood);
    c.check(set && paused && stood > 0.49 && stood < 0.52, "▶ runs to the play range's end and stays paused on that frame");
    const auto x = [&](double t) { return sequencer::axisOf(activeSequence(b.graph()), timeline->width(), panel->atLeast()).x(t); };
    mouse(timeline, QEvent::MouseButtonPress, QPointF(x(0.2), 8)), mouse(timeline, QEvent::MouseButtonRelease, QPointF(x(0.2), 8));
    pump(60);
    auto* note = panel->findChild<QLabel*>("sequencerNote");
    c.check(b.mode() == SceneBuilder::Mode::Paused && worldTime() == stood && note && note->text().contains("Стоп"),
            "a press on the ruler in a run moves nothing back and says why: the run is not kept");
    play->trigger(); // past the end: a deliberate ▶ goes on
    pump(300);
    const double past = worldTime();
    const QJsonObject late = ops->runNow("sim_record", {{"on", true}});
    std::printf("  ▶ again at the end: %s, the world at %.3f s; «Запись» there: %s\n", b.mode() == SceneBuilder::Mode::Playing ? "playing" : "NOT PLAYING",
                past, qPrintable(late.value("error").toString()));
    c.check(b.mode() == SceneBuilder::Mode::Playing && past > 0.55, "▶ after the play range's end goes on past it");
    c.check(!late.value("ok").toBool() && late.value("error").toString().contains("past"), "«Запись» past the play range's end is refused and says why");
    stop->trigger();
    pump(300);

    // The take's window: «От» 0.3 s, «До» 0.5 s. At «До» the take closes and the world pauses; ▶ goes on, not recording;
    // ■ Стоп brings the take in - 0.3..0.5 s of the run only.
    auto* start = panel->findChild<QDoubleSpinBox*>("sequencerFrom");
    if (!start) return void(c.check(false, "the Sequencer has the take's start, the field «От»"));
    start->setValue(0.8);
    pump(60);
    const double put = double(b.graph().world.recordFrom);
    c.check(std::fabs(put - 0.45) < 1e-6 && std::fabs(start->value() - 0.45) < 1e-6, "«От» past «До» is put back inside: the flags never pass each other");
    start->setValue(0.3);
    pump(60);
    record->trigger();
    const bool closed = waitUntil([&] { return b.mode() == SceneBuilder::Mode::Paused && !ops->recording(); }, 5000);
    pump(200);
    const QString said = w.statusBar()->currentMessage();
    const bool waits = !activeSequence(b.graph());
    const QJsonObject second = ops->runNow("sim_record", {{"on", true}});
    std::printf("  «Запись» with «От» 0.3 s, «До» 0.5 s: %s at %.3f s - «%s»; another take: %s\n", closed ? "paused" : "DID NOT PAUSE", worldTime(),
                qPrintable(said), qPrintable(second.value("error").toString()));
    c.check(closed && waits && said.contains("■ Стоп"), "at «До» the take closes, the world pauses and the take waits for ■ Стоп, saying so");
    c.check(!second.value("ok").toBool() && second.value("error").toString().contains("waits"), "a new take while one waits is refused and says why");
    play->trigger();
    pump(300);
    const bool goes = b.mode() == SceneBuilder::Mode::Playing && worldTime() > 0.55 && !ops->recording();
    c.check(goes, "▶ after the take's end goes on, not recording");
    stop->trigger();
    const bool ended = waitUntil([&] { return b.editing() && activeSequence(b.graph()); }, 5000);
    pump(200);
    const SequenceObject* take = activeSequence(b.graph());
    const SequenceTrack* track = take ? trackOf(*take, cube) : nullptr;
    const double first = track && !track->keys.empty() ? double(track->keys.front().time) : -1, last = track && !track->keys.empty() ? double(track->keys.back().time) : -1;
    std::printf("  ■ Стоп: the take %s, the cube's keys %.3f..%.3f s\n", ended ? "came in" : "DID NOT COME", first, last);
    c.check(ended && std::fabs(first - 0.3) < 0.01 && last > 0.45 && last < 0.52, "the take holds «От»..«До» only, at the world's time - not the run after it");
    start->setValue(0);
    pump(60);

    call("sequence_set", {{"loop", true}});
    pump(60);
    play->trigger();
    pump(1300); // more than two ranges
    const bool loops = b.mode() == SceneBuilder::Mode::Playing;
    const double at = worldTime();
    stop->trigger();
    pump(300);
    std::printf("  «Петля» and ▶ for 1.3 s: %s, the world's time %.3f s\n", loops ? "still playing" : "NOT PLAYING", at);
    c.check(loops && at < 0.52, "with «Петля» the run starts over at the play range's end");

    const QPointF from(x(0.5) - 4, 8), to(x(1.5), 8); // by its flag, left of the line
    mouse(timeline, QEvent::MouseButtonPress, from), mouse(timeline, QEvent::MouseMove, to), mouse(timeline, QEvent::MouseButtonRelease, to);
    pump(60);
    const double dragged = double(b.graph().world.duration);
    std::printf("  the range's handle dragged on the ruler from 0.5 s to 1.5 s: «До» %.2f s, the field %.2f\n", dragged, end->value());
    c.check(std::fabs(dragged - 1.5) < 0.06 && std::fabs(end->value() - dragged) < 1e-6, "the play range's end is dragged by its handle on the ruler");
    start->setValue(0.2); // the take's start has its flag too, right of its line
    pump(60);
    const QPointF grab(x(0.2) + 4, 8), drop(x(0.35), 8);
    mouse(timeline, QEvent::MouseButtonPress, grab), mouse(timeline, QEvent::MouseMove, drop), mouse(timeline, QEvent::MouseButtonRelease, drop);
    pump(60);
    const double moved = double(b.graph().world.recordFrom);
    std::printf("  the take's start dragged by its flag from 0.2 s to 0.35 s: «От» %.2f s\n", moved);
    c.check(std::fabs(moved - 0.35) < 0.06 && std::fabs(start->value() - moved) < 1e-6, "the take's start is dragged by its flag on the ruler");
    start->setValue(0);
    end->setValue(0);
    pump(60);
    c.check(b.graph().world.duration == 0 && b.graph().save().find("duration") == std::string::npos, "«До» 0: no end, nothing written into the scene");
    record->trigger(); // no end: the take still has one - two minutes
    pump(100);
    const double limit = b.playEnd();
    stop->trigger();
    pump(300);
    std::printf("  «Запись» with no «До»: the run stops itself at %.0f s; after ■ Стоп %.0f\n", limit, b.playEnd());
    c.check(std::fabs(limit - 120) < 1e-6 && b.playEnd() == 0, "a take with no play range ends by itself after two minutes, not without end");
}

} // namespace

int runSequencerTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) { e.position = Vector3(0.0f, 0.5f, 0.0f), e.rigid.enabled = e.collider.enabled = true; });
    auto* dock = w.findChild<QDockWidget*>("sequencerDock");
    auto* panel = w.findChild<SequencerPanel*>();
    auto* timeline = w.findChild<QWidget*>("sequencerTimeline");
    auto* key = w.findChild<QToolButton*>("sequencerKey");
    if (!dock || !panel || !timeline || !key) {
        c.check(false, "the Sequencer is a tab of the bottom panel: its timeline and «◆ Ключ»");
        return c.failures;
    }
    dock->show(), dock->raise();
    b.select(cube);
    pump(150);
    key->click();
    pump(60);
    const SequenceObject* q = activeSequence(b.graph());
    const bool first = b.graph().sequences.size() == 1 && q && q->tracks.size() == 1 && q->tracks[0].object == cube && q->tracks[0].keys.size() == 1;

    const auto clickAt = [&](double t) {
        const QPointF at(xAt(timeline, b, t), 11); // the ruler
        mouse(timeline, QEvent::MouseButtonPress, at), mouse(timeline, QEvent::MouseButtonRelease, at);
        pump(60);
    };
    clickAt(2);
    const double two = activeSequence(b.graph())->time;
    dragArrowX(v, 1.0f, Qt::ControlModifier);
    pump(60);
    key->click();
    pump(60);
    q = activeSequence(b.graph());
    const bool second = q && q->tracks[0].keys.size() == 2 && std::fabs(q->tracks[0].keys[1].position.x - 1) < 1e-4f;
    std::printf("  ◆ with the cube selected: a sequence, a track, a key at 0 s %s; the ruler clicked at 2 s: the playhead at %.3f s; the gizmo 1 m and ◆: "
                "a key there %s\n", first ? "yes" : "no", two, second ? "yes" : "no");
    c.check(first && std::fabs(two - 2) < 0.05 && second, "«◆ Ключ» and the ruler key the selected cube where it stands at the playhead");

    // The playhead dragged to the middle: the cube half way; released, one undo step takes it back where it stood.
    const double length = q ? double(q->length()) : 0;
    const QPointF from(xAt(timeline, b, length), 11), to(xAt(timeline, b, length / 2), 11);
    mouse(timeline, QEvent::MouseButtonPress, from);
    pump(30);
    const double pressed = activeSequence(b.graph()) ? double(activeSequence(b.graph())->time) : -1; // each step said: where a drag stops
    mouse(timeline, QEvent::MouseMove, to);
    pump(30);
    const double moved = activeSequence(b.graph()) ? double(activeSequence(b.graph())->time) : -1;
    const float during = findObject(b.graph(), cube)->position.x;
    mouse(timeline, QEvent::MouseButtonRelease, to);
    pump(60);
    q = activeSequence(b.graph());
    Vector3 p;
    Quaternion r;
    samplePose(q->tracks[0].keys, q->time, false, q->length(), p, r);
    const double middle = double(q->time); // q goes with the graph an undo replaces
    const float after = findObject(b.graph(), cube)->position.x;
    b.undoAction()->trigger();
    pump(60);
    const float undone = findObject(b.graph(), cube)->position.x;
    b.redoAction()->trigger();
    pump(60);
    std::printf("  the playhead dragged to %.3f s: the cube at x %.3f during the drag, %.3f after (the sequence says %.3f); one undo: x %.3f\n", middle,
                double(during), double(after), double(p.x), double(undone));
    std::printf("    its steps: a sequence of %.3f s on a timeline %d px wide; pressed at x %.1f - %.3f s, moved to x %.1f - %.3f s\n", length,
                timeline->width(), from.x(), pressed, to.x(), moved);
    c.check(std::fabs(after - p.x) < 1e-5f && std::fabs(after - 0.5f) < 0.03f && std::fabs(during - after) < 1e-5f && std::fabs(undone - 1) < 1e-4f,
            "dragging the playhead shows the cube on its track and leaves one undo step");

    // The second diamond clicked, Delete: one key left.
    const double last = double(activeSequence(b.graph())->tracks[0].keys[1].time);
    const QPointF diamond = static_cast<SequencerTimeline*>(timeline)->pointOf(cube, last);
    mouse(timeline, QEvent::MouseButtonPress, diamond), mouse(timeline, QEvent::MouseButtonRelease, diamond);
    timeline->setFocus();
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(timeline, &del);
    pump(60);
    const bool removed = activeSequence(b.graph())->tracks[0].keys.size() == 1;
    b.undoAction()->trigger();
    pump(60);
    c.check(removed && activeSequence(b.graph())->tracks[0].keys.size() == 2, "a diamond clicked and Delete remove that key; undo brings it back");

    // Space over the timeline plays the sequence and stops it where it is, as a video editor's timeline does; the scene
    // stays in edit (over the 3D view Space is «Пуск»).
    clickAt(0);
    auto* playButton = w.findChild<QToolButton*>("sequencerPlay");
    const auto space = [timeline] {
        QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_Space, Qt::NoModifier, " ");
        over.ignore();
        QCoreApplication::sendEvent(timeline, &over);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
        QCoreApplication::sendEvent(timeline, &press);
        return over.isAccepted();
    };
    const bool taken = space();
    const bool started = taken && playButton && playButton->isChecked() && b.editing();
    pump(600);
    space();
    pump(60);
    const double stoppedAt = double(activeSequence(b.graph())->time);
    const bool stopped = playButton && !playButton->isChecked() && stoppedAt > 0.2 && b.editing();
    std::printf("  Space over the timeline: taken from «Пуск» %s, plays %s; Space again: stopped at %.2f s, the scene in edit %s\n",
                taken ? "yes" : "no", started ? "yes" : "no", stoppedAt, b.editing() ? "yes" : "no");
    c.check(started && stopped, "Space over the Sequencer plays its sequence and stops it where it is; the scene stays in edit");

    // In play the world plays the sequence: the playhead follows its time.
    clickAt(0);
    b.play();
    pump(900);
    const double following = panel->playhead();
    const bool front = !dock->visibleRegion().isEmpty(); // the plots opened by ▶ stay behind the timeline being watched
    if (!shotsDir.isEmpty()) w.grab().save(shotsDir + "/sequencer.png");
    b.stop();
    pump(100);
    std::printf("  in play the playhead at %.3f s; the Sequencer's tab still in front %s\n", following, front ? "yes" : "no");
    c.check(following > 0.2 && front, "in play the playhead follows the world's time, the timeline stays in front");

    // A take: the cube dropped from 1 m, recorded, stopped.
    b.newScene();
    const uint32_t drop = made(b, b.createActions()[0]);
    b.editEntity(drop, [](Entity& e) { e.position = Vector3(0.0f, 1.0f, 0.0f), e.rotationDeg = Vector3(20, 0, 30), e.rigid.enabled = e.collider.enabled = true; });
    pump(100);
    auto* record = w.findChild<QToolButton*>("sequencerRecord");
    if (!record) {
        c.check(false, "the Sequencer has «⏺ Запись»");
        return c.failures;
    }
    record->click();
    pump(80);
    const bool playing = b.mode() == SceneBuilder::Mode::Playing;
    pump(1500);
    record->click();
    pump(400);
    const SequenceObject* take = activeSequence(b.graph());
    const bool made = take && take->name == "Запись 1" && take->tracks.size() == 1 && take->tracks[0].object == drop && take->tracks[0].keys.size() > 2;
    const float back = findObject(b.graph(), drop)->position.y;
    const size_t keys = made ? take->tracks[0].keys.size() : 0; // take goes with the graph the next edit replaces
    const double seconds = made ? double(take->length()) : 0;
    float landed = -1;
    if (made) {
        const QPointF end(xAt(timeline, b, seconds), 11); // on the ruler: a diamond there would take the click
        mouse(timeline, QEvent::MouseButtonPress, end), mouse(timeline, QEvent::MouseButtonRelease, end);
        pump(80);
        landed = findObject(b.graph(), drop)->position.y;
        if (!shotsDir.isEmpty()) w.grab().save(shotsDir + "/sequencer-take.png");
    }
    b.undoAction()->trigger(), b.undoAction()->trigger(); // the playhead, then the take
    pump(80);
    const bool gone = b.graph().sequences.empty();
    std::printf("  «⏺ Запись» on a cube off 1 m: played %s; stopped: «Запись 1» %s, %zu keys over %.2f s; the cube back at y %.3f, at the take's end "
                "y %.3f; two undos: no sequence %s\n", playing ? "yes" : "no", made ? "yes" : "no", keys, seconds, double(back), double(landed),
                gone ? "yes" : "no");
    c.check(playing && made && std::fabs(back - 1) < 1e-5f && landed > 0.05f && landed < 0.2f && gone,
            "a take records the physics into a sequence that shows the cube landed at its end");

    // The same from the top bar: «Запись» beside ▶ plays and records, ■ Стоп ends the take.
    auto* recordAct = w.findChild<QAction*>("actionRecord");
    auto* stopAct = w.findChild<QAction*>("actionStop");
    if (recordAct && stopAct) recordAct->trigger();
    pump(80);
    const bool barPlays = recordAct && recordAct->isChecked() && b.mode() == SceneBuilder::Mode::Playing;
    pump(1000);
    if (stopAct) stopAct->trigger();
    pump(400);
    const SequenceObject* barTake = activeSequence(b.graph());
    const bool barMade = barTake && barTake->name == "Запись 1" && barTake->tracks.size() == 1 && !recordAct->isChecked() && b.editing();
    std::printf("  «Запись» on the top bar: played and checked %s; ■ Стоп: «%s», the button off %s\n", barPlays ? "yes" : "no",
                barTake ? barTake->name.c_str() : "", recordAct && !recordAct->isChecked() ? "yes" : "no");
    c.check(barPlays && barMade, "«Запись» beside ▶ records; ■ Стоп ends the take into a sequence");

    // A second take with the first one open: the physics runs again (a take is shown, not played), «Запись 2» holds the
    // fall. «▶ в Пуске» on it: ▶ replays it, the cube a kinematic body on its track.
    recordAct->trigger();
    pump(1000);
    stopAct->trigger();
    pump(400);
    const SequenceObject* again = activeSequence(b.graph());
    const bool fell = again && again->name == "Запись 2" && again->tracks.size() == 1 && again->tracks[0].keys.size() > 2 && !again->play;
    const std::string againName = again ? again->name : std::string(); // again goes with the graph the next edit replaces
    const size_t againKeys = again && !again->tracks.empty() ? again->tracks[0].keys.size() : 0;
    if (auto* plays = w.findChild<QToolButton*>("sequencerPlays")) plays->click();
    pump(60);
    const bool replays = activeSequence(b.graph()) && activeSequence(b.graph())->play;
    std::printf("  a second take after the first: «%s», %zu keys, not played %s; «▶ в Пуске» sets it to play %s\n", againName.c_str(), againKeys,
                fell ? "yes" : "no", replays ? "yes" : "no");
    c.check(fell && replays, "a take is shown, not played: the next take records the physics again; «▶ в Пуске» replays it");

    // Folding: three cubes in a group and one loose, all keyed - «Все дорожки», the group folded, the loose cube's track;
    // the group's arrow unfolds its three; the arrow of «Все дорожки» folds everything into one row.
    auto* tree = static_cast<SequencerTimeline*>(timeline);
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    const auto call = [ops](const char* name, const QJsonObject& args) {
        bool ok = false;
        ops->run(name, args, [&ok](const QJsonObject& r) { ok = r["ok"].toBool(); }, "ui");
        return ok;
    };
    b.newScene();
    std::vector<uint32_t> cubes;
    for (int k = 0; k < 4; ++k) cubes.push_back(selftest::made(b, b.createActions()[0]));
    const QJsonArray all{int(cubes[0]), int(cubes[1]), int(cubes[2]), int(cubes[3])}, three{int(cubes[0]), int(cubes[1]), int(cubes[2])};
    const bool built = call("scene_group", {{"ids", three}}) && call("sequence_key", {{"ids", all}, {"time", 0.0}}) &&
                       call("scene_move", {{"ids", all}, {"by", QJsonArray{0.0, 0.5, 0.0}}}) && call("sequence_key", {{"ids", all}, {"time", 2.0}});
    pump(80);
    const int grouped = tree->rows();
    QPointF fold = tree->foldOf(1);
    mouse(tree, QEvent::MouseButtonPress, fold), mouse(tree, QEvent::MouseButtonRelease, fold);
    pump(40);
    const int unfolded = tree->rows();
    fold = tree->foldOf(0);
    mouse(tree, QEvent::MouseButtonPress, fold), mouse(tree, QEvent::MouseButtonRelease, fold);
    pump(40);
    const int folded = tree->rows();
    mouse(tree, QEvent::MouseButtonPress, fold), mouse(tree, QEvent::MouseButtonRelease, fold); // open again for the curves
    pump(40);
    std::printf("  three cubes grouped and one loose, keyed: rows %d (all, the group folded, the loose track); the group unfolded %d; all folded %d\n",
                grouped, unfolded, folded);
    c.check(built && grouped == 3 && unfolded == 6 && folded == 1, "the tracks fold by the scene's groups");

    // The curves: the loose cube's; its second key's y dragged up 40 px - y higher, one edit, one undo back.
    auto* curvesButton = w.findChild<QToolButton*>("sequencerCurvesButton");
    auto* curves = static_cast<SequencerCurves*>(w.findChild<QWidget*>("sequencerCurves"));
    b.select(cubes[3]);
    if (curvesButton) curvesButton->click();
    pump(120);
    const auto yOfKey = [&b, &cubes] { return trackOf(*activeSequence(b.graph()), cubes[3])->keys[1].position.y; };
    const float before = yOfKey();
    const QPointF dot = curves->pointOf(1, 1);
    mouse(curves, QEvent::MouseButtonPress, dot), mouse(curves, QEvent::MouseMove, dot + QPointF(0, -10)), mouse(curves, QEvent::MouseMove, dot + QPointF(0, -40));
    mouse(curves, QEvent::MouseButtonRelease, dot + QPointF(0, -40));
    pump(60);
    const float raised = yOfKey();
    b.undoAction()->trigger();
    pump(60);
    const float lowered = yOfKey();
    if (!shotsDir.isEmpty()) {
        b.select(cubes[3]);
        pump(60);
        w.grab().save(shotsDir + "/sequencer-curves.png");
    }
    { // its own names: the test around has its own
        // Look closer: the wheel zooms the time about the mouse - the tracks' axis with it -, Shift+wheel the values, the middle
        // button moves the view, F over the Sequencer shows the whole again.
        const auto wheel = [curves](const QPointF& at, int delta, Qt::KeyboardModifiers mods) {
            QWheelEvent e(at, curves->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(curves, &e);
            pump(30);
        };
        const auto pan = [curves](QEvent::Type type, const QPointF& at) {
            QMouseEvent e(type, at, curves->mapToGlobal(at), Qt::MiddleButton, type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MiddleButton,
                          Qt::NoModifier);
            QCoreApplication::sendEvent(curves, &e);
            pump(30);
        };
        auto* tracks = w.findChild<QWidget*>("sequencerTimeline");
        const sequencer::Axis whole = panel->axis(curves->width());
        const QPointF key2 = curves->pointOf(1, 1);
        wheel(key2, 240, Qt::NoModifier); // two notches in, over the key at 2 s
        const sequencer::Axis near = panel->axis(curves->width());
        const double tracksSpan = panel->axis(tracks->width()).span;
        const QPointF closer = curves->pointOf(1, 1);
        const QPointF mouseAt(closer.x(), closer.y() - 40);
        wheel(mouseAt, 240, Qt::ShiftModifier); // the values, about a point 40 px over the key
        const double apart = std::fabs(mouseAt.y() - curves->pointOf(1, 1).y());
        pan(QEvent::MouseButtonPress, QPointF(600, 80)), pan(QEvent::MouseMove, QPointF(500, 80)), pan(QEvent::MouseButtonRelease, QPointF(500, 80));
        const sequencer::Axis moved = panel->axis(curves->width());
        curves->setFocus();
        QKeyEvent overF(QEvent::ShortcutOverride, Qt::Key_F, Qt::NoModifier, "f"), pressF(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier, "f");
        overF.ignore();
        QCoreApplication::sendEvent(curves, &overF), QCoreApplication::sendEvent(curves, &pressF);
        pump(60);
        const sequencer::Axis back = panel->axis(curves->width());
        const QPointF again = curves->pointOf(1, 1);
        std::printf("  look closer: the wheel over the key at 2 s - %.2f s in sight from %.2f s (was %.2f), the key stays at x %.1f (was %.1f), the tracks' "
                    "the same %.2f s; Shift+wheel - the key %.1f px from the mouse (was 40); the middle button 100 px - from %.2f s; F - %.2f s from %.2f, "
                    "the key back at y %.1f (was %.1f), F taken from the 3D view %s\n", near.span, near.from, whole.span, closer.x(), key2.x(),
                    tracksSpan, apart, moved.from, back.span, back.from, again.y(), key2.y(), overF.isAccepted() ? "yes" : "no");
        c.check(std::fabs(near.span - whole.span * 0.85 * 0.85) < 1e-6 && std::fabs(closer.x() - key2.x()) < 1.0 && tracksSpan == near.span,
                "the wheel zooms the curves' time about the mouse, the tracks' with it");
        c.check(apart > 40 * 1.3, "Shift+wheel zooms the values about the mouse");
        c.check(moved.from > near.from + 0.01, "the middle button moves the view along the time");
        c.check(overF.isAccepted() && back.span == whole.span && back.from == 0 && std::fabs(again.y() - key2.y()) < 1.0,
                "F over the Sequencer shows the whole sequence again, the values fitted");
    }

    if (curvesButton) curvesButton->click();
    std::printf("  the curves of «%s»: its key at 2 s, y %.3f m, dragged up 40 px: %.3f m; one undo: %.3f m\n", findObject(b.graph(), cubes[3])->name.c_str(),
                double(before), double(raised), double(lowered));
    c.check(dot.x() > 0 && raised > before + 0.01f && std::fabs(lowered - before) < 1e-6f, "a key's dot dragged on the curves changes its value, one undo step");

    // A hundred thousand tracks (bound to objects not there: lost): folded by themselves, built and drawn in a blink.
    std::string error;
    b.transact([](SceneGraph& g, std::string&) {
        SequenceObject q;
        q.id = nextId(g), q.name = "Сто тысяч", q.active = true, q.play = false;
        q.tracks.reserve(100000);
        for (uint32_t k = 0; k < 100000; ++k)
            q.tracks.push_back({5000000 + k, {{0, Vector3(0.0f), Vector3(0.0f)}, {Real(1 + (k % 50) * 0.1f), Vector3(1, 0, 0), Vector3(0.0f)}}});
        for (SequenceObject& s : g.sequences) s.active = false;
        g.sequences.push_back(std::move(q));
        return true;
    }, error);
    pump(60);
    QElapsedTimer clock;
    clock.start();
    tree->setFilter(QString()); // built again
    const qint64 buildMs = clock.elapsed();
    const int onlyAll = tree->rows();
    clock.restart();
    tree->repaint();
    const qint64 foldedMs = clock.elapsed();
    tree->toggle(0);
    const int every = tree->rows();
    clock.restart();
    tree->repaint();
    const qint64 openMs = clock.elapsed();
    tree->toggle(0);
    b.undoAction()->trigger();
    pump(60);
    std::printf("  100000 tracks: built in %lld ms, %d row folded (drawn in %lld ms), %d rows unfolded (drawn in %lld ms)\n", static_cast<long long>(buildMs),
                onlyAll, static_cast<long long>(foldedMs), every, static_cast<long long>(openMs));
    c.check(onlyAll == 1 && every == 100001 && buildMs < 1500 && foldedMs < 100 && openMs < 100,
            "a hundred thousand tracks come folded, build and draw fast");

    // ⇔: the bottom panel under the inspector too, its right edge at the window's (the tool shelf keeps the left); again,
    // back beside the inspector.
    auto* wide = w.findChild<QToolButton*>("actionBottomWideButton");
    const int narrow = dock->width();
    if (wide) wide->click();
    pump(150);
    const int widened = dock->width(), reach = dock->mapTo(&w, QPoint(dock->width(), 0)).x();
    if (wide) wide->click();
    pump(150);
    std::printf("  ⇔: the bottom panel %d px wide, its right edge at %d of the window's %d px; back %d px\n", widened, reach, w.width(),
                dock->width());
    c.check(wide && widened > narrow + 100 && reach >= w.width() - 12 && std::abs(dock->width() - narrow) <= 2,
            "⇔ spreads the bottom panel the window's whole width and back");
    autoKeyTests(w, b, c);
    keyedCameraTests(w, b, v, c);
    playRangeTests(w, b, v, panel, timeline, c);
    return c.failures;
}
