// The component tests of the self-test (see SelfTest.h): the "Объект" tab shows only what an object
// has, "Твёрдое тело" comes with a collider, a collider alone is a fixed obstacle, the collider never
// changes the geometry, three clicks make a cube fall, and the cards of a joint, a sensor, a battery and a driver edit
// them. Each check prints one line.
#include "SelfTestSupport.h"
#include "scene/Assembly.h"
#include "scene/SensorSpec.h"
#include "sim/RobotWorld.h"

#include "ColliderGuides.h"
#include "InspectorWidgets.h"
#include "EditorOps.h"
#include "MainWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>

#include <algorithm>
#include <cmath>

using namespace ke;
using namespace selftest;

namespace {

// The component cards the "Объект" tab shows now, by title.
QStringList visibleCards(const SceneBuilder& b) {
    QStringList titles;
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>())
        if (card->isVisibleTo(b.inspectorPanel())) titles << card->title();
    return titles;
}

ComponentCard* cardTitled(const SceneBuilder& b, const QString& title) {
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>())
        if (card->title() == title) return card;
    return nullptr;
}

// Presses the collider kind button (Авто, Коробка, Сфера, Капсула, Выпуклая оболочка, Точно).
void clickColliderKind(const SceneBuilder& b, ColliderKind kind) {
    const QList<QToolButton*> buttons = b.inspectorPanel()->findChildren<QToolButton*>("colliderButton");
    if (int(kind) < buttons.size()) buttons[int(kind)]->click();
}

// The window with an open popup menu drawn where it stands (a popup is a window of its own).
QImage windowWithMenu(QMainWindow& w, QMenu* menu) {
    QImage shot = windowShot(w);
    QPainter p(&shot);
    p.drawPixmap(w.mapFromGlobal(menu->pos()), menu->grab());
    return shot;
}

void testBareObject(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    b.newScene();
    b.createActions()[0]->trigger(); // Куб
    b.frameSelected();
    pump(300);
    auto* tabs = w.findChild<QTabWidget*>("inspectorTabs");
    c.check(tabs && tabs->currentIndex() == 1 && tabs->tabText(1) == "Объект", "a new cube is selected and the panel turns to «Объект»");
    const QStringList cards = visibleCards(b);
    std::printf("  bare cube: %lld component card(s)\n", static_cast<long long>(cards.size()));
    c.check(cards.isEmpty() && entityIsGeometryOnly(last(b)), "a bare cube shows «Объект» and «Геометрия» only: no component cards");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/object-tab-bare-cube.png");
}

void testRigidBringsCollider(Checker& c, QMainWindow& w, SceneBuilder& b) {
    clickRole(w, RoleIcon::Rigid);
    pump(100);
    const QStringList cards = visibleCards(b);
    std::printf("  after «Твёрдое»: cards %s\n", qPrintable(cards.join(", ")));
    c.check(cards.size() == 2 && cards.contains("Твёрдое тело") && cards.contains("Коллайдер") && last(b).collider.enabled &&
                last(b).collider.kind == ColliderKind::Auto,
            "+Твёрдое: two cards, the rigid body and an automatic collider");
    ComponentCard* collider = cardTitled(b, "Коллайдер");
    if (auto* remove = collider ? collider->findChild<QToolButton*>("cardRemove") : nullptr) remove->click();
    pump(50);
    ComponentCard* rigid = cardTitled(b, "Твёрдое тело");
    InlineBanner* banner = rigid ? rigid->findChild<InlineBanner*>() : nullptr;
    const QString warning = banner ? banner->message() : QString();
    std::printf("  collider removed; the rigid card says: %s\n", qPrintable(warning));
    c.check(!last(b).collider.enabled && last(b).rigid.enabled && warning.contains("провалится"),
            "✕ on the collider: the rigid card warns that the body falls through everything");
    if (auto* fix = banner ? banner->findChild<QPushButton*>("bannerButton") : nullptr) fix->click();
    pump(50);
    c.check(last(b).collider.enabled && banner && banner->message().isEmpty(), "[Добавить коллайдер] puts the collider back");
}

