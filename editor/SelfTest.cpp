// The scene builder's self-test (see SelfTest.h). Each check prints one line, as ke_tests does.
#include "SelfTest.h"
#include "SelfTestSupport.h"

#include "ApiConsole.h"
#include "EditorOps.h"
#include "scene/SceneActions.h"
#include "scene/SceneEdits.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "Gizmo.h"
#include "MainWindow.h"
#include "PlotChannels.h"
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "UiLanguage.h"
#include "Viewport.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMainWindow>
#include <QMenu>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QMouseEvent>
#include <QThread>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

using namespace ke;
using namespace selftest;

namespace {

bool idsUnique(const SceneGraph& g) {
    std::set<uint32_t> ids;
    for (const Entity& e : g.entities)
        if (e.id == 0 || !ids.insert(e.id).second) return false;
    return true;
}

// A pixel where the gizmo reports this handle: along the arrows, then around the rings.
Vector2 handlePoint(const Gizmo& g, const GizmoView& v, GizmoHandle want) {
    const Vector3 c = g.position();
    const float L = g.armLength(v);
    for (float s = 0.35f; s <= 1.0f; s += 0.05f)
        for (int k = 0; k < 3; ++k) {
            const Vector2 p = v.project(c + g.axis(k) * (L * s));
            if (g.hitTest(v, p) == want) return p;
        }
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 96; ++i) {
            const float t = 2 * kPi * i / 96;
            const Vector2 p = v.project(c + (g.axis((k + 1) % 3) * std::cos(t) + g.axis((k + 2) % 3) * std::sin(t)) * g.ringRadius(v));
            if (g.hitTest(v, p) == want) return p;
        }
    return Vector2(-1.0f, -1.0f);
}

GizmoView testView() {
    GizmoView v;
    v.size = QSize(1200, 800);
    v.camera.frame(AABB({-1, 0, -1}, {1, 1.5f, 1}), 50.0f, 25.0f); // looking down a little: +Y faces the eye
    return v;
}

float screenAngle(const Vector2& p, const Vector2& c) { return std::atan2(-(p.y - c.y), p.x - c.x); }

// ---------------------------------------------------------------------------
// The gizmo's arithmetic
// ---------------------------------------------------------------------------
void testEuler(Checker& c) {
    const Vector3 angles[] = {{0, 0, 0}, {30, 45, 60}, {-20, 170, 5}, {89.9f, 10, 20}, {90, 30, 40}, {-90, -45, 10}, {45, -120, -150}};
    float worst = 0;
    for (const Vector3& d : angles) {
        const Matrix3x3 a = quaternionFromEulerDeg(d).toMatrix3x3();
        const Matrix3x3 b = quaternionFromEulerDeg(eulerDegFromQuaternion(quaternionFromEulerDeg(d))).toMatrix3x3();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) worst = std::max(worst, std::fabs(a.m[i][j] - b.m[i][j]));
    }
    std::printf("  Euler -> quaternion -> Euler -> quaternion: worst matrix difference %.1e (gimbal lock at pitch 90 included)\n", double(worst));
    c.check(worst < 1e-3f, "rotation survives the round trip through Euler angles");
}

void testArrowDrag(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Translate);
    const Vector3 start(0.3f, 0.5f, -0.2f);
    g.setTarget(start, Quaternion());
    const float L = g.armLength(v);
    const Vector2 m0 = v.project(start + Vector3(1, 0, 0) * (0.6f * L));
    c.check(g.hitTest(v, m0) == GizmoHandle::AxisX, "the mouse on the middle of the X arrow picks the X arrow");
    g.begin(GizmoHandle::AxisX, v, m0);
    const Vector2 dir = normalize(v.project(start + Vector3(L, 0, 0)) - v.project(start));
    const Vector2 m1 = m0 + dir * 60.0f;
    const GizmoPose pose = g.drag(v, m1, false);
    const float miss = length(v.project(pose.position + Vector3(0.6f * L, 0, 0)) - m1);
    std::printf("  X arrow dragged 60 px: the object moved %.4f m along X; the grabbed point is %.2f px from the cursor\n",
                double(pose.position.x - start.x), double(miss));
    c.check(miss < 1.5f && pose.position.x > start.x && pose.position.y == start.y && pose.position.z == start.z,
            "an arrow drag moves along its axis only and keeps the grabbed point under the cursor");
    const GizmoPose snapped = g.drag(v, m0 + dir * 47.0f, true);
    const float steps = (snapped.position.x - start.x) / 0.1f;
    c.check(std::fabs(steps - std::round(steps)) < 1e-3f, "Ctrl snaps a move to 0.1 m");
    g.end();
}

