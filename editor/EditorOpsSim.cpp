// Time and the view in the API (EditorOps.h): play, pause, stop, N frames stepped, the live state of the
// bodies and joints and the channels the plots draw, read on the simulation's thread (ke::Simulation); a
// screenshot, framing, an agent's note in the console, and every button of the window by its text
// (FreeCAD's runCommand: what the UI can do, a script can). The live state and the channels are the
// privileged loop's truth of the physics engine (the observability contract): for checks and plots, never
// a robot's input - a robot reads its joints through encoders.
#include "EditorOps.h"
#include "EditorRuntime.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "UiWindowSpace.h"
#include "Viewport.h"
#include "scene/Assembly.h"
#include "scene/SceneGraph.h"
#include "scene/SensorSpec.h"
#include "sim/Simulation.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QTabBar>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <map>

using namespace ke;

namespace {

QJsonObject activateVisibleTab(QWidget* window, const QString& text) {
    QTabBar* selected = nullptr;
    int index = -1;
    for (QTabBar* bar : window->findChildren<QTabBar*>())
        if (bar->isVisible() && bar->isEnabled())
            for (int k = 0; k < bar->count(); ++k)
                if (bar->isTabEnabled(k) && bar->isTabVisible(k) && bar->tabText(k).remove('&') == text) {
                    if (selected) return EditorOps::error("more than one visible tab has text '" + text + "'");
                    selected = bar, index = k;
                }
    if (!selected) return EditorOps::error("no enabled action, button or tab '" + text + "'");
    selected->setCurrentIndex(index);
    return EditorOps::ok();
}

QJsonArray json(const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; }

const char* modeName(SceneBuilder::Mode m) {
    return m == SceneBuilder::Mode::Edit ? "edit" : m == SceneBuilder::Mode::Playing ? "playing" : "paused";
}

// What the running world said about a thing it could not build («Плечо»: ...), else "".
QString buildNote(const RobotWorld& robot, const std::string& name) {
    for (const std::string& n : robot.notes())
        if (n.rfind("«" + name + "»", 0) == 0) return QString::fromStdString(n);
    return QString();
}

// One joint now: its kind, its six coordinates (travel of frame a from frame b along b's x y z [m], turns about
// them [rad]), its main coordinate (a hinge's angle, a slider's travel), its drive's effort and, for a DC motor,
// its current and the power it takes.
QJsonObject jointState(const Simulation& s, const JointObject& j) {
    QJsonObject r{{"id", int(j.id)}, {"name", QString::fromStdString(j.name)}, {"kind", jointKindWord(j)}, {"built", false}};
    const RobotWorld& robot = *s.robot();
    Real x[6];
    if (!robot.jointCoordinates(*s.physics(), j.id, x)) return r.insert("problem", buildNote(robot, j.name)), r;
    QJsonArray coords;
    for (const Real c : x) coords.append(double(c));
    r.insert("built", true);
    r.insert("coordinates", coords);
    const int main = mainFreedom(j);
    if (main < 0) return r;
    r.insert(main >= DofRx ? "angle" : "travel", double(x[main]));
    const JointDrive& d = j.dof[main].drive;
    if (!d.enabled) return r;
    const double effort = double(robot.jointEffort(*s.physics(), j.id));
    r.insert("effort", effort);
    if (d.constant > 0) { // a DC motor's torque is K I
        const double current = effort / double(d.constant);
        r.insert("motors", QJsonArray{QJsonObject{{"dof", main}, {"current", current}, {"power", double(d.voltage) * current}}});
    }
    return r;
}

// A sensor's quantities by its kind's words (what Python's kinetica.Sensor reads): an IMU's accel and gyro, a wrench's
// force and torque, a sonar's distance, a touch pad's force and points, an effort's effort, an encoder's count and the
// coordinate it means (`reading`: with a known zero only).
QJsonObject sensorValues(SensorKind kind, const SensorValues& v, bool referenced) {
    const auto vec = [](const Vector3& x) { return QJsonArray{double(x.x), double(x.y), double(x.z)}; };
    switch (kind) {
    case SensorKind::Imu: return {{"accel", vec(v.a)}, {"gyro", vec(v.b)}};
    case SensorKind::Wrench: return {{"force", vec(v.a)}, {"torque", vec(v.b)}};
    case SensorKind::Sonar: return {{"distance", double(v.value)}};
    case SensorKind::Touch: return {{"force", double(v.value)}, {"points", double(v.count)}};
    case SensorKind::Effort: return {{"effort", double(v.value)}};
    case SensorKind::Encoder:
        return {{"count", double(v.count)}, {"relative", double(v.value)}, {"reading", referenced ? QJsonValue(double(v.value)) : QJsonValue()}};
    case SensorKind::Stereo: break;
    }
    return {};
}

// One sensor now: what the robot reads (its datasheet's device), and beside it the truth of its probe - the
// privileged loop's, for checks.
QJsonObject sensorState(const LiveSensor& x) {
    const SensorReading& r = x.reading();
    QJsonObject out = sensorValues(x.spec().kind, r, r.referenced);
    out.insert("id", int(x.spec().id));
    out.insert("kind", sensorKindKey(x.spec().kind));
    if (!x.spec().model.empty()) out.insert("model", QString::fromStdString(x.spec().model));
    out.insert("sample_ns", double(r.sampleNs));
    if (r.saturated) out.insert("saturated", true);
    out.insert("problem", QString::fromStdString(r.problem));
    QJsonObject truth = sensorValues(x.spec().kind, x.truth(), true);
    truth.remove("count"), truth.remove("relative"), truth.remove("points");
    out.insert("truth", truth);
    return out;
}

// The live state on the simulation's thread: the time, every body's pose and velocity, every joint's
// coordinates and drive, every sensor's reading (and its truth), which entities touch, and what the world could
// not build.
QJsonObject liveState(const Simulation& s) {
    QJsonObject out{{"ok", true}, {"time", double(s.time())}, {"frame", double(s.frame())}};
    const RobotWorld* robot = s.robot();
    const PhysicsWorld* physics = s.physics();
    if (!robot || !physics) return out;
    QJsonArray shapes, joints, contacts, notes;
    for (const Entity& e : robot->graph().entities) {
        const int b = robot->bodyOf(e.id);
        if (b < 0 || !physics->alive(b)) continue;
        const BodyState st = physics->state(b);
        QJsonObject shape{{"id", int(e.id)}, {"name", QString::fromStdString(e.name)}, {"position", json(st.position)},
                          {"rotation", json(eulerDegrees(st.rotation))}, {"velocity", json(st.velocity)}, {"spin", json(st.angularVelocity)}};
        if (!st.awake) shape.insert("asleep", true); // at rest: its velocity is 0 because it sleeps, not by chance
        if (st.mass <= 0) shape.insert("fixed", true);
        shapes.append(shape);
    }
    for (const JointObject& j : robot->graph().joints) joints.append(jointState(s, j));
    // The contact points of the last substep by the pair of entities they join (0: the floor), their normal
    // force (the impulse over that substep) and the deepest overlap.
    const double h = double(s.frameDt) / std::max(1, robot->graph().world.physics.substeps);
    struct Touch { int points = 0; double force = 0, depth = 0; };
    std::map<std::pair<uint32_t, uint32_t>, Touch> touches;
    for (const ContactPoint& c : physics->contacts()) {
        uint32_t a = robot->objectIdOfBody(c.a), b = robot->objectIdOfBody(c.b);
        if (a > b) std::swap(a, b);
        Touch& t = touches[{a, b}];
        ++t.points;
        t.force += double(c.normalImpulse) / h;
        t.depth = std::max(t.depth, double(c.depth));
    }
    for (const auto& [pair, t] : touches)
        contacts.append(QJsonObject{{"a", int(pair.first)}, {"b", int(pair.second)}, {"points", t.points}, {"force", t.force}, {"depth", t.depth}});
    for (const std::string& n : robot->notes()) notes.append(QString::fromStdString(n));
    out.insert("shapes", shapes);
    out.insert("joints", joints);
    QJsonArray sensors;
    for (const auto& x : robot->sensors()) sensors.append(sensorState(*x));
    out.insert("sensors", sensors);
    out.insert("contacts", contacts);
    if (!notes.isEmpty()) out.insert("notes", notes);
    return out;
}

// sim_measure's arguments as the channels to measure: their labels are the ids, as the plots' are names.
ChannelRequests channelRequests(const QJsonObject& a) {
    ChannelRequests r;
    r.scene = a.value("scene").toBool(false);
    for (const QJsonValue& v : a.value("objects").toArray()) r.objects.push_back({uint32_t(v.toInt()), std::to_string(v.toInt())});
    for (const QJsonValue& v : a.value("joints").toArray()) r.joints.push_back({uint32_t(v.toInt()), std::to_string(v.toInt())});
    for (const QJsonValue& v : a.value("goals").toArray()) r.goals.push_back({uint32_t(v.toInt()), std::to_string(v.toInt())});
    if (a.contains("group")) {
        GroupRef g{"group", {}};
        for (const QJsonValue& v : a.value("group").toArray()) g.members.push_back({uint32_t(v.toInt()), std::to_string(v.toInt())});
        r.groups.push_back(g);
    }
    return r;
}

QJsonObject measurementsJson(const std::vector<Measurement>& ms) {
    QJsonArray out;
    for (const Measurement& m : ms)
        out.append(QJsonObject{{"id", QString::fromStdString(m.info.id)}, {"value", m.value}, {"unit", QString::fromStdString(m.info.unit)},
                               {"name", QString::fromStdString(m.info.name)}});
    return QJsonObject{{"channels", out}};
}

} // namespace

