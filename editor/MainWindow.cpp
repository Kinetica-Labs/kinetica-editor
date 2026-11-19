// The main window (see MainWindow.h): the 3D view in the middle, the scene builder on the right,
// the readings and plots at the bottom under "Графики" (the frame's numbers and the probe's raw
// channels beside them under "Эксперт"). This file builds the actions, the menus and the docks, and
// runs the automation of the command line (a scene, frames, a screenshot).
// Other parts of the window live in MainWindowStart.cpp (the first minute), MainWindowControls.cpp
// (keys, mouse schemes, many objects), MainWindowToolbar.cpp (the top bar) and MainWindowPlay.cpp
// (the simulation status row, K, Ctrl+K, the white-window check).
#include "MainWindow.h"
#include "EditorOps.h"
#include "RuPlural.h"

#include "RoleBar.h"
#include "PlotPanel.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include "core/Probe.h"

#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

using namespace ke;
using Snap = Frame;

MainWindow::MainWindow() {
    setWindowTitle("Kinetica Editor");
    ctrl_ = std::make_unique<SimController>();

    view_ = new Viewport(this);
    auto* workspace = new QWidget(this);
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0); workspaceLayout->setSpacing(0);
    workspaceLayout->addWidget(view_, 1);
    setCentralWidget(workspace);
    connectViewport();
    buildActions();
    buildResultsDock();
    // The right column (scene list, inspector) runs the full height, like the panels of a 3D package.
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    buildSceneBuilderDock();
    connectSceneBuilder();
    buildOperators();
    buildConsoleDock();
    buildLayoutActions();
    buildLaboratory(); // before the top bar: its button «Лаборатория» sits there
    buildMainToolbar();
    buildToolShelf();
    addLayoutButtons(); // after every dock and the shelf: the switches of the panels (MainWindowLayout.cpp)
    buildSceneMenu();
    buildEditMenu();
    buildControlsMenu();
    buildCamerasMenu();
    buildCameraPicker();
    buildPlayExtras();
    connectViewportControls();
    connectPlots();         // the plots with the Laboratory's timeline and the selected object
    setExpertMode(false);   // a beginner's screen: the builder and a big 3D view
    setGraphsVisible(false);

    status_ = new QLabel;
    statusBar()->addPermanentWidget(status_);
    onModeChanged();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::poll);
    timer_->start(15);
    poll();
}

// What the 3D view asks of the simulation: the mouse's hand (pick with the cursor ray, drag the target,
// release: ke::RobotWorld::grab), and a restart without the GPU when OpenGL 3 is missing.
void MainWindow::connectViewport() {
    connect(view_, &Viewport::openGLUnsupported, this, [this](QString renderer) {
        auto answer = QMessageBox::question(
            this, "OpenGL",
            QString("Видеокарта или драйвер не поддерживает OpenGL 3.0 (%1).\n\n"
                    "Перезапустить программу в программном режиме? Всё будет рисоваться процессором "
                    "(медленнее, но работает на любом ПК).").arg(renderer));
        if (answer != QMessageBox::Yes) return;
        QStringList args = QCoreApplication::arguments().mid(1);
        args << "--software-gl";
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        qApp->quit();
    });
    connect(view_, &Viewport::grabStarted, this, [this](int body, Vector3 p) {
        ctrl_->post([body, p](Simulation& s) {
            if (s.robot()) s.robot()->grab(*s.physics(), body, p);
        }, "grab");
    });
    connect(view_, &Viewport::grabMoved, this, [this](Vector3 t) {
        ctrl_->post([t](Simulation& s) {
            if (s.robot()) s.robot()->setGrabTarget(t);
        }, "grab");
    });
    connect(view_, &Viewport::grabReleased, this, [this] {
        ctrl_->post([](Simulation& s) {
            if (s.robot()) s.robot()->releaseGrab(*s.physics());
        }, "grab");
    });
}

MainWindow::~MainWindow() {
    timer_->stop();
    leftBarAct_ = nullptr; // the docks hide as the window closes: the layout buttons stop following them
    ctrl_.reset(); // stop the simulation thread before the widgets go away
}

// ---------------------------------------------------------------------------
// Toolbar & menu
// ---------------------------------------------------------------------------
void MainWindow::buildActions() {
    buildRunActions();
    buildMenus();
}