void testRingTurn(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Rotate);
    const Vector3 start(0.0f, 0.4f, 0.0f);
    g.setTarget(start, Quaternion());
    const Vector2 m0 = handlePoint(g, v, GizmoHandle::AxisY);
    c.check(m0.x >= 0, "a pixel on the Y ring picks the Y ring");
    g.begin(GizmoHandle::AxisY, v, m0);
    const Vector2 centre = v.project(start);
    const float r = length(m0 - centre), a0 = screenAngle(m0, centre);
    GizmoPose pose;
    for (int i = 1; i <= 10; ++i) { // a quarter turn anticlockwise on screen, in steps
        const float a = a0 + 0.5f * kPi * float(i) / 10;
        pose = g.drag(v, centre + Vector2(std::cos(a), -std::sin(a)) * r, false);
    }
    const Vector3 euler = eulerDegFromQuaternion(pose.rotation);
    std::printf("  Y ring turned a quarter anticlockwise on screen: %.2f degrees; the object's yaw %.2f\n", double(g.dragAngleDeg()),
                double(euler.y));
    c.check(std::fabs(g.dragAngleDeg() - 90.0f) < 0.5f && std::fabs(euler.y - 90.0f) < 0.5f, "a quarter turn of the ring is +90 degrees about Y");
    g.drag(v, centre + Vector2(std::cos(a0 + 0.4f), -std::sin(a0 + 0.4f)) * r, true);
    c.check(std::fabs(g.dragAngleDeg() / 15.0f - std::round(g.dragAngleDeg() / 15.0f)) < 1e-3f, "Ctrl snaps a turn to 15 degrees");
    // The hints of a quarter turn: the turn is free while it is dragged - 86 degrees is 86 -, and let go there it is
    // completed to 90; let go far from a quarter, or nearly back at the start, it stays as the hand left it.
    const auto turnedTo = [&](float degrees) {
        g.end();
        g.setTarget(start, Quaternion());
        g.begin(GizmoHandle::AxisY, v, m0);
        for (int i = 1; i <= 10; ++i) {
            const float a = a0 + degToRad(degrees) * float(i) / 10;
            g.drag(v, centre + Vector2(std::cos(a), -std::sin(a)) * r, false);
        }
        return g.dragAngleDeg();
    };
    GizmoPose settled;
    const float free = turnedTo(86.0f);
    const bool taken = g.settle(settled);
    const float completed = g.dragAngleDeg(), yaw = eulerDegFromQuaternion(settled.rotation).y;
    const float far = turnedTo(60.0f);
    const bool left = !g.settle(settled);
    const float back = turnedTo(3.0f);
    const bool small = !g.settle(settled);
    std::printf("  a turn let go at %.1f degrees: completed to %.1f (yaw %.2f); at %.1f: left; at %.1f: left\n", double(free), double(completed),
                double(yaw), double(far), double(back));
    c.check(std::fabs(free - 86.0f) < 0.6f && taken && completed == 90.0f && std::fabs(yaw - 90.0f) < 1e-3f,
            "a turn is free while dragged; let go within 6 degrees of a quarter, it is completed to the quarter exactly");
    c.check(left && small && std::fabs(far - 60.0f) < 0.6f, "a turn let go away from a quarter - or a small one, near its start - stays as the hand left it");
    g.end();
}

void testScaleDrag(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Scale);
    const Vector3 start(0.0f, 0.3f, 0.2f);
    g.setTarget(start, Quaternion());
    const float L = g.armLength(v);
    const Vector2 m0 = v.project(start + Vector3(L, 0, 0));
    c.check(g.hitTest(v, m0) == GizmoHandle::AxisX, "the X cube picks the X scale handle");
    g.begin(GizmoHandle::AxisX, v, m0);
    const GizmoPose pose = g.drag(v, v.project(start + Vector3(2 * L, 0, 0)), false);
    std::printf("  X cube dragged to twice its distance: scale %.3f x %.3f x %.3f\n", double(pose.scale.x), double(pose.scale.y), double(pose.scale.z));
    c.check(std::fabs(pose.scale.x - 2.0f) < 0.02f && pose.scale.y == 1.0f && pose.scale.z == 1.0f, "dragging the X cube twice as far scales X by 2");
    const GizmoPose snapped = g.drag(v, v.project(start + Vector3(1.37f * L, 0, 0)), true);
    c.check(std::fabs(snapped.scale.x * 10 - std::round(snapped.scale.x * 10)) < 1e-3f, "Ctrl snaps a scale to 10 %");
    g.end();
}

// Keyboard transforms: G, then X, then "0.5": exactly half a metre along X; Shift+R, Z, "90": a quarter turn.
void testKeyboardTransform(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Translate);
    const Vector3 start(0.1f, 0.2f, 0.3f);
    g.setTarget(start, Quaternion());
    const Vector2 mouse(700, 350);
    g.beginModal(GizmoMode::Translate, v, mouse);
    g.constrainModal(0, v);
    g.setTyped(true, 0.5f);
    const GizmoPose moved = g.drag(v, mouse + Vector2(40, 10), false);
    g.end();
    c.check(std::fabs(moved.position.x - 0.6f) < 1e-6f && moved.position.y == start.y && moved.position.z == start.z,
            "keyboard: G, X, 0.5 moves exactly 0.5 m along X");
    g.beginModal(GizmoMode::Rotate, v, mouse);
    g.constrainModal(2, v);
    g.setTyped(true, 90.0f);
    const GizmoPose turned = g.drag(v, mouse, false);
    g.end();
    const Vector3 e = eulerDegFromQuaternion(turned.rotation);
    c.check(std::fabs(e.z - 90.0f) < 0.01f && std::fabs(e.x) < 0.01f && std::fabs(e.y) < 0.01f && g.mode() == GizmoMode::Translate,
            "keyboard: Shift+R, Z, 90 turns a quarter about Z; the tool comes back afterwards");
}

// ---------------------------------------------------------------------------
// The builder in the window
// ---------------------------------------------------------------------------
void testEditPlayStop(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    pump(100);
    const Entity& floor = b.graph().entities[0];
    c.check(b.editing() && b.graph().entities.size() == 1 && floor.locked && floor.collider.enabled && !floor.rigid.enabled,
            "a new scene is a locked floor with a collider only (a fixed obstacle), in edit mode");
    b.createActions()[0]->trigger(); // Куб
    const Entity cube = last(b);
    c.check(b.graph().entities.size() == 2 && cube.shape == ShapeKind::Box && entityIsGeometryOnly(cube) &&
                std::fabs(cube.position.y - (0.8f + 0.5f * cube.size.y)) < 1e-6f,
            "the cube button makes geometry only: no component, hanging 0.8 m above the floor (ready to fall)");
    pump(700);
    c.check(b.editing() && same(last(b).position, cube.position) && v->snapshot()->bodies.size() == 2 &&
                same(v->snapshot()->bodies[1].pos, cube.position), "edit mode: the cube does not move (nothing is simulated)");
    b.editEntity(cube.id, [](Entity& e) { e.position.y = 1.0f; });
    clickRole(w, RoleIcon::Rigid);
    pump(700);
    c.check(last(b).rigid.enabled && last(b).position.y == 1.0f && v->snapshot()->bodies[1].pos.y == 1.0f,
            "a rigid cube 1 m up still does not move in edit mode");
    b.play();
    const bool ran = waitFrames(v, 60, 15000);
    float y = 1e9f;
    for (const auto& body : v->snapshot()->bodies)
        if (body.movable) y = body.pos.y;
    std::printf("  after 60 frames of play the cube is at y = %.3f m\n", double(y));
    c.check(ran && y < 0.3f, "play: the cube falls onto the floor");
    b.stop();
    pump(300);
    c.check(b.editing() && last(b).position.y == 1.0f && v->snapshot()->bodies[1].pos.y == 1.0f, "stop: the cube is back where it was authored");
}

