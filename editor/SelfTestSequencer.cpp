// The Sequencer tab, pressed the way a person presses it (see SelfTest.h): a cube selected and «◆ Ключ» makes a
// sequence with its first key at 0 s; a click on the timeline's ruler puts the playhead at 2 s (one edit, sequence_seek);
// the gizmo moves the cube 1 m and «◆ Ключ» keys it there; a drag of the playhead to the middle shows the cube half way
// (the preview) and leaves it there on release, one undo step that takes it back; a click on the second diamond and
// Delete remove that key; Space over the timeline plays the sequence and stops it where it is; in play the playhead
// follows the world's time. «⏺ Запись» on a cube dropped from 1 m plays the
// scene and records it; pressed again, the scene stops - the cube back where it stood - and the take is a new active
// sequence «Запись 1» whose end has the cube on the floor; one undo takes the take away. With a directory, the pictures
// sequencer.png and sequencer-take.png.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "MainWindow.h"
#include "SequencerPanel.h"
#include "SequencerViews.h"
#include "scene/Animation.h"

#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QToolButton>

#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

void mouse(QWidget* w, QEvent::Type type, const QPointF& p) {
    QMouseEvent e(type, p, w->mapToGlobal(p), Qt::LeftButton, type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

// Where the views draw time t (SequencerViews.h, the axis they share).
double xAt(QWidget* timeline, const SceneBuilder& b, double t) { return sequencer::axisOf(activeSequence(b.graph()), timeline->width()).x(t); }

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
    return c.failures;
}
