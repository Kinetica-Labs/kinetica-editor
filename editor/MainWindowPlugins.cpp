// The «Плагины» menu (MainWindow.h; the host: PluginHost.h): the plugins' commands - buttons with their keys, found by
// Ctrl+K too -, «Новый плагин…» (a file from the template, started, opened in the system's editor for a person), the
// plugins' folder, and starting them all again. Plugins start with a person's window (main.cpp); an unattended run -
// the self-test, pictures - starts none of them, so it does not depend on what a user installed.
#include "MainWindow.h"
#include "UiLanguage.h"

#include "ApiConsole.h"
#include "EditorOps.h"
#include "PluginHost.h"
#include "Viewport.h"
#include "PluginPanel.h"
#include "RunHooks.h"
#include "sim/Simulation.h"
#include <QPointer>
#include "SceneBuilder.h"
#include "SimController.h"

#include <QDockWidget>
#include <QScrollArea>
#include <QTabWidget>

#include <QDesktopServices>
#include <QFileDialog>
#include <QDir>
#include <QInputDialog>
#include <QRadioButton>
#include <QPushButton>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QUrl>

void MainWindow::startPlugins() {
    if (plugins_) plugins_->start(false);
}

// The plugins' hooks follow the run, over their own wire (RunHooks.h): ▶ from the edit - «play» to each, and the lockstep
// of Webots' controllers: every stepped frame's sensors, as the robot reads them (no truth), to their on_tick, its
// commands into the running world before the next frame; Stop - «stop», no lockstep. A pause steps nothing, so ticks
// nothing. A frame goes from the simulation's thread to the wire's and its answer straight back: this window's thread
// is not in the loop, so a paint that waits for the screen holds no tick.
void MainWindow::syncControllers() {
    if (!plugins_) return;
    const bool running = !builder_->editing();
    const bool started = running && !runSeen_, stopped = !running && runSeen_;
    runSeen_ = running;
    if (stopped) {
        ctrl_->setLockstep(nullptr); // the last frame handed is on the wire before «stop»
        if (runLive_) runLive_->store(false);
        runHooks_->send("stop");
    }
    if (!started) return;
    commandWarned_ = false;
    plugins_->runStarted();
    runHooks_->send("play");
    if (!runHooks_->ticking()) return;
    QPointer<MainWindow> self(this);
    RunHooks* hooks = runHooks_.get();
    SimController* ctrl = ctrl_.get();
    const auto live = runLive_ = std::make_shared<std::atomic<bool>>(true);
    const bool world = runHooks_->ticking(true); // the bodies' truth gathered only when a physics plugin takes it
    ctrl_->setLockstep([self, hooks, ctrl, live, world](ke::Simulation& s) {
        QJsonObject packet{{"time", double(s.time())}, {"dt", double(s.frameDt)}, {"frame", double(s.frame())}, {"sensors", sensorReadings(s)}};
        if (world) packet.insert("bodies", bodyStates(s));
        hooks->send("tick", packet, [self, ctrl, live, frame = s.frame()](QJsonObject commands) { // on the wire's thread
            if (!commands.isEmpty() && live->load())
                ctrl->post([self, live, commands](ke::Simulation& s) {
                    if (!live->load()) return; // stopped meanwhile: the world is gone
                    const QString why = applyCommands(s, commands);
                    if (!why.isEmpty()) // a command that did nothing is said, once a run - not sixty times a second
                        QMetaObject::invokeMethod(self, [self, why] {
                            if (self && !self->commandWarned_) self->commandWarned_ = true, emit self->plugins_->output(uiText("Kinetica", "такт"), uiText("Kinetica", "команда не выполнена: ") + why);
                        }, Qt::QueuedConnection);
                }, "controllers");
            ctrl->release(frame);
        });
    });
}