// Undo and redo of a component, and ids never given out twice in a session.
void testUndoRedo(Checker& c, QMainWindow& w, SceneBuilder& b) {
    b.createActions()[1]->trigger(); // Сфера
    clickRole(w, RoleIcon::Rigid);
    c.check(last(b).rigid.enabled && last(b).collider.enabled, "the rigid component brings its collider along");
    const uint32_t highest = last(b).id;
    b.undoAction()->trigger();
    c.check(!last(b).rigid.enabled, "undo takes the rigid component off");
    b.redoAction()->trigger();
    c.check(last(b).rigid.enabled, "redo puts it back");
    b.createActions()[0]->trigger(); // Куб
    c.check(last(b).id > highest && idsUnique(b.graph()), "ids are never reused within a session");
}

// A real mouse drag in the window: press on the X arrow of the selected cube, move 80 px, release.
void testMouseDrag(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.createActions()[0]->trigger();
    trigger(w, "toolMove");
    pump(300);
    const Entity before = last(b);
    const GizmoView gv = v->gizmoView();
    const Vector2 m0 = handlePoint(v->gizmo(), gv, GizmoHandle::AxisX);
    const Vector2 dir = normalize(gv.project(before.position + Vector3(1, 0, 0)) - gv.project(before.position));
    sendMouse(v, QEvent::MouseMove, toPoint(m0), Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, toPoint(m0), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i) sendMouse(v, QEvent::MouseMove, toPoint(m0 + dir * (10.0f * i)), Qt::NoButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, toPoint(m0 + dir * 80.0f), Qt::LeftButton, Qt::NoButton);
    pump(100);
    const Vector3 after = last(b).position;
    std::printf("  mouse drag of the X arrow by 80 px: x %.3f -> %.3f m\n", double(before.position.x), double(after.x));
    c.check(m0.x >= 0 && after.x - before.position.x > 0.03f && after.y == before.position.y && after.z == before.position.z,
            "the mouse drags the cube along X by its arrow");
    b.undoAction()->trigger();
    c.check(b.graph().entities.size() == 2 && same(last(b).position, before.position), "one Ctrl+Z undoes the whole drag");
}

// Three cubes in a row along one ray: clicks on the same pixel take them nearest first, then round.
void testCyclePicking(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    for (int i = 0; i < 3; ++i) b.createActions()[0]->trigger();
    pump(200);
    const GizmoView gv = v->gizmoView();
    const Vector2 pixel(float(v->width()) * 0.5f, float(v->height()) * 0.55f);
    const Ray ray = gv.ray(pixel);
    const float toFloor = -ray.origin.y / ray.dir.y;
    const size_t n = b.graph().entities.size();
    uint32_t ids[3];
    for (int i = 0; i < 3; ++i) {
        ids[i] = b.graph().entities[n - 3 + size_t(i)].id;
        const Vector3 p = ray.at(toFloor * (0.5f + 0.15f * float(i)));
        b.editEntity(ids[i], [p](Entity& e) { e.position = p; });
    }
    trigger(w, "toolSelect");
    pump(100);
    uint32_t picked[4];
    for (uint32_t& id : picked) {
        sendMouse(v, QEvent::MouseButtonPress, toPoint(pixel), Qt::LeftButton, Qt::LeftButton);
        sendMouse(v, QEvent::MouseButtonRelease, toPoint(pixel), Qt::LeftButton, Qt::NoButton);
        pump(40);
        id = b.selectedId();
    }
    std::printf("  four clicks on one pixel picked cubes %u %u %u %u (in a row: %u %u %u)\n", picked[0], picked[1], picked[2], picked[3],
                ids[0], ids[1], ids[2]);
    c.check(picked[0] == ids[0] && picked[1] == ids[1] && picked[2] == ids[2] && picked[3] == ids[0],
            "clicking the same spot again selects the next object behind, then round to the nearest");
    trigger(w, "toolMove");
}

// The ruler of the tool shelf: its button takes the view; a drag from a point of the floor to another leaves the line
// between them with its length over it - the length view_measure answers -; Esc leaves the ruler and takes the line off.
void testRuler(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    auto* ruler = w.findChild<QAction*>("toolRuler");
    if (!ruler) return void(c.check(false, "the tool shelf has «Линейка»"));
    b.newScene();
    b.select(0);
    pump(100);
    ruler->trigger();
    pump(50);
    const bool taken = v->pluginTool() == "view_measure" && ruler->isChecked();
    const Vector3 a(-1.0f, 0.0f, 0.0f), z(1.0f, 0.0f, 0.5f);
    const QPointF from = v->pixelOf(a), to = v->pixelOf(z);
    sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i) sendMouse(v, QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    pump(100);
    const Viewport::Drawing* line = v->drawing("editor/ruler");
    const float drawn = line && line->points.size() == 2 ? length(line->points[1] - line->points[0]) : -1.0f;
    const double said = ops->runNow("view_measure", {{"point", QJsonArray{-1, 0, 0}}, {"to", QJsonArray{1, 0, 0.5}}}).value("length").toDouble();
    std::printf("  the ruler dragged over the floor from (-1, 0, 0) to (1, 0, 0.5): the line %.3f m, «%s»; view_measure %.4f m\n",
                double(drawn), line ? qPrintable(line->text) : "-", said);
    c.check(taken && std::fabs(drawn - length(z - a)) < 0.02f && std::fabs(said - double(length(z - a))) < 1e-4,
            "the ruler: a drag from point to point leaves the line between them with its length");
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &esc);
    pump(50);
    c.check(v->pluginTool().isEmpty() && !ruler->isChecked() && !v->hasDrawing("editor/ruler"), "Esc leaves the ruler and takes its line off");
}