// Пуск, Пауза, Стоп, Шаг: shared toolbar and menu actions.
void MainWindow::buildRunActions() {
    // Edit -> play -> stop, as in a game engine: the builder's scene is only simulated while it plays.
    playAct_ = new QAction(controlIcon(ControlIcon::Play, 48), "Пуск", this);
    playAct_->setShortcut(Qt::Key_Space);
    playAct_->setToolTip("Пуск (пробел): физика оживает — сцена строится из того, что нарисовано");
    connect(playAct_, &QAction::triggered, this, &MainWindow::togglePlay);
    playAct_->setObjectName("actionPlay");
    pauseAct_ = new QAction(controlIcon(ControlIcon::Pause, 48), "Пауза", this);
    pauseAct_->setObjectName("actionPause");
    pauseAct_->setToolTip("Пауза (пробел)");
    connect(pauseAct_, &QAction::triggered, this, [this] { builder_->pause(); });
    stopAct_ = new QAction(controlIcon(ControlIcon::Stop, 48), "Стоп", this);
    stopAct_->setObjectName("actionStop");
    stopAct_->setShortcut(QKeySequence("Shift+Space"));
    stopAct_->setToolTip("Стоп (Shift+пробел): назад к правке — сцена такая, какой её нарисовали");
    connect(stopAct_, &QAction::triggered, this, [this] { builder_->stop(); });

    stepAct_ = new QAction(controlIcon(ControlIcon::Step, 48), "Шаг", this);
    stepAct_->setShortcut(Qt::Key_Period);
    stepAct_->setToolTip("Один кадр (.)");
    connect(stepAct_, &QAction::triggered, this, [this] { builder_->step(); });
}

// The menu bar: Файл (import, export, screenshot), Симуляция, Вид (filled later), Справка.
void MainWindow::buildMenus() {
    auto* importAct = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Импорт модели…", this);
    importAct->setShortcut(QKeySequence("Ctrl+I")); // Ctrl+O opens a scene (menu "Сцена")
    importAct->setToolTip("Модель OBJ или STL как новый объект сцены");
    connect(importAct, &QAction::triggered, this, &MainWindow::importModel);
    auto* shotAct = new QAction("Скриншот…", this);
    shotAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(shotAct, &QAction::triggered, this, &MainWindow::screenshot);
    auto* csvAct = new QAction("Экспорт всех каналов CSV…", this);
    csvAct->setToolTip("Всё, что записано за прогон, по столбцу на канал; только нарисованное — «Сохранить CSV» под графиками");
    csvAct->setShortcut(QKeySequence("Ctrl+E"));
    connect(csvAct, &QAction::triggered, this, &MainWindow::exportCsv);

    auto* file = menuBar()->addMenu("&Файл");
    file->setObjectName("fileMenu");
    file->addAction(importAct);
    file->addAction(csvAct);
    file->addAction(shotAct);
    file->addSeparator();
    file->addAction("Выход", QKeySequence::Quit, this, &QWidget::close);

    auto* sim = menuBar()->addMenu("&Симуляция");
    sim->setObjectName("simulationMenu");
    for (QAction* a : {playAct_, pauseAct_, stopAct_, stepAct_}) sim->addAction(a);
    auto* rt = sim->addAction("Не быстрее реального времени");
    rt->setCheckable(true);
    rt->setChecked(true);
    connect(rt, &QAction::toggled, this, [this](bool on) { ctrl_->setRealtimeLimit(on); });

    auto* viewMenu = menuBar()->addMenu("&Вид");
    viewMenu->setObjectName("viewMenu");
    viewMenu->addSeparator();

    auto* help = menuBar()->addMenu("&Справка");
    help->setObjectName("helpMenu");
    help->addAction("Управление и методы", QKeySequence::HelpContents, this, &MainWindow::showHelp);
    help->addAction("О программе", this, &MainWindow::showAbout);
}

