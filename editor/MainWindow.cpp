// The main window (see MainWindow.h): the 3D view in the middle, the scene builder on the right,
// the Sequencer, the readings and plots and the API console at the bottom. This file builds the actions, the menus and the docks, and
// runs the automation of the command line (a scene, frames, a screenshot).
// Other parts of the window live in MainWindowStart.cpp (the first minute), MainWindowControls.cpp
// (keys, mouse schemes, many objects), MainWindowToolbar.cpp (the top bar) and MainWindowPlay.cpp
// (the simulation status row, K, Ctrl+K, the white-window check).
#include "MainWindow.h"
#include <utility>
#include "Icons.h"

#include "RunHooks.h"
#include "PluginHost.h"
#include "EditorOps.h"
#include "UiLanguage.h"

#include "RoleBar.h"
#include "PlotPanel.h"
#include "SceneBuilder.h"
#include "SequencerPanel.h"
#include "Viewport.h"

#include "core/Probe.h"

#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QScrollArea>
#include <QDir>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
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
    buildSequencerDock();
    buildPlugins();
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
    setGraphsVisible(false);

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
            QString(uiText("Kinetica", "Видеокарта или драйвер не поддерживает OpenGL 3.0 (%1).\n\n"
                    "Перезапустить программу в программном режиме? Всё будет рисоваться процессором "
                    "(медленнее, но работает на любом ПК).")).arg(renderer));
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
    // ~QWidget hides the docks and the rest after this body: their signals must not reach a MainWindow that is gone
    // (UBSan: a member called on an object already a plain QWidget).
    for (QObject* child : findChildren<QObject*>()) child->disconnect(this);
    delete std::exchange(plugins_, nullptr); // before the operations it unhooks itself from (children go in the order made)
    ctrl_->setLockstep(nullptr); // no frame handed to the plugins' hooks any more, so their wire may go:
    runHooks_.reset();           // its thread ends here, and with it the answers that reach for the simulation
    ctrl_.reset(); // stop the simulation thread before the widgets go away
}

// ---------------------------------------------------------------------------
// Toolbar & menu
// ---------------------------------------------------------------------------
void MainWindow::buildActions() {
    buildRunActions();
    buildMenus();
}

// Пуск, Пауза, Стоп (and Шаг in the menu): shared toolbar and menu actions.
void MainWindow::buildRunActions() {
    // Edit -> play -> stop: the builder's scene is only simulated while it plays.
    playAct_ = new QAction(controlIcon(ControlIcon::Play, 48), uiText("Kinetica", "Пуск"), this);
    playAct_->setShortcut(Qt::Key_Space);
    playAct_->setToolTip(uiText("Kinetica", "Пуск (пробел): физика оживает — сцена строится из того, что нарисовано"));
    connect(playAct_, &QAction::triggered, this, &MainWindow::togglePlay);
    playAct_->setObjectName("actionPlay");
    pauseAct_ = new QAction(controlIcon(ControlIcon::Pause, 48), uiText("Kinetica", "Пауза"), this);
    pauseAct_->setObjectName("actionPause");
    pauseAct_->setToolTip(uiText("Kinetica", "Пауза (пробел)"));
    connect(pauseAct_, &QAction::triggered, this, [this] { builder_->pause(); });
    stopAct_ = new QAction(controlIcon(ControlIcon::Stop, 48), uiText("Kinetica", "Стоп"), this);
    stopAct_->setObjectName("actionStop");
    stopAct_->setShortcut(QKeySequence("Shift+Space"));
    stopAct_->setToolTip(uiText("Kinetica", "Стоп (Shift+пробел): назад к правке — сцена такая, какой её нарисовали"));
    connect(stopAct_, &QAction::triggered, this, [this] { builder_->stop(); });

    buildRecordAction();
    stepAct_ = new QAction(controlIcon(ControlIcon::Step, 48), uiText("Kinetica", "Шаг"), this);
    stepAct_->setShortcut(Qt::Key_Period);
    stepAct_->setToolTip(uiText("Kinetica", "Один кадр (.)"));
    connect(stepAct_, &QAction::triggered, this, [this] { builder_->step(); });
}