// A joint's drive changed while the scene plays: the servo of a hinge gets a new target and the arm turns
// to it in the running world - nothing is built again, the post's body keeps its index; Stop brings the
// target back as it was authored. A change of what the world is built of (the post's size)
// cannot be taken in place: the scene stops with the edit kept.
void testLiveEdit(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    uint32_t post = 0, joint = 0;
    std::string error;
    b.transact([&](SceneGraph& g, std::string&) {
        post = ke::addShape(g, ShapeKind::Box);
        const uint32_t arm = ke::addShape(g, ShapeKind::Box);
        for (Entity& e : g.entities) {
            if (e.id == post) e.size = Vector3(0.1f, 1.0f, 0.1f), e.position = Vector3(0.0f, 0.5f, 0.0f), e.rigid.fixed = true;
            if (e.id == arm) e.size = Vector3(0.4f, 0.04f, 0.04f), e.position = Vector3(0.2f, 1.05f, 0.0f);
            if (e.id == post || e.id == arm) e.rigid.enabled = e.collider.enabled = true;
        }
        JointObject j;
        j.id = joint = ke::nextId(g);
        j.name = "Плечо";
        j.a = arm, j.b = post;
        j.anchorA = Vector3(-0.2f, 0.0f, 0.0f), j.anchorB = Vector3(0.0f, 0.55f, 0.0f);
        j.frameA = j.frameB = Vector3(0.0f, -90.0f, 0.0f); // x onto the world's z: the arm swings up and down
        j.dof[DofRx].motion = JointMotion::Free;
        JointDrive& d = j.dof[DofRx].drive;
        d.enabled = true, d.stiffness = 60, d.damping = 4, d.target = 0, d.maxForce = 100;
        g.joints.push_back(j);
        return true;
    }, error);
    auto angle = [&] { return v->snapshot() && !v->snapshot()->joints.empty() ? double(v->snapshot()->joints[0].angle) : 1e9; };
    auto target = [&](float to) {
        return b.transact([&](SceneGraph& g, std::string&) {
            for (JointObject& j : g.joints) j.dof[DofRx].drive.target = to;
            return true;
        }, error);
    };
    b.play();
    const bool ran = waitFrames(v, 60, 15000);
    const double before = angle();
    const int postBody = b.bodyOfEntity(post);
    const uint64_t frame = v->snapshot() ? v->snapshot()->frame : 0;
    const bool set = target(0.6f);
    waitFrames(v, frame + 120, 15000);
    const double after = angle();
    std::printf("  the servo's target 0 -> 0.6 rad while playing: the arm %.3f -> %.3f rad; post body %d -> %d\n", before, after, postBody,
                b.bodyOfEntity(post));
    c.check(ran && set && std::fabs(before) < 0.05 && std::fabs(after - 0.6) < 0.05 && b.mode() == SceneBuilder::Mode::Playing &&
                b.bodyOfEntity(post) == postBody,
            "during play, a servo's new target turns the arm at once; nothing is built again");
    b.stop();
    pump(300);
    double authored = 1e9;
    for (const JointObject& j : b.graph().joints)
        if (j.id == joint) authored = double(j.dof[DofRx].drive.target);
    c.check(b.editing() && authored == 0.0, "stop: the target is back as authored (edits during play are not kept)");
    b.play();
    waitFrames(v, 10, 15000);
    b.editEntity(post, [](Entity& e) { e.size.y = 1.2f; });
    pump(300);
    float size = 0;
    for (const Entity& e : b.graph().entities)
        if (e.id == post) size = e.size.y;
    c.check(b.editing() && size == 1.2f, "a body resized during play stops the scene, the edit kept");
    // The answer says which it was: what the world took is until_stop, what stopped it is play_stopped and why.
    EditorOps& ops = *dynamic_cast<MainWindow&>(w).operators();
    b.play();
    waitFrames(v, 10, 15000);
    const QJsonObject taken = ops.runNow("scene_set", {{"id", int(joint)}, {"line", "drive"}, {"key", "target"}, {"values", QJsonArray{0.3}}});
    c.check(taken.value("ok").toBool() && taken.contains("until_stop") && !taken.contains("play_stopped") && !b.editing(),
            "an edit the running world took: until_stop, the world goes on");
    const QJsonObject undone = ops.runNow("edit_undo", {});
    c.check(undone.value("ok").toBool() && undone.contains("until_stop") && !undone.contains("play_stopped") && !b.editing(),
            "edit_undo of such an edit while the world is built: until_stop too");
    const QJsonObject moved = ops.runNow("scene_move", {{"ids", QJsonArray{int(post)}}, {"by", QJsonArray{0, 0.1, 0}}});
    c.check(moved.value("ok").toBool() && !moved.contains("until_stop") && moved.value("play_stopped").toBool() &&
                moved.value("reload").toString() == "bodies" && b.editing(),
            "an edit that stopped the world: play_stopped and reload, no until_stop, mode edit, the edit kept");
    const QJsonObject idle = ops.runNow("scene_move", {{"ids", QJsonArray{int(post)}}, {"by", QJsonArray{0, 0.1, 0}}});
    c.check(idle.value("ok").toBool() && !idle.contains("until_stop") && !idle.contains("play_stopped"), "no world, no note");
}

// The right-click menu: "Вращение" and "Локальная" switch the gizmo, and the toolbar follows.
void testContextMenu(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    auto* mw = dynamic_cast<MainWindow*>(&w);
    if (!mw) return c.check(false, "the window is the editor's MainWindow");
    b.newScene();
    b.createActions()[0]->trigger();
    trigger(w, "toolMove");
    pump(100);
    auto find = [](QMenu* m, const QString& text) -> QAction* {
        for (QAction* a : m->actions())
            if (a->text() == text) return a;
        return nullptr;
    };
    QMenu* menu = mw->buildEditContextMenu();
    QAction* rotate = find(menu, "Вращение");
    QAction* local = find(menu, "Оси объекта");
    const bool complete = rotate && local && find(menu, "Удалить") && find(menu, "Дублировать") && !find(menu, "Роль");
    if (rotate) rotate->trigger();
    if (local) local->trigger();
    auto* toolbarRotate = w.findChild<QAction*>("toolRotate");
    c.check(complete && v->gizmo().mode() == GizmoMode::Rotate && v->gizmo().local() && toolbarRotate && toolbarRotate->isChecked(),
            "the right-click menu switches the gizmo to rotation and local axes; the toolbar follows");
    delete menu;
    QMenu* again = mw->buildEditContextMenu();
    if (QAction* axes = find(again, "Оси объекта")) axes->trigger(); // ticked: off again - world axes
    delete again;
    trigger(w, "toolMove");
    c.check(!v->gizmo().local() && v->gizmo().mode() == GizmoMode::Translate, "and back: world axes, the move tool");
}