void testAddComponentMenu(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    auto* menu = b.inspectorPanel()->findChild<QMenu*>("addComponentMenu");
    auto* button = b.inspectorPanel()->findChild<QPushButton*>("addComponent");
    if (!menu || !button) return c.check(false, "the «+ Добавить компонент» button and its menu exist");
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>()) card->setOpen(false); // the menu fits below
    for (QWidget* p = button->parentWidget(); p; p = p->parentWidget()) // scrolled into view, as a person would
        if (auto* scroll = qobject_cast<QScrollArea*>(p)) scroll->ensureWidgetVisible(button);
    pump(50);
    emit menu->aboutToShow(); // filled, so its height is known: it opens upwards, inside the window
    menu->popup(button->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
    pump(250);
    QStringList items;
    for (QAction* a : menu->actions())
        if (!a->isSeparator()) items << a->text();
    std::printf("  «+ Добавить компонент» offers: %s\n", qPrintable(items.join(", ")));
    if (!dir.isEmpty()) windowWithMenu(w, menu).save(dir + "/add-component-menu.png");
    QAction* imu = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == "IMU") imu = a;
    menu->hide();
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>()) card->setOpen(true);
    c.check(!items.contains("Твёрдое тело") && !items.contains("Коллайдер") && imu, "the menu offers only what the object does not have");
    const uint32_t cube = last(b).id;
    const size_t sensors = b.graph().sensors.size();
    if (imu) imu->trigger();
    pump(50);
    const bool put = b.graph().sensors.size() == sensors + 1 && b.graph().sensors.back().kind == SensorKind::Imu &&
                     b.graph().sensors.back().parent == cube;
    c.check(put, "choosing «IMU» puts the sensor on the cube");
    b.undoAction()->trigger();
    c.check(b.graph().sensors.size() == sensors, "one undo takes the sensor away");
}

// A cube with a collider only hangs in the air as a fixed obstacle; a rigid ball dropped on it
// bounces off, and the cube does not move.
void testStaticCollider(Checker& c, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.createActions()[0]->trigger();
    const uint32_t cube = last(b).id;
    b.editEntity(cube, [](Entity& e) {
        e.size = Vector3(0.6f, 0.2f, 0.6f);
        e.position = Vector3(0.0f, 0.5f, 0.0f);
        e.collider.enabled = true;
    });
    b.createActions()[1]->trigger(); // Сфера
    const uint32_t ball = last(b).id;
    b.editEntity(ball, [](Entity& e) {
        e.size = Vector3(0.2f);
        e.position = Vector3(0.05f, 1.4f, 0.0f);
        e.rigid.enabled = true;
        e.rigid.restitution = 0.8f;
        e.collider.enabled = true;
    });
    b.play();
    float lowest = 1e9f, riseAfterLowest = 0.0f, cubeDrift = 0.0f;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 20000 && (!v->snapshot() || v->snapshot()->frame < 120)) {
        pump(15);
        const auto& s = *v->snapshot();
        const int cb = b.bodyOfEntity(cube), bb = b.bodyOfEntity(ball);
        if (cb < 0 || bb < 0 || size_t(std::max(cb, bb)) >= s.bodies.size()) continue;
        cubeDrift = std::max(cubeDrift, length(s.bodies[size_t(cb)].pos - Vector3(0.0f, 0.5f, 0.0f)));
        const float y = s.bodies[size_t(bb)].pos.y;
        if (y < lowest) lowest = y, riseAfterLowest = 0.0f;
        riseAfterLowest = std::max(riseAfterLowest, y - lowest);
    }
    std::printf("  ball on a collider-only cube: lowest y = %.3f m (cube top 0.6 + radius 0.1), bounced up %.3f m; cube moved %.6f m\n",
                double(lowest), double(riseAfterLowest), double(cubeDrift));
    c.check(cubeDrift < 1e-5f && lowest > 0.66f && lowest < 1.0f, "a collider alone is a fixed obstacle: the ball stops on it, the cube stays");
    c.check(riseAfterLowest > 0.02f, "the ball bounces off the collider-only cube");
    b.stop();
    pump(200);
}

