// The «Плагины» menu (MainWindow.h; the host: PluginHost.h): the plugins' commands - buttons with their keys, found by
// Ctrl+K too -, «Новый плагин…» (a file from the template, started, opened in the system's editor for a person), the
// plugins' folder, and starting them all again. Plugins start with a person's window (main.cpp); an unattended run -
// the self-test, pictures - starts none of them, so it does not depend on what a user installed.
#include "MainWindow.h"

#include "EditorOps.h"
#include "PluginHost.h"
#include "PluginPanel.h"

#include <QDockWidget>
#include <QScrollArea>
#include <QTabWidget>

#include <QDesktopServices>
#include <QFileDialog>
#include <QDir>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QUrl>

void MainWindow::startPlugins() {
    if (plugins_) plugins_->start(false);
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
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(panel);
        inspectorTabs_->addTab(scroll, panel->title());
    });
    QAction* make = menu->addAction("Новый плагин…");
    make->setObjectName("actionNewPlugin");
    make->setToolTip("Плагин на Python из одного файла: команда с полем, кнопка здесь же; сохраните файл — он перезапустится");
    connect(make, &QAction::triggered, this, [this] {
        bool given = false;
        const QString name = QInputDialog::getText(this, "Новый плагин", "Имя (латиница, цифры, _):", QLineEdit::Normal, "my_plugin", &given);
        if (!given) return;
        ops_->run("plugin_new", {{"name", name}}, [this](const QJsonObject& r) {
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