// The authored scene: a few shapes, a model and the robot arm of the examples, in edit mode and then playing.
void sceneShots(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    QString error;
    if (!b.openFile(MainWindow::examplesDir() + "/robot-arm.kscene", error)) b.newScene();
    for (int k = 0; k < 2; ++k) b.createActions()[size_t(k)]->trigger();
    b.importModel(MainWindow::examplesDir() + "/models/house.obj", error);
    pump(500);
    windowShot(w).save(dir + "/edit.png");
    b.play();
    waitFrames(v, 90, 20000);
    windowShot(w).save(dir + "/play.png");
    b.stop();
    pump(200);
}

} // namespace

int runGizmoShots(QMainWindow& w, const QString& dir) {
    auto* b = w.findChild<SceneBuilder*>();
    auto* v = w.findChild<Viewport*>();
    if (!b || !v) return 3;
    QDir().mkpath(dir);
    pump(300);
    b->newScene();
    b->createActions()[0]->trigger();
    b->editEntity(last(*b).id, [](Entity& e) { e.size = Vector3(0.5f); e.position.y = 0.25f; e.rotationDeg = Vector3(0, 25, 0); });
    const struct { const char* tool; const char* name; GizmoHandle hover; } shots[] = {
        {"toolMove", "gizmo-w", GizmoHandle::AxisX}, {"toolRotate", "gizmo-e", GizmoHandle::AxisX}, {"toolScale", "gizmo-r", GizmoHandle::AxisX}};
    int missing = 0;
    for (const auto& s : shots) {
        trigger(w, s.tool);
        pump(250);
        const GizmoView gv = v->gizmoView();
        const Vector2 p = handlePoint(v->gizmo(), gv, s.hover);
        sendMouse(v, QEvent::MouseMove, toPoint(p), Qt::NoButton, Qt::NoButton);
        pump(250);
        const QImage frame = v->grabFramebuffer();
        missing += windowShot(w).save(dir + "/" + s.name + ".png") ? 0 : 1;
        const Vector2 c = gv.project(v->gizmo().position());
        frame.copy(int(c.x) - 230, int(c.y) - 200, 460, 400).save(dir + "/" + s.name + "-close.png");
    }
    // A turn in progress: the pie slice and the angle next to the cursor.
    trigger(w, "toolRotate");
    pump(150);
    const GizmoView gv = v->gizmoView();
    const Vector2 m0 = handlePoint(v->gizmo(), gv, GizmoHandle::AxisY), centre = gv.project(v->gizmo().position());
    const float r = length(m0 - centre), a0 = screenAngle(m0, centre);
    sendMouse(v, QEvent::MouseButtonPress, toPoint(m0), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i) {
        const float a = a0 + 1.05f * float(i) / 12;
        sendMouse(v, QEvent::MouseMove, toPoint(centre + Vector2(std::cos(a), -std::sin(a)) * r), Qt::NoButton, Qt::LeftButton);
    }
    pump(250);
    const QImage frame = v->grabFramebuffer();
    missing += windowShot(w).save(dir + "/gizmo-e-drag.png") ? 0 : 1;
    frame.copy(int(centre.x) - 230, int(centre.y) - 200, 460, 400).save(dir + "/gizmo-e-drag-close.png");
    sendMouse(v, QEvent::MouseButtonRelease, toPoint(centre), Qt::LeftButton, Qt::NoButton);
    trigger(w, "toolMove");
    return missing;
}

// The API console reads back the Python line it writes for any call - the name and the arguments the
// same, the literals (a string with quotes and an "=" in it, numbers, a list, True, None) as JSON; single
// quotes and a line without "ed." read too; a broken line is refused.
void testApiConsole(Checker& c) {
    const QJsonObject args{{"target", 166}, {"kind", "sonar"}, {"point", QJsonArray{0.5, 1, -2}}, {"flip", true}, {"note", QJsonValue::Null},
                           {"text", "a = \"b\" 'c'"}};
    QString name, error;
    QJsonObject back, single;
    const bool read = ApiConsole::parseCall(ApiConsole::pythonCall("assembly_sensor", args), name, back, error);
    c.check(read && name == "assembly_sensor" && back == args, "the API console reads back its own Python line");
    c.check(ApiConsole::parseCall("scene_get(id=5, mode='a b')", name, single, error) && single.value("mode").toString() == "a b" &&
                single.value("id").toInt() == 5,
            "the API console reads single quotes and a call without 'ed.'");
    c.check(!ApiConsole::parseCall("ed.scene_list(id=)", name, single, error), "the API console refuses a broken line");
}

// The ids of `g` from `from` up moved by `by`: the scene the lines made again after an undo, where the
// editor gave the same objects new ids (it never reuses one), compared with the scene the hand made.
void shiftIds(SceneGraph& g, uint32_t from, uint32_t by) {
    auto s = [&](uint32_t& id) { id += id >= from ? by : 0; };
    auto obj = [&](SceneObject& o) { s(o.id), s(o.parent); };
    for (Entity& e : g.entities) obj(e), s(e.instanceOf);
    for (Group& o : g.groups) obj(o);
    for (ArrayObject& o : g.arrays) obj(o), s(o.templateId);
    for (Light& o : g.lights) obj(o);
    for (Camera& o : g.cameras) obj(o);
    for (SensorObject& o : g.sensors) obj(o), s(o.against);
    for (JointObject& j : g.joints) s(j.id), s(j.a), s(j.b);
    for (BatteryObject& o : g.batteries) s(o.id);
    for (DriverObject& d : g.drivers) s(d.id), s(d.joint), s(d.battery);
}

