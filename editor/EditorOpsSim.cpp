// Time and the view in the API (EditorOps.h): play, pause, stop, N frames stepped, the live state and the world's
// own operations (src/sim/WorldOps.h) put on the simulation's thread; a screenshot, framing, an agent's note in the
// console, and every button of the window by its text (what the UI can do, a script can).
#include "EditorOps.h"
#include "EditorRuntime.h"
#include "SceneBuilder.h"
#include "SimController.h"
#include "UiWindowSpace.h"
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
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QTabBar>
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
                               "a picture for the documentation; view_frame {id} first puts an object in the middle of it; file: where to save it - none, a new file in the temporary folder, "
                               "named in the answer)",
            "{file?: str, window?: bool, screen?: bool, width?: int, clean?: bool}",
            [this](const QJsonObject& a) {
        static int shots = 0;
        const QString file = a.contains("file") ? a.value("file").toString()
                                                : QDir::tempPath() + QString("/kinetica-shot-%1-%2.png").arg(QCoreApplication::applicationPid()).arg(++shots);
        if (refresh_) refresh_();
        const bool clean = a.value("clean").toBool();
        QWidget* banner = clean ? window_->findChild<QWidget*>("apiBanner") : nullptr;
        const bool bannerShown = banner && banner->isVisible();
        if (clean) view_->setOverlayVisible(false);
        if (bannerShown) banner->hide();
        QImage image = a.value("screen").toBool() ? seenWindow(window_) : a.value("window").toBool() ? wholeWindow(window_) : view_->grabFramebuffer();
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