// The sphere's mesh: every vertex at the same distance from the centre (within 5 %).
bool looksLikeSphere(const TriMesh& m) {
    if (m.positions.empty()) return false;
    Vector3 centre(0.0f);
    for (const Vector3& p : m.positions) centre += p;
    centre = centre / float(m.positions.size());
    float lo = 1e9f, hi = 0.0f;
    for (const Vector3& p : m.positions) {
        lo = std::min(lo, length(p - centre));
        hi = std::max(hi, length(p - centre));
    }
    return hi > 0.0f && (hi - lo) / hi < 0.05f;
}

// A sphere with a rigid body and a capsule collider: the geometry stays a sphere, the green lines
// are a capsule, the simulated body collides as a capsule and is drawn as the sphere.
void testSphereCapsule(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    b.newScene();
    b.createActions()[1]->trigger(); // Сфера
    clickRole(w, RoleIcon::Rigid);
    clickColliderKind(b, ColliderKind::Capsule);
    v->setFocusBox(AABB(last(b).position - Vector3(0.6f, 0.9f, 0.6f), last(b).position + Vector3(0.6f, 0.2f, 0.6f)));
    v->frameScene(); // the ball and the floor it lands on
    pump(300);
    const Entity e = last(b);
    const std::vector<ShapeDesc> shapes = colliderShapes(e, entityLocalMesh(e, b.graph().baseDirectory));
    const bool capsule = shapes.size() == 1 && shapes[0].kind == ShapeDesc::Kind::Capsule;
    const size_t expected = capsule ? capsuleGuide(float(shapes[0].radius), float(shapes[0].halfHeight)).size() : 0;
    const size_t drawn = v->colliderGuides().size() / 4;
    std::printf("  sphere + Твёрдое + Капсула: shape %d, collider %d, guide points %zu (a capsule has %zu)\n", int(e.shape),
                shapes.empty() ? -1 : int(shapes[0].kind), drawn, expected);
    c.check(e.shape == ShapeKind::Sphere && e.collider.kind == ColliderKind::Capsule && expected > 0 && drawn == expected,
            "the capsule button: the geometry stays a sphere, the green wireframe is a capsule");
    // The user's case: a capsule taller than the ball (fit off, typed size) - a barrel around a sphere.
    b.editEntity(e.id, [](Entity& x) {
        x.collider.fitToGeometry = false;
        x.collider.size = Vector3(0.22f, 0.5f, 0.22f);
    });
    pump(300);
    c.check(last(b).shape == ShapeKind::Sphere && v->colliderGuides().size() / 4 == expected,
            "a typed capsule size changes the wireframe only: the geometry stays a sphere");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/collider-sphere-capsule-edit.png");
    b.play();
    waitFrames(v, 40, 15000);
    const int body = b.bodyOfEntity(e.id);
    const auto& s = *v->snapshot();
    int drawnBody = -1;
    for (size_t i = 0; i < s.bodies.size(); ++i)
        if (s.bodies[i].object == e.id) drawnBody = int(i);
    const std::vector<ShapeDesc> typed = colliderShapes(last(b), entityLocalMesh(last(b), b.graph().baseDirectory));
    const bool ok = body >= 0 && drawnBody >= 0 && s.bodies[size_t(drawnBody)].body >= 0 && typed.size() == 1 &&
                    typed[0].kind == ShapeDesc::Kind::Capsule && s.bodies[size_t(drawnBody)].mesh &&
                    looksLikeSphere(*s.bodies[size_t(drawnBody)].mesh) && v->colliderGuides().size() / 4 == expected;
    c.check(ok && !v->colliderGuides().empty(), "play: the body collides as a capsule, is drawn as the sphere, the wireframe follows it");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/collider-sphere-capsule-play.png");
    b.stop();
    pump(200);
}