// The lines of a hand, run as the operations they name after an undo back to where it began: the ids
// it named that the lines made anew are moved by `by`, known once the first line answers: `by` (0: a line
// failed).
uint32_t replayLines(EditorOps& ops, std::vector<std::pair<QString, QJsonObject>> lines, uint32_t from, uint32_t firstMade) {
    uint32_t by = 0;
    bool all = true;
    for (auto& [op, a] : lines) {
        if (a.contains("id")) a["id"] = int(uint32_t(a.value("id").toInt()) + (uint32_t(a.value("id").toInt()) >= from ? by : 0));
        QJsonArray ids;
        for (const QJsonValue& v : a.value("ids").toArray()) ids.append(int(uint32_t(v.toInt()) + (uint32_t(v.toInt()) >= from ? by : 0)));
        if (a.contains("ids")) a["ids"] = ids;
        const QJsonObject answer = ops.runNow(op, a);
        if (!answer.value("ok").toBool())
            std::printf("  %s refused: %s\n", qPrintable(op), qPrintable(answer.value("error").toString()));
        all = all && answer.value("ok").toBool();
        if (op == "scene_add" && by == 0) by = uint32_t(answer.value("id").toInt()) - firstMade;
    }
    return all ? by : 0;
}

// The console's Python (ApiConsole, kinetica.console): a line typed in the window's console runs with the
// whole object model - a cone made by scene.add, a value printed - and the call it made shows as the user's.
void testPythonConsole(Checker& c, QMainWindow& w, SceneBuilder& b) {
    auto* input = w.findChild<QLineEdit*>("apiConsoleInput");
    auto* log = w.findChild<QDockWidget*>("consoleDock") ? w.findChild<QDockWidget*>("consoleDock")->findChild<QPlainTextEdit*>() : nullptr;
    if (!input || !log) return c.check(false, "the window has its console");
    b.newScene();
    const size_t before = b.graph().entities.size();
    auto type = [input](const QString& line) {
        input->setText(line);
        QMetaObject::invokeMethod(input, "returnPressed");
    };
    type("scene.add(\"cone\", position=(0.3, 0.4, 0))");
    for (int k = 0; k < 100 && b.graph().entities.size() == before; ++k) pump(50);
    type("len(scene.bodies)");
    for (int k = 0; k < 100 && !log->toPlainText().contains(QString::number(int(before))); ++k) pump(50);
    const QString text = log->toPlainText();
    c.check(b.graph().entities.size() == before + 1 && text.contains("[вы]") && text.contains(QString::number(int(before))),
            "a Python line in the console makes a cone, prints a value, and its call shows as the user's");
}

// A hand's session in the window: a cube made rigid, copied, the copy moved by a field; the two grouped
// and ungrouped; the copy picked, dragged by the gizmo (a move - an agent asking the scene's list halfway
// - then a turn), the rotate tool and back, the copy deleted, a lamp added.
void handSession(QMainWindow& w, SceneBuilder& b, EditorOps& ops, uint32_t& cube, uint32_t& copy) {
    b.createActions().front()->trigger();
    cube = b.selectedId();
    b.toggleRole(RoleIcon::Rigid);
    b.duplicateSelected();
    copy = b.selectedId();
    b.editEntity(copy, [](Entity& e) { e.position.x += 0.4_r; });
    pump(500); // the edit rests: its line
    b.selectMany({cube, copy});
    b.groupSelected();
    b.ungroupSelected();
    b.selectMany({copy});
    GizmoPose pose;
    Quaternion q;
    worldPose(b.graph(), *findObject(b.graph(), copy), pose.position, q);
    pose.mode = GizmoMode::Translate, pose.position += Vector3(0.1f, 0.0f, 0.0f), pose.rotation = q;
    b.onGizmoStarted(), b.onGizmoMoved(pose);
    ops.runNow("scene_list", {}); // a question halfway: the drag stays one line
    pose.position += Vector3(0.15f, 0.0f, 0.0f);
    b.onGizmoMoved(pose), b.onGizmoFinished();
    worldPose(b.graph(), *findObject(b.graph(), copy), pose.position, q);
    pose.mode = GizmoMode::Rotate, pose.rotation = Quaternion::fromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 0.5f) * q;
    b.onGizmoStarted(), b.onGizmoMoved(pose), b.onGizmoFinished();
    trigger(w, "toolRotate");
    trigger(w, "toolMove");
    b.removeSelected();
    b.addLight(LightKind::Point);
    pump(500);
}

// Tags: a word put on a shape and on a joint is found by scene_list, shows in the object's card and is one undo step;
// and a joint selected together with a shape - what an agent's selection_set may ask for - does not bring the window
// down (the card of several things read every one of them as a thing with a place).
void testTags(Checker& c, QMainWindow& w, SceneBuilder& b) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    ops->runNow("scene_new", {});
    const int base = ops->runNow("scene_add", {{"shape", "box"}, {"role", "rigid"}, {"fixed", true}}).value("id").toInt();
    const int link = ops->runNow("scene_add", {{"shape", "box"}, {"role", "rigid"}, {"position", QJsonArray{0, 0.6, 0}}}).value("id").toInt();
    const int joint = ops->runNow("assembly_joint", {{"a", link}, {"b", base}, {"anchor", QJsonArray{0, 0.4, 0}}}).value("id").toInt();
    const QJsonObject put = ops->runNow("scene_tag", {{"ids", QJsonArray{base, joint}}, {"tag", "мишень"}});
    const int found = ops->runNow("scene_list", {{"tag", "мишень"}}).value("objects").toArray().size();
    ops->runNow("selection_set", {{"ids", QJsonArray{base}}});
    pump(200);
    auto* field = w.findChild<QLineEdit*>("objectTags");
    const QString shown = field ? field->text() : QString("no field");
    ops->runNow("edit_undo", {});
    const int after = ops->runNow("scene_list", {{"tag", "мишень"}}).value("objects").toArray().size();
    c.check(put.value("changed").toInt() == 2 && found == 2 && shown == "мишень" && after == 0,
            "a tag on a shape and a joint: found by scene_list, shown in the card, gone by one undo");
    ops->runNow("selection_set", {{"ids", QJsonArray{joint, base}}});
    pump(200);
    c.check(b.selection().size() == 2, "a joint selected together with a shape: the window stands");
    // Two shapes with different tags selected: a word typed into the card goes on both, each keeps its own.
    ops->runNow("scene_tag", {{"ids", QJsonArray{base}}, {"tag", "свой"}});
    ops->runNow("scene_tag", {{"ids", QJsonArray{link}}, {"tag", "чужой"}});
    ops->runNow("selection_set", {{"ids", QJsonArray{base, link}}});
    pump(200);
    if (field) {
        const QString typed = field->text() + " общий";
        field->selectAll(), field->insert(typed);
        emit field->editingFinished();
    }
    pump(200);
    const auto tagsOf = [&](int id) {
        QStringList words;
        for (const QJsonValue& t : ops->runNow("scene_get", {{"id", id}}).value("tags").toArray()) words << t.toString();
        return words.join(' ');
    };
    c.check(tagsOf(base) == "свой общий" && tagsOf(link) == "чужой общий",
            ("a tag typed for two shapes goes on both, each keeps its own: «" + tagsOf(base) + "», «" + tagsOf(link) + "»").toStdString().c_str());
    ops->runNow("selection_set", {{"ids", QJsonArray{}}});
}