void MainWindow::buildLayoutActions() {
    expertAct_ = new QAction("Эксперт", this);
    expertAct_->setCheckable(true);
    expertAct_->setToolTip("Числа кадра и сырые каналы отладчика под графиками, шаг и частота кадров над видом");
    connect(expertAct_, &QAction::toggled, this, &MainWindow::setExpertMode);
    graphsAct_ = new QAction("Графики", this);
    graphsAct_->setObjectName("actionGraphs");
    graphsAct_->setCheckable(true);
    graphsAct_->setShortcut(QKeySequence("F9")); // Ctrl+G is Group, as in Maya
    graphsAct_->setToolTip("Показания и графики внизу (F9); без них главные числа — в строке состояния");
    connect(graphsAct_, &QAction::toggled, this, &MainWindow::setGraphsVisible);
    collidersAct_ = new QAction(roleIcon(RoleIcon::Collider, 20), "Коллайдеры", this);
    collidersAct_->setCheckable(true);
    collidersAct_->setToolTip("Показать, чем сталкивается каждый объект: тонкий зелёный каркас. "
                              "Выбранный объект показывает свой всегда");
    connect(collidersAct_, &QAction::toggled, builder_, &SceneBuilder::setShowAllColliders);
    auto* hud = new QAction("Счётчик кадров", this);
    hud->setObjectName("actionHud");
    hud->setCheckable(true);
    hud->setChecked(view_->showHud());
    hud->setShortcut(QKeySequence(Qt::Key_F3));
    hud->setToolTip("Табличка в углу вида (F3): кадров в секунду крупно, график последних кадров, время рисования и шага физики, "
                    "скорость против реального времени, кто рисует — видеокарта или процессор");
    connect(hud, &QAction::triggered, this, [this](bool on) { ops_->run("view_hud", {{"on", on}}, [](const QJsonObject&) {}, "ui"); });
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    viewMenu->addSeparator();
    viewMenu->addAction(hud);
    viewMenu->addAction(collidersAct_);
    viewMenu->addAction(graphsAct_);
    viewMenu->addAction(expertAct_);
}

void MainWindow::setExpertMode(bool on) {
    view_->setShowStats(on); // the time step and frame rate: numbers for the curious
    if (auto* tables = findChild<QWidget*>("resultsTables")) tables->setVisible(on); // the probe's raw channels
    expertAct_->setChecked(on);
}

void MainWindow::setGraphsVisible(bool on) {
    settingGraphs_ = true;
    if (auto* dock = findChild<QDockWidget*>("resultsDock")) {
        dock->setVisible(on);
        if (on) dock->raise(); // shares its place with the console: asked for, it comes in front
    }
    graphsAct_->setChecked(on);
    settingGraphs_ = false;
    updateChannelRequests(); // the scene's channels are measured while the plots are open
}

void MainWindow::togglePlay() {
    if (builder_->mode() == SceneBuilder::Mode::Playing) builder_->pause();
    else builder_->play();
}

QString MainWindow::modeText() const {
    if (builder_->editing()) return "Правка — физика стоит. ▶ Пуск (пробел) оживит сцену";
    return builder_->mode() == SceneBuilder::Mode::Playing ? "Симуляция идёт — приводы и свет меняются на лету" : "Пауза";
}

// Edit mode and pause share the viewport's edit tools (select, gizmo); on pause they work on the
// simulation as it stands, and a drag reaches it on release.
void MainWindow::onModeChanged() {
    const bool edit = builder_->editing(), gizmo = builder_->gizmoAllowed();
    const SceneBuilder::Mode m = builder_->mode();
    view_->setEditMode(gizmo);
    view_->setEditCaption(edit ? "Правка: физика стоит. Двигайте, вращайте, масштабируйте; ▶ Пуск оживит сцену."
                               : "Пауза: гизмо двигает объект там, где он сейчас. ■ Стоп — к исходной сцене.");
    playAct_->setEnabled(m != SceneBuilder::Mode::Playing);
    pauseAct_->setEnabled(m == SceneBuilder::Mode::Playing);
    stopAct_->setEnabled(!edit);
    updateToolShelf();
    status_->setText(modeText());
    updateSimulationBar();
    updateLabTimeline(); // the Laboratory's strip: not while the scene is edited
}

// The right click of the edit mode, as the quad menu of 3ds Max: an object under the cursor that is
// not selected is selected first (the same click rules), then the menu opens at the cursor.
void MainWindow::showEditContextMenu(const QPointF& pos) {
    const uint32_t under = view_->pickEntity(view_->camera().screenRay(pos, view_->size()));
    if (under && !builder_->drawnSelected(under)) view_->clickSelect(pos, false);
    QMenu* menu = buildEditContextMenu();
    menu->exec(view_->mapToGlobal(pos.toPoint()));
    menu->deleteLater();
}

QMenu* MainWindow::buildEditContextMenu() {
    auto* menu = new QMenu(this);
    for (QAction* a : toolGroup_->actions()) menu->addAction(a); // the toolbar's own actions: one state
    menu->addSection("Система координат");
    auto* space = new QActionGroup(menu);
    const struct { const char* text; bool local; } spaces[] = {{"Мировая (World)", false}, {"Локальная (Local)", true}};
    for (const auto& s : spaces) {
        QAction* a = menu->addAction(s.text);
        a->setCheckable(true);
        a->setChecked(localAct_->isChecked() == s.local);
        a->setShortcut(Qt::Key_L);
        space->addAction(a);
        const bool local = s.local;
        connect(a, &QAction::triggered, this, [this, local] { localAct_->setChecked(local); });
    }
    if (builder_->selectedId()) addObjectActions(menu);
    return menu;
}

