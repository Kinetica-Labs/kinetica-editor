// The API of the window (MainWindow.h): the operations (EditorOps) made once, the magnet on top of
// them, and the server that lets a script or an agent call them from outside.
#include "MainWindow.h"
#include "UiLanguage.h"

#include "ApiConsole.h"
#include "EditorOps.h"
#include "EditorServer.h"
#include "MagnetController.h"
#include "SceneBuilder.h"
#include "SequencerPanel.h"
#include "Viewport.h"

#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>

void MainWindow::buildOperators() {
    ops_ = new EditorOps(builder_, view_, ctrl_.get(), this);
    ops_->onRecordingChanged([this] { // ■ Stop ends a take too, and the run's end: both record buttons go out with it
        syncRecord();
        if (sequencer_) sequencer_->sync();
    });
    builder_->setCodeReporter([this](const QString& op, const QJsonObject& args) { ops_->report("ui", op, args); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { ops_->endSession(); }); // a clean end: no crash offered next time
    connect(builder_, &SceneBuilder::fireForceRequested, this, [this](uint32_t id) { // a refusal goes to the status bar
        ops_->run("force_fire", {{"id", int(id)}}, [this](const QJsonObject& r) {
            if (!r.value("ok").toBool()) statusBar()->showMessage(r.value("error").toString(), 6000);
        }, "ui");
    });
    magnet_ = new MagnetController(ops_, builder_, view_, this);
    connect(magnet_, &MagnetController::statusMessage, this, [this](const QString& text) {
        if (text.isEmpty()) statusBar()->clearMessage();
        else statusBar()->showMessage(text); // stays while the magnet is on: it says what a click does here
    });
    // An operation that waits for the simulation answers after the window took its newest frame, so a
    // screenshot right after sim_step shows that frame, not the one before.
    ops_->setRefresh([this] {
        if (auto s = ctrl_->snapshot()) onSnapshot(s);
        view_->update();
    });
}

// The API console at the bottom, a tab beside the graphs, open and in front from the start: what an agent, a
// script, the window and the user do, as Python lines, and the Python line to type in (ApiConsole.h) - the
// window is one client of the API among others, and the line says so. The View menu brings it back.
void MainWindow::buildConsoleDock() {
    auto* dock = new QDockWidget(uiText("Kinetica", "Консоль API"), this);
    dock->setObjectName("consoleDock");
    auto* console = new ApiConsole(ops_, dock);
    console->setApiPort([this] { return apiPort(); });
    dock->setWidget(console);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    if (auto* graphs = findChild<QDockWidget*>("resultsDock")) tabifyDockWidget(graphs, dock); // a tab behind the Sequencer's
    if (auto* view = menuBar()->findChild<QMenu*>("viewMenu")) {
        QAction* show = view->addAction(uiText("Kinetica", "Консоль API"));
        show->setObjectName("actionApiConsole");
        show->setToolTip(uiText("Kinetica", "Каждое действие агента, скрипта, окна и ваше — строкой Python; строка ввода для своих вызовов"));
        connect(show, &QAction::triggered, dock, [dock] {
            dock->show();
            dock->raise();
        });
    }
}

// While requests come in, a plate over the 3D view and the window's title say that an agent or a
// script drives the editor - a hand on the mouse then fights it; 4 s after the last request it goes. The
// API console opens too, once a session: what the agent types is there, line by line. It never takes the
// person's tab or comes back after they closed it - a person scrubbing the Sequencer while an agent works
// had the console thrown over it at every call.
void MainWindow::showDrivenByApi() {
    static bool opened = false;
    if (auto* console = findChild<QDockWidget*>("consoleDock"); console && !opened) {
        opened = true;
        if (!console->isVisible()) console->show();
    }
    if (!apiBanner_) {
        apiBanner_ = new QLabel(uiText("Kinetica", "АГЕНТ управляет редактором через API — не трогайте мышь и клавиши"), view_);
        apiBanner_->setObjectName("apiBanner"); // view_screenshot's clean picture leaves it out
        apiBanner_->setAttribute(Qt::WA_TransparentForMouseEvents);
        apiBanner_->setStyleSheet("QLabel { background: rgba(255, 140, 0, 225); color: #111; font-weight: bold;"
                                  " padding: 6px 14px; border-radius: 6px; }");
        apiIdle_ = new QTimer(this);
        apiIdle_->setSingleShot(true);
        apiIdle_->setInterval(4000);
        connect(apiIdle_, &QTimer::timeout, this, [this] {
            apiBanner_->hide();
            setWindowTitle(windowTitle().remove(uiText("Kinetica", "[АГЕНТ] ")));
            if (view_) view_->setAgentDriving(false); // the gizmo back for a hand
        });
    }
    apiBanner_->adjustSize();
    apiBanner_->move((view_->width() - apiBanner_->width()) / 2, 10);
    view_->setAgentDriving(true);
    if (!apiBanner_->isVisible()) {
        apiBanner_->show();
        apiBanner_->raise();
        const char* prefix = uiText("Kinetica", "[АГЕНТ] ");
        if (!windowTitle().startsWith(prefix)) setWindowTitle(prefix + windowTitle());
    }
    apiIdle_->start();
}

quint16 MainWindow::apiPort() {
    if (!apiServer_ && !startApi(0)) return 0; // 0: any free port, on 127.0.0.1 only
    return apiServer_->port();
}

bool MainWindow::startApi(quint16 port) {
    auto* server = new EditorServer(ops_, port, this);
    if (!server->listening()) {
        delete server;
        return false;
    }
    server->setOnActivity([this] { // the plate warns a person at the computer; a window nobody watches has nobody to warn
        if (qApp->property("keInteractive").toBool()) showDrivenByApi();
    });
    apiServer_ = server;
    statusBar()->showMessage(QString(uiText("Kinetica", "API редактора: 127.0.0.1:%1")).arg(server->port()), 8000);
    return true;
}