// The window's buttons as code: each action of a hand's session is its one line of the object model
// (scene.add("box"), editor.select(...), a gizmo drag as .move(...) / .turn("y", ...), the tool shelf as
// editor.gizmo.tool = ...); each edit one undo step; and the lines run as the operations they name make
// the same scene to the bit. Undo's button is scene.undo().
void testWindowAsCode(Checker& c, QMainWindow& w, SceneBuilder& b) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    // What an operation is to its caller, as ops_list says it - an agent's client marks its tools by it (MCP) - and the
    // list of chosen families only.
    std::map<QString, QString> kind;
    for (const QJsonValue& o : ops->runNow("ops_list", {}).value("ops").toArray()) kind[o.toObject().value("name").toString()] = o.toObject().value("kind").toString();
    int foreign = 0, simulation = 0;
    for (const QJsonValue& o : ops->runNow("ops_list", {{"families", QJsonArray{"sim"}}}).value("ops").toArray())
        o.toObject().value("name").toString().startsWith("sim_") ? ++simulation : ++foreign;
    c.check(kind["scene_list"] == "query" && kind["scene_add"] == "edit" && kind["edit_undo"] == "edit" && kind["selection_set"] == "window" &&
            kind["sim_stop"] == "window" && kind["ui_pointer"] == "window" && kind["scene_save"] == "action" && kind["app_quit"] == "action",
            "ops_list says what each operation is: a query, an edit of the scene, the window's own state, or an action");
    c.check(simulation >= 6 && foreign == 0, "ops_list of chosen families gives those operations only");
    std::vector<std::pair<QString, QJsonObject>> lines;
    b.setCodeReporter([&lines](const QString& op, const QJsonObject& args) { lines.push_back({op, args}); });
    pump(500);
    lines.clear();
    const std::string before = b.graph().save();
    uint32_t cube = 0, copy = 0;
    if (!ops) return c.check(false, "the window has its operations");
    handSession(w, b, *ops, cube, copy);
    b.setCodeReporter(nullptr);
    QStringList names, py;
    for (const auto& [op, a] : lines) names << op, py << a.value("_py").toString();
    c.check(names.join(" ") == "scene_add scene_role scene_duplicate scene_set selection_set scene_group scene_ungroup selection_set scene_move "
                               "scene_turn gizmo_set gizmo_set scene_delete scene_add_light",
            ("a hand's session, one line an action: " + names.join(" ").toStdString()).c_str());
    const QString cubeName = QString::fromStdString(ke::findObject(b.graph(), cube)->name);
    const QString cubeRef = "scene.objects[\"" + cubeName + "\"]", copyRef = "scene.objects[\"" + cubeName + " копия\"]";
    c.check(py.size() == 14 && py[0] == "scene.add(\"box\")" && py[1] == cubeRef + ".roles.add(\"rigid\")" && py[2] == cubeRef + ".duplicate()" &&
                py[3].startsWith(copyRef + ".position = (") && py[4] == "editor.select(" + cubeRef + ", " + copyRef + ")" &&
                py[8].startsWith(copyRef + ".move((0.25") && py[9].startsWith(copyRef + ".turn(\"y\", 28.64") &&
                py[10] == "editor.gizmo.tool = \"rotate\"" && py[12] == copyRef + ".delete()" && py[13] == "scene.add_light(\"point\")",
            ("their lines are the object model's: " + py.join(" | ").toStdString()).c_str());
    const SceneGraph hand = b.graph();
    int steps = 0;
    while (b.graph().save() != before && steps < 30) b.undoStep(), ++steps;
    c.check(steps == 10, ("each edit is one undo step, the selection and the tool none (" + std::to_string(steps) + " back)").c_str());
    SceneGraph expected = hand;
    const uint32_t by = replayLines(*ops, lines, cube, cube);
    shiftIds(expected, cube, by);
    c.check(by > 0 && expected.save() == b.graph().save(), "the hand's lines run as operations make the same scene to the bit, the gizmo's too");
    b.setCodeReporter([&lines](const QString& op, const QJsonObject& args) { lines.push_back({op, args}); });
    lines.clear();
    b.undoAction()->trigger();
    pump(100);
    c.check(lines.size() == 1 && lines[0].first == "edit_undo" && lines[0].second.value("_py").toString() == "scene.undo()",
            "the Undo button is the line scene.undo()");
    // As the window has it: the groups after this one are a hand's work too, and with no reporter left their edits made
    // no journal line (seen only in a whole run: «an orange point dragged ... one journal line» of the plugins group).
    b.setCodeReporter([ops](const QString& op, const QJsonObject& args) { ops->report("ui", op, args); });
}

