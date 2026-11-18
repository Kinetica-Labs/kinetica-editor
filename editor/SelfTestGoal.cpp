// The IK goal while the scene plays; the gripper button under a goal (UR5e of examples/) (SceneBuilder::followGoals, Viewport::playHandleAt): an arm of two servo
// hinges and a hand; ▶, then «Цель IK» on the hand puts a see-through cube around it with the gizmo on it at
// once; the gizmo's X arrow dragged by the mouse in play moves the cube, the servos get new targets and the
// hand follows by its motors; a click on the cube in play selects it again, not grabbing the hand inside.
#include "SelfTestSupport.h"

#include "EditorOps.h"
#include "IkSolverView.h"
#include "MainWindow.h"
#include "PlotPanel.h"
#include "PlotSeries.h"
#include "scene/Kinematics.h"

#include <QDockWidget>
#include <QElapsedTimer>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

#include <algorithm>
#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

Entity link(uint32_t id, const char* name, const Vector3& size, const Vector3& at, bool fixed, const Vector3& color) {
    Entity e;
    e.id = id, e.name = name, e.size = size, e.position = at, e.color = color;
    e.rigid.enabled = e.collider.enabled = true;
    e.rigid.fixed = fixed;
    return e;
}

JointObject hinge(uint32_t id, uint32_t a, uint32_t b, const Vector3& anchorA, const Vector3& anchorB) {
    JointObject j;
    j.id = id, j.a = a, j.b = b, j.anchorA = anchorA, j.anchorB = anchorB;
    j.frameA = j.frameB = {0, -90, 0}; // the frames' x along the world z: a turn in the x-y plane
    j.dof[DofRx].motion = JointMotion::Free;
    JointDrive& d = j.dof[DofRx].drive;
    d.enabled = true, d.stiffness = 4000, d.damping = 150, d.maxForce = 400;
    return j;
}

// A planar arm standing up: base, upper arm 0.4 m, forearm 0.3 m on servo hinges about z, a hand welded on.
SceneGraph standingArm() {
    SceneGraph g;
    const Vector3 orange(0.95f, 0.55f, 0.2f);
    g.entities = {link(1, "Основание", {0.3f, 0.1f, 0.3f}, {0, 0.05f, 0}, true, {0.4f, 0.4f, 0.45f}),
                  link(2, "Плечо", {0.08f, 0.4f, 0.08f}, {0, 0.3f, 0}, false, orange),
                  link(3, "Предплечье", {0.06f, 0.3f, 0.06f}, {0, 0.65f, 0}, false, orange),
                  link(4, "Схват", {0.08f, 0.04f, 0.08f}, {0, 0.82f, 0}, false, {0.3f, 0.6f, 0.95f})};
    g.joints = {hinge(10, 2, 1, {0, -0.2f, 0}, {0, 0.05f, 0}), hinge(11, 3, 2, {0, -0.15f, 0}, {0, 0.2f, 0})};
    JointObject weld;
    weld.id = 12, weld.a = 4, weld.b = 3, weld.anchorA = {0, -0.02f, 0}, weld.anchorB = {0, 0.15f, 0};
    g.joints.push_back(weld);
    return g;
}

const Entity* entity(const SceneBuilder& b, uint32_t id) {
    for (const Entity& e : b.graph().entities)
        if (e.id == id) return &e;
    return nullptr;
}

// The hand's live position: its body in the newest snapshot.
Vector3 handNow(SceneBuilder& b, Viewport* v, uint32_t id = 4) {
    const int body = b.bodyOfEntity(id);
    return v->snapshot() && body >= 0 && size_t(body) < v->snapshot()->bodies.size() ? v->snapshot()->bodies[size_t(body)].pos : Vector3(0.0f);
}

// The gizmo's arrow along world axis k dragged by the mouse, `pixels` along its picture on the screen.
void dragArrow(Viewport* v, int k, float pixels) {
    const GizmoView gv = v->gizmoView();
    Vector3 axis(0.0f);
    axis[k] = 1;
    const Vector2 m0 = gv.project(v->gizmo().position() + v->gizmo().axis(k) * (0.6f * v->gizmo().armLength(gv)));
    const Vector2 dir = normalize(gv.project(v->gizmo().position() + axis) - gv.project(v->gizmo().position()));
    sendMouse(v, QEvent::MouseMove, toPoint(m0), Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, toPoint(m0), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i) sendMouse(v, QEvent::MouseMove, toPoint(m0 + dir * (pixels * float(i) / 12.0f)), Qt::NoButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, toPoint(m0 + dir * pixels), Qt::LeftButton, Qt::NoButton);
    pump(60);
}

