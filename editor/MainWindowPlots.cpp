// The plots in the window (see PlotPanel.h), and what ties them to the rest of it:
//   1. zero clicks: the first ▶ opens the plots by itself, compact (unless the reader closed them by
//      hand), and selecting a body gives it its own charts, selecting an IK goal its arm's;
//   2. what the simulation measures for them (sim/Channels.h): the scene's channels while the
//      plots or the Laboratory are open - closed, nothing is measured and nothing is paid; the
//      channels of every body the reader looks at - selected in the scene, or clicked in the
//      Laboratory - under the object's name («Куб 1/height»); and an arm's joints and goal;
//   3. the Laboratory's timeline: its frame is a line through the plots, and a click on a plot takes
//      the timeline to that moment;
//   4. «Сохранить CSV»: the lines on the charts, to a file.
#include "MainWindow.h"

#include "scene/Assembly.h"
#include "scene/Kinematics.h"
#include "PlotPanel.h"
#include "SceneBuilder.h"
#include "EditorOps.h"
#include "SequencerPanel.h"
#include "Viewport.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>

// The charts, and under them a quiet bar: «Ещё величины…» and the layouts on the left, the history
// and the file on the right.
QWidget* MainWindow::buildPlots() {
    plots_ = new PlotPanel;
    auto* host = new QWidget;
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, 0, 0);
    col->addWidget(plots_, 1);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 4, 0, 0);
    bar->addWidget(plots_->moreButton());
    bar->addSpacing(8);
    bar->addWidget(plots_->arrangementSwitch());
    bar->addStretch(1);
    auto* clear = new QPushButton("Очистить");
    clear->setToolTip("Стереть записанную историю; графики остаются");
    connect(clear, &QPushButton::clicked, plots_, &PlotPanel::clear);
    auto* csv = new QPushButton("Сохранить CSV");
    csv->setObjectName("plotSaveCsv");
    csv->setToolTip("Линии с графиков в файл: время и по столбцу на линию, с именем и единицей");
    connect(csv, &QPushButton::clicked, this, &MainWindow::saveVisibleCsv);
    bar->addWidget(clear);
    bar->addWidget(csv);
    col->addLayout(bar);
    return host;
}

void MainWindow::connectPlots() {
    connect(builder_, &SceneBuilder::selectionChanged, this, [this] { watchSelected(); });
    connect(builder_, &SceneBuilder::modeChanged, this, [this] { watchSelected(); });
    connect(plots_, &PlotPanel::saveCsvRequested, this, &MainWindow::saveVisibleCsv);
    connect(plots_, &PlotPanel::roomWanted, this, [this](int height) { // the lanes: taller, up to half the window
        auto* dock = findChild<QDockWidget*>("resultsDock");
        if (dock && dock->isVisible() && dock->height() < height) resizeDocks({dock}, {std::min(height, this->height() / 2)}, Qt::Vertical);
    });
    // Closed by the reader's hand (F9, the menu, the dock's ✕): the next ▶ leaves the plots closed.
    connect(graphsAct_, &QAction::triggered, this, [this](bool on) { plotsClosedByHand_ = !on; });
    auto* dock = findChild<QDockWidget*>("resultsDock");
    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool on) {
        if (on || !dock->isHidden() || settingGraphs_ || !graphsAct_->isChecked()) return;
        plotsClosedByHand_ = true;
        setGraphsVisible(false);
    });
}

// Every simulated frame into the plots, in order - the view draws only the newest frame, the plots
// miss none. A new scene: its own charts (and no body watched from the old one); the frame count
// going back: a new run, a new history.
void MainWindow::recordFrames(const std::vector<SimController::Reading>& readings) {
    if (readings.empty() || builder_->editing()) return; // edit mode shows the authored scene, not a run
    for (const SimController::Reading& r : readings) {
        if (r.scene != plotScene_) {
            plots_->setScene(r.scene);
            if (!plotScene_.empty()) forgetWatched();
            plotScene_ = r.scene;
        } else if (r.frame < plotFrame_) {
            plots_->clear();
        }
        if (r.frame != plotFrame_ && r.frame > 0) plots_->append(r.time, r.channels, r.measurements);
        plotFrame_ = r.frame;
    }
    if (sceneRunning()) autoOpenPlots(); // the first ▶ shows the plots by itself
}