void MainWindow::addObjectActions(QMenu* menu) {
    const uint32_t id = builder_->selectedId();
    const ke::SceneObject* o = builder_->object(id);
    if (!o) return;
    const int n = int(builder_->selection().size());
    menu->addSection(n > 1 ? "Выбрано: " + ruPlural(n, "объект", "объекта", "объектов") : builder_->objectTitle(id));
    menu->addAction("Показать", QKeySequence(Qt::Key_F), this, [this] { builder_->frameSelected(); });
    menu->addAction("Дублировать", duplicateAct_->shortcut(), this, [this] { builder_->duplicateSelected(); });
    menu->addAction("Удалить", deleteAct_->shortcut(), this, [this] { builder_->removeSelected(); });
    menu->addAction(o->visible ? "Скрыть" : "Показать объект", QKeySequence(Qt::Key_H), this, [this, id] { builder_->toggleVisible(id); });
    menu->addAction(o->locked ? "Разблокировать" : "Заблокировать", this, [this, id] { builder_->toggleLocked(id); });
    addManyActions(menu);
    addSelectHelpers(menu);
    const ke::Entity* e = nullptr;
    for (const ke::Entity& x : builder_->graph().entities)
        if (x.id == builder_->masterOf(id)) e = &x;
    if (!e) return; // a group or an array has no components of its own
    QMenu* roles = menu->addMenu(roleIcon(RoleIcon::Rigid, 20), "Роль");
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const RoleIcon role = RoleIcon(k);
        QAction* a = roles->addAction(roleIcon(role, 20), roleName(role), this, [this, role] { builder_->toggleRole(role); });
        a->setCheckable(true);
        a->setChecked(roleEnabled(*e, role));
    }
}

// The gizmo's tools: edit mode and pause; while the scene plays with an IK goal selected too - a handle, so
// the tool is Move if it was Select (the gizmo shows at once on a goal just made or clicked).
void MainWindow::updateToolShelf() {
    if (!toolGroup_ || !localAct_) return; // a selection before the shelf is built
    const bool gizmo = builder_->gizmoOnSelection();
    for (QAction* a : toolGroup_->actions()) a->setEnabled(gizmo);
    localAct_->setEnabled(gizmo);
    if (builder_->goalSelected() && view_->gizmo().mode() == GizmoMode::Select)
        if (auto* move = findChild<QAction*>("toolMove")) move->trigger();
}

void MainWindow::setTool(GizmoMode m) {
    static const char* tools[] = {"select", "move", "rotate", "scale"};
    if (view_->gizmo().mode() != m) // the tool shelf as code: editor.gizmo.tool = "rotate"
        builder_->sayAction("gizmo_set", QString("editor.gizmo.tool = \"%1\"").arg(tools[int(m)]), {{"tool", tools[int(m)]}});
    view_->setCreateMode(false); // a tool of the shelf ends drawing shapes
    view_->gizmo().setMode(m);
    view_->gizmo().setHover(GizmoHandle::None);
    view_->update();
}

// The tool shelf at the left edge of the 3D view, as in Blender: select, move, rotate, scale, and
// whether the gizmo's axes are the world's or the object's own.
void MainWindow::buildToolShelf() {
    auto* tb = new QToolBar("Инструменты", this);
    tb->setObjectName("toolShelf");
    tb->setMovable(false);
    tb->setIconSize(QSize(28, 28));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::LeftToolBarArea, tb);
    toolGroup_ = new QActionGroup(this);
    struct Tool { GizmoMode mode; ControlIcon icon; const char* name; const char* id; const char* key; const char* tip; };
    const Tool tools[] = {
        {GizmoMode::Select, ControlIcon::ToolSelect, "Выбор", "toolSelect", "Q", "Выбор (Q): щелчок выбирает, ещё щелчок туда же — то, что позади"},
        {GizmoMode::Translate, ControlIcon::ToolMove, "Перемещение", "toolMove", "W", "Перемещение (W): стрелки — по оси, квадраты — в плоскости, центр — свободно; Ctrl — шаг 0,1 м"},
        {GizmoMode::Rotate, ControlIcon::ToolRotate, "Вращение", "toolRotate", "E", "Вращение (E): кольца — вокруг оси, внешнее — вокруг взгляда; Ctrl — шаг 15°"},
        {GizmoMode::Scale, ControlIcon::ToolScale, "Масштаб", "toolScale", "R", "Масштаб (R): кубики — по оси, центр — целиком; Ctrl — шаг 10 %"}};
    for (const Tool& t : tools) {
        QAction* a = tb->addAction(controlIcon(t.icon, 28), t.name);
        a->setObjectName(t.id);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(t.key));
        a->setToolTip(t.tip);
        a->setChecked(t.mode == GizmoMode::Translate);
        toolGroup_->addAction(a);
        const GizmoMode m = t.mode;
        connect(a, &QAction::triggered, this, [this, m] { setTool(m); });
    }
    tb->addSeparator();
    localAct_ = tb->addAction("Мир");
    localAct_->setObjectName("toolLocal");
    localAct_->setCheckable(true);
    localAct_->setShortcut(Qt::Key_L);
    localAct_->setToolTip("Оси гизмо (L): «Мир» — оси сцены, «Свои» — оси самого объекта");
    connect(localAct_, &QAction::toggled, this, [this](bool on) {
        builder_->sayAction("gizmo_set", QString("editor.gizmo.local = %1").arg(on ? "True" : "False"), {{"local", on}});
        view_->gizmo().setLocal(on);
        localAct_->setText(on ? "Свои" : "Мир");
        view_->update();
    });
    setTool(GizmoMode::Translate);
}