void EditorOps::addSimOps() {
    auto mode = [this] { return ok({{"mode", modeName(builder_->mode())}}); };
    addSync("sim_play", "Run the scene (built from the graph)", "{}", [this, mode](const QJsonObject&) {
        if (runtime_->stopping()) return error("runtime is stopping");
        builder_->play();
        return mode();
    });
    addSync("sim_pause", "Pause it", "{}", [this, mode](const QJsonObject&) { builder_->pause(); return mode(); });
    addSync("sim_stop", "Stop: the scene as it was before play", "{}", [this, mode](const QJsonObject&) { builder_->stop(); return mode(); });
    // Paused (built from the graph first when editing), then exactly N frames on the simulation's
    // thread; the answer is the state after them.
    addStepOp();
    addStateOp();
    addImpulseOp();
    addChannelsOp();
}

// console_note: a line in the API console and nothing else - an agent's reasoning between its calls, so that
// whoever watches reads why it does what the lines under it do.
void EditorOps::addNoteOp() {
    addSync("console_note", "A line of an agent's reasoning in the API console, between its calls (changes nothing)", "{text: str}",
            [](const QJsonObject&) { return ok(); });
}

// The window as a person sees it: the window itself, then the application's other windows on it - dialogs, then open
// menus, then tooltips - painted where they stand (a dialog left out once hid that it held the keyboard). Drawn by the
// application, not read from the screen: the user's other programs on top are not in it (they were, and they are the
// user's), and a monitor gone to sleep does not blind it.
static QImage seenWindow(QWidget* window) {
    QImage image = window->grab().toImage();
    QPainter painter(&image);
    const QPoint corner = window->mapToGlobal(QPoint(0, 0));
    for (QWidget* w : uiWindowLayers(window)) painter.drawPixmap(uiWindowOrigin(w) - corner, w->grab());
    return image;
}

