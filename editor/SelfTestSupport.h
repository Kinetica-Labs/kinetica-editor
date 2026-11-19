#pragma once
// What the self-test files share (see SelfTest.h): the checker that prints one line per check, and
// the hands of the test - wait while the window works, press a role button, trigger an action by
// name, send a mouse event to the viewport, make a shape or a light, read the brightness of the
// picture at a point of the world, wait for simulated frames.
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <functional>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <QToolButton>

#include <algorithm>
#include <cstdio>

namespace selftest {

struct Checker {
    int failures = 0;
    void check(bool ok, const char* what) {
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
        std::fflush(stdout);
        failures += ok ? 0 : 1;
    }
};

// Lets the window work for a while: events, the simulation's snapshots, painting.
inline void pump(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    } while (t.elapsed() < ms);
}

// Lets the window work until `done` holds, at most `ms`: a check waits for what it checks, not for a guessed time - a
// busy machine (a build beside it) is slower, not wrong.
inline bool waitUntil(const std::function<bool()>& done, int ms) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) pump(20);
    return done();
}

// Presses the role button with this label (the role bar's own button, as a mouse click would).
inline void clickRole(QMainWindow& w, RoleIcon role) {
    for (QToolButton* b : w.findChildren<QToolButton*>("roleButton"))
        if (b->text() == roleName(role)) b->click();
}

inline void trigger(QMainWindow& w, const char* action) {
    if (auto* a = w.findChild<QAction*>(action)) a->trigger();
}

inline const ke::Entity& last(const SceneBuilder& b) { return b.graph().entities.back(); }

inline bool same(const ke::Vector3& a, const ke::Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

inline void sendMouse(Viewport* v, QEvent::Type type, const QPointF& p, Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent e(type, p, v->mapToGlobal(p), button, buttons, modifiers);
    QCoreApplication::sendEvent(v, &e);
}

inline QPointF toPoint(const ke::Vector2& p) { return QPointF(p.x, p.y); }

// The whole window as it is on the screen: a widget grab draws the 3D view without its painted
// overlay (the caption, the navigation cube, the selection box), so the view's own frame is laid over it.
// A drag of the gizmo's X arrow from 60 % of its length by `metres` along X, with these keys held
// from the press to the release (Shift: a clone; Ctrl: steps of 0.1 m, so the distance is exact).
inline void dragArrowX(Viewport* v, float metres, Qt::KeyboardModifiers keys) {
    const GizmoView gv = v->gizmoView();
    const Gizmo& g = v->gizmo();
    const ke::Vector3 grab = g.position() + g.axis(0) * (0.6f * g.armLength(gv));
    const QPointF from = toPoint(gv.project(grab)), to = toPoint(gv.project(grab + ke::Vector3(metres, 0, 0)));
    sendMouse(v, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, keys);
    for (int i = 1; i <= 12; ++i) sendMouse(v, QEvent::MouseMove, from + (to - from) * (i / 12.0), Qt::NoButton, Qt::LeftButton, keys);
    sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, keys);
    pump(60);
}

inline QImage windowShot(QMainWindow& w) {
    QImage shot = w.grab().toImage();
    if (auto* v = w.findChild<Viewport*>()) {
        QPainter p(&shot);
        p.drawImage(QRect(v->mapTo(&w, QPoint(0, 0)), v->size()), v->grabFramebuffer());
        for (QWidget* c : v->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) // the play banner, ▶ ⏸ ■
            if (c->isVisible()) c->render(&p, c->mapTo(&w, QPoint(0, 0)), QRegion(), QWidget::DrawChildren); // rounded: no square backdrop
    }
    return shot;
}

// The id of what the last "create" made (the builder selects it).
inline uint32_t made(SceneBuilder& b, QAction* action) {
    b.select(0);
    action->trigger();
    pump(60);
    return b.selectedId();
}

inline const ke::Light* lightOf(const SceneBuilder& b, uint32_t id) {
    for (const ke::Light& l : b.graph().lights)
        if (l.id == id) return &l;
    return nullptr;
}