// ---------------------------------------------------------------------------
// Scene builder ("Конструктор"): shapes with roles, saved as *.kscene
// ---------------------------------------------------------------------------
// One panel on the right with two tabs, like Unity's Hierarchy and Inspector: "Сцена" (the list of
// objects and the world) and "Объект" (the selected object: its card, its geometry, its components).
// Selecting an object turns to "Объект". The visualisation settings share the place as a tab.
static QScrollArea* scrolled(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // the fields shrink, the panel never scrolls sideways
    scroll->setWidget(content);
    scroll->setMinimumWidth(340);
    return scroll;
}

void MainWindow::buildSceneBuilderDock() {
    builder_ = new SceneBuilder(ctrl_.get(), this);
    inspectorTabs_ = new QTabWidget;
    inspectorTabs_->setObjectName("inspectorTabs");
    inspectorTabs_->addTab(scrolled(builder_->sceneListPanel()), "Сцена");
    inspectorTabs_->addTab(scrolled(builder_->inspectorPanel()), "Объект");
    inspectorTabs_->addTab(scrolled(builder_->worldPanel()), "Мир");
    inspectorTabs_->setTabToolTip(0, "Все объекты сцены");
    inspectorTabs_->setTabToolTip(2, "Физика всей сцены: гравитация, шаг, контакты, суставы");
    inspectorTabs_->setTabToolTip(1, "Выбранный объект: где стоит, как выглядит и что умеет");
    auto* dock = new QDockWidget("Инспектор", this);
    dock->setObjectName("inspectorDock");
    dock->setWidget(inspectorTabs_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(dock->toggleViewAction());
}

void MainWindow::connectSceneBuilder() {
    connect(builder_, &SceneBuilder::statusMessage, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    connect(builder_, &SceneBuilder::modeChanged, this, &MainWindow::onModeChanged);
    connect(builder_, &SceneBuilder::editSnapshot, this, [this](std::shared_ptr<const ke::Frame> s) {
        if (builder_->editing()) view_->setSnapshot(std::move(s));
    });
    connect(builder_, &SceneBuilder::highlightBodies, view_, &Viewport::setHighlightBodies);
    connect(builder_, &SceneBuilder::hoverBodies, view_, &Viewport::setHoverBodies);
    connect(builder_, &SceneBuilder::ghostBodies, view_, &Viewport::setGhostBodies);
    connect(builder_, &SceneBuilder::unpickableBodies, view_, &Viewport::setUnpickableBodies);
    connect(builder_, &SceneBuilder::gizmoTarget, view_, &Viewport::setGizmoTarget);
    connect(builder_, &SceneBuilder::frameRequested, this, [this](const ke::AABB& box) {
        view_->setFocusBox(box);
        if (!view_->lookingThrough()) view_->frameScene(); // through a camera, framing would move the camera
    });
    connect(builder_, &SceneBuilder::sceneMarkers, view_, &Viewport::setSceneMarkers);
    connect(builder_, &SceneBuilder::cameraView, view_, &Viewport::setLookThrough);
    connect(view_, &Viewport::shadowsLeftOut, this, &MainWindow::showShadowBudget);
    connect(view_, &Viewport::lookThroughMoved, builder_, &SceneBuilder::onViewMovedThroughCamera);
    builder_->setViewEye([this] {
        const QVector3D e = view_->camera().eye();
        return ke::Vector3(e.x(), e.y(), e.z());
    });
    connect(view_, &Viewport::imageDropped, builder_, &SceneBuilder::assignImage);
    connect(view_, &Viewport::bodyClicked, builder_, &SceneBuilder::selectBody);
    connect(view_, &Viewport::goalDragged, builder_, &SceneBuilder::dragGoal);
    connect(view_, &Viewport::sensorClicked, builder_, [this](uint32_t id) { builder_->select(id); });
    connect(view_, &Viewport::entityClicked, builder_, &SceneBuilder::onEntityClicked);
    connect(view_, &Viewport::entityHovered, builder_, &SceneBuilder::onEntityHovered);
    connect(view_, &Viewport::gizmoStarted, builder_, &SceneBuilder::onGizmoStarted);
    connect(view_, &Viewport::gizmoMoved, builder_, &SceneBuilder::onGizmoMoved);
    connect(view_, &Viewport::gizmoFinished, builder_, &SceneBuilder::onGizmoFinished);
    connect(view_, &Viewport::gizmoCancelled, builder_, &SceneBuilder::onGizmoCancelled);
    connect(view_, &Viewport::cloneDragStarted, builder_, &SceneBuilder::onCloneDragStarted);
    connect(view_, &Viewport::cloneDragFinished, this, &MainWindow::showClonePopover);
    connect(view_, &Viewport::editContextMenu, this, &MainWindow::showEditContextMenu);
    // What the mouse ray hits: the authored shapes in edit mode, the simulated bodies on pause.
    view_->setEditPicker([this](const Ray& ray) {
        if (builder_->editing()) return builder_->pickAll(ray);
        std::vector<PickHit> hits;
        int body;
        ke::Vector3 hit;
        if (view_->pickAnyBody(ray, body, hit) && builder_->clickTarget(builder_->entityOfBody(body)))
            hits.push_back({builder_->clickTarget(builder_->entityOfBody(body)), ke::length(hit - ray.origin), hit, ""});
        return hits;
    });
    connect(builder_, &SceneBuilder::selectionChanged, this, [this](uint32_t id) {
        if (id != 0) inspectorTabs_->setCurrentIndex(1); // selecting shows the object
        updateToolShelf();
    });
    connect(builder_, &SceneBuilder::colliderGuides, view_, &Viewport::setColliderGuides);
    connect(builder_, &SceneBuilder::pathLines, view_, &Viewport::setPathLines);
}

void MainWindow::buildSceneMenu() {
    auto* menu = new QMenu("С&цена", this);
    menu->addAction("Новая пустая сцена", QKeySequence::New, this, [this] { builder_->newScene(); });
    menu->addAction("Открыть…", QKeySequence::Open, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Открыть сцену", examplesDir(), "Сцена Kinetica (*.kscene)");
        if (path.isEmpty()) return;
        QString error;
        if (!builder_->openFile(path, error)) QMessageBox::warning(this, "Открыть сцену", error);
    });
    menu->addAction("Сохранить…", QKeySequence::Save, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Сохранить сцену", examplesDir(), "Сцена Kinetica (*.kscene)");
        if (path.isEmpty()) return;
        QString error;
        if (!builder_->saveFile(path, error)) QMessageBox::warning(this, "Сохранить сцену", error);
    });
    auto* file = menuBar()->findChild<QMenu*>("fileMenu");
    const char* names[] = {"sceneNew", "sceneOpen", "sceneSave"};
    // The same actions appear in File and Scene: one shortcut and one implementation each.
    const auto sceneActions = menu->actions(); const auto anchor = file->actions().front();
    for (int i = 0; i < 3; ++i) {
        sceneActions[i]->setObjectName(names[i]); file->insertAction(anchor, sceneActions[i]);
    }
    file->insertSeparator(anchor);
    menu->addSeparator();
    menu->addAction("Примеры…", this, &MainWindow::openGallery);
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    menuBar()->insertMenu(viewMenu->menuAction(), menu);
}