// The cube of the coordinator's picture: rigid, with a capsule collider standing in it.
void cubeCapsuleShot(QMainWindow& w, SceneBuilder& b, const QString& dir) {
    b.newScene();
    b.createActions()[0]->trigger();
    b.editEntity(last(b).id, [](Entity& e) { e.size = Vector3(0.4f, 0.6f, 0.4f); });
    clickRole(w, RoleIcon::Rigid);
    clickColliderKind(b, ColliderKind::Capsule);
    b.frameSelected();
    pump(400);
    windowShot(w).save(dir + "/object-tab-cube-rigid-capsule.png");
}

// The first minute: Куб, the tile «Твёрдое», ▶ - three clicks and the cube falls onto the floor.
void testThreeClicks(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    int clicks = 0;
    b.createActions()[0]->trigger(), ++clicks;
    const float start = last(b).position.y;
    clickRole(w, RoleIcon::Rigid), ++clicks;
    if (auto* play = w.findChild<QAction*>("actionPlay")) play->trigger(), ++clicks;
    waitFrames(v, 90, 15000);
    float y = 1e9f;
    const int body = b.bodyOfEntity(last(b).id);
    if (body >= 0 && size_t(body) < v->snapshot()->bodies.size()) y = v->snapshot()->bodies[size_t(body)].pos.y;
    std::printf("  %d clicks: the cube went from y = %.2f m to y = %.3f m\n", clicks, double(start), double(y));
    c.check(clicks == 3 && y < 0.2f, "three clicks (Куб, Твёрдое, ▶): the cube falls and lands on the floor");
    b.stop();
    pump(200);
}

// The very first start: the welcome scene and the bubble that leads through Куб, Твёрдое, ▶. The
// user's own "seen it" flag is put back afterwards.
void testFirstMinute(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    auto* mw = dynamic_cast<MainWindow*>(&w);
    if (!mw) return c.check(false, "the window is the editor's MainWindow");
    const QVariant seen = QSettings().value("hints/firstMinuteDone");
    mw->setForceFirstStart(true);
    mw->startBuilder(true);
    pump(600);
    const bool welcome = std::any_of(b.graph().entities.begin(), b.graph().entities.end(), [](const Entity& e) { return e.name == "Горка"; });
    const QString first = mw->firstMinuteText();
    std::printf("  first start: %zu objects; the bubble says: %s\n", b.graph().entities.size(), qPrintable(first));
    c.check(welcome && first.contains("Куб"), "first start: the welcome scene and a bubble pointing at «Куб»");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/first-start.png");
    b.createActions()[0]->trigger();
    pump(150);
    const QString second = mw->firstMinuteText();
    clickRole(w, RoleIcon::Rigid);
    pump(150);
    const QString third = mw->firstMinuteText();
    if (!dir.isEmpty()) windowShot(w).save(dir + "/first-start-step3.png");
    if (auto* play = w.findChild<QAction*>("actionPlay")) play->trigger();
    pump(150);
    c.check(second.contains("Твёрдое") && third.contains("Пуск") && mw->firstMinuteText().isEmpty() &&
                QSettings().value("hints/firstMinuteDone").toBool(),
            "the bubble moves to «Твёрдое», then to ▶, and is gone after ▶ for good");
    waitFrames(v, 60, 15000);
    b.stop();
    pump(200);
    mw->setForceFirstStart(false);
    if (seen.isValid()) QSettings().setValue("hints/firstMinuteDone", seen);
    else QSettings().remove("hints/firstMinuteDone");
}