// The first ▶ opens the plots by themselves, compact - unless the reader closed them by hand (then
// they stay closed) or nobody watches (the automation's pictures keep their layout).
void MainWindow::autoOpenPlots() {
    if (graphsAct_->isChecked() || plotsClosedByHand_ || auto_.active) return;
    auto* sequencer = findChild<QDockWidget*>("sequencerDock");
    const bool timeline = sequencer && sequencer->isVisible() && !sequencer->visibleRegion().isEmpty(); // its tab in front
    setGraphsVisible(true);
    if (timeline) sequencer->raise(); // ▶ watched on the timeline: the plots open behind it
    if (auto* dock = findChild<QDockWidget*>("resultsDock")) {
        resizeDocks({dock}, {210}, Qt::Vertical);
        if (armWatched_) dock->raise(); // a goal selected before ▶: its arm's charts in front
    }
}

// --- What the simulation measures -----------------------------------------------------------------

// The scene's channels (its energy, momentum, the deepest contact) are measured while someone looks:
// the plots or the Laboratory open.
void MainWindow::updateChannelRequests() {
    const bool on = (graphsAct_ && graphsAct_->isChecked()) || (labAct_ && labAct_->isChecked());
    ctrl_->post([on](ke::Simulation& sim) { sim.channels.scene = on; });
}

// The name a body's channels go under: the name of the object it is in the scene (an array's copy: the
// array's, with its id), else «тело №id». A body already watched keeps its name; a name another
// watched body has gets this body's id.
QString MainWindow::watchLabel(int object) const {
    for (const ke::ObjectRef& w : watched_)
        if (int(w.entity) == object) return QString::fromStdString(w.label);
    const uint32_t id = uint32_t(object);
    QString label = QString("тело №%1").arg(object);
    if (const ke::SceneObject* o = ke::findObject(builder_->graph(), id); o && !o->name.empty())
        label = QString::fromStdString(o->name);
    else if (const ke::SceneObject* a = ke::isArrayCopyId(id) ? ke::findObject(builder_->graph(), ke::arrayOfCopyId(id)) : nullptr)
        label = QString::fromStdString(a->name) + QString(" №%1").arg(object);
    for (const ke::ObjectRef& w : watched_)
        if (QString::fromStdString(w.label) == label) return label + QString(" №%1").arg(object);
    return label;
}

// A body's channels - its height, speed, energy, the force of its contacts - are measured from the
// next frame on. A body once watched stays watched until the scene is edited or another one loaded.
void MainWindow::watchBody(int object) {
    if (object <= 0 || builder_->editing()) return;
    for (const ke::ObjectRef& w : watched_)
        if (int(w.entity) == object) return;
    const ke::ObjectRef ref{uint32_t(object), watchLabel(object).toStdString()};
    watched_.push_back(ref);
    ctrl_->post([ref](ke::Simulation& sim) { sim.channels.objects.push_back(ref); });
}

// The object selected in the scene: its charts in the plots («Объект: Куб 1»: height, speed), and,
// while the scene runs, its channels measured. Nothing selected: no object charts. Editing again
// forgets the watched bodies (the next run builds the bodies anew); the charts keep the last run.
void MainWindow::watchSelected() {
    if (builder_->editing()) {
        forgetWatched();
        plots_->showObject(QString());
    } else if (const auto s = view_->snapshot()) updatePlotObjects(*s);
}

void MainWindow::forgetWatched() {
    if (watched_.empty() && !armWatched_) return;
    watched_.clear();
    armWatched_ = false;
    ctrl_->post([](ke::Simulation& sim) { sim.channels.objects.clear(), sim.channels.joints.clear(), sim.channels.goals.clear(); });
}