void EditorOps::addViewOps() {
    addSync("view_screenshot", "A PNG of the 3D view (window: the whole window, panels too; screen: the window as a person sees it, its open "
                               "menus and tooltips painted where they stand - drawn by the editor, never read from the screen, so no other "
                               "program's window is in it; "
                               "width: scaled to so many pixels across, smoothly; clean: without the text and the navigation cube over the view - "
                               "a picture for the documentation)",
            "{file: str, window?: bool, screen?: bool, width?: int, clean?: bool}",
            [this](const QJsonObject& a) {
        const QString file = a.value("file").toString();
        if (refresh_) refresh_();
        const bool clean = a.value("clean").toBool();
        QWidget* banner = clean ? window_->findChild<QWidget*>("apiBanner") : nullptr;
        const bool bannerShown = banner && banner->isVisible();
        if (clean) view_->setOverlayVisible(false);
        if (bannerShown) banner->hide();
        QImage image = a.value("screen").toBool() ? seenWindow(window_) : a.value("window").toBool() ? window_->grab().toImage() : view_->grabFramebuffer();
        if (clean) view_->setOverlayVisible(true);
        if (bannerShown) banner->show();
        if (const int width = a.value("width").toInt(); width > 0 && width < image.width())
            image = image.scaledToWidth(width, Qt::SmoothTransformation);
        if (file.isEmpty() || !image.save(file)) return error("cannot save '" + file + "'");
        QJsonObject answer{{"file", file}, {"width", image.width()}, {"height", image.height()}};
        return ok(answer);
    });
    addSync("view_frame", "Show the object (selected), or the whole scene without an id", "{id?: int}", [this](const QJsonObject& a) {
        if (a.contains("id")) builder_->select(uint32_t(a.value("id").toInt())), builder_->frameSelected();
        else builder_->frameAll();
        return ok();
    });
    addSync("view_joint_debug", "Joints drawn with both their frames and the gap between them (on / off)", "{on: bool}", [this](const QJsonObject& a) {
        view_->setJointDebug(a.value("on").toBool());
        return ok();
    });
    addSync("view_axis", "Look along an axis: 0..5 = +x -x +y -y +z -z (the navigation cube's balls)", "{axis: int}", [this](const QJsonObject& a) {
        view_->viewAlong(std::clamp(a.value("axis").toInt(), 0, 5));
        return ok();
    });
    addSync("view_hud", "The performance plate in the view's lower left corner: the frames a second in large figures coloured by "
            "smoothness, the last two seconds of frame times, one frame's paint and the physics' step, the scene's speed against the "
            "wall clock, the renderer (a GPU or the processor). on: show or hide it; answers whether it shows and its numbers",
            "{on?: bool}", [this](const QJsonObject& a) {
        if (a.contains("on")) view_->setShowHud(a.value("on").toBool());
        if (auto* act = window_->findChild<QAction*>("actionHud")) act->setChecked(view_->showHud()); // the menu says what the view shows
        return ok({{"on", view_->showHud()}, {"fps", view_->liveFps()}, {"paint_ms", double(view_->paintMs())},
                   {"realtime", view_->realtime()}, {"renderer", view_->renderer()}});
    });
    addSync("ui_actions", "Every button and menu item of the window: text, name, on/off", "{}", [this](const QJsonObject&) {
        QJsonArray out;
        for (const QAction* act : window_->findChildren<QAction*>())
            if (!act->text().isEmpty() && !act->isSeparator())
                out.append(QJsonObject{{"text", act->text().remove('&')}, {"name", act->objectName()}, {"enabled", act->isEnabled()},
                                       {"checked", act->isCheckable() ? QJsonValue(act->isChecked()) : QJsonValue()}});
        return ok({{"actions", out}});
    });
    addSync("ui_trigger", "Press a button or a menu item by its text or name (as ui_actions lists them), a plain panel button "
            "by its text or tooltip, or a visible tab by its text", "{text: str}", [this](const QJsonObject& a) {
        const QString want = a.value("text").toString();
        for (QAction* act : window_->findChildren<QAction*>())
            if (act->isEnabled() && (act->text().remove('&') == want || act->objectName() == want)) return act->trigger(), ok();
        for (QAbstractButton* b : window_->findChildren<QAbstractButton*>())
            if (b->isEnabled() && b->isVisible() && (b->text().remove('&') == want || b->objectName() == want || b->toolTip().startsWith(want)))
                return b->click(), ok();
        return activateVisibleTab(window_, want);
    });
}

