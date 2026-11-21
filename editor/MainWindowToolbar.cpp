// The big bar on top of the window (see MainWindow.h): what you can create (shapes, a model, the
// lights, a camera), then run+record / pause / stop, then undo / redo, and at the right end the text
// buttons Примеры, Лаборатория, Графики. A shape of «Фигура ▾» is drawn
// with the mouse in the view (CreateTool, ViewportTools.h) and made by one call of scene_add.
//
// A narrow window never cuts a word ("Кол...еры"): the bar gives up room step by step instead:
// first the captioned buttons sit closer together, then the
// create buttons drop their captions (the icon and the tooltip stay), then run / step / undo do too,
// then the icons get smaller. fitMainToolbar() picks the first step at which everything fits, on
// every resize of the window.
#include "MainWindow.h"
#include "UiLanguage.h"
#include "MagnetController.h"

#include "EditorOps.h"
#include "Icons.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QResizeEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QTabWidget>
#include <QDockWidget>
#include <QToolButton>

namespace {

// How a button of the bar looks: `tight` - no minimum width and less padding, `iconOnly` - the
// caption goes too. The button's own style sheet (not a property rule of the window's sheet): setting
// it tells the button that its style changed, so it measures itself again.
void setLook(QToolButton* b, bool tight, bool iconOnly) {
    const Qt::ToolButtonStyle style = iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon;
    const QString sheet = tight ? "QToolButton { min-width: 0; padding: 4px 4px; }" : QString();
    if (b->styleSheet() == sheet && b->toolButtonStyle() == style) return;
    b->setStyleSheet(sheet);
    b->setToolButtonStyle(style);
}

} // namespace

void MainWindow::buildMainToolbar() {
    auto* tb = new QToolBar(uiText("Kinetica", "Создать"), this);
    createBar_ = tb;
    tb->setObjectName("createBar");
    tb->setMovable(false);
    tb->setIconSize(QSize(48, 48));
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addToolBar(Qt::TopToolBarArea, tb);
    std::vector<QAction*> create = shapeCreateActions();
    connect(view_, &Viewport::created, this, &MainWindow::addDrawn);
    auto* modelAct = new QAction(shapeIcon(ke::ShapeKind::Mesh, 48), uiText("Kinetica", "Модель"), this);
    modelAct->setToolTip(uiText("Kinetica", "Модель из файла OBJ / STL: только форма, роль — плитками справа"));
    connect(modelAct, &QAction::triggered, this, &MainWindow::importModel);
    create.push_back(modelAct);
    addShapeButton(tb, create);
    addLightButtons(tb);
    tb->addAction(magnet_->action()); // a link on a face, with a joint (MagnetController)
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(magnet_->action()))) {
        b->setPopupMode(QToolButton::MenuButtonPopup);
        createButtons_.push_back(b); // narrows with the shapes on a narrow window
    }
    addSensorButton(tb);
    addForceButton(tb);
    tb->addSeparator();
    for (QAction* a : {playAct_, recordAct_, pauseAct_, stopAct_, builder_->undoAction(), builder_->redoAction()}) { // a step: «.», Симуляция
        if (a == builder_->undoAction()) tb->addSeparator();
        tb->addAction(a);
        runButtons_.push_back(qobject_cast<QToolButton*>(tb->widgetForAction(a)));
    }
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    addCompactButtons(tb);
}

// Compact text buttons at the right end: the examples, the Laboratory, the graphs. They keep their words at every width.
// The world's physics is the inspector's «Мир» tab (Симуляция → Физика мира… opens it); the colliders' wireframes
// are a switch of «Вид» (MainWindow.cpp, buildMenus).
void MainWindow::addCompactButtons(QToolBar* tb) {
    auto compact = [tb](QToolButton* b) {
        b->setObjectName("compactButton");
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tb->addWidget(b);
    };
    auto* samplesButton = new QToolButton;
    samplesButton->setText(uiText("Kinetica", "Примеры"));
    samplesButton->setToolTip(uiText("Kinetica", "Все готовые сцены картинками: UR5e, рука-манипулятор, текстуры… Нажмите — откроется"));
    connect(samplesButton, &QToolButton::clicked, this, &MainWindow::openGallery);
    compact(samplesButton);
    auto* physicsAction = new QAction(uiText("Kinetica", "Физика мира…"), this); physicsAction->setObjectName("worldPhysics");
    connect(physicsAction, &QAction::triggered, this, [this] {
        auto* dock = findChild<QDockWidget*>("inspectorDock"); dock->show(); dock->raise();
        inspectorTabs_->setCurrentIndex(2);
    });
    physicsAction->setToolTip(uiText("Kinetica", "Физика мира: гравитация, шаг и подшаги, контакты, жёсткость суставов — вкладка «Мир» инспектора"));
    menuBar()->findChild<QMenu*>("simulationMenu")->addAction(physicsAction);
    for (QAction* a : {labAct_, graphsAct_}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        compact(b);
    }
}