// The menu bar: Файл (import, export, screenshot), Симуляция, Вид (filled later), Справка.
void MainWindow::buildMenus() {
    auto* importAct = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), uiText("Kinetica", "Импорт…"), this);
    importAct->setShortcut(QKeySequence("Ctrl+I")); // Ctrl+O opens a scene (menu "Сцена")
    importAct->setToolTip(uiText("Kinetica", "Модель OBJ или STL как новый объект, робот URDF и форматы плагинов; файл можно и перетащить в вид"));
    connect(importAct, &QAction::triggered, this, &MainWindow::importModel);
    auto* scriptAct = new QAction(uiText("Kinetica", "Выполнить скрипт…"), this);
    scriptAct->setObjectName("actionRunScript");
    scriptAct->setToolTip(uiText("Kinetica", "Файл Python, который строит или меняет сцену: пример кодом. Всё, что он сделал, — один шаг отмены"));
    connect(scriptAct, &QAction::triggered, this, &MainWindow::runScript);
    auto* stopScriptAct = new QAction(uiText("Kinetica", "Остановить скрипт"), this);
    stopScriptAct->setObjectName("actionStopScript");
    stopScriptAct->setToolTip(uiText("Kinetica", "Прервать скрипт, который сейчас идёт: всё, что он успел сделать, отменяется"));
    stopScriptAct->setEnabled(false); // lit while a script runs (ScriptRunner::running)
    connect(stopScriptAct, &QAction::triggered, this, [this] { ops_->run("script_stop", {}, [](const QJsonObject&) {}, "ui"); });
    auto* shotAct = new QAction(uiText("Kinetica", "Скриншот…"), this);
    shotAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(shotAct, &QAction::triggered, this, &MainWindow::screenshot);
    auto* csvAct = new QAction(uiText("Kinetica", "Экспорт всех каналов CSV…"), this);
    csvAct->setToolTip(uiText("Kinetica", "Всё, что записано за прогон, по столбцу на канал; только нарисованное — «Сохранить CSV» под графиками"));
    csvAct->setShortcut(QKeySequence("Ctrl+E"));
    connect(csvAct, &QAction::triggered, this, &MainWindow::exportCsv);

    auto* file = menuBar()->addMenu(uiText("Kinetica", "&Файл"));
    file->setObjectName("fileMenu");
    file->addAction(importAct);
    file->addAction(scriptAct);
    file->addAction(stopScriptAct);
    file->addAction(csvAct);
    file->addAction(shotAct);
    file->addSeparator();
    file->addAction(uiText("Kinetica", "Выход"), QKeySequence::Quit, this, &QWidget::close);

    auto* sim = menuBar()->addMenu(uiText("Kinetica", "&Симуляция"));
    sim->setObjectName("simulationMenu");
    for (QAction* a : {playAct_, recordAct_, pauseAct_, stopAct_, stepAct_}) sim->addAction(a);
    auto* rt = sim->addAction(uiText("Kinetica", "Не быстрее реального времени"));
    rt->setCheckable(true);
    rt->setChecked(true);
    connect(rt, &QAction::toggled, this, [this](bool on) { ctrl_->setRealtimeLimit(on); });

    auto* viewMenu = menuBar()->addMenu(uiText("Kinetica", "&Вид"));
    viewMenu->setObjectName("viewMenu");
    viewMenu->addSeparator();

    auto* settings = menuBar()->addMenu(uiText("Kinetica", "Настройки"));
    settings->setObjectName("settingsMenu");
    auto* language = settings->addMenu(uiText("Kinetica", "Язык"));
    language->setObjectName("languageMenu");
    auto* languages = new QActionGroup(language);
    const auto addLanguage = [&](const QString& code, const QString& label) {
        auto* action = language->addAction(label);
        action->setObjectName("language_" + code);
        languages->addAction(action);
        action->setCheckable(true);
        action->setChecked(code == UiLanguage::active());
        connect(action, &QAction::triggered, this, [this, code] {
            if (code == UiLanguage::active()) return;
            QTimer::singleShot(0, this, [this, code] { changeLanguage(code); });
        });
    };
    addLanguage("ru", "Русский");
    addLanguage("en", "English");

    auto* help = menuBar()->addMenu(uiText("Kinetica", "&Справка"));
    help->setObjectName("helpMenu");
    help->addAction(uiText("Kinetica", "Управление и методы"), QKeySequence::HelpContents, this, &MainWindow::showHelp);
    help->addAction(uiText("Kinetica", "О программе"), this, &MainWindow::showAbout);
}