// Bulk stepping is cancelled between frames by the runtime's independent atomic epoch.
void EditorOps::addStepOp() {
    add("sim_step", "Step N frames (1/60 s each) on pause and answer the live state, as sim_state gives it (frames 0: the scene built as play "
                    "builds it, its state at time 0)", "{frames?: int}", [this](const QJsonObject& a, const Reply& reply) {
        if (runtime_->stopping()) return reply(error("runtime is stopping"));
        if (builder_->editing() && !builder_->loadPaused()) return reply(error("the scene cannot be built"));
        if (builder_->mode() == SceneBuilder::Mode::Playing) builder_->pause();
        const int frames = std::clamp(a.value("frames").toInt(1), 0, 6000); // 0: built, not stepped
        const auto cancellation = runtime_->cancellationEpoch();
        const auto epoch = cancellation->load();
        ctrl_->post([this, frames, reply, cancellation, epoch](Simulation& s) {
            int completed = 0;
            for (; completed < frames && cancellation->load() == epoch; ++completed) s.stepFrame();
            // Answered from the next command: the frame of these steps is published by then.
            ctrl_->post([this, reply, completed, frames](Simulation& after) {
                QJsonObject state = liveState(after);
                state.insert("requested_frames", frames), state.insert("completed_frames", completed);
                state.insert("cancelled", completed != frames);
                QMetaObject::invokeMethod(this, [this, reply, state] {
                    if (refresh_) refresh_();
                    reply(state);
                }, Qt::QueuedConnection);
            });
        });
    });
}