// The card «Сочленение» of the UR5e's elbow (a servo hinge): its numbers in degrees and its drive's units; another
// drive picked starts from that drive's usual numbers and shows only its own fields; a target typed in degrees is
// radians in the scene; a limit ticked shows its two ends; each edit one undo step - a number typed right after an undo
// its own, not joined to the step before (it took the scene's opening with it).
void testJointCard(Checker& c, SceneBuilder& b) {
    b.stop(); // the first minute ended playing
    pump(60);
    QString error;
    if (!b.openFile(QString(KE_EXAMPLES) + "/ur5e.kscene", error) || !findJoint(b.graph(), 7))
        return c.check(false, "examples/ur5e.kscene opens with its elbow");
    b.select(7);
    pump(60);
    ComponentCard* card = cardTitled(b, "Сочленение");
    const QList<QDoubleSpinBox*> spins = card ? card->findChildren<QDoubleSpinBox*>() : QList<QDoubleSpinBox*>();
    auto* drive = card ? card->findChild<QComboBox*>() : nullptr;
    auto* limit = card ? card->findChild<QCheckBox*>() : nullptr;
    const auto joint = [&b]() -> const JointObject& { return *findJoint(b.graph(), 7); };
    const auto shows = [card](QWidget* w) { return w && w->isVisibleTo(card); };
    // The spin boxes in the card's order: lower, upper, target, speed, stiffness, damping, force, voltage, winding, K.
    const bool shape = card && card->isVisibleTo(b.inspectorPanel()) && spins.size() == 10 && drive && limit;
    const bool servo = shape && drive->currentIndex() == 1 && spins[4]->value() == 8000 && spins[4]->suffix() == " Н·м/рад" &&
                       spins[2]->suffix() == " °" && shows(spins[2]) && shows(spins[4]) && !shows(spins[7]) && !shows(spins[0]);
    if (!shape) return c.check(false, "the joint's card shows its limit, its drive and ten numbers");
    drive->setCurrentIndex(3);
    pump(30);
    const QList<QDoubleSpinBox*> after = card->findChildren<QDoubleSpinBox*>();
    const JointDrive& dc = joint().dof[DofRx].drive;
    const bool motor = dc.enabled && dc.stiffness == 0 && std::fabs(dc.constant - 0.05_r) < 1e-6_r && shows(after[7]) && !shows(after[4]);
    b.undoAction()->trigger();
    pump(30);
    const bool undone = joint().dof[DofRx].drive.stiffness == 8000 && drive->currentIndex() == 1;
    card->findChildren<QDoubleSpinBox*>()[2]->setValue(30);
    pump(30);
    const Real target = joint().dof[DofRx].drive.target;
    limit->setChecked(true);
    pump(30);
    const QList<QDoubleSpinBox*> limited = card->findChildren<QDoubleSpinBox*>();
    const bool ends = joint().dof[DofRx].motion == JointMotion::Limited && shows(limited[0]) && shows(limited[1]);
    b.undoAction()->trigger();
    pump(30);
    const bool unticked = joint().dof[DofRx].motion == JointMotion::Free && std::fabs(joint().dof[DofRx].drive.target - target) < 1e-6_r;
    b.undoAction()->trigger();
    pump(30);
    const bool back = unticked && findJoint(b.graph(), 7) && joint().dof[DofRx].drive.target == 0;
    std::printf("  the elbow's card: a servo %s; a DC motor picked %s, undone %s; 30 deg typed: %.4f rad; a limit %s; two undos back %s\n",
                servo ? "yes" : "no", motor ? "yes" : "no", undone ? "yes" : "no", double(target), ends ? "shows its ends" : "no", back ? "yes" : "no");
    c.check(servo && motor && undone && std::fabs(target - 0.5236_r) < 1e-3_r && ends && back,
            "a joint's card edits its drive in degrees and its own units, a drive picked shows only its fields, each edit one undo");
}