// ---------------------------------------------------------------------------
// Results dock: numbers on the left, one chart per quantity on the right
// ---------------------------------------------------------------------------
void MainWindow::buildResultsDock() {
    auto* dock = new QDockWidget("Графики", this);
    dock->setObjectName("resultsDock");

    info_ = new QTableWidget(0, 2);
    info_->setHorizontalHeaderLabels({"Величина", "Значение"});
    info_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    info_->horizontalHeader()->setStretchLastSection(true);
    info_->verticalHeader()->setVisible(false);
    info_->verticalHeader()->setDefaultSectionSize(22);
    info_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    info_->setSelectionMode(QAbstractItemView::NoSelection);
    info_->setShowGrid(false);
    info_->setAlternatingRowColors(true);

    QWidget* plotHost = buildPlots(); // the charts and the quiet bar under them (MainWindowPlots.cpp)

    // The probe's channels: every quantity the engine reported this frame, by name.
    sensors_ = new QTableWidget(0, 2);
    sensors_->setHorizontalHeaderLabels({"Датчик (Probe)", "Значение"});
    sensors_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    sensors_->horizontalHeader()->setStretchLastSection(true);
    sensors_->verticalHeader()->setVisible(false);
    sensors_->verticalHeader()->setDefaultSectionSize(20);
    sensors_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sensors_->setSelectionMode(QAbstractItemView::NoSelection);
    sensors_->setShowGrid(false);
    sensors_->setAlternatingRowColors(true);
    sensors_->setToolTip("Всё, что мир робота сообщил отладчику ke::Probe: значения, счётчики за кадр, таймеры (мс)");
    auto* left = new QSplitter(Qt::Vertical); // the tables: an expert's, hidden for everyone else (setExpertMode)
    left->setObjectName("resultsTables");
    left->addWidget(info_);
    left->addWidget(sensors_);
    left->setSizes({260, 200});

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(plotHost);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({340, 1000});
    dock->setWidget(split);
    addDockWidget(Qt::BottomDockWidgetArea, dock); // shown and hidden by «Графики» (F9), and by the first ▶
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
// Every frame the simulation published since the last look goes into the Laboratory's recording;
// only the newest of them is drawn (on a slow screen the simulation steps several frames per paint).
// Nothing published: nothing to do.
void MainWindow::poll() {
    recordFrames(ctrl_->takeReadings()); // every simulated frame into the plots
    const auto frames = ctrl_->takePublished();
    if (frames.empty()) return;
    labRecord(frames);
    onSnapshot(frames.back());
}

void MainWindow::updateInfo(const Snap& s) {
    info_->setRowCount(int(s.info.size()));
    for (int r = 0; r < int(s.info.size()); ++r)
        for (int c = 0; c < 2; ++c) {
            QTableWidgetItem* it = info_->item(r, c);
            if (!it) info_->setItem(r, c, it = new QTableWidgetItem);
            it->setText(QString::fromStdString(c == 0 ? s.info[r].first : s.info[r].second));
        }
    // With the graphs folded away, the status bar carries the three main readings of the scene.
    QString readings;
    int shown = 0;
    for (const auto& [name, value] : s.info) {
        if (!expertAct_->isChecked() || graphsAct_->isChecked() || shown == 3 || name == "Время") continue;
        readings += QString("  ·  %1 %2").arg(QString::fromStdString(name), QString::fromStdString(value));
        ++shown;
    }
    status_->setText(QString("%1  ·  кадр %2  ·  t = %3 с  ·  %4%5")
                         .arg(QString::fromStdString(s.sceneName))
                         .arg(s.frame)
                         .arg(s.time, 0, 'f', 3)
                         .arg(modeText(), readings));
    updateSensors(s);
}

void MainWindow::updateSensors(const Snap& s) {
    const auto& ch = s.probe.channels;
    sensors_->setRowCount(int(ch.size()));
    for (int r = 0; r < int(ch.size()); ++r) {
        const Probe::Channel& c = ch[r];
        QString value = QString::number(c.value, 'g', 4);
        if (c.kind == Probe::Kind::TimerMs) value += " мс";
        else if (c.kind == Probe::Kind::Counter && c.value == std::floor(c.value)) value = QString::number(qint64(c.value));
        for (int col = 0; col < 2; ++col) {
            QTableWidgetItem* it = sensors_->item(r, col);
            if (!it) sensors_->setItem(r, col, it = new QTableWidgetItem);
            it->setText(col == 0 ? QString::fromStdString(c.name) : value);
        }
    }
}

void MainWindow::onSnapshot(const std::shared_ptr<const Snap>& s) {
    if (builder_->editing()) return; // edit mode shows the authored scene (SceneBuilder::editSnapshot), not the simulation
    builder_->onSimulationSnapshot(*s);
    if (labOnSnapshot()) view_->setSnapshot(s); // the Laboratory's timeline may be showing a kept frame
    updatePlotObjects(*s);
    lastSceneName_ = s->sceneName;
    lastFrame_ = s->frame;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastInfoUpdate_ > 120) {
        lastInfoUpdate_ = now;
        updateInfo(*s);
    }

    if (auto_.active && int(s->frame) >= auto_.frames && ++auto_.settle == 1) {
        ctrl_->setRunning(false);
        QTimer::singleShot(300, this, [this] {
            if (!auto_.shot.isEmpty()) (auto_.wholeWindow ? windowImage() : view_->grabFramebuffer()).save(auto_.shot);
            qApp->quit();
        });
    }
}