int runBuilderSelfTest(QMainWindow& w, const QString& shotsDir) {
    Checker c;
    auto* b = w.findChild<SceneBuilder*>();
    auto* v = w.findChild<Viewport*>();
    if (!b || !v) {
        c.check(false, "the window has a scene builder and a viewport");
        return c.failures;
    }
    pump(400); // the window gets its size, the viewport its first frame
    // Every body's collider is drawn from the start (a person asked for it), the Laboratory's checkbox with it; the checks
    // below count and look at their own drawings, so the wireframes go off for them.
    auto* colliders = w.findChild<QAction*>("actionColliders");
    QCheckBox* labRow = w.findChild<QCheckBox*>("labActionRow");
    c.check(colliders && colliders->isChecked() && labRow && labRow->isChecked(),
            "every body's collider is drawn from the start, «Коллайдеры всех тел» ticked in the Laboratory");
    if (labRow) labRow->click();
    pump(30);
    c.check(colliders && !colliders->isChecked(), "the Laboratory's «Коллайдеры всех тел» switches them off");
    // KE_OPS_LIST=file: every operation of the window written there, for an API reference.
    if (const QString file = qEnvironmentVariable("KE_OPS_LIST"); !file.isEmpty())
        if (EditorOps* ops = dynamic_cast<MainWindow&>(w).operators()) {
            QFile f(file);
            c.check(f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(ops->list()).toJson()) > 0, "the list of operations is written for the API reference");
        }
    // KE_EDITOR_TEST=name runs only the groups whose name contains it (as KE_TEST does for ke_tests).
    const QString only = qEnvironmentVariable("KE_EDITOR_TEST");
    auto run = [&](const char* name, const std::function<void()>& group) {
        if (only.isEmpty() || QString(name).contains(only)) group();
    };
    run("language", [&] {
        const bool english = UiLanguage::active() == "en";
        auto* menu = w.findChild<QMenu*>("languageMenu");
        auto* ru = w.findChild<QAction*>("language_ru");
        auto* en = w.findChild<QAction*>("language_en");
        auto* play = w.findChild<QAction*>("actionPlay");
        c.check(menu && menu->title() == (english ? "Language" : "Язык") && ru && en
                    && ru->text() == "Русский" && en->text() == "English",
                "the localized menu offers both languages");
        c.check(play && play->text() == (english ? "Play" : "Пуск")
                    && QString::fromUtf8(uiText("Kinetica", ke::shapeTitle(ke::ShapeKind::Box))) == (english ? "Cube" : "Куб")
                    && uiCount(2, UiCount::Object) == (english ? "2 objects" : "2 объекта"),
                "the action, core label and plural use the selected language");
        const auto energy = PlotCatalog().card("scene/kinetic energy");
        const auto& sensors = b->sensorActions();
        c.check(energy.name == (english ? "Kinetic energy" : "Кинетическая энергия")
                    && energy.unit == (english ? "J" : "Дж") && !sensors.empty()
                    && sensors.front()->text() == (english ? "Encoder" : "Энкодер"),
                "chart labels, units and sensor menus localize when read from label tables");
        c.check(ru && en && ru->isChecked() == !english && en->isChecked() == english,
                "the current language is checked");
        auto* push = w.findChild<QAction*>("addForcePush");
        c.check(push && push->text() == (english ? "Push" : "Толчок"), "the force menu localizes");
    });
    run("gizmo", [&] { testEuler(c), testArrowDrag(c), testRingTurn(c), testScaleDrag(c), testKeyboardTransform(c); });
    run("play", [&] { testEditPlayStop(c, w, *b, v), testUndoRedo(c, w, *b); });
    run("mouse", [&] { testMouseDrag(c, w, *b, v), testCyclePicking(c, w, *b, v), testRuler(c, w, *b, v); });
    run("menu", [&] { testContextMenu(c, w, *b, v); });
    run("console", [&] { testApiConsole(c), testWindowAsCode(c, w, *b), testPythonConsole(c, w, *b); });
    run("tags", [&] { testTags(c, w, *b); });
    run("live", [&] { testLiveEdit(c, w, *b, v); });
    run("components", [&] { c.failures += runComponentTests(w, *b, v, shotsDir); });
    run("controls", [&] { c.failures += runControlTests(w, *b, v, shotsDir); });
    if (!shotsDir.isEmpty()) QDir().mkpath(shotsDir);
    run("create", [&] { c.failures += runCreateTests(w, *b, v, shotsDir); });
    run("many", [&] { c.failures += runManyTests(w, *b, v, shotsDir); });
    run("lights", [&] { c.failures += runLightTests(w, *b, v, shotsDir); });
    run("forces", [&] { c.failures += runForceTests(w, *b, v, shotsDir); });
    run("sequencer", [&] { c.failures += runSequencerTests(w, *b, v, shotsDir); });
    run("plugins", [&] { c.failures += runPluginTests(w, *b, v, shotsDir); });
    run("scripts", [&] { c.failures += runScriptTests(w, *b, v, shotsDir); });
    run("shaders", [&] { c.failures += runShaderTests(v); });
    run("bump", [&] { c.failures += runBumpTests(w, *b, v, shotsDir); });
    run("textures", [&] { c.failures += runTextureTests(w, *b, v, shotsDir); });
    run("shadows", [&] { c.failures += runShadowTests(w, *b, v, shotsDir); });
    run("goal", [&] { c.failures += runGoalTests(w, *b, v, shotsDir); });
    run("journal", [&] { c.failures += runJournalTests(w, *b, v, shotsDir); });
    run("world", [&] { c.failures += runWorldTests(w, *b, v, shotsDir); });
    run("wire-parity", [&] { c.failures += runWireParityTests(); });
    run("window", [&] { c.failures += runPlayTests(w, *b, v, shotsDir); });
    run("layout", [&] { c.failures += runLayoutTests(w, *b, v, shotsDir); });
    run("laboratory", [&] { c.failures += runLabTests(w, *b, v, shotsDir); });
    run("plots", [&] { c.failures += runPlotTests(w, *b, v, shotsDir); });
    if (!shotsDir.isEmpty()) run("shots", [&] {
        c.check(runGizmoShots(w, shotsDir) == 0, "screenshots of the gizmo");
        sceneShots(w, *b, v, shotsDir);
    });
    std::printf(c.failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", c.failures);
    return c.failures;
}