void MainWindow::buildLayoutActions() {
    graphsAct_ = new QAction(uiText("Kinetica", "Графики"), this);
    graphsAct_->setObjectName("actionGraphs");
    graphsAct_->setCheckable(true);
    graphsAct_->setShortcut(QKeySequence("F9")); // Ctrl+G is Group
    graphsAct_->setToolTip(uiText("Kinetica", "Графики внизу (F9): любая величина сцены во времени"));
    connect(graphsAct_, &QAction::toggled, this, &MainWindow::setGraphsVisible);
    collidersAct_ = new QAction(roleIcon(RoleIcon::Collider, 20), uiText("Kinetica", "Коллайдеры всех тел"), this);
    collidersAct_->setObjectName("actionColliders");
    collidersAct_->setCheckable(true);
    collidersAct_->setToolTip(uiText("Kinetica", "Чем сталкивается каждое тело — то, что видит физика: зелёный каркас поверх модели (серый — у неподвижных). "
                              "Выбранное тело показывает свой всегда"));
    connect(collidersAct_, &QAction::toggled, builder_, &SceneBuilder::setShowAllColliders);
    collidersAct_->setChecked(true); // on from the start: what the physics collides with is seen (a person asked for it)
    auto* hud = new QAction(uiText("Kinetica", "Производительность"), this);
    hud->setObjectName("actionHud");
    hud->setCheckable(true);
    hud->setChecked(view_->showHud());
    hud->setShortcut(QKeySequence(Qt::Key_F3));
    hud->setToolTip(uiText("Kinetica", "Табличка в углу вида (F3): кадров в секунду крупно, график последних кадров, время рисования и шага физики, "
                    "скорость против реального времени, кто рисует — видеокарта или процессор"));
    connect(hud, &QAction::triggered, this, [this](bool on) { ops_->run("view_hud", {{"on", on}}, [](const QJsonObject&) {}, "ui"); });
    auto* follow = new QAction(uiText("Kinetica", "Следить за выделенным"), this);
    follow->setObjectName("actionFollowSelected");
    follow->setCheckable(true);
    follow->setShortcut(QKeySequence("Shift+F"));
    follow->setToolTip(uiText("Kinetica", "Камера держит выбранное тело в центре, пока мир играет, — сейчас или с ▶; вращать и приближать её можно. "
                                          "Ещё раз, Esc, F или Home — свой вид"));
    connect(follow, &QAction::triggered, this, [this, follow](bool on) {
        if (on && !builder_->selectedId()) {
            follow->setChecked(false);
            return statusBar()->showMessage(uiText("Kinetica", "Выберите тело, за которым следить"), 4000);
        }
        ops_->run("view_follow", {{"body", on ? int(builder_->selectedId()) : 0}}, [this, follow](const QJsonObject& r) {
            follow->setChecked(view_->followWish() != 0);
            if (!r.value("ok").toBool()) statusBar()->showMessage(r.value("error").toString(), 4000);
        }, "ui");
    });
    connect(view_, &Viewport::followChanged, follow, [follow](uint32_t body) { follow->setChecked(body != 0); });
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    viewMenu->addSeparator();
    viewMenu->addAction(follow);
    viewMenu->addAction(hud);
    viewMenu->addAction(collidersAct_);
    viewMenu->addAction(graphsAct_);
}

void MainWindow::setGraphsVisible(bool on) {
    settingGraphs_ = true;
    if (auto* dock = findChild<QDockWidget*>("resultsDock")) {
        dock->setVisible(on);
        if (on) dock->raise(); // shares its place with the Sequencer and the console: asked for, it comes in front
    }
    graphsAct_->setChecked(on);
    settingGraphs_ = false;
    updateChannelRequests(); // the scene's channels are measured while the plots are open
}

void MainWindow::togglePlay() {
    if (builder_->mode() == SceneBuilder::Mode::Playing) builder_->pause();
    else builder_->play();
}

// Edit mode and pause share the viewport's edit tools (select, gizmo); on pause they work on the
// simulation as it stands, and a drag reaches it on release.
void MainWindow::onModeChanged() {
    const bool edit = builder_->editing(), gizmo = builder_->gizmoAllowed();
    const SceneBuilder::Mode m = builder_->mode();
    view_->setEditMode(gizmo);
    if (edit) view_->clearPlayDrawings(); // Stop: the run's pictures go with it
    playAct_->setEnabled(m != SceneBuilder::Mode::Playing);
    pauseAct_->setEnabled(m == SceneBuilder::Mode::Playing);
    stopAct_->setEnabled(!edit);
    syncRecord();
    syncControllers();
    updateToolShelf();
    updateSimulationBar();
}