// An IK goal selected while the scene runs: its arm in the plots, «Рука: Цель «Схват»» - for each joint of the
// chain its angle with its servo's target, the motors' torques together, and the goal's miss (its turn's too
// when the chain can take a turn: six freedoms or more). False: no goal selected.
bool MainWindow::watchArm() {
    const ke::SceneGraph& g = builder_->graph();
    const ke::Entity* goal = nullptr;
    for (const ke::Entity& e : g.entities)
        if (e.id == builder_->selectedId() && e.goalOf != 0) goal = &e;
    if (!goal || builder_->editing()) return false;
    const char* dofs[] = {"tx", "ty", "tz", "rx", "ry", "rz"};
    ke::ChannelRequests asked;
    std::vector<PlotPanel::Chart> charts;
    std::vector<std::string> prefixes;
    PlotPanel::Chart torques{"Моторы руки: момент", {}, {}, true};
    const std::vector<ke::IkLink> chain = ke::ikChain(g, goal->goalOf);
    for (const ke::IkLink& l : chain) {
        const ke::JointObject* j = ke::findJoint(g, l.joint);
        const std::string label = j && !j->name.empty() ? j->name : "Шарнир " + std::to_string(l.joint), id = label + "/" + dofs[l.dof];
        asked.joints.push_back({l.joint, label});
        charts.push_back({QString::fromStdString(label) + ": угол и цель сервы", {id + " coordinate", id + " target"}, {}, true});
        torques.ids.push_back(id + " effort");
        prefixes.push_back(label + "/");
    }
    charts.push_back(torques);
    asked.goals.push_back({goal->id, goal->name});
    charts.push_back({"Промах схвата до цели", {goal->name + "/miss"}, {}, true});
    if (chain.size() >= 6) charts.push_back({"Промах поворота", {goal->name + "/turn miss"}, {}, true});
    prefixes.push_back(goal->name + "/");
    const QString key = "arm " + QString::number(goal->id);
    if (plots_->objectKey() != key) // a goal newly selected: the plots come to the front of their dock, as a body's do
        if (auto* dock = findChild<QDockWidget*>("resultsDock"); dock && dock->isVisible()) dock->raise();
    armWatched_ = true;
    ctrl_->post([asked](ke::Simulation& sim) { sim.channels.joints = asked.joints, sim.channels.goals = asked.goals; });
    plots_->showSection(key, "Рука: " + QString::fromStdString(goal->name), charts, prefixes);
    return true;
}

// --- The file -------------------------------------------------------------------------------------

void MainWindow::saveVisibleCsv() {
    if (plots_->empty()) {
        QMessageBox::information(this, "Сохранить CSV", "Нечего сохранять: графики появятся после ▶.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Сохранить графики", "plots.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!plots_->writeVisibleCsv(path)) QMessageBox::warning(this, "Сохранить CSV", "Не удалось записать файл.");
    else statusBar()->showMessage("Сохранено: " + path, 4000);
}

// The plots' object: the one selected in the scene when the frame has it as a body of the physics engine (by the
// entity it is - stable while the scene lives, however the frame orders its bodies); else none.
void MainWindow::updatePlotObjects(const ke::Frame& snapshot) {
    if (watchArm()) return;
    if (armWatched_) { // the goal let go: its arm's channels too
        armWatched_ = false;
        ctrl_->post([](ke::Simulation& sim) { sim.channels.joints.clear(), sim.channels.goals.clear(); });
    }
    int object = -1;
    for (const ke::Frame::Body& b : snapshot.bodies)
        if (b.body >= 0 && !b.marker && b.object != 0 && b.object == builder_->selectedId()) object = int(b.object);
    if (object > 0) watchBody(object);
    plots_->showObject(object > 0 ? watchLabel(object) : QString());
}

// The Sequencer, a tab of the bottom panel beside the plots and the console, and the one in front from the start - a
// timeline under the view is what Blender, Cinema 4D and Unreal open with; the API console is a tab behind it. It
// follows every change of the scene (an edit, an undo, a call of the API) and the mode; I keys the selection anywhere in
// the window (a text field keeps it).
void MainWindow::buildSequencerDock() {
    auto* dock = new QDockWidget("Секвенсор", this);
    dock->setObjectName("sequencerDock");
    sequencer_ = new SequencerPanel(ops_, builder_, dock);
    dock->setWidget(sequencer_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    if (auto* graphs = findChild<QDockWidget*>("resultsDock")) tabifyDockWidget(graphs, dock);
    QTimer::singleShot(0, dock, [this, dock] { // after the first layout: in front, low - the view keeps the height
        dock->show();
        dock->raise();
        resizeDocks({dock}, {170}, Qt::Vertical);
    });
    connect(builder_, &SceneBuilder::editSnapshot, sequencer_, [this] { sequencer_->sync(); });
    connect(builder_, &SceneBuilder::modeChanged, sequencer_, &SequencerPanel::sync);
    connect(builder_, &SceneBuilder::selectionChanged, sequencer_, &SequencerPanel::sync);
    connect(new QShortcut(QKeySequence(Qt::Key_I), this), &QShortcut::activated, sequencer_, &SequencerPanel::keySelected);
    ops_->addListener([this](const QString&, const QString& name, const QJsonObject&, const QJsonObject&, bool) {
        if (name == "sim_record" || name == "scene_put") syncRecord(), sequencer_->sync(); // the take started or came in
    });
    if (auto* view = menuBar()->findChild<QMenu*>("viewMenu")) {
        QAction* show = view->addAction("Секвенсор");
        show->setObjectName("actionSequencer");
        show->setToolTip("Анимация объектов: секвенции, ключи, бегунок");
        connect(show, &QAction::triggered, dock, [dock] {
            dock->show();
            dock->raise();
        });
    }
}