void testGoalInPlay(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    const SceneGraph arm = standingArm();
    QString error;
    c.check(b.restoreSnapshot(QString::fromStdString(arm.save()), QString(), nextId(arm), error), ("the arm loads: " + error).toStdString().c_str());
    v->camera().lookAt(QVector3D(0.9f, 0.9f, 1.9f), QVector3D(0.1f, 0.55f, 0));
    // The top bar: «Фигура ▾» holds the primitives and a model; «Датчик ▾» the six kinds - greyed with nothing selected,
    // all six for the hand, which hangs on a joint.
    auto* shapes = w.findChild<QToolButton*>("shapesButton");
    auto* sensors = w.findChild<QToolButton*>("sensorsButton");
    b.select(0);
    b.updateSensorActions();
    const bool greyed = std::none_of(b.sensorActions().begin(), b.sensorActions().end(), [](QAction* a) { return a->isEnabled(); });
    b.select(4);
    b.updateSensorActions();
    const bool open = std::all_of(b.sensorActions().begin(), b.sensorActions().end(), [](QAction* a) { return a->isEnabled(); });
    c.check(shapes && shapes->menu() && shapes->menu()->actions().size() == 7 && sensors && sensors->menu() && greyed && open,
            "the top bar groups the shapes under «Фигура ▾» and puts the six sensors under «Датчик ▾», greyed until a body is selected");
    b.play();
    waitFrames(v, 30, 10000);
    if (auto* add = w.findChild<QPushButton*>("addGoal")) add->click();
    pump(200);
    const uint32_t goal = b.selectedId();
    const Entity* g = entity(b, goal);
    c.check(g && g->goalOf == 4 && b.mode() == SceneBuilder::Mode::Playing && b.goalSelected() && !v->editMode() && v->gizmoShown(),
            "«Цель IK» while the scene plays: the goal is selected and its gizmo shows at once (the view stays in play)");
    const Vector3 at = g ? g->position : Vector3(0.0f);
    const Real targetBefore = b.graph().joints[1].dof[DofRx].drive.target;
    std::vector<QString> lines;
    b.setCodeReporter([&lines](const QString& op, const QJsonObject&) { lines.push_back(op); });
    dragArrow(v, 1, -60.0f); // down, then aside: the arm standing straight reaches nothing to its side at its full height
    dragArrow(v, 0, 60.0f);
    b.setCodeReporter([&w](const QString& op, const QJsonObject& a) { dynamic_cast<MainWindow&>(w).operators()->report("ui", op, a); });
    g = entity(b, goal);
    const float moved = g ? g->position.x - at.x : 0.0f, lowered = g ? at.y - g->position.y : 0.0f;
    const Real targetAfter = b.graph().joints[1].dof[DofRx].drive.target;
    std::printf("  the goal's Y and X arrows dragged 60 px in play: the goal %.3f m down, %.3f m aside; the elbow's servo target %.3f -> %.3f rad; "
                "%zu lines, the last %s\n", double(lowered), double(moved), double(targetBefore), double(targetAfter), lines.size(),
                lines.empty() ? "none" : lines.back().toStdString().c_str());
    c.check(moved > 0.03f && lowered > 0.03f && std::fabs(targetAfter - targetBefore) < 1e-6_r && lines.size() == 2 && lines.back() == "scene_move",
            "the goal's gizmo drags it while the scene plays, each drag one scene_move line; the scene's servo targets stay - the "
            "running world leads the hand to the goal");
    const uint64_t from = v->snapshot() ? v->snapshot()->frame : 0;
    waitFrames(v, from + 150, 20000);
    const float off = g ? length(handNow(b, v) - g->position) : 1.0f;
    std::printf("  2.5 s later the hand is %.4f m from the goal\n", double(off));
    c.check(off < 0.01f, "the hand follows the goal by its servos (within 1 cm)");
    // The goal selected while the scene runs: its arm in the plots, a chart a motor; its solver under its card.
    auto* plots = w.findChild<PlotPanel*>();
    const PlotSeries* elbow = plots ? plots->series("Шарнир 11/rx coordinate") : nullptr;
    const PlotSeries* miss = plots ? plots->series(entity(b, goal)->name + "/miss") : nullptr;
    auto* ik = dynamic_cast<IkSolverView*>(w.findChild<QWidget*>("ikSolverView"));
    std::printf("  plots: «%s», %zu charts; the elbow's angle %s, the miss %s; the solver's card %s with %zu rounds\n",
                plots ? plots->objectTitle().toStdString().c_str() : "?", plots ? plots->charts().size() : 0, elbow ? "recorded" : "missing",
                miss ? "recorded" : "missing", ik && ik->isVisible() ? "shown" : "hidden", ik ? ik->residual().size() : 0);
    c.check(plots && plots->objectTitle().startsWith("Рука: ") && elbow && miss, "an IK goal selected in play shows its arm's motors and miss in the plots");
    c.check(ik && ik->isVisible() && ik->residual().size() >= 2, "the goal's card shows its solver's residual round by round");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/goal-gizmo-play.png");
    b.select(4); // the hand: no gizmo in play
    pump(60);
    const bool hidden = !v->gizmoShown();
    sendMouse(v, QEvent::MouseButtonPress, toPoint(v->gizmoView().project(g ? g->position : Vector3(0.0f))), Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, toPoint(v->gizmoView().project(g ? g->position : Vector3(0.0f))), Qt::LeftButton, Qt::NoButton);
    pump(60);
    c.check(hidden && b.selectedId() == goal && v->gizmoShown(), "in play a click on the see-through goal selects it again, the hand inside not grabbed");
    b.stop();
    pump(100);
}

} // namespace