// The right click of the edit mode, a quad menu: an object under the cursor that is
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
    // One switch, the shelf's «Глоб./Лок.» in words (it was a section of two with English in brackets).
    QAction* local = menu->addAction(uiText("Kinetica", "Оси объекта"), this, [this](bool on) { localAct_->setChecked(on); });
    local->setObjectName("menuLocalAxes");
    local->setCheckable(true);
    local->setChecked(localAct_->isChecked());
    local->setShortcut(Qt::Key_L);
    if (builder_->selectedId()) addObjectActions(menu);
    return menu;
}

void MainWindow::addObjectActions(QMenu* menu) {
    const uint32_t id = builder_->selectedId();
    const ke::SceneObject* o = builder_->object(id);
    if (!o) return;
    const int n = int(builder_->selection().size());
    menu->addSection(n > 1 ? uiText("Kinetica", "Выбрано: ") + uiCount(n, UiCount::Object) : builder_->objectTitle(id));
    menu->addAction(uiText("Kinetica", "К выбранному"), QKeySequence(Qt::Key_F), this, [this] { builder_->frameSelected(); }); // «Показать» was also «unhide»
    menu->addAction(uiText("Kinetica", "Дублировать"), duplicateAct_->shortcut(), this, [this] { builder_->duplicateSelected(); });
    menu->addAction(uiText("Kinetica", "Удалить"), deleteAct_->shortcut(), this, [this] { builder_->removeSelected(); });
    menu->addAction(o->visible ? uiText("Kinetica", "Скрыть") : uiText("Kinetica", "Показать объект"), QKeySequence(Qt::Key_H), this, [this, id] { builder_->toggleVisible(id); });
    menu->addAction(o->locked ? uiText("Kinetica", "Разблокировать") : uiText("Kinetica", "Заблокировать"), this, [this, id] { builder_->toggleLocked(id); });
    addManyActions(menu);
    // The selection helpers and the components stay where they are found once: «Правка» and the inspector (a fourth way to
    // a component, «Роль ▸», did not open for a newcomer three times; the menu was twenty lines).
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
    view_->setCreateMode(false); // a tool of the shelf ends drawing shapes, and a plugin's tool
    view_->setPluginTool({}, {});
    view_->gizmo().setMode(m);
    view_->gizmo().setHover(GizmoHandle::None);
    view_->update();
}

// The tool shelf at the left edge of the 3D view: select, move, rotate, scale, and
// whether the gizmo's axes are the world's or the object's own.
void MainWindow::buildToolShelf() {
    auto* tb = new QToolBar(uiText("Kinetica", "Инструменты"), this);
    tb->setObjectName("toolShelf");
    tb->setMovable(false);
    tb->setIconSize(QSize(28, 28));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::LeftToolBarArea, tb);
    toolGroup_ = new QActionGroup(this);
    struct Tool { GizmoMode mode; ControlIcon icon; const char* name; const char* id; const char* key; const char* tip; };
    const Tool tools[] = {
        {GizmoMode::Select, ControlIcon::ToolSelect, uiText("Kinetica", "Выбор"), "toolSelect", "Q", uiText("Kinetica", "Выбор (Q): щелчок выбирает, ещё щелчок туда же — то, что позади")},
        {GizmoMode::Translate, ControlIcon::ToolMove, uiText("Kinetica", "Перемещение"), "toolMove", "W", uiText("Kinetica", "Перемещение (W): стрелки — по оси, квадраты — в плоскости, центр — свободно; Ctrl — шаг 0,1 м")},
        {GizmoMode::Rotate, ControlIcon::ToolRotate, uiText("Kinetica", "Вращение"), "toolRotate", "E", uiText("Kinetica", "Вращение (E): кольца — вокруг оси, внешнее — вокруг взгляда; Ctrl — шаг 15°")},
        {GizmoMode::Scale, ControlIcon::ToolScale, uiText("Kinetica", "Масштаб"), "toolScale", "R", uiText("Kinetica", "Масштаб (R): кубики — по оси, центр — целиком; Ctrl — шаг 10 %")}};
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
    localAct_ = tb->addAction(uiText("Kinetica", "Глоб."));
    localAct_->setObjectName("toolLocal");
    localAct_->setCheckable(true);
    localAct_->setShortcut(Qt::Key_L);
    localAct_->setToolTip(uiText("Kinetica", "Оси гизмо (L): «Глоб.» — оси сцены, «Лок.» — оси самого объекта"));
    connect(localAct_, &QAction::toggled, this, [this](bool on) {
        builder_->sayAction("gizmo_set", QString("editor.gizmo.local = %1").arg(on ? "True" : "False"), {{"local", on}});
        view_->gizmo().setLocal(on);
        localAct_->setText(on ? uiText("Kinetica", "Лок.") : uiText("Kinetica", "Глоб.")); // not «Мир»: that is the inspector's tab of the world's physics
        view_->update();
    });
    // The ruler, a group of its own under a line - it measures and moves nothing: press on a point, drag to another, let
    // go - the line between them with its length, by view_measure, which the drag gives its two points; the line stays
    // until the next one or until the ruler is left.
    static const QString measure = "view_measure";
    tb->addSeparator();
    QAction* ruler = tb->addAction(controlIcon(ControlIcon::ToolRuler, 28), uiText("Kinetica", "Линейка"));
    ruler->setObjectName("toolRuler");
    ruler->setCheckable(true);
    ruler->setShortcut(QKeySequence("M"));
    ruler->setToolTip(uiText("Kinetica", "Линейка (M): нажмите на точку, тяните к другой, отпустите — линия и её длина; Esc — выход"));
    connect(ruler, &QAction::triggered, this, [this, ruler] {
        view_->setPluginTool(ruler->isChecked() ? measure : QString(), uiText("Kinetica", "Линейка"), true);
    });
    connect(view_, &Viewport::pluginToolChanged, ruler, [this, ruler](const QString& op) {
        ruler->setChecked(op == measure);
        if (op != measure && view_->hasDrawing("editor/ruler")) operators()->run(measure, {}, [](const QJsonObject&) {}, "ui");
    });
    connect(view_, &Viewport::toolDragged, this, [this](const QString& op, ke::Vector3 from, ke::Vector3, uint32_t, ke::Vector3 to, uint32_t) {
        const auto three = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
        if (op == measure) operators()->run(measure, {{"point", three(from)}, {"to", three(to)}}, [](const QJsonObject&) {}, "ui");
    });
    setTool(GizmoMode::Translate);
}

