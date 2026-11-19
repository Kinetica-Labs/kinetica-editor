// The Sequencer tab, pressed the way a person presses it (see SelfTest.h): a cube selected and «◆ Ключ» makes a
// sequence with its first key at 0 s; a click on the timeline's ruler puts the playhead at 2 s (one edit, sequence_seek);
// the gizmo moves the cube 1 m and «◆ Ключ» keys it there; a drag of the playhead to the middle shows the cube half way
// (the preview) and leaves it there on release, one undo step that takes it back; a click on the second diamond and
// Delete remove that key; in play the playhead follows the world's time. «⏺ Запись» on a cube dropped from 1 m plays the
// scene and records it; pressed again, the scene stops - the cube back where it stood - and the take is a new active
// sequence «Запись 1» whose end has the cube on the floor; one undo takes the take away. With a directory, the pictures
// sequencer.png and sequencer-take.png.
#include "SelfTestSupport.h"

#include "SequencerPanel.h"
#include "scene/Animation.h"

#include <QAction>
#include <QDockWidget>
#include <QToolButton>

#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

void mouse(QWidget* w, QEvent::Type type, const QPointF& p) {
    QMouseEvent e(type, p, w->mapToGlobal(p), Qt::LeftButton, type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

// Where the timeline draws time t (SequencerPanel.cpp: the names' column 160 px, a margin 12, the view at least 5 s).
double xAt(QWidget* timeline, const SceneBuilder& b, double t) {
    const SequenceObject* q = activeSequence(b.graph());
    const double span = std::max(5.0, std::ceil(double(q ? q->length() : 0) + 1));
    return 160 + 12 + t * double(timeline->width() - 160 - 24) / span;
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
        const QPointF at(xAt(timeline, b, t), 22 + 11);
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
    const QPointF from(xAt(timeline, b, length), 22 + 30), to(xAt(timeline, b, length / 2), 22 + 30);
    mouse(timeline, QEvent::MouseButtonPress, from);
    pump(30);
    mouse(timeline, QEvent::MouseMove, to);
    pump(30);
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
    c.check(std::fabs(after - p.x) < 1e-5f && std::fabs(after - 0.5f) < 0.03f && std::fabs(during - after) < 1e-5f && std::fabs(undone - 1) < 1e-4f,
            "dragging the playhead shows the cube on its track and leaves one undo step");

    // The second diamond clicked, Delete: one key left.
    const double last = double(activeSequence(b.graph())->tracks[0].keys[1].time);
    const QPointF diamond(xAt(timeline, b, last), 22 + 11);
    mouse(timeline, QEvent::MouseButtonPress, diamond), mouse(timeline, QEvent::MouseButtonRelease, diamond);
    timeline->setFocus();
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(timeline, &del);
    pump(60);
    const bool removed = activeSequence(b.graph())->tracks[0].keys.size() == 1;
    b.undoAction()->trigger();
    pump(60);
    c.check(removed && activeSequence(b.graph())->tracks[0].keys.size() == 2, "a diamond clicked and Delete remove that key; undo brings it back");

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
    return c.failures;
}
