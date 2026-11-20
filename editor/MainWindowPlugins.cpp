// The «Плагины» menu (MainWindow.h; the host: PluginHost.h): the plugins' commands - buttons with their keys, found by
// Ctrl+K too -, «Новый плагин…» (a file from the template, started, opened in the system's editor for a person), the
// plugins' folder, and starting them all again. Plugins start with a person's window (main.cpp); an unattended run -
// the self-test, pictures - starts none of them, so it does not depend on what a user installed.
#include "MainWindow.h"

#include "ApiConsole.h"
#include "EditorOps.h"
#include "PluginHost.h"
#include "PluginPanel.h"
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

// The plugins' controllers follow the run (PluginHost::broadcast): ▶ from the edit - «play» to each, and the lockstep of
// Webots' controllers: every stepped frame's sensors, as the robot reads them (no truth), to their on_tick, its commands
// into the running world before the next frame; Stop - «stop», no lockstep. A pause steps nothing, so ticks nothing.
void MainWindow::syncControllers() {
    if (!plugins_) return;
    const bool running = !builder_->editing();
    const bool started = running && !runSeen_, stopped = !running && runSeen_;
    runSeen_ = running;
    if (stopped) {
        ctrl_->setLockstep(nullptr);
        plugins_->broadcast("stop", {}, [](QJsonObject) {});
    }
    if (!started) return;
    commandWarned_ = false;
    plugins_->broadcast("play", {}, [](QJsonObject) {});
    if (!plugins_->ticking()) return;
    QPointer<MainWindow> self(this);
    const bool world = plugins_->ticking(true); // the bodies' truth gathered only when a physics plugin takes it
    ctrl_->setLockstep([self, world](ke::Simulation& s) {
        QJsonObject packet{{"time", double(s.time())}, {"dt", double(s.frameDt)}, {"frame", double(s.frame())}, {"sensors", sensorReadings(s)}};
        if (world) packet.insert("bodies", bodyStates(s));
        QMetaObject::invokeMethod(self, [self, packet] {
            if (self) self->tickControllers(packet);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::tickControllers(const QJsonObject& packet) {
    const uint64_t frame = uint64_t(packet.value("frame").toDouble());
    plugins_->broadcast("tick", packet, [this, frame](QJsonObject commands) {
        if (builder_->editing()) return; // stopped meanwhile: the world is gone
        if (!commands.isEmpty()) {
            QPointer<MainWindow> self(this);
            ctrl_->post([self, commands](ke::Simulation& s) {
                const QString why = applyCommands(s, commands);
                if (!why.isEmpty()) // a command that did nothing is said, once a run - not sixty times a second
                    QMetaObject::invokeMethod(self, [self, why] {
                        if (self && !self->commandWarned_) self->commandWarned_ = true, emit self->plugins_->output("такт", "команда не выполнена: " + why);
                    }, Qt::QueuedConnection);
            }, "controllers");
        }
        ctrl_->release(frame);
    });
}

void MainWindow::buildPlugins() {
    auto* menu = new QMenu("&Плагины", this);
    menu->setObjectName("pluginsMenu");
    menu->setToolTipsVisible(true);
    menuBar()->insertMenu(menuBar()->findChild<QMenu*>("helpMenu")->menuAction(), menu);
    plugins_ = new PluginHost(ops_, builder_, this, menu, [this] { return apiPort(); }, [this](PluginPanel* panel, bool add) {
        // A plugin's panel: a tab of the inspector (right) or of the bottom panel; taken out when the plugin stops.
        if (panel->place() == "bottom") {
            if (!add) return delete panel->parentWidget(); // its dock
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
    QAction* make = menu->addAction("Новый плагин…");
    make->setObjectName("actionNewPlugin");
    make->setToolTip("Плагин на Python из одного файла: команда с полем, кнопка здесь же; сохраните файл — он перезапустится");
    connect(make, &QAction::triggered, this, [this] {
        // What to make, in words, then a name: each kind's file works at once - a button, a waving arm, a wind.
        QDialog ask(this);
        ask.setWindowTitle("Новый плагин");
        auto* form = new QFormLayout(&ask);
        auto* name = new QLineEdit("my_plugin");
        name->setObjectName("pluginNewName");
        form->addRow("Имя", name);
        const std::pair<const char*, const char*> kinds[] = {
            {"command", "Команда — кнопка, строка Ctrl+K и инструмент агента: сделать что-то со сценой"},
            {"controller", "Контроллер робота — каждый кадр Пуска: датчики → команды суставам"},
            {"world", "Физика мира — каждый кадр Пуска: тела → силы (газ, вода, ветер…)"}};
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
        buttons->button(QDialogButtonBox::Ok)->setText("Создать");
        buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
        connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
        form->addRow(buttons);
        if (ask.exec() != QDialog::Accepted) return;
        const QString kind = group->checkedButton()->property("kind").toString();
        ops_->run("plugin_new", {{"name", name->text()}, {"kind", kind}}, [this](const QJsonObject& r) {
            if (!r.value("ok").toBool()) return statusBar()->showMessage(r.value("error").toString(), 10000);
            const QString file = r.value("file").toString();
            statusBar()->showMessage("Плагин создан: " + file + " — его кнопка в «Плагины»; сохраните файл, и он перезапустится", 15000);
            if (!auto_.active) QDesktopServices::openUrl(QUrl::fromLocalFile(file)); // a person writes it on
        }, "ui");
    });
    QAction* add = menu->addAction("Добавить папку плагинов…");
    add->setObjectName("actionAddPluginFolder");
    add->setToolTip("Папка, где каждая подпапка с plugin.py — плагин: например, репозиторий kinetica-plugins. Редактор запомнит её");
    connect(add, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Папка плагинов", QDir::homePath());
        if (dir.isEmpty()) return;
        plugins_->addFolder(dir);
        statusBar()->showMessage("Плагины из " + dir + " запущены; папка запомнена", 8000);
    });
    QAction* folder = menu->addAction("Папка моих плагинов");
    folder->setToolTip("Каждая папка с plugin.py в ней — плагин");
    connect(folder, &QAction::triggered, this, [this] {
        QDir().mkpath(plugins_->userFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(plugins_->userFolder()));
    });
    QAction* again = menu->addAction("Перезапустить плагины");
    connect(again, &QAction::triggered, this, [this] {
        for (const QString& name : plugins_->names()) ops_->run("plugin_reload", {{"name", name}}, [](const QJsonObject&) {}, "ui");
    });
}