// ---------------------------------------------------------------------------
// Scene builder ("Конструктор"): shapes with roles, saved as *.kscene
// ---------------------------------------------------------------------------
// One panel on the right with tabs: "Сцена" (the list of objects),
// "Объект" (the selected object: its card, its geometry, its components) and "Мир" (gravity, the
// solver); plugins add their panels as further tabs. Selecting an object turns to "Объект".
QScrollArea* MainWindow::scrolled(QWidget* content) {
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
    inspectorTabs_->addTab(scrolled(builder_->sceneListPanel()), panelIcon(PanelIcon::Scene), uiText("Kinetica", "Сцена"));
    inspectorTabs_->addTab(scrolled(builder_->inspectorPanel()), panelIcon(PanelIcon::Object), uiText("Kinetica", "Объект"));
    inspectorTabs_->addTab(scrolled(builder_->worldPanel()), panelIcon(PanelIcon::World), uiText("Kinetica", "Мир"));
    inspectorTabs_->setTabToolTip(0, uiText("Kinetica", "Все объекты сцены"));
    inspectorTabs_->setTabToolTip(2, uiText("Kinetica", "Физика всей сцены: гравитация, шаг, контакты, суставы"));
    inspectorTabs_->setTabToolTip(1, uiText("Kinetica", "Выбранный объект: где стоит, как выглядит и что умеет"));
    auto* dock = new QDockWidget(uiText("Kinetica", "Инспектор"), this);
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
    // The collider mode says itself over the view, with its way out - a person did not know what took them back: Esc was
    // only in the status bar's line.
    auto* colliderBar = new QFrame(view_);
    colliderBar->setObjectName("colliderModeBar");
    colliderBar->setStyleSheet("#colliderModeBar { background: rgba(28, 74, 44, 235); border: 1px solid #6ee882; border-radius: 6px; }"
                               "#colliderModeBar QLabel { color: #e8ffe9; background: transparent; }"
                               "#colliderModeBar QPushButton { background: #2f7a45; color: #f0fff1; border: 1px solid #6ee882; "
                               "border-radius: 4px; font-weight: bold; padding: 3px 10px; }"
                               "#colliderModeBar QPushButton:hover { background: #3c9a58; }");
    auto* barRow = new QHBoxLayout(colliderBar);
    barRow->setContentsMargins(12, 5, 6, 5);
    auto* barText = new QLabel(colliderBar);
    auto* done = new QPushButton(uiText("Kinetica", "Готово (Esc)"), colliderBar);
    done->setObjectName("colliderModeDone");
    barRow->addWidget(barText);
    barRow->addWidget(done);
    colliderBar->hide();
    connect(done, &QPushButton::clicked, builder_, [this] { builder_->setColliderEdit(false); });
    connect(builder_, &SceneBuilder::colliderEditChanged, this, [this, colliderBar, barText](bool on) {
        colliderBar->setVisible(on);
        if (!on) return;
        barText->setText(uiText("Kinetica", "<b>Правка коллайдера</b> «") + builder_->objectTitle(builder_->selectedId()) +
                         uiText("Kinetica", "» — точки: размер, гизмо: место и поворот, модель не меняется"));
        colliderBar->adjustSize();
        colliderBar->move((view_->width() - colliderBar->width()) / 2, view_->height() - colliderBar->height() - 12); // the top: the hints
        colliderBar->raise();
    });
    connect(view_, &Viewport::shadowsLeftOut, this, &MainWindow::showShadowBudget);
    connect(view_, &Viewport::lookThroughMoved, builder_, &SceneBuilder::onViewMovedThroughCamera);
    builder_->setViewEye([this] {
        const QVector3D e = view_->camera().eye();
        return ke::Vector3(e.x(), e.y(), e.z());
    });
    connect(view_, &Viewport::imageDropped, builder_, &SceneBuilder::assignImage);
    connect(view_, &Viewport::fileDropped, this, &MainWindow::importFile); // a model or a robot dropped on the view
    view_->setOpensFile([this](const QString& file) {
        const QString lower = file.toLower();
        return lower.endsWith(".obj") || lower.endsWith(".stl") || (plugins_ && !plugins_->openerFor(file).isEmpty());
    });
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
    view_->setObjectPose([this](uint32_t id, ke::Vector3& position, ke::Quaternion& rotation) { return builder_->objectPoseNow(id, position, rotation); });
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
    connect(builder_, &SceneBuilder::shapeHandles, view_, &Viewport::setShapeHandles);
    connect(view_, &Viewport::shapeHandleDragged, builder_, &SceneBuilder::onShapeHandle);
    connect(builder_, &SceneBuilder::pathLines, view_, &Viewport::setPathLines);
}