void EditorOps::addStateOp() {
    add("sim_state", "The live state: time and frame, every body's pose (position of its frame, rotation: Euler degrees [x, y, z], the "
        "turn R = Ry Rx Rz - as the scene file), velocity and spin (asleep: at rest, put to sleep; fixed: a static body), every "
        "joint's coordinates, main angle or travel, its drive's effort (N m or N) and a DC motor's current and power, every sensor's reading "
        "as the robot reads it (its datasheet's device: an IMU's accel and gyro, a sonar's distance, an encoder's count, a wrench's force and "
        "torque, a pad's force) with its probe's truth beside it, which entities "
        "touch (contacts: a, b - 0 the floor -, points, normal force N, deepest overlap m), what the world could not build (notes); "
        "frame_timing: the last 120 frames' mean wall ms in posted commands, the step and the snapshot, and the window's painting - its "
        "frames a second, one frame's ms and the renderer, a GPU or llvmpipe (the scene runs slower than "
        "the wall clock when they pass frame_dt_ms). The physics engine's truth, for checks - not a robot's sensors", "{}",
        [this](const QJsonObject&, const Reply& reply) {
        if (builder_->mode() == SceneBuilder::Mode::Edit) return reply(ok({{"mode", "edit"}}));
        ctrl_->post([this, reply](Simulation& s) {
            QJsonObject state = liveState(s);
            QMetaObject::invokeMethod(this, [this, reply, state]() mutable {
                state.insert("mode", modeName(builder_->mode()));
                const auto t = ctrl_->timing();
                state.insert("frame_timing", QJsonObject{{"frames", t.frames}, {"frame_dt_ms", t.frameDtMs}, {"commands_ms", t.commandsMs},
                                                          {"step_ms", t.stepMs}, {"step_max_ms", t.stepMaxMs}, {"publish_ms", t.publishMs},
                                                          {"commands_per_frame", t.commandsPerFrame}, {"slowest_command_ms", t.slowestCommandMs},
                                                          {"slowest_command", QString::fromStdString(t.slowestCommand)},
                                                          {"render_fps", double(view_->renderFps())}, {"paint_ms", double(view_->paintMs())},
                                                          {"renderer", view_->renderer()}});
                reply(state);
            }, Qt::QueuedConnection);
        }, "sim_state");
    });
}

// sim_measure: the channels the plots draw (sim/Channels.h), measured now on the simulation's thread - the same
// numbers the plots record, read by an agent without opening them. Nothing is changed, and the plots' own requests
// (ke::Simulation::channels) stay as they are.
void EditorOps::addChannelsOp() {
    add("sim_measure", "The plots' channels measured now (the physics engine's truth, for checks): scene - energy of motion, rotation and "
                       "height, momentum, angular momentum, the deepest contact; objects - each body by entity id: mass, centre, height, "
                       "velocity, energy, spin, sleep, contact force; group - several bodies as one; joints - each freedom's coordinate, a "
                       "servo's target, the drive's effort; goals - an IK goal's miss (mm) and turn miss (°). Each channel: id "
                       "(\"<id>/<quantity>\"), value, unit, name",
        "{scene?: bool, objects?: [int], group?: [int], joints?: [int], goals?: [int]}", [this](const QJsonObject& a, const Reply& reply) {
        if (builder_->mode() == SceneBuilder::Mode::Edit) return reply(error("the scene is not running: play or step it first"));
        const ChannelRequests requests = channelRequests(a);
        if (requests.empty()) return reply(error("nothing asked: scene, objects, group, joints or goals"));
        QPointer<EditorOps> self(this);
        ctrl_->post([self, reply, requests](Simulation& s) {
            QJsonObject answer = measurementsJson(measureAll(s, requests));
            answer.insert("time", double(s.time()));
            QMetaObject::invokeMethod(qApp, [self, reply, answer] {
                if (self) reply(ok(answer));
            }, Qt::QueuedConnection);
        }, "sim_measure");
    });
}