void MainWindow::buildPlugins() {
    auto* menu = new QMenu(uiText("Kinetica", "&Плагины"), this);
    menu->setObjectName("pluginsMenu");
    menu->setToolTipsVisible(true);
    menuBar()->insertMenu(menuBar()->findChild<QMenu*>("helpMenu")->menuAction(), menu);
    runHooks_ = std::make_unique<RunHooks>();
    plugins_ = new PluginHost(ops_, builder_, this, menu, runHooks_.get(), [this] { return apiPort(); }, [this](PluginPanel* panel, bool add) {
        // A plugin's panel: a tab of the inspector (right) or of the bottom panel; taken out when the plugin stops.
        if (panel->place() == "bottom") {
            if (!add) return panel->parentWidget()->deleteLater(); // its dock, the panel in it: the host lets go of the panel after this
            auto* dock = new QDockWidget(panel->title(), this);
            dock->setObjectName("pluginDock");
            dock->setWidget(panel);
            addDockWidget(Qt::BottomDockWidgetArea, dock);
            if (auto* graphs = findChild<QDockWidget*>("resultsDock")) tabifyDockWidget(graphs, dock);
            dock->show(), dock->raise();
            return;
        }
        for (int i = 0; i < inspectorTabs_->count(); ++i)
            if (auto* scroll = qobject_cast<QScrollArea*>(inspectorTabs_->widget(i)); scroll && scroll->widget() == panel) {
                scroll->takeWidget();
                inspectorTabs_->removeTab(i);
                scroll->deleteLater();
            }
        if (!add) return;
        inspectorTabs_->addTab(scrolled(panel), panel->title());
    });
    if (auto* dock = findChild<QDockWidget*>("consoleDock"))
        if (auto* console = dynamic_cast<ApiConsole*>(dock->widget())) connect(plugins_, &PluginHost::output, console, &ApiConsole::pluginLine);
    // A plugin's tool: its shelf button makes it the view's tool, a click in the view calls it, Esc lights the button off.
    connect(plugins_, &PluginHost::toolPicked, view_, &Viewport::setPluginTool);
    connect(view_, &Viewport::pluginToolChanged, plugins_, &PluginHost::setToolChecked);
    connect(view_, &Viewport::toolClicked, plugins_, [this](const QString& op, ke::Vector3 point, ke::Vector3 normal, uint32_t object) {
        const auto three = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
        plugins_->useTool(op, {{"point", three(point)}, {"normal", three(normal)}, {"object", int(object)}});
    });
    connect(view_, &Viewport::toolDragged, plugins_, [this](const QString& op, ke::Vector3 point, ke::Vector3 normal, uint32_t object,
                                                           ke::Vector3 to, uint32_t toObject) {
        const auto three = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
        plugins_->useTool(op, {{"point", three(point)}, {"normal", three(normal)}, {"object", int(object)}, {"to", three(to)},
                               {"to_object", int(toObject)}});
    });
    QAction* make = menu->addAction(uiText("Kinetica", "Новый плагин…"));
    make->setObjectName("actionNewPlugin");
    make->setToolTip(uiText("Kinetica", "Плагин на Python из одного файла: команда с полем, кнопка здесь же; сохраните файл — он перезапустится"));
    connect(make, &QAction::triggered, this, [this] {
        // What to make, in words, then a name: each kind's file works at once - a button, a waving arm, a wind.
        QDialog ask(this);
        ask.setWindowTitle(uiText("Kinetica", "Новый плагин"));
        auto* form = new QFormLayout(&ask);
        auto* name = new QLineEdit("my_plugin");
        name->setObjectName("pluginNewName");
        form->addRow(uiText("Kinetica", "Имя"), name);
        const std::pair<const char*, const char*> kinds[] = {
            {"command", uiText("Kinetica", "Команда — кнопка, строка Ctrl+K и инструмент агента: сделать что-то со сценой")},
            {"controller", uiText("Kinetica", "Контроллер робота — каждый кадр Пуска: датчики → команды суставам")},
            {"world", uiText("Kinetica", "Физика мира — каждый кадр Пуска: тела → силы (ветер, магнит, тяга…)")}};
        auto* group = new QButtonGroup(&ask);
        for (const auto& [key, words] : kinds) {
            auto* option = new QRadioButton(words);
            option->setObjectName(QString("pluginNewKind_") + key);
            option->setProperty("kind", key);
            option->setChecked(group->buttons().isEmpty());
            group->addButton(option);
            form->addRow(option);
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(uiText("Kinetica", "Создать"));
        buttons->button(QDialogButtonBox::Cancel)->setText(uiText("Kinetica", "Отмена"));
        connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
        form->addRow(buttons);
        if (ask.exec() != QDialog::Accepted) return;
        const QString kind = group->checkedButton()->property("kind").toString();
        ops_->run("plugin_new", {{"name", name->text()}, {"kind", kind}}, [this](const QJsonObject& r) {
            if (!r.value("ok").toBool()) return statusBar()->showMessage(r.value("error").toString(), 10000);
            const QString file = r.value("file").toString();
            statusBar()->showMessage(uiText("Kinetica", "Плагин создан: ") + file + uiText("Kinetica", " — его кнопка в «Плагины»; сохраните файл, и он перезапустится"), 15000);
            if (!auto_.active) QDesktopServices::openUrl(QUrl::fromLocalFile(file)); // a person writes it on
        }, "ui");
    });
    QAction* add = menu->addAction(uiText("Kinetica", "Добавить папку плагинов…"));
    add->setObjectName("actionAddPluginFolder");
    add->setToolTip(uiText("Kinetica", "Папка, где каждая подпапка с plugin.py — плагин: например, репозиторий kinetica-plugins. Редактор запомнит её"));
    connect(add, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, uiText("Kinetica", "Папка плагинов"), QDir::homePath());
        if (dir.isEmpty()) return;
        ops_->run("plugin_scan", {{"folder", dir}}, [this, dir](const QJsonObject& r) {
            statusBar()->showMessage(r.value("ok").toBool() ? uiText("Kinetica", "Плагины из ") + dir + uiText("Kinetica", " запущены; папка запомнена") : r.value("error").toString(), 8000);
        }, "ui");
    });
    QAction* folder = menu->addAction(uiText("Kinetica", "Папка моих плагинов"));
    folder->setToolTip(uiText("Kinetica", "Каждая папка с plugin.py в ней — плагин"));
    connect(folder, &QAction::triggered, this, [this] {
        QDir().mkpath(plugins_->userFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(plugins_->userFolder()));
    });
    QAction* again = menu->addAction(uiText("Kinetica", "Перезапустить плагины"));
    again->setToolTip(uiText("Kinetica", "Каждый плагин запускается заново; папки плагинов просматриваются ещё раз — новая папка с plugin.py подхватывается"));
    connect(again, &QAction::triggered, this, [this] {
        const QStringList known = plugins_->names(); // the ones a scan starts now are fresh already
        ops_->run("plugin_scan", {}, [](const QJsonObject&) {}, "ui");
        for (const QString& name : known) ops_->run("plugin_reload", {{"name", name}}, [](const QJsonObject&) {}, "ui");
    });
}