// "Фигура ▾": the primitives in one list - the
// cube, sphere, cylinder, cone, plane, then a model from a file - so the bar keeps room for the sensors.
void MainWindow::addShapeButton(QToolBar* tb, const std::vector<QAction*>& shapes) {
    auto* button = new QToolButton;
    button->setObjectName("shapesButton");
    button->setText(uiText("Kinetica", "Фигура ▾"));
    button->setIcon(shapeIcon(ke::ShapeKind::Box, 48));
    button->setIconSize(tb->iconSize());
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setToolTip(uiText("Kinetica", "Создать фигуру: куб, сфера, цилиндр, конус, плоскость или модель из файла. Только форма — роль плитками справа"));
    button->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(button);
    menu->setToolTipsVisible(true);
    for (size_t k = 0; k < shapes.size(); ++k) {
        if (k + 1 == shapes.size()) menu->addSeparator(); // the model: from a file, not a primitive
        menu->addAction(shapes[k]);
    }
    button->setMenu(menu);
    tb->addWidget(button);
    createButtons_.push_back(button);
}

// The primitives of «Фигура ▾»: each starts making its shape with the mouse in the view, step by step and
// one after another. The builder's own create actions still add a shape at
// once over a free spot, as scene_add with no place does for a script or an agent.
std::vector<QAction*> MainWindow::shapeCreateActions() {
    const char* tips[] = {uiText("Kinetica", "Куб: угол основания, противоположный угол, высота — три щелчка (или протяжка и щелчок)"),
                          uiText("Kinetica", "Шар: центр, затем радиус"), uiText("Kinetica", "Цилиндр: центр основания, радиус, высота"),
                          uiText("Kinetica", "Конус: центр основания, радиус, высота"), uiText("Kinetica", "Плоскость: угол, затем противоположный угол")};
    std::vector<QAction*> out;
    for (int k = 0; k < 5; ++k) {
        CreateWhat what;
        what.shape = ke::ShapeKind(k);
        out.push_back(createAction(what, shapeIcon(what.shape, 48), shapeName(what.shape), tips[k], shapeWord(what.shape)));
    }
    return out;
}

QAction* MainWindow::createAction(const CreateWhat& what, const QIcon& icon, const QString& text, const QString& tip, const QString& name) {
    auto* a = new QAction(icon, text, this);
    a->setObjectName("create_" + name);
    a->setToolTip(tip);
    connect(a, &QAction::triggered, this, [this, what] { startCreating(what); });
    return a;
}

void MainWindow::startCreating(const CreateWhat& what) {
    if (!builder_->editing()) return statusBar()->showMessage(uiText("Kinetica", "Создавать можно в режиме правки — остановите сцену"), 6000);
    magnet_->setActive(false);
    statusBar()->clearMessage(); // the hint line says what the mouse does now
    view_->setCreateMode(true, what);
    view_->setFocus(); // Esc and Backspace go to the view at once
}