// The card «Датчик»: an encoder put on the UR5e's shoulder (on its joint: nothing to pick it «относительно») shows
// its lines, its axis and its count-zero reference, its resolution from its lines; an IMU on a cube its datasheet's
// noise and the seed of it. Each edit is the sensor's line in the scene; a tick undone comes back.
void testSensorCard(Checker& c, SceneBuilder& b) {
    QWidget* panel = b.inspectorPanel();
    const auto field = [panel](const char* name) { return panel->findChild<QWidget*>(name); };
    const auto shows = [&](const char* name) { return field(name) && field(name)->isVisibleTo(panel); };
    const auto number = [&](const char* name, double v) {
        if (auto* s = qobject_cast<QSpinBox*>(field(name))) s->setValue(int(v)), emit s->editingFinished();
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(field(name))) d->setValue(v), emit d->editingFinished();
        pump(30);
    };
    const auto tick = [&](const char* name, bool on) { if (auto* box = qobject_cast<QCheckBox*>(field(name))) box->setChecked(on); pump(30); };
    const auto says = [panel](const QString& text) {
        for (QLabel* l : panel->findChildren<QLabel*>()) if (l->isVisibleTo(panel) && l->text().contains(text)) return true;
        return false;
    };
    const auto sensor = [&b]() -> const SensorObject* {
        for (const SensorObject& s : b.graph().sensors) if (s.id == b.selectedId()) return &s;
        return nullptr;
    };
    const auto put = [&b](uint32_t on, SensorKind kind) {
        b.select(on);
        b.updateSensorActions();
        b.sensorActions()[size_t(kind)]->trigger();
        pump(60);
    };
    put(4, SensorKind::Encoder);
    const bool encoder = sensor() && sensor()->kind == SensorKind::Encoder && shows("sensorLines") && shows("sensorDof") && !shows("sensorAgainst") &&
                         !shows("sensorNoise") && shows("encoderReferenced");
    number("sensorLines", 1000);
    const bool lines = sensor() && sensor()->lines == 1000 && says("4000 отсчётов/оборот");
    tick("encoderReferenced", true);
    number("encoderReference", 0.5);
    const bool zero = sensor() && sensor()->hasEncoderReference && std::fabs(sensor()->encoderReference - 0.5_r) < 1e-6_r;
    put(23, SensorKind::Imu);
    const bool imu = sensor() && sensor()->kind == SensorKind::Imu && shows("sensorNoise") && shows("sensorSeed") && !shows("sensorLines") &&
                     qobject_cast<QCheckBox*>(field("sensorNoise"))->text() == "Шум и смещения по паспорту";
    // Its datasheet: the table's first model, its numbers in the card; one typed over them is the sensor's, the model said changed.
    auto* model = qobject_cast<QComboBox*>(field("sensorModel"));
    const std::vector<const SensorModel*> imus = sensorCatalog().models(SensorKind::Imu);
    const bool sheet = sensor() && !imus.empty() && sensor()->model == imus.front()->name && shows("sensorModel") && shows("spec_gyro_noise_dps") &&
                       !shows("spec_min_m") && model && model->currentText() == QString::fromStdString(imus.front()->name);
    number("spec_rate_hz", 200);
    const bool typed = sensor() && specValue(*sensor(), "rate_hz") == 200 && model && model->currentText().contains("изменены");
    tick("sensorNoise", false);
    const bool quiet = sensor() && !sensor()->noise;
    b.undoAction()->trigger();
    pump(30);
    const bool noisy = sensor() && sensor()->noise;
    number("sensorSeed", 7);
    const bool seed = sensor() && sensor()->seed == 7;
    std::printf("  the sensor card: an encoder %s, 1000 lines %s, its zero at 0.5 %s; an IMU %s, its model's datasheet %s, 200 Hz typed over it %s; "
                "quiet %s, undone %s, seed 7 %s\n", encoder ? "yes" : "no", lines ? "yes" : "no", zero ? "yes" : "no", imu ? "yes" : "no", sheet ? "yes" : "no",
                typed ? "yes" : "no", quiet ? "yes" : "no", noisy ? "yes" : "no", seed ? "yes" : "no");
    c.check(encoder && lines && zero && imu && quiet && noisy && seed, "a sensor's card shows its kind's fields and writes each edit into its line");
    c.check(sheet && typed, "an IMU's card: its model's datasheet from the table, a number typed over it the sensor's, the model said changed");
}