// A light's turn that makes it shine along `dir` (it shines along its -y).
inline ke::Vector3 shining(const ke::Vector3& dir) {
    return ke::eulerDegrees(ke::Quaternion::fromTwoVectors(ke::Vector3(0, -1, 0), ke::normalize(dir)));
}

// The mean brightness (0..255) of the 7 x 7 pixels of the view's picture around a point in the world.
inline float brightnessAt(Viewport* v, const QImage& image, const ke::Vector3& p) {
    const ke::Vector2 s = v->gizmoView().project(p);
    const float k = float(image.width()) / float(std::max(1, v->width()));
    const int cx = int(s.x * k), cy = int(s.y * k);
    float sum = 0;
    int n = 0;
    for (int y = cy - 3; y <= cy + 3; ++y)
        for (int x = cx - 3; x <= cx + 3; ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
            const QColor c = image.pixelColor(x, y);
            sum += 0.299f * float(c.red()) + 0.587f * float(c.green()) + 0.114f * float(c.blue());
            ++n;
        }
    return n ? sum / float(n) : 0.0f;
}

// Waits until the viewport shows simulated frame `frames` (false: it did not come in time).
inline bool waitFrames(Viewport* v, uint64_t frames, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        pump(40);
        if (v->snapshot() && v->snapshot()->frame >= frames) return true;
    }
    return false;
}

} // namespace selftest

// The component tests and their screenshots (SelfTestComponents.cpp): the inspector's cards, the
// collider as a component of its own, the first minute. Return the number of failures.
int runComponentTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// The keys and the mouse of the edit mode (SelfTestControls.cpp).
int runControlTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// Shapes drawn with the mouse, as in 3ds Max: footprint, height, click (SelfTestCreate.cpp).
int runCreateTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// Many objects at once: Shift-clone, instances, arrays, groups, multi-edit (SelfTestMany.cpp).
int runManyTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// Lights and cameras: made, picked, moved, lighting a cube, looked through (SelfTestLights.cpp).
int runLightTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
int runSequencerTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir); // SelfTestSequencer.cpp
int runPluginTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);    // SelfTestPlugins.cpp
// The play-mode pass: the top bar's widths, the same look in play, the banner, K, Ctrl+K, the
// orbit pivot, the blank-window check (SelfTestPlay.cpp).
int runPlayTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// The layout buttons of the top bar, as in VS Code (SelfTestLayout.cpp).
int runLayoutTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// Shadows by the checkbox: a spotlight's and a lamp's shadow on the floor, on and off, the budget,
// the checkbox through undo and a saved file (SelfTestShadows.cpp).
int runShadowTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// The Laboratory: F8, the layers and presets, the card of a clicked contact or body, the timeline,
// the profiler (SelfTestLab.cpp).
int runLabTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// The plots: the default track, the picker, the three arrangements, the colours, the Laboratory's
// cursor both ways, a body's own channels, a long history painted quickly (SelfTestPlots.cpp).
int runPlotTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);

// Textures: a picture dropped on an object, saved and read back (SelfTestTextures.cpp).
int runTextureTests(QMainWindow&, SceneBuilder&, Viewport*, const QString&);

// The mesh shader's texture paths on this GL (SelfTestShaders.cpp) and the bump of a height map (SelfTestBump.cpp).
int runShaderTests(Viewport*);
int runBumpTests(QMainWindow&, SceneBuilder&, Viewport*, const QString&);
// The «Мир» tab: the world's settings through the panel, undo and the scene file (SelfTestWorld.cpp).
int runWorldTests(QMainWindow&, SceneBuilder&, Viewport*, const QString&);

// The IK goal dragged by its gizmo while the scene plays (SelfTestGoal.cpp).
int runGoalTests(QMainWindow&, SceneBuilder&, Viewport*, const QString&);
// The journal as a way back: a session restored to the bit, to a line, a crash found (SelfTestJournal.cpp).
int runJournalTests(QMainWindow&, SceneBuilder&, Viewport*, const QString&);

// Research: exact histograms, body selection and the simulation's recorded measurements.
int runResearchTests(QMainWindow&, SceneBuilder&, Viewport*, const QString&);
