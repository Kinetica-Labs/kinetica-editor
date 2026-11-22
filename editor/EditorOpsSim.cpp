// Time and the view in the API (EditorOps.h): play, pause, stop, N frames stepped, the live state and the world's
// own operations (src/sim/WorldOps.h) put on the simulation's thread; a screenshot, framing, an agent's note in the
// console, and every button of the window by its text (what the UI can do, a script can).
#include "EditorOps.h"
#include "EditorRuntime.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "UiWindowSpace.h"
#include "SequencerPanel.h"
#include "SequencerViews.h"
#include "Viewport.h"
#include "api/Scene.h"
#include "scene/SceneGraph.h"
#include "sim/WorldOps.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QTabBar>
#include <QToolButton>
#include <QWidget>

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

QJsonObject worldAnswer(bool done, const Value& value, const std::string& why) {
    return done ? coreAnswerJson(value) : EditorOps::error(QString::fromStdString(why));
}

} // namespace

void EditorOps::addSimOps() {
    auto mode = [this] { return ok({{"mode", modeWord(builder_->mode())}}); };
    addSync("sim_play", "Run the scene (built from the graph)", "{}", [this, mode](const QJsonObject&) {
        if (runtime_->stopping()) return error("runtime is stopping");
        builder_->play();
        return mode();
    });
    addSync("sim_pause", "Pause it", "{}", [this, mode](const QJsonObject&) { builder_->pause(); return mode(); });
    addHostSync("sim_stop", [this, mode](const QJsonObject&) { builder_->stop(); return mode(); });
    // Paused (built from the graph first when editing), then exactly N frames on the simulation's
    // thread; the answer is the state after them.
    addStepOp();
    addRecordOp();
    // The world's own operations (WorldOps.h): the edit mode answers each at once with no world, else the simulation's
    // thread does and the reply comes back to this one. The controllers' side of the observability contract: sensor_read
    // gives what the sensors read, never the truth beside it.
    for (const WorldOp& op : worldOps())
        add(QString::fromStdString(op.name), QString::fromStdString(op.help), QString::fromStdString(op.args), [this, &op](const QJsonObject& a, const Reply& reply) {
            const Value args = coreValue(a);
            const std::vector<ControllerObject>& controllers = builder_->graph().controllers; // here: the simulation's thread must not read the graph
            const uint32_t first = controllers.empty() ? 0 : controllers.front().id;
            if (builder_->editing()) {
                Value value = Value::map();
                std::string why;
                const bool done = op.run(nullptr, first, args, value, why);
                return reply(worldAnswer(done, value, why));
            }
            QPointer<EditorOps> self(this);
            ctrl_->post([self, reply, &op, first, args](Simulation& s) {
                Value value = Value::map();
                std::string why;
                const bool done = op.run(&s, first, args, value, why);
                QMetaObject::invokeMethod(qApp, [self, reply, done, value, why] {
                    if (self) reply(worldAnswer(done, value, why));
                }, Qt::QueuedConnection);
            }, op.name.c_str());
        });
    addStateOp();
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
// The 3D view is put in again from its own framebuffer: a grab of the window leaves out what the view paints over its
// picture (the scene's name, the clock, «Схват: держит», the navigation cube, the HUD) - a newcomer's trial saw none of
// it - and the view's own child widgets (a bubble, a banner) go back on top of it.
static QImage wholeWindow(QWidget* window) {
    QImage image = window->grab().toImage();
    QPainter painter(&image);
    if (auto* view = window->findChild<Viewport*>(); view && view->isVisible()) {
        painter.drawImage(QRectF(view->mapTo(window, QPoint(0, 0)), QSizeF(view->size())), view->grabFramebuffer());
        for (QWidget* child : view->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (child->isVisible()) painter.drawPixmap(child->mapTo(window, QPoint(0, 0)), child->grab());
    }
    return image;
}

// The window's parts a picture can be of (view_screenshot part), by the words a person calls them.
static const std::pair<const char*, const char*> kParts[] = {
    {"view", ""}, {"toolbar", "createBar"}, {"tools", "toolShelf"}, {"inspector", "inspectorDock"}, {"sequencer", "sequencerDock"},
    {"plots", "resultsDock"}, {"console", "consoleDock"}, {"laboratory", "labDock"}};

// Where a widget stands in the window's picture, in its pixels (what ui_pointer space window takes).
static QJsonObject placeIn(QWidget* window, QWidget* w, const QRect& r) {
    const double k = window->devicePixelRatioF();
    const QPoint at = w->mapTo(window, r.topLeft());
    return QJsonObject{{"x", at.x() * k}, {"y", at.y() * k}, {"width", r.width() * k}, {"height", r.height() * k}};
}

static QImage seenWindow(QWidget* window) {
    QImage image = wholeWindow(window);
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
                               "a picture for the documentation; marks: false - without the editor's own marks in the scene either (joints and their ties, "
                               "sensors, lights, cameras, the gizmo, the selection's outline): what a camera would film - what a plugin or an agent drew (view_draw) stays in both; view_frame {id} first puts an object in the middle of it; file: where to save it - none, a new file in the temporary folder, "
                               "named in the answer; a picture of the window answers where the view stands in it, `view` in its pixels; "
                               "part: one part of the window - the 3D view, the top bar, the tool shelf, a panel -, cut out of the window's picture)",
            "{file?: str, window?: bool, screen?: bool, width?: int, clean?: bool, marks?: bool, "
            "part?: 'view'|'toolbar'|'tools'|'inspector'|'sequencer'|'plots'|'console'|'laboratory'}",
            [this](const QJsonObject& a) {
        static int shots = 0;
        const QString file = a.contains("file") ? a.value("file").toString()
                                                : QDir::tempPath() + QString("/kinetica-shot-%1-%2.png").arg(QCoreApplication::applicationPid()).arg(++shots);
        if (refresh_) refresh_();
        const bool clean = a.value("clean").toBool();
        std::vector<QWidget*> over; // the bar «an agent drives» and «Смотрю через»: words over the view, out of a clean picture
        for (const char* name : {"apiBanner", "cameraPicker"})
            if (QWidget* w = clean ? window_->findChild<QWidget*>(name) : nullptr; w && w->isVisible()) over.push_back(w);
        const bool marks = a.value("marks").toBool(true);
        if (clean) view_->setOverlayVisible(false);
        if (!marks) view_->setMarksVisible(false);
        for (QWidget* w : over) w->hide();
        QWidget* part = nullptr; // one part of the window: cut out of its picture, the 3D view in it as it is drawn
        if (const QString want = a.value("part").toString(); !want.isEmpty()) {
            for (const auto& [word, name] : kParts)
                if (want == word) part = *name ? window_->findChild<QWidget*>(name) : static_cast<QWidget*>(view_);
            if (!part || !part->isVisible()) {
                for (QWidget* w : over) w->show();
                if (clean) view_->setOverlayVisible(true);
                if (!marks) view_->setMarksVisible(true);
                return error("the " + want + " is not shown now: open it first (ui_trigger its tab or button)");
            }
        }
        const bool whole = a.value("screen").toBool() || a.value("window").toBool() || part;
        QImage image = a.value("screen").toBool() ? seenWindow(window_) : whole ? wholeWindow(window_) : view_->grabFramebuffer();
        if (part) {
            const QJsonObject r = placeIn(window_, part, part->rect());
            image = image.copy(QRect(int(r["x"].toDouble()), int(r["y"].toDouble()), int(r["width"].toDouble()), int(r["height"].toDouble())));
        }
        if (clean) view_->setOverlayVisible(true);
        if (!marks) view_->setMarksVisible(true);
        for (QWidget* w : over) w->show();
        const double logical = image.width() / image.devicePixelRatio(); // the window's own pixels: the view stands in them
        if (const int width = a.value("width").toInt(); width > 0 && width < image.width())
            image = image.scaledToWidth(width, Qt::SmoothTransformation);
        if (file.isEmpty() || !image.save(file)) return error("cannot save '" + file + "'");
        QJsonObject answer{{"file", file}, {"width", image.width()}, {"height", image.height()}};
        if (whole && !part && view_->isVisible()) { // where the 3D view stands in the picture, in its pixels
            const double k = image.width() / logical;
            const QPoint at = view_->mapTo(window_, QPoint(0, 0));
            answer["view"] = QJsonObject{{"x", at.x() * k}, {"y", at.y() * k}, {"width", view_->width() * k}, {"height", view_->height() * k}};
        }
        return ok(answer);
    });
    addSync("view_frame", "Show the object (selected), or the whole scene without an id", "{id?: int}", [this](const QJsonObject& a) {
        if (a.contains("id")) builder_->select(uint32_t(a.value("id").toInt())), builder_->frameSelected();
        else builder_->frameAll();
        return ok();
    });
    // The Sequencer's view, as the wheel, the middle button and F set it: the window's state, no change of the scene.
    addSync("sequence_frame", "The Sequencer's view, as F, the wheel and the middle button set it, answered as it stands: from, to - the "
            "time in sight, s; fit - the whole sequence again and the values fitted (F); curves - one track's curves (the selected "
            "object's) or the tracks; range - the curves' values [low, high] from the bottom to the top ([]: fitted to the curve in "
            "sight). Nothing given: only the answer. The window's view, no change of the scene",
            "{from?: number, to?: number, fit?: bool, curves?: bool, range?: [number]}", [this](const QJsonObject& a) {
        auto* panel = window_->findChild<SequencerPanel*>();
        if (!panel) return error("the window has no Sequencer");
        if (a.contains("from") != a.contains("to")) return error("from and to come together: the time in sight, s");
        const double from = a.value("from").toDouble(), to = a.value("to").toDouble();
        if (a.contains("from") && !(from >= 0 && to > from)) return error("the time in sight: 0 <= from < to, s");
        const QJsonArray range = a.value("range").toArray();
        if (a.contains("range") && !(range.isEmpty() || (range.size() == 2 && range[1].toDouble() > range[0].toDouble())))
            return error("range: [low, high], low below high - or [] to fit it");
        if (a.value("fit").toBool()) panel->fitTime();
        if (a.contains("from")) panel->frameTime(from, to);
        if (a.contains("curves")) panel->setCurves(a.value("curves").toBool());
        if (a.contains("range")) range.isEmpty() ? panel->curvesView()->fitValues() : panel->curvesView()->setValueRange(range[0].toDouble(), range[1].toDouble());
        const sequencer::Axis in = panel->axis(panel->curvesView()->width());
        const std::array<double, 2> values = panel->curvesView()->valueRange();
        return ok({{"from", in.from}, {"to", in.from + in.span}, {"curves", panel->curvesShown()}, {"range", QJsonArray{values[0], values[1]}}});
    });
    addSync("view_hud", "The performance plate in the view's lower left corner: the frames a second in large figures coloured by "
            "smoothness, the last two seconds of frame times, one frame's paint and the physics' step, the scene's speed against the "
            "wall clock, the renderer (a GPU or the processor). on: show or hide it; answers whether it shows and its numbers",
            "{on?: bool}", [this](const QJsonObject& a) {
        if (a.contains("on")) view_->setShowHud(a.value("on").toBool());
        if (auto* act = window_->findChild<QAction*>("actionHud")) act->setChecked(view_->showHud()); // the menu says what the view shows
        return ok({{"on", view_->showHud()}, {"fps", view_->liveFps()}, {"paint_ms", double(view_->paintMs())},
                   {"gpu_ms", double(view_->gpuMs())}, {"gpu_shadow_ms", double(view_->gpuShadowMs())},
                   {"realtime", view_->realtime()}, {"renderer", view_->renderer()}});
    });
    addSync("ui_actions", "Every button and menu item of the window, a panel's own buttons and the visible tabs: text, name, on/off, and "
            "for what is shown where it stands - x, y, width, height in the window's picture (the pixels view_screenshot window gives "
            "and ui_pointer space window takes): a hand finds it by its name, not by its looks; ui_trigger presses any of them by its text",
            "{}", [this](const QJsonObject&) {
        QJsonArray out;
        const auto entry = [](const QString& text, const QString& name, bool enabled, QJsonValue checked) {
            return QJsonObject{{"text", QString(text).remove('&')}, {"name", name}, {"enabled", enabled}, {"checked", checked}};
        };
        const auto placed = [this](QJsonObject e, QWidget* w, const QRect& r) {
            const QJsonObject at = placeIn(window_, w, r);
            for (auto it = at.begin(); it != at.end(); ++it) e.insert(it.key(), it.value());
            return e;
        };
        for (QAction* act : window_->findChildren<QAction*>()) {
            if (act->text().isEmpty() || act->isSeparator()) continue;
            QJsonObject e = entry(act->text(), act->objectName(), act->isEnabled(), act->isCheckable() ? QJsonValue(act->isChecked()) : QJsonValue());
            for (QObject* o : act->associatedObjects()) // its button on a bar, when one shows
                if (auto* w = qobject_cast<QWidget*>(o); w && !qobject_cast<QMenu*>(w) && w->isVisible() && w->window() == window_) {
                    e = placed(e, w, w->rect());
                    break;
                }
            out.append(e);
        }
        for (QAbstractButton* b : window_->findChildren<QAbstractButton*>()) { // a panel's own: no action behind it
            const auto* tool = qobject_cast<QToolButton*>(b);
            if ((tool && tool->defaultAction()) || !b->isVisible() || b->window() != window_ || (b->text().isEmpty() && b->toolTip().isEmpty())) continue;
            out.append(placed(entry(b->text().isEmpty() ? b->toolTip() : b->text(), b->objectName(), b->isEnabled(),
                                    b->isCheckable() ? QJsonValue(b->isChecked()) : QJsonValue()), b, b->rect()));
        }
        for (QTabBar* bar : window_->findChildren<QTabBar*>()) // the visible tabs: a panel's, the docks'
            for (int k = 0; bar->isVisible() && k < bar->count(); ++k)
                if (bar->isTabVisible(k))
                    out.append(placed(entry(bar->tabText(k), bar->objectName(), bar->isTabEnabled(k), QJsonValue(bar->currentIndex() == k)), bar, bar->tabRect(k)));
        return ok({{"actions", out}});
    });
    addSync("ui_trigger", "Press a button or a menu item by its text or name (as ui_actions lists them), a plain panel button "
            "by its text or tooltip, or a visible tab by its text", "{text: str}", [this](const QJsonObject& a) {
        const QString want = a.value("text").toString();
        if (want.isEmpty()) return error("text: the button's, item's or tab's words or name (ui_actions lists them)"); // "" begins every tooltip
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
    addHost("sim_step", [this](const QJsonObject& a, const Reply& reply) {
        if (runtime_->stopping()) return reply(error("runtime is stopping"));
        if (builder_->editing() && !builder_->loadPaused()) return reply(error("the scene cannot be built"));
        if (builder_->mode() == SceneBuilder::Mode::Playing) builder_->pause();
        const int frames = framesArg(coreValue(a)); // 0: built, not stepped
        const auto cancellation = runtime_->cancellationEpoch();
        const auto epoch = cancellation->load();
        // Through the simulation's loop, frame by frame: a plugin's on_tick answers each before the next (the lockstep).
        ctrl_->stepFrames(frames, [cancellation, epoch] { return cancellation->load() != epoch; },
                          [this, reply, frames](Simulation& after, int completed) {
            const QJsonObject state = coreAnswerJson(stepAnswer(after, frames, completed));
            QMetaObject::invokeMethod(this, [this, reply, state] {
                if (refresh_) refresh_();
                reply(state);
            }, Qt::QueuedConnection);
        });
    });
}

void EditorOps::addStateOp() {
    addHost("sim_state",
        [this](const QJsonObject&, const Reply& reply) {
        if (builder_->mode() == SceneBuilder::Mode::Edit) return reply(ok({{"mode", "edit"}}));
        ctrl_->post([this, reply](Simulation& s) {
            QJsonObject state = coreAnswerJson(liveState(s));
            QMetaObject::invokeMethod(this, [this, reply, state]() mutable {
                state.insert("mode", modeWord(builder_->mode()));
                const auto t = ctrl_->timing();
                state.insert("frame_timing", QJsonObject{{"frames", t.frames}, {"frame_dt_ms", t.frameDtMs}, {"commands_ms", t.commandsMs},
                                                          {"controllers_ms", t.controllersMs},
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