void MainWindow::buildSceneMenu() {
    auto* menu = new QMenu(uiText("Kinetica", "С&цена"), this);
    menu->addAction(uiText("Kinetica", "Новая пустая сцена"), QKeySequence::New, this, [this] {
        const size_t before = builder_->undoTaken();
        builder_->newScene();
        if (builder_->undoTaken() > before) statusBar()->showMessage(uiText("Kinetica", "Новая сцена. Прежняя не потеряна: Ctrl+Z вернёт её"), 10000);
    });
    menu->addAction(uiText("Kinetica", "Открыть…"), QKeySequence::Open, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, uiText("Kinetica", "Открыть сцену"), examplesDir(), uiText("Kinetica", "Сцена Kinetica (*.kscene)"));
        if (path.isEmpty()) return;
        QString error;
        if (!builder_->openFile(path, error)) QMessageBox::warning(this, uiText("Kinetica", "Открыть сцену"), error);
    });
    menu->addAction(uiText("Kinetica", "Сохранить…"), QKeySequence::Save, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, uiText("Kinetica", "Сохранить сцену"), examplesDir(), uiText("Kinetica", "Сцена Kinetica (*.kscene)"));
        if (path.isEmpty()) return;
        QString error;
        if (!builder_->saveFile(path, error)) return void(QMessageBox::warning(this, uiText("Kinetica", "Сохранить сцену"), error));
        statusBar()->showMessage(uiText("Kinetica", "Сохранено: ") + QDir::toNativeSeparators(path), 8000);
    });
    // New, open and save live in Файл; Сцена is what goes into the scene - a newcomer found it a copy of Файл and no
    // joint in it. The toolbar's lists and the inspector's «+ Добавить компонент» are the same actions here.
    auto* file = menuBar()->findChild<QMenu*>("fileMenu");
    const char* names[] = {"sceneNew", "sceneOpen", "sceneSave"};
    const auto sceneActions = menu->actions();
    const auto anchor = file->actions().front();
    for (int i = 0; i < 3; ++i) {
        sceneActions[i]->setObjectName(names[i]);
        menu->removeAction(sceneActions[i]);
        file->insertAction(anchor, sceneActions[i]);
    }
    auto* gallery = new QAction(uiText("Kinetica", "Примеры…"), file);
    connect(gallery, &QAction::triggered, this, &MainWindow::openGallery);
    file->insertAction(anchor, gallery);
    file->insertSeparator(anchor);
    menu->addSection(uiText("Kinetica", "Добавить"));
    const std::pair<const char*, const char*> lists[] = {{"shapesButton", uiText("Kinetica", "Фигура")}, {"lightsButton", uiText("Kinetica", "Свет")},
                                                         {"camerasButton", uiText("Kinetica", "Камера")}, {"sensorsButton", uiText("Kinetica", "Датчик на выбранное")},
                                                         {"forcesButton", uiText("Kinetica", "Сила")}};
    for (const auto& [button, title] : lists)
        if (auto* b = findChild<QToolButton*>(button); b && b->menu()) {
            QMenu* sub = menu->addMenu(b->icon(), title);
            sub->setToolTipsVisible(true);
            connect(sub, &QMenu::aboutToShow, this, [sub, b] { // the same actions, as the list under the button has them now
                emit b->menu()->aboutToShow();
                sub->clear();
                sub->addActions(b->menu()->actions());
            });
        }
    QMenu* component = menu->addMenu(uiText("Kinetica", "Компонент выбранного: сочленение, датчик…"));
    component->setToolTipsVisible(true);
    connect(component, &QMenu::aboutToShow, this, [this, component] {
        builder_->fillAddMenu(component);
        if (component->isEmpty()) component->addAction(uiText("Kinetica", "Сначала выберите тело (с Ctrl — второе: сочленение между ними)"))->setEnabled(false);
    });
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    menuBar()->insertMenu(viewMenu->menuAction(), menu);
}