// The cards «Батарея» and «Драйвер мотора»: the UR5e's elbow made a DC motor and fed (assembly_power: a driver and a
// 3S pack); the pack's cells and charge typed, the driver's duty slid, its stop and its pack picked - each into the
// scene, a pick undone comes back.
void testPowerCards(Checker& c, QMainWindow& w, SceneBuilder& b) {
    EditorOps* ops = dynamic_cast<MainWindow&>(w).operators();
    const auto driverIn = [](const SceneGraph& g, uint32_t id) -> const DriverObject* {
        for (const DriverObject& d : g.drivers) if (d.id == id) return &d;
        return nullptr;
    };
    const auto batteryIn = [](const SceneGraph& g, uint32_t id) -> const BatteryObject* {
        for (const BatteryObject& p : g.batteries) if (p.id == id) return &p;
        return nullptr;
    };
    ops->runNow("assembly_drive", {{"joint", 9}, {"kind", "dcmotor"}, {"voltage", 12}, {"resistance", 1}, {"constant", 0.05}});
    const QJsonObject power = ops->runNow("assembly_power", {{"joint", 9}});
    const uint32_t driver = uint32_t(power.value("id").toInt());
    const DriverObject* d = driverIn(b.graph(), driver);
    if (!d) return c.check(false, "assembly_power feeds the elbow's DC motor through a driver");
    const uint32_t pack = d->battery;
    QWidget* panel = b.inspectorPanel();
    const auto field = [panel](const char* name) { return panel->findChild<QWidget*>(name); };
    const auto number = [&](const char* name, double v) {
        if (auto* s = qobject_cast<QSpinBox*>(field(name))) s->setValue(int(v));
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(field(name))) d->setValue(v);
        else if (auto* s = qobject_cast<QSlider*>(field(name))) s->setValue(int(v));
        pump(30);
    };
    const auto pick = [&](const char* name, int index) { if (auto* box = qobject_cast<QComboBox*>(field(name))) box->setCurrentIndex(index); pump(30); };
    b.select(pack);
    pump(30);
    number("batterySeries", 4);
    number("batteryCharge", 50);
    const BatteryObject* battery = batteryIn(b.graph(), pack);
    const bool cells = battery && battery->series == 4 && std::fabs(battery->soc - 0.5_r) < 1e-6_r;
    b.select(driver);
    pump(30);
    number("driverDuty", 50);
    pick("driverStop", 1);
    const bool duty = driverIn(b.graph(), driver) && std::fabs(driverIn(b.graph(), driver)->duty - 0.5_r) < 1e-6_r && driverIn(b.graph(), driver)->brake;
    pick("driverBattery", 0);
    const bool unfed = driverIn(b.graph(), driver) && driverIn(b.graph(), driver)->battery == 0;
    b.undoAction()->trigger();
    pump(30);
    const bool fed = driverIn(b.graph(), driver) && driverIn(b.graph(), driver)->battery == pack;
    std::printf("  the power cards: 4S at 50 %% %s; the driver at 50 %% braking %s, unfed %s, its pack back on undo %s\n", cells ? "yes" : "no",
                duty ? "yes" : "no", unfed ? "yes" : "no", fed ? "yes" : "no");
    c.check(cells && duty && unfed && fed, "a battery's and a driver's cards write their numbers, slider and picks into the scene");
}

} // namespace

int runComponentTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    if (!shotsDir.isEmpty()) QDir().mkpath(shotsDir);
    testBareObject(c, w, b, shotsDir);
    testRigidBringsCollider(c, w, b);
    testAddComponentMenu(c, w, b, shotsDir);
    testStaticCollider(c, b, v);
    testSphereCapsule(c, w, b, v, shotsDir);
    testThreeClicks(c, w, b, v);
    testFirstMinute(c, w, b, v, shotsDir);
    testJointCard(c, b);
    testSensorCard(c, b);
    testPowerCards(c, w, b);
    if (!shotsDir.isEmpty()) cubeCapsuleShot(w, b, shotsDir);
    return c.failures;
}
