// The world's physics through the same controls a contributor sees: discovery, search, persistence,
// undo, a precise gravity vector kept, and the authored settings delivered to the running world (the
// physics engine's settings and gravity as the scene plays them).
#include "SelfTestSupport.h"
#include "WorldPanel.h"
#include "InspectorWidgets.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
using namespace ke;
using namespace selftest;

int runWorldTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    Checker c; b.newScene(); trigger(w, "worldPhysics"); pump(100);
    auto* panel = w.findChild<WorldPanel*>(); auto* tabs = w.findChild<QTabWidget*>("inspectorTabs");
    c.check(panel && panel->isVisible() && tabs && tabs->currentIndex() == 2 && tabs->tabText(2) == "Мир",
            "Physics opens the world tab without Expert or a selected object");
    if (!panel) return c.failures;
    auto* g = panel->findChild<QDoubleSpinBox*>("gravityStrength");
    auto* steps = panel->findChild<QSpinBox*>("substeps");
    auto* hertz = panel->findChild<QDoubleSpinBox*>("contactHertz");
    auto* continuous = panel->findChild<QCheckBox*>("continuous");
    auto* sleeping = panel->findChild<QCheckBox*>("sleeping");
    c.check(g && steps && hertz && continuous && sleeping && g->isVisible() && steps->isVisible() && continuous->isVisible() &&
                std::fabs(g->value() - 9.81) < 1e-5 && steps->value() == PhysicsSettings{}.substeps,
            "gravity, substeps and continuous collisions are visible immediately, with their real defaults");
    if (!g || !steps || !hertz || !continuous || !sleeping) return c.failures;
    auto* search = panel->findChild<QLineEdit*>("physicsFilter"); search->setText("CCD"); pump(50);
    c.check(continuous->isVisible() && !g->isVisible() && !steps->isVisible(), "continuous collisions are searchable by their English name");
    search->setText("угловое"); pump(50);
    c.check(panel->findChild<QDoubleSpinBox*>("angularDamping")->isVisible(), "search reveals a matching field of a folded section");
    search->setText("zzzz-no-such-setting"); pump(50);
    c.check(panel->findChild<QLabel*>("physicsNoMatch")->isVisible(), "empty physics search explains how to recover");
    search->clear(); pump(50);
    panel->findChild<QPushButton*>("gravityPreset1")->click(); pump(100);
    c.check(std::fabs(b.graph().world.gravity.y + 1.62f) < 1e-6f, "Moon preset writes actual gravity to the scene graph");
    b.undoAction()->trigger(); pump(100);
    c.check(std::fabs(b.graph().world.gravity.y + 9.81f) < 1e-6f, "Ctrl+Z restores the world's gravity and its widget");
    steps->setValue(7); hertz->setValue(120); continuous->click(); sleeping->click(); pump(100);
    SceneGraph back; std::string error;
    c.check(back.load(b.graph().save(), error) && back.world.physics.substeps == 7 && back.world.physics.contactHertz == 120 &&
                !back.world.physics.continuous && !back.world.physics.sleeping,
            "substeps, contact stiffness and the engine's switches survive a kscene round trip");
    auto precise = panel->settings(); precise.gravity = {0.1234567f, -1.620034f, 0.9876543f};
    panel->setWorld(precise); steps->setValue(8); pump(100);
    c.check(length(b.graph().world.gravity - precise.gravity) == 0, "changing the substeps never rounds or rotates a precise gravity vector");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/world-physics.png");
    b.createActions()[0]->trigger();
    b.editEntity(last(b).id, [](Entity& e) { e.rigid.enabled = true; e.collider.enabled = true; e.position = Vector3(0.0f, 3.0f, 0.0f); });
    const Vector3 start = last(b).position;
    b.select(0); trigger(w, "worldPhysics"); b.play(); waitFrames(v, 20, 5000); b.pause(); pump(100);
    // The authored gravity in the running world: the cube falls from rest by g t^2 / 2 along the precise vector.
    Vector3 moved(1e9f);
    for (const auto& body : v->snapshot()->bodies)
        if (body.movable) moved = body.pos - start;
    const float t = float(v->snapshot()->frame) / 60.0f;
    const Vector3 expected = precise.gravity * (0.5f * t * t);
    std::printf("  after %.3f s the cube moved (%.4f, %.4f, %.4f) m; g t^2 / 2 = (%.4f, %.4f, %.4f)\n", double(t), double(moved.x),
                double(moved.y), double(moved.z), double(expected.x), double(expected.y), double(expected.z));
    c.check(b.mode() == SceneBuilder::Mode::Paused && length(moved - expected) < 0.02f * length(expected) + 1e-3f,
            "Play builds the running world from the authored settings: the cube falls along the precise gravity");
    c.check(!steps->isEnabled() && panel->findChild<QLabel*>("physicsEditStatus")->text().contains("Стоп"),
            "play and pause show read-only settings with a clear Stop instruction");
    b.stop(); pump(100); c.check(steps->isEnabled(), "Stop enables world edits again");
    panel->findChild<QPushButton*>("resetWorldPhysics")->click(); pump(100);
    c.check(b.graph().world.physics.substeps == PhysicsSettings{}.substeps && b.graph().world.physics.continuous &&
                std::fabs(b.graph().world.gravity.y + 9.81f) < 1e-6f, "reset restores the engine's defaults and Earth gravity");
    return c.failures;
}