// The window as it is on the screen: a widget grab draws the 3D view without its painted overlay
// (the caption, the navigation cube), so the view's own frame is laid over it.
QImage MainWindow::windowImage() {
    QImage shot = grab().toImage();
    QPainter p(&shot);
    p.drawImage(QRect(view_->mapTo(this, QPoint(0, 0)), view_->size()), view_->grabFramebuffer());
    for (QWidget* c : view_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) // camera picker and Laboratory timeline
        if (c->isVisible()) c->render(&p, c->mapTo(this, QPoint(0, 0)), QRegion(), QWidget::DrawChildren); // rounded: no square backdrop
    return shot;
}

void MainWindow::runAutomation(int frames, const QString& shot) {
    startBuilder();
    if (auto_.throughCamera) builder_->lookThroughActiveCamera();
    auto_.active = !shot.isEmpty();
    if (!auto_.active) return;
    auto_.frames = std::max(1, frames);
    auto_.shot = shot;
    updateChannelRequests();
    ctrl_->setRealtimeLimit(false);
    if (auto_.editOnly) { // the authored scene as it is: no simulation
        QTimer::singleShot(900, this, [this] {
            (auto_.wholeWindow ? windowImage() : view_->grabFramebuffer()).save(auto_.shot);
            qApp->quit();
        });
        return;
    }
    if (auto_.wholeWindow) // the whole window in play as a person sees it a moment after ▶: the plots opened by themselves
        QTimer::singleShot(0, this, [this] { // after the first layout, which puts the console's tab in front
            setGraphsVisible(true);
            if (auto* dock = findChild<QDockWidget*>("resultsDock")) resizeDocks({dock}, {210}, Qt::Vertical);
        });
    builder_->play();
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------
void MainWindow::importModel() {
    const QString path = QFileDialog::getOpenFileName(this, "Модель как форма", examplesDir() + "/models", "3D-модели (*.obj *.stl)");
    if (path.isEmpty()) return;
    QString error;
    if (!builder_->importModel(path, error)) QMessageBox::warning(this, "Модель", "Не удалось загрузить модель:\n" + error);
}

void MainWindow::exportCsv() {
    if (plots_->empty()) {
        QMessageBox::information(this, "Экспорт", "Нет данных: запустите расчёт.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Экспорт графиков", "results.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!plots_->writeCsv(path)) QMessageBox::warning(this, "Экспорт", "Не удалось записать файл.");
    else statusBar()->showMessage("Сохранено: " + path, 4000);
}

void MainWindow::screenshot() {
    QImage img = view_->grabFramebuffer();
    QString def = QString("kinetica_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString path = QFileDialog::getSaveFileName(this, "Сохранить скриншот", def, "PNG (*.png)");
    if (!path.isEmpty() && img.save(path)) statusBar()->showMessage("Сохранено: " + path, 4000);
}

void MainWindow::showHelp() {
    QMessageBox box(this);
    box.setWindowTitle("Управление и методы");
    box.setTextFormat(Qt::RichText);
    box.setText(
        "<h3>Управление</h3>"
        "<b>Пробел</b> — пуск/пауза, <b>Shift+пробел</b> — стоп, <b>.</b> — шаг, <b>F</b> — показать выбранное, "
        "<b>Ctrl+K</b> — найти команду.<br>"
        "Камера: <b>зажатое колесо</b> — вращение, <b>Shift+колесо</b> или ПКМ — сдвиг, прокрутка — масштаб "
        "(схема — в «Вид → Управление»).<br>"
        "<b>ЛКМ</b> во время игры — рука мыши: тело тянется к курсору пружиной.<br><br>"
        "<h3>Мир робота</h3>"
        "Твёрдые тела и суставы считает движок Box3D за границей ke::PhysicsWorld. Серво и мотор постоянного "
        "тока — код робота: момент на звенья на каждом подшаге. Обратная связь по суставам — через модели "
        "энкодеров; истинные позы и углы движка видят только графики и Лаборатория.");
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "О программе",
                       "<b>Kinetica Editor 0.1</b><br>Редактор робототехники: звенья, суставы, приводы, датчики.<br>"
                       "Физика твёрдых тел — Box3D. C++17 · Qt 6 · OpenGL 3.0");
}