// ---------------------------------------------------------------------------
// Results dock: numbers on the left, one chart per quantity on the right
// ---------------------------------------------------------------------------
void MainWindow::buildResultsDock() {
    auto* dock = new QDockWidget(uiText("Kinetica", "Графики"), this);
    dock->setWindowIcon(panelIcon(PanelIcon::Plots)); // on its tab, beside the word
    dock->setObjectName("resultsDock");

    QWidget* plotHost = buildPlots(); // the charts and the quiet bar under them (MainWindowPlots.cpp)

    dock->setWidget(plotHost);
    addDockWidget(Qt::BottomDockWidgetArea, dock); // shown and hidden by «Графики» (F9), and by the first ▶
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
// Every frame the simulation published since the last look is timed by the Laboratory's profiler;
// only the newest of them is drawn (on a slow screen the simulation steps several frames per paint).
void MainWindow::poll() {
    recordFrames(ctrl_->takeReadings()); // every simulated frame into the plots
    const bool ended = ctrl_->rangeEnded(); // asked before the frames are taken: the play range's last frame is among them
    const auto frames = ctrl_->takePublished();
    if (!frames.empty()) {
        labProfile(frames);
        if (sequencer_) sequencer_->setPlayTime(double(frames.back()->time)); // the world plays the sequence: the playhead follows
        onSnapshot(frames.back());
    }
    if (ended) { // only a take's run ends by itself (SceneBuilder::runEnd)
        builder_->playRangeEnded();
        if (ops_ && ops_->recording()) ops_->closeTake(); // the world paused; the take waits for ■ Stop
    }
}

void MainWindow::onSnapshot(const std::shared_ptr<const Snap>& s) {
    if (builder_->editing()) return; // edit mode shows the authored scene (SceneBuilder::editSnapshot), not the simulation
    builder_->onSimulationSnapshot(*s);
    labSync();
    view_->setSnapshot(s);
    updatePlotObjects(*s);

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
        QTimer::singleShot(0, this, [this] { // after the first layout, which puts the Sequencer's tab in front
            setGraphsVisible(true);
            if (auto* dock = findChild<QDockWidget*>("resultsDock")) resizeDocks({dock}, {210}, Qt::Vertical);
        });
    builder_->play();
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------
// «Импорт…»: a model (OBJ, STL) as a shape, or a file a plugin opens (a robot's URDF) - one dialog, the format by the file.
void MainWindow::importModel() {
    QStringList filters{uiText("Kinetica", "3D-модели (*.obj *.stl)")}, masks{"*.obj", "*.stl"};
    for (const QString& f : plugins_->formats()) filters << f, masks << f.section('(', 1).chopped(1).split(' ');
    filters.prepend(uiText("Kinetica", "Всё, что откроется (") + masks.join(' ') + ")");
    const QString path = QFileDialog::getOpenFileName(this, uiText("Kinetica", "Импорт"), examplesDir(), filters.join(";;"));
    if (!path.isEmpty()) importFile(path);
}

// «Выполнить скрипт…»: the operation script_run on a file picked (examples/scripts first); how it ended on the status bar.
void MainWindow::runScript() {
    const QString path = QFileDialog::getOpenFileName(this, uiText("Kinetica", "Выполнить скрипт"), examplesDir() + "/scripts",
                                                      uiText("Kinetica", "Скрипты Python (*.py)"));
    if (path.isEmpty()) return;
    ops_->run("script_run", {{"file", path}}, [this](const QJsonObject& r) {
        statusBar()->showMessage(r.value("ok").toBool() ? QString(uiText("Kinetica", "Скрипт «%1» выполнен")).arg(r.value("script").toString())
                                                        : r.value("error").toString(), 10000);
    }, "ui");
}

void MainWindow::importFile(const QString& path) {
    if (const QString op = plugins_->openerFor(path); !op.isEmpty()) return plugins_->openFile(op, path);
    QString error;
    if (!builder_->importModel(path, error)) QMessageBox::warning(this, uiText("Kinetica", "Модель"), uiText("Kinetica", "Не удалось загрузить модель:\n") + error);
}

void MainWindow::exportCsv() {
    if (plots_->empty()) {
        QMessageBox::information(this, uiText("Kinetica", "Экспорт"), uiText("Kinetica", "Нет данных: запустите расчёт."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, uiText("Kinetica", "Экспорт графиков"), "results.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!plots_->writeCsv(path)) QMessageBox::warning(this, uiText("Kinetica", "Экспорт"), uiText("Kinetica", "Не удалось записать файл."));
    else statusBar()->showMessage(uiText("Kinetica", "Сохранено: ") + path, 4000);
}

void MainWindow::screenshot() {
    QImage img = view_->grabFramebuffer();
    QString def = QString("kinetica_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString path = QFileDialog::getSaveFileName(this, uiText("Kinetica", "Сохранить скриншот"), def, "PNG (*.png)");
    if (!path.isEmpty() && img.save(path)) statusBar()->showMessage(uiText("Kinetica", "Сохранено: ") + path, 4000);
}

void MainWindow::showHelp() {
    QMessageBox box(this);
    box.setWindowTitle(uiText("Kinetica", "Управление и методы"));
    box.setTextFormat(Qt::RichText);
    box.setText(
        uiText("Kinetica", "<h3>Управление</h3>"
        "<b>Пробел</b> — пуск/пауза (над секвенсором — проиграть и остановить секвенцию, как в видеоредакторе), "
        "<b>Shift+пробел</b> — стоп, <b>.</b> — шаг, <b>F</b> — показать выбранное, "
        "<b>Ctrl+K</b> — найти команду.<br>"
        "Камера: <b>зажатое колесо</b> — вращение, <b>Shift+колесо</b> или ПКМ — сдвиг, прокрутка — масштаб "
        "(схема — в «Вид → Управление»).<br>"
        "В правке: <b>щелчок</b> — выбрать (ещё щелчок там же — то, что глубже: датчик, деталь внутри), "
        "<b>тяните тело</b> при инструменте «Перемещение» — едет по полу, стрелки гизмо — по оси.<br>"
        "Добавить в сцену — меню «Сцена»: фигура, свет, камера, датчик, сочленение между двумя выбранными "
        "(движется выбранное последним).<br>"
        "<b>ЛКМ</b> во время игры — рука мыши: тело тянется к курсору пружиной.<br>"
        "Рука UR5e: <b>W S A D</b> — по полу, <b>R F</b> — вверх-вниз, <b>Z X</b> — поворот, <b>пробел</b> — схват; "
        "или щёлкните предмет и нажмите «Рука — к этому предмету» справа.<br><br>"
        "<h3>Мир робота</h3>"
        "Твёрдые тела и суставы считает движок Box3D. Серво и мотор постоянного тока — код робота: момент на "
        "звенья на каждом подшаге. Обратная связь по суставам — через модели энкодеров; истинные позы и углы "
        "движка видят только графики и Лаборатория.<br><br>"
        "<h3>Свои команды — плагины</h3>"
        "«Плагины → Новый плагин…» создаёт файл plugin.py с готовой командой: она сразу становится кнопкой в "
        "«Плагины», строкой в Ctrl+K, вызовом в консоли и инструментом для ИИ-агента. Сохраните файл — плагин "
        "перезапустится. Каждое действие окна видно строкой Python в «Консоли API» — с неё и начинают свою команду. "
        "Подробно — docs/plugins.md."));
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, uiText("Kinetica", "О программе"),
                       uiText("Kinetica", "<b>Kinetica Editor 0.1</b><br>Редактор робототехники: звенья, суставы, приводы, датчики.<br>"
                       "Физика твёрдых тел — Box3D. C++17 · Qt 6 · OpenGL 3.0"));
}