// The thing made with the mouse as one call of the API, the line a script would write: one undo step, a
// line in the console and the journal; the window selects it.
void MainWindow::addDrawn(const CreateWhat& what, const ke::Vector3& position, const ke::Vector3& size, const ke::Vector3& target) {
    const auto array = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
    QString op = "scene_add", py;
    QJsonObject args;
    if (what.kind == CreateWhat::Kind::Shape) {
        args = {{"shape", shapeWord(what.shape)}, {"position", array(position)}, {"size", array(size)}};
        py = QString("scene.add(\"%1\", position=%2, size=%3)").arg(shapeWord(what.shape), tuple(position), tuple(size));
    } else if (what.kind == CreateWhat::Kind::Light) {
        op = "scene_add_light";
        args = {{"kind", lightWord(what.light)}, {"position", array(position)}};
        py = QString("scene.add_light(\"%1\", position=%2").arg(lightWord(what.light), tuple(position));
        if (what.light != ke::LightKind::Point) args["target"] = array(target), py += ", target=" + tuple(target); // a lamp shines everywhere
        py += ")";
    } else {
        op = "scene_add_camera";
        args = {{"eye", array(position)}, {"target", array(target)}};
        py = QString("scene.add_camera(eye=%1, target=%2").arg(tuple(position), tuple(target));
        if (what.target) { // the object the aim lies on, seen from the camera: its target; else the point
            args["kind"] = "target", py += ", kind=\"target\"";
            const ke::Vector3 along = target - position;
            const float far = ke::length(along);
            const std::vector<PickHit> hits = far > 0 ? view_->pickAll(Ray{position, along / far}) : std::vector<PickHit>();
            const auto first = std::min_element(hits.begin(), hits.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
            if (first != hits.end() && std::fabs(first->t - far) < 0.02f) args["at"] = int(first->id), py += QString(", at=%1").arg(first->id);
        }
        py += ")";
    }
    args["_py"] = py;
    const QJsonObject done = ops_->runNow(op, args);
    if (!done.value("ok").toBool()) statusBar()->showMessage(uiText("Kinetica", "Создание: ") + done.value("error").toString(), 6000);
}

// "Свет ▾" opens its three lights at once (no second click on an arrow); "Камера" beside it. Each is placed
// with the mouse: where it stands, how high, where it looks.
void MainWindow::addLightButtons(QToolBar* tb) {
    auto* lights = new QToolButton;
    lights->setObjectName("lightsButton");
    lights->setText(uiText("Kinetica", "Свет ▾"));
    lights->setIcon(sceneIcon(SceneIcon::Bulb, 48));
    lights->setIconSize(tb->iconSize());
    lights->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    lights->setToolTip(uiText("Kinetica", "Свет: солнце, лампа или прожектор. Физика его не видит — он для глаза и скриншотов"));
    lights->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(lights);
    menu->setToolTipsVisible(true);
    const char* tips[] = {uiText("Kinetica", "Солнце: место, высота, куда светит — важно только направление; с тенями"),
                          uiText("Kinetica", "Лампа: место, затем высота; светит во все стороны"), uiText("Kinetica", "Прожектор: место, высота, куда светит — конус света")};
    for (int k = 0; k < 3; ++k) {
        CreateWhat what;
        what.kind = CreateWhat::Kind::Light;
        what.light = ke::LightKind(k);
        menu->addAction(createAction(what, sceneIcon(lightIcon(what.light), 48), lightName(what.light), tips[k], lightWord(what.light)));
    }
    lights->setMenu(menu);
    tb->addWidget(lights);
    createButtons_.push_back(lights);
    // Two kinds: a free camera looks where it is turned; a target camera always looks at its
    // target - the object its third click aims at (a robot's gripper: the camera follows it in play), or a point.
    auto* cameras = new QToolButton;
    cameras->setObjectName("camerasButton");
    cameras->setText(uiText("Kinetica", "Камера ▾"));
    cameras->setIcon(sceneIcon(SceneIcon::Camera, 48));
    cameras->setIconSize(tb->iconSize());
    cameras->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    cameras->setPopupMode(QToolButton::InstantPopup);
    cameras->setToolTip(uiText("Kinetica", "Камера: свободная или с целью. Место, высота, куда смотрит. Через неё — вид и скриншоты"));
    auto* cameraMenu = new QMenu(cameras);
    CreateWhat camera, aimed;
    camera.kind = aimed.kind = CreateWhat::Kind::Camera;
    aimed.target = true;
    cameraMenu->addAction(createAction(camera, sceneIcon(SceneIcon::Camera, 48), uiText("Kinetica", "Свободная камера"),
                                       uiText("Kinetica", "Свободная: смотрит туда, куда повёрнута. Место, высота, куда смотрит"), "camera"));
    cameraMenu->addAction(createAction(aimed, sceneIcon(SceneIcon::Camera, 48), uiText("Kinetica", "Камера с целью"),
                                       uiText("Kinetica", "С целью: всегда смотрит на цель — объект под третьим щелчком (схват робота: камера следит за ним в игре) "
                                       "или точку. Двигаете камеру — она продолжает смотреть на цель"), "camera_target"));
    cameras->setMenu(cameraMenu);
    tb->addWidget(cameras);
    createButtons_.push_back(cameras);
}

// "Датчик ▾": a sensor has no shape of its own - it is put on the selected shape (a face toward the view) or on the
// joint the shape hangs on, as a property.
void MainWindow::addSensorButton(QToolBar* tb) {
    auto* sensors = new QToolButton;
    sensors->setObjectName("sensorsButton");
    sensors->setText(uiText("Kinetica", "Датчик ▾"));
    sensors->setIcon(deviceIcon(DeviceIcon::Sensor, 48));
    sensors->setIconSize(tb->iconSize());
    sensors->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    sensors->setToolTip(uiText("Kinetica", "Датчик на выбранное тело или на его сочленение: без своей формы, накидывается как свойство"));
    sensors->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(sensors);
    menu->setToolTipsVisible(true);
    const std::vector<QAction*>& kinds = builder_->sensorActions();
    menu->addSection(uiText("Kinetica", "На тело"));
    for (int k : {1, 2, 3}) // IMU, sonar, touch
        if (size_t(k) < kinds.size()) menu->addAction(kinds[size_t(k)]);
    menu->addSection(uiText("Kinetica", "На сочленение тела"));
    for (int k : {0, 4, 5}) // encoder, wrench, effort
        if (size_t(k) < kinds.size()) menu->addAction(kinds[size_t(k)]);
    connect(menu, &QMenu::aboutToShow, builder_, &SceneBuilder::updateSensorActions);
    sensors->setMenu(menu);
    tb->addWidget(sensors);
    createButtons_.push_back(sensors);
}

// "Сила ▾": the four forces, each added at once (over the selection, else at the scene's centre), like a sensor.
void MainWindow::addForceButton(QToolBar* tb) {
    auto* forces = new QToolButton;
    forces->setObjectName("forcesButton");
    forces->setText(uiText("Kinetica", "Сила ▾"));
    forces->setIcon(sceneIcon(SceneIcon::Force, 48));
    forces->setIconSize(tb->iconSize());
    forces->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    forces->setToolTip(uiText("Kinetica", "Сила: толчок или взрыв разом, тяга или притяжение всё время. Действует на тела, пока идёт мир"));
    forces->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(forces);
    menu->setToolTipsVisible(true);
    struct Kind { const char* name; ke::ForceShape shape; ke::ForceMode mode; const char* text; };
    const Kind kinds[] = {
        {"addForcePush", ke::ForceShape::Directional, ke::ForceMode::Impulse, uiText("Kinetica", "Толчок")},
        {"addForceBlast", ke::ForceShape::Radial, ke::ForceMode::Impulse, uiText("Kinetica", "Взрыв")},
        {"addForceThrust", ke::ForceShape::Directional, ke::ForceMode::Constant, uiText("Kinetica", "Тяга")},
        {"addForcePull", ke::ForceShape::Radial, ke::ForceMode::Constant, uiText("Kinetica", "Притяжение")}};
    const char* tips[] = {uiText("Kinetica", "Толчок: импульс, Н·с — срабатывает один раз. С выбранным телом прикладывается к нему: стрелка из его центра масс"),
                          uiText("Kinetica", "Взрыв: импульс, Н·с — один раз, от своей точки во все стороны, телам внутри сферы"),
                          uiText("Kinetica", "Тяга: сила, Н — действует всё время игры. С выбранным телом прикладывается к нему и едет с ним"),
                          uiText("Kinetica", "Притяжение: сила, Н — всё время, к своей точке, на тела внутри сферы")};
    for (int k = 0; k < 4; ++k) {
        auto* a = menu->addAction(sceneIcon(forceIcon(kinds[k].shape), 48), kinds[k].text);
        a->setObjectName(kinds[k].name);
        a->setToolTip(tips[k]);
        connect(a, &QAction::triggered, this, [this, shape = kinds[k].shape, mode = kinds[k].mode] { builder_->addForce(shape, mode); });
    }
    forces->setMenu(menu);
    tb->addWidget(forces);
    createButtons_.push_back(forces);
}

// One step of giving up room (see the head of the file): 0 every caption, roomy; 1 every caption,
// tight; 2 the create buttons icons only; 3 run / step / undo too; 4 icons of 32 px instead of 48.
void MainWindow::setToolbarLevel(int level) {
    const int icon = level >= 4 ? 32 : 48;
    createBar_->setIconSize(QSize(icon, icon));
    for (QToolButton* b : createButtons_) {
        if (!b) continue;
        b->setIconSize(QSize(icon, icon));
        setLook(b, level >= 1, level >= 2);
    }
    for (QToolButton* b : runButtons_)
        if (b) setLook(b, level >= 1, level >= 3);
    // Keep the settings buttons captioned, but give them less padding on narrow windows.
    for (QToolButton* b : createBar_->findChildren<QToolButton*>("compactButton")) {
        const QString sheet = level >= 1 ? QString("QToolButton { min-width: 0; padding: 6px %1px; margin-left: 2px; }")
                                                .arg(level >= 4 ? 6 : 8) : QString();
        if (b->styleSheet() != sheet) b->setStyleSheet(sheet);
    }
}

// The first step at which the whole bar fits the window's width.
void MainWindow::fitMainToolbar() {
    if (!createBar_) return;
    const int room = width() - 8; // the bar's own frame
    for (int level = 0; level <= 4; ++level) {
        setToolbarLevel(level);
        createBar_->layout()->invalidate();
        if (createBar_->sizeHint().width() <= room) return;
    }
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (e->size().width() != e->oldSize().width()) fitMainToolbar();
}
