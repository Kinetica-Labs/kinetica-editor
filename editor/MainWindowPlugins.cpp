// The «Плагины» menu (MainWindow.h; the host: PluginHost.h): the plugins' commands - buttons with their keys, found by
// Ctrl+K too -, «Новый плагин…» (a file from the template, started, opened in the system's editor for a person), the
// plugins' folder, and starting them all again. Plugins start with a person's window (main.cpp); an unattended run -
// the self-test, pictures - starts none of them, so it does not depend on what a user installed.
#include "MainWindow.h"
#include "UiLanguage.h"

#include "ApiConsole.h"
#include "EditorOps.h"
#include "PluginHost.h"
#include "ScriptRunner.h"
#include "Viewport.h"
#include "PluginPanel.h"
#include "PlotPanel.h"
#include "RunHooks.h"
#include "sim/WorldOps.h"
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
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QVBoxLayout>
#include <memory>
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

// The plugins' hooks follow the run, over their own wire (RunHooks.h): ▶ from the edit - «play» to each, and the lockstep:
// every stepped frame's sensors, as the robot reads them (no truth), to their on_tick, its
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
        QJsonObject packet{{"time", double(s.time())}, {"dt", double(s.frameDt)}, {"frame", double(s.frame())}, {"sensors", coreJson(ke::sensorReadings(s))}};
        if (world) packet.insert("bodies", coreJson(ke::bodyStates(s)));
        hooks->send("tick", packet, [self, ctrl, live, frame = s.frame()](QJsonObject commands) { // on the wire's thread
            if (!commands.isEmpty() && live->load())
                ctrl->post([self, live, commands](ke::Simulation& s) {
                    if (!live->load()) return; // stopped meanwhile: the world is gone
                    const QString why = QString::fromStdString(ke::applyCommands(s, coreValue(commands)));
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
        // A plugin's panel: a tab of the inspector, wherever it asked to be - the window's first level is not a plugin's
        // (docs/contracts/ui.md); taken out when the plugin stops. Its charts are a section of the plots, and leave them
        // before the panel goes; its «Графики» button opens them, as F9 does.
        if (plots_) plots_->showPluginCharts(QString::number(quintptr(panel)), panel->title(), add ? panel->sharedCharts() : std::vector<SharedChart>());
        if (add) panel->onCharts([this] { showDock("resultsDock"); if (auto* dock = findChild<QDockWidget*>("resultsDock")) dock->raise(); });
        for (int i = 0; i < inspectorTabs_->count(); ++i)
            if (auto* scroll = qobject_cast<QScrollArea*>(inspectorTabs_->widget(i)); scroll && scroll->widget() == panel) {
                scroll->takeWidget();
                inspectorTabs_->removeTab(i);
                scroll->deleteLater();
            }
        if (!add) return;
        inspectorTabs_->addTab(scrolled(panel), panel->title());
    });
    scripts_ = new ScriptRunner(ops_, builder_, [this] { return apiPort(); }, this);
    if (auto* stop = findChild<QAction*>("actionStopScript")) connect(scripts_, &ScriptRunner::running, stop, &QAction::setEnabled);
    if (auto* dock = findChild<QDockWidget*>("consoleDock"))
        if (auto* console = dynamic_cast<ApiConsole*>(dock->widget())) {
            connect(plugins_, &PluginHost::output, console, &ApiConsole::pluginLine);
            connect(scripts_, &ScriptRunner::output, console, &ApiConsole::pluginLine); // a script's print(), under its file's name
        }
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

// «Файл → Подключить ИИ-ассистентов…»: every assistant of the table (assistant_list) a row - its name and what it is,
// whether it is here and connected - and one button that connects or disconnects Kinetica (assistant_connect,
// assistant_disconnect); the answer under the rows says what to restart.
void MainWindow::openAssistants() {
    auto* dialog = new QDialog(this);
    dialog->setObjectName("assistantsDialog");
    dialog->setWindowTitle(uiText("Kinetica", "Подключить ИИ-ассистентов"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(dialog);
    auto* intro = new QLabel(uiText("Kinetica", "Kinetica становится инструментом ассистента: вы просите в его чате — он строит, водит и "
                                                "проверяет роботов в этом окне."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* grid = new QGridLayout;
    layout->addLayout(grid);
    auto* said = new QLabel;
    said->setObjectName("assistantsNote");
    said->setWordWrap(true);
    layout->addWidget(said);
    auto* close = new QPushButton(uiText("Kinetica", "Закрыть"));
    connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(close, 0, Qt::AlignRight);
    auto fill = std::make_shared<std::function<void()>>();
    *fill = [this, dialog, grid, said, weak = std::weak_ptr<std::function<void()>>(fill)] {
        while (QLayoutItem* item = grid->takeAt(0)) delete item->widget(), delete item;
        int r = 0;
        for (const QJsonValue& v : operators()->runNow("assistant_list", {}).value("assistants").toArray()) {
            const QJsonObject a = v.toObject();
            const QString app = a.value("app").toString();
            const bool here = a.value("found").toBool(), on = a.value("connected").toBool();
            auto* name = new QLabel("<b>" + a.value("name").toString() + "</b><br>" + a.value("about").toString());
            auto* state = new QLabel(!here ? uiText("Kinetica", "не найден на этом компьютере") : on ? uiText("Kinetica", "подключён") : uiText("Kinetica", "не подключён"));
            auto* button = new QPushButton(on ? uiText("Kinetica", "Отключить") : uiText("Kinetica", "Подключить"));
            button->setObjectName("assistant_" + app);
            button->setEnabled(here);
            connect(button, &QPushButton::clicked, dialog, [this, app, on, button, said = QPointer<QLabel>(said), weak] {
                button->setEnabled(false);
                said->setText(on ? uiText("Kinetica", "Отключаю…") : uiText("Kinetica", "Подключаю…"));
                operators()->run(on ? "assistant_disconnect" : "assistant_connect", {{"app", app}}, [said, weak](const QJsonObject& r) {
                    if (!said) return; // the dialog closed meanwhile
                    said->setText(r.value("ok").toBool() ? (r.value("connected").toBool() ? uiText("Kinetica", "Подключено: ") : uiText("Kinetica", "Отключено: ")) +
                                                               r.value("note").toString()
                                                         : r.value("error").toString());
                    if (auto again = weak.lock()) (*again)();
                }, "ui");
            });
            grid->addWidget(name, r, 0), grid->addWidget(state, r, 1), grid->addWidget(button, r, 2);
            ++r;
        }
    };
    (*fill)();
    connect(dialog, &QObject::destroyed, [fill] {}); // the rows' refresher lives as long as the dialog
    dialog->show();
}