// The UR5e of examples/: in edit mode its goal's gizmo moves the goal, not the arm; its goal's card has «Схват: сжать»; a click closes the fingers (their drives to the far end)
// as one assembly_grip line in the console, the button turns into «Схват: разжать», one undo opens them again; in play the mouse tugs the arm by a finger;
// the whole robot selected shows its collider guides at once.
void testGripButton(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    QString error;
    const bool opened = b.openFile(QString(KE_EXAMPLES) + "/ur5e.kscene", error);
    c.check(opened, "examples/ur5e.kscene opens");
    if (!opened) return;
    // The whole UR5e selected: its links' collider guides drawn at once - from the points the engine gets (HullPoints.h),
    // not from the hull of each mesh's tens of thousands of points (26 s, the window hung at a click on the arm).
    QElapsedTimer clock;
    clock.start();
    b.select(54);
    pump(30);
    const qint64 selectMs = clock.elapsed();
    std::printf("  the UR5e's group selected with its collider guides in %lld ms\n", static_cast<long long>(selectMs));
    c.check(selectMs < 1000, "selecting the whole UR5e draws its links' collider guides at once (under a second)");
    b.select(52);
    pump(60);
    // In edit mode there are no motors: the goal's gizmo moves the goal, the arm stands; the card forecasts.
    const Vector3 hand = entity(b, 16)->position, goalAt = entity(b, 52)->position;
    dragArrow(v, 1, -60.0f);
    const float goalMoved = length(entity(b, 52)->position - goalAt), handMoved = length(entity(b, 16)->position - hand);
    std::printf("  edit mode, the goal's Y arrow dragged: the goal %.3f m, the hand %.4f m\n", double(goalMoved), double(handMoved));
    c.check(goalMoved > 0.03f && handMoved < 1e-6f,
            "in edit mode the goal's gizmo moves only the goal - the arm stands (no motors without the physics)");
    b.undoAction()->trigger();
    pump(60);
    auto* grip = w.findChild<QPushButton*>("gripToggle");
    auto* dock = w.findChild<QDockWidget*>("consoleDock");
    auto* log = dock ? dock->findChild<QPlainTextEdit*>() : nullptr;
    const auto target = [&] {
        for (const JointObject& j : b.graph().joints)
            if (j.id == 19) return j.dof[0].drive.target;
        return Real(-1);
    };
    const bool offered = grip && grip->isVisible() && grip->text() == "Схват: сжать";
    if (grip) grip->click();
    pump(60);
    const bool closed = target() > 0.0279f && grip && grip->text() == "Схват: разжать" && log &&
                        log->toPlainText().contains("ed.assembly_grip(goal=52, close=True)");
    b.undoAction()->trigger();
    pump(60);
    c.check(offered && closed && target() < 1e-6f && grip->text() == "Схват: сжать",
            "the UR5e's goal offers «Схват: сжать»; a click closes the fingers as one assembly_grip line, one undo opens them");
    // In play the mouse tugs the arm by a finger of 17 g - under the force-torque sensor's glyph, whose gizmo the press
    // selects there - and let go, the servos bring the gripper back to where it stood.
    b.select(0); // the goal's gizmo around the hand would take the press: a drag of its handle moves the goal
    b.play();
    const auto frame = [v] { return v->snapshot() ? v->snapshot()->frame : 0; };
    waitFrames(v, frame() + 60, 10000);
    const Vector3 home = handNow(b, v, 16);
    const QPointF from = toPoint(v->gizmoView().project(handNow(b, v, 18))), to = from + QPointF(0, -80);
    sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 8; ++k) sendMouse(v, QEvent::MouseMove, from + (to - from) * (k / 8.0), Qt::NoButton, Qt::LeftButton), pump(16);
    waitFrames(v, frame() + 60, 10000);
    const float tugged = length(handNow(b, v, 16) - home);
    sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    waitFrames(v, frame() + 360, 20000);
    const float back = length(handNow(b, v, 16) - home), goalMovedInPlay = length(entity(b, 52)->position - goalAt);
    std::printf("  in play a finger dragged 80 px up: the gripper %.3f m from where it stood; let go, 6 s later %.4f m (the goal moved %.4f m)\n",
                double(tugged), double(back), double(goalMovedInPlay));
    c.check(tugged > 0.05f && back < 0.005f && goalMovedInPlay < 1e-6f,
            "in play the mouse tugs the UR5e by a finger under a sensor's glyph, and let go, the arm comes back");
    b.stop();
    pump(100);
}

int runGoalTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    testGoalInPlay(c, w, b, v, shotsDir);
    testGripButton(c, w, b, v);
    return c.failures;
}
