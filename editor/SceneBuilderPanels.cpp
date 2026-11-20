// The scene builder's two tabs (see SceneBuilder.h): building the widgets, and copying between the
// scene graph and the widgets. The editing logic is in SceneBuilder.cpp, the collider wireframes in
// SceneBuilderColliders.cpp, several selected objects in SceneBuilderMulti.cpp. The scene list is a
// tree: a group holds its members, an array its hidden template; ⧉ marks instances and their master.
#include "SceneBuilder.h"
#include "scene/Collider.h"

#include "IkSolverView.h"
#include "scene/Assembly.h"
#include "TexturePanel.h"
#include "WorldPanel.h"
#include "RuPlural.h"

#include "InspectorWidgets.h"
#include "ManyPanels.h"
#include "ObjectInspector.h"
#include "RoleBar.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTreeWidget>

#include <map>
#include <set>

using namespace ke;

namespace {

// What the thing is, in words, for the scene list: "Куб 1 — твёрдое, коллайдер".
QString rolesText(const Entity& e) {
    QStringList roles;
    for (int k = 0; k < int(RoleIcon::Count); ++k)
        if (roleEnabled(e, RoleIcon(k))) roles << roleName(RoleIcon(k)).toLower();
    if (e.rigid.enabled && e.rigid.fixed) roles.replaceInStrings("твёрдое", "неподвижное");
    if (!e.rigid.enabled && e.collider.enabled) roles.replaceInStrings("коллайдер", "препятствие");
    const QString name = QString::fromStdString(e.name);
    return roles.isEmpty() ? name : name + " — " + roles.join(", ");
}

QVBoxLayout* column(QWidget* w, int margin) {
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(margin, margin, margin, margin);
    col->setSpacing(8);
    return col;
}

QLabel* hintLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("roleRowLabel");
    l->setWordWrap(true);
    return l;
}

const char* kColliderTips[] = {
    "Авто: сталкивается самой формой — коробка для куба, шар для сферы, оболочка для остального",
    "Коробка: самый быстрый коллайдер; ящики, стены, всё прямоугольное",
    "Сфера: катится; мячи, камни, всё круглое",
    "Капсула: цилиндр с полусферами на концах; персонажи, бочки, бутылки, звенья руки",
    "Выпуклая оболочка: форма, натянутая как плёнка на геометрию, без вмятин",
    "Цилиндр: круглый цилиндр по оси y (многогранник из 24 граней) — колёса, валы, стаканы"};
static_assert(sizeof(kColliderTips) / sizeof(kColliderTips[0]) == int(ColliderKind::Cylinder) + 1, "a tip for every collider kind");

QString colliderName(ColliderKind k) {
    static const char* names[] = {"Авто", "Коробка", "Сфера", "Капсула", "Выпуклая оболочка", "Цилиндр"};
    return names[int(k)];
}

QString collisionShapeName(ShapeDesc::Kind k) {
    switch (k) {
    case ShapeDesc::Kind::Box: return "коробка";
    case ShapeDesc::Kind::Sphere: return "сфера";
    case ShapeDesc::Kind::Capsule: return "капсула";
    case ShapeDesc::Kind::Hull: return "выпуклая оболочка";
    }
    return "?";
}

} // namespace

// ---------------------------------------------------------------------------
// Actions: the big "create" buttons and undo / redo
// ---------------------------------------------------------------------------
void SceneBuilder::buildActions() {
    const char* tips[] = {"Куб 20 см: появится в воздухе; только форма — компоненты справа", "Шар 20 см в воздухе", "Цилиндр в воздухе",
                          "Конус в воздухе", "Плоскость 3 × 3 м: пол, стол, наклонная плоскость"};
    for (int k = 0; k < 5; ++k) {
        const ShapeKind shape = ShapeKind(k);
        auto* a = new QAction(shapeIcon(shape, 48), shapeName(shape), this);
        a->setToolTip(tips[k]);
        connect(a, &QAction::triggered, this, [this, shape] { addEntity(shape); });
        createActions_.push_back(a);
    }
    undoAct_ = new QAction(controlIcon(ControlIcon::Undo, 48), "Отменить", this);
    undoAct_->setShortcut(QKeySequence::Undo);
    redoAct_ = new QAction(controlIcon(ControlIcon::Redo, 48), "Повторить", this);
    redoAct_->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
    connect(undoAct_, &QAction::triggered, this, &SceneBuilder::undo);
    connect(redoAct_, &QAction::triggered, this, &SceneBuilder::redo);
    deselectAct_ = new QAction("Снять выделение", this);
    connect(deselectAct_, &QAction::triggered, this, [this] { setSelected(0); });
}

// ---------------------------------------------------------------------------
// The "Сцена" tab: everything in the scene, with the eye and the lock; the world below
// ---------------------------------------------------------------------------
void SceneBuilder::buildSceneList() {
    listPanel_ = new QWidget;
    listPanel_->setObjectName("builderPanel");
    auto* col = column(listPanel_, 8);
    list_ = new QTreeWidget;
    list_->setMinimumHeight(150);
    list_->setColumnCount(3);
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(true); // groups open and shut
    list_->setObjectName("sceneList");
    list_->setIconSize(QSize(22, 22));
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c : {1, 2}) {
        list_->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        list_->setColumnWidth(c, 28);
    }
    list_->setToolTip("Всё, что есть в сцене. Глаз — видимость, замок — мышь не трогает. Delete — удалить. "
                      "⧉ — экземпляр: правка одного меняет все");
    connect(list_, &QTreeWidget::itemClicked, this, &SceneBuilder::onListClicked);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection); // every selected object is highlighted
    connect(list_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        const bool adding = QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
        if (item && !filling_ && !adding) setSelected(item->data(0, Qt::UserRole).toUInt()); // the arrow keys
    });
    auto* del = new QShortcut(QKeySequence::Delete, list_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &SceneBuilder::removeSelected);
    col->addWidget(list_, 1);

    auto* row = new QHBoxLayout;
    auto* dup = new QPushButton("Дублировать");
    auto* rem = new QPushButton("Удалить");
    connect(dup, &QPushButton::clicked, this, &SceneBuilder::duplicateSelected);
    connect(rem, &QPushButton::clicked, this, &SceneBuilder::removeSelected);
    row->addWidget(dup);
    row->addWidget(rem);
    col->addLayout(row);
    const auto showFor = [dup, rem](uint32_t id) { dup->setVisible(id != 0), rem->setVisible(id != 0); }; // nothing selected: nothing to act on
    connect(this, &SceneBuilder::selectionChanged, dup, showFor);
    showFor(0);
}

void SceneBuilder::buildWorldPanel() {
    worldPanel_ = new WorldPanel;
    connect(worldPanel_, &WorldPanel::settingsEdited, this, &SceneBuilder::onWorldEdited);
}

// ---------------------------------------------------------------------------
// The "Объект" tab: palette, "Объект", "Геометрия", the components (the collider first), "+ Добавить
// компонент", "Текстура"
// ---------------------------------------------------------------------------
void SceneBuilder::buildInspector() {
    inspectorPanel_ = new QWidget;
    inspectorPanel_->setObjectName("builderPanel");
    auto* col = column(inspectorPanel_, 8);
    banner_ = new InlineBanner;
    col->addWidget(banner_);
    playHint_ = new QLabel;
    playHint_->setObjectName("emptyHint");
    playHint_->setWordWrap(true);
    playHint_->setVisible(false);
    col->addWidget(playHint_);
    emptyHint_ = new QLabel("Выберите объект в сцене\nили создайте новый кнопкой сверху.");
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setObjectName("emptyHint");
    col->addWidget(emptyHint_);
    inspectorBody_ = new QWidget;
    auto* body = column(inspectorBody_, 0);
    buildHeader(body);
    roleBar_ = new RoleBar;
    connect(roleBar_, &RoleBar::roleClicked, this, &SceneBuilder::toggleRole);
    body->addWidget(roleBar_);
    buildObjectCard(body);
    buildManyCards(body);
    buildLightCards(body);
    buildSensorCard(body);
    shapePart_ = new QWidget; // what only a shape has
    auto* shape = column(shapePart_, 0);
    buildGeometryCard(shape);
    shape->addWidget(sectionTitle("Компоненты"));
    buildColliderCard(shape); // right under the shape: what the body looks like, then what it collides with
    buildMaterialCards(shape);
    buildAddComponent(shape);
    buildTextureCard(shape);
    body->addWidget(shapePart_);
    col->addWidget(inspectorBody_);
    buildDeviceCards(col);
    col->addStretch(1);
}

void SceneBuilder::buildHeader(QVBoxLayout* col) {
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    headerIcon_ = new QLabel;
    headerIcon_->setFixedSize(44, 44);
    headerName_ = new QLabel;
    headerName_->setObjectName("headerName");
    headerChips_ = new QWidget;
    auto* chips = new QHBoxLayout(headerChips_);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(2);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(headerName_);
    text->addWidget(headerChips_);
    row->addWidget(headerIcon_);
    row->addLayout(text, 1);
    col->addLayout(row);
}

// What every thing in a scene has (SceneObject), and for a shape its size.
void SceneBuilder::buildObjectCard(QVBoxLayout* col) {
    object_ = new ObjectInspector;
    connect(object_, &ObjectInspector::edited, this, &SceneBuilder::onObjectEdited);
    col->addWidget(object_);
    sizeRow_ = new QWidget;
    auto* f = new QFormLayout(sizeRow_);
    f->setContentsMargins(0, 0, 0, 0);
    f->setSpacing(6);
    size_ = vectorField(f, "Размер, м", 0.005, 20, 0.05, 3); // millimetres, as the place
    col->addWidget(sizeRow_);
}

// The link of an instance, and the cards of a group and of an array.
void SceneBuilder::buildManyCards(QVBoxLayout* col) {
    instanceLink_ = new InlineBanner;
    instanceLink_->setObjectName("instanceLink");
    col->addWidget(instanceLink_);
    groupCard_ = new ComponentCard("Группа", objectIcon(ObjectIcon::Group, 22));
    groupCard_->setObjectName("groupCard");
    groupPanel_ = new GroupPanel;
    groupCard_->body()->addWidget(groupPanel_);
    connect(groupCard_, &ComponentCard::removeClicked, this, &SceneBuilder::ungroupSelected);
    connect(groupPanel_, &GroupPanel::ungroupClicked, this, &SceneBuilder::ungroupSelected);
    connect(groupPanel_, &GroupPanel::gluedToggled, this, [this](bool on) { setGlued(selectedId_, on); });
    col->addWidget(groupCard_);
    arrayCard_ = new ComponentCard("Массив", objectIcon(ObjectIcon::ArrayLine, 22));
    arrayCard_->setObjectName("arrayCard");
    arrayCard_->body()->addWidget(arrayFields());
    auto* explode = new QPushButton("Разобрать на объекты");
    explode->setObjectName("explodeArray");
    explode->setToolTip("Каждая копия станет отдельным объектом на своём месте");
    connect(explode, &QPushButton::clicked, this, [this] { explodeArray(selectedId_); });
    arrayCard_->body()->addWidget(explode);
    connect(arrayCard_, &ComponentCard::removeClicked, this, [this] { explodeArray(selectedId_); });
    col->addWidget(arrayCard_);
}

// The pattern as three icon buttons, and only its own fields: Ряд - how many and the step between two copies;
// Сетка - how many along x, y, z and the spacing; Круг - how many and the radius.
FieldForm* SceneBuilder::arrayFields() {
    ArrayObject* a = &arrayDraft_;
    const auto grid = [a] { return a->pattern == ArrayPattern::Grid; };
    const auto circle = [a] { return a->pattern == ArrayPattern::Circle; };
    arrayFields_ = new FieldForm([this](bool click) { commitDraft(arrayById(selectedId_), arrayDraft_, click); });
    arrayFields_->tiles({{objectIcon(ObjectIcon::ArrayLine, 32), "Ряд"}, {objectIcon(ObjectIcon::ArrayGrid, 32), "Сетка"},
                         {objectIcon(ObjectIcon::ArrayCircle, 32), "Круг"}}, 32,
                        [a] { return double(int(a->pattern)); }, [a](double p) {
                            a->pattern = ArrayPattern(int(p));
                            if (a->pattern != ArrayPattern::Grid) a->count[1] = a->count[2] = 1;
                        }).name("arrayPattern")
        .number("Сколько", 1, 1000, 1, 0, a->count[0]).shown([=] { return !grid(); })
        .vector("Сколько", 1, 1000, 1, 0, [a] { return Vector3(Real(a->count[0]), Real(a->count[1]), Real(a->count[2])); },
                [a](const Vector3& n) { for (int k = 0; k < 3; ++k) a->count[k] = std::max(1, int(std::lround(n[k]))); }).shown(grid)
        .vector("Шаг, м", -50, 50, 0.05, 3, a->step).label([=] { return QString(grid() ? "Расстояние, м" : "Шаг, м"); })
        .shown([=] { return !circle(); })
        .number("Радиус, м", 0.01, 50, 0.1, 2, a->radius).shown(circle)
        .vector("Поворот каждой, °", -360, 360, 5, 1, a->rotationStepDeg)
        .number("Разброс, м", 0, 5, 0.01, 3, a->jitter).tip("Каждая копия сдвинута на случайное расстояние до этого (одно и то же при каждом запуске)")
        .note([this, a] {
            const SceneObject* t = findObject(graph_, a->templateId);
            return "Копии формы «" + (t ? QString::fromStdString(t->name) : QString("?")) + "» (она скрыта: правьте её в списке — изменятся все копии)";
        }).name("roleRowLabel");
    return arrayFields_;
}

// What the object is shaped like: physics never changes it, but takes from it the mass and the Auto collider.
void SceneBuilder::buildGeometryCard(QVBoxLayout* col) {
    col->addWidget(sectionTitle("Геометрия"));
    col->addWidget(hintLabel("форма объекта: из неё физика берёт массу и коллайдер «Авто»"));
    auto* f = new QFormLayout;
    f->setSpacing(6);
    shape_ = new QComboBox;
    shape_->setIconSize(QSize(20, 20));
    shape_->setToolTip("Форма — то, что видно. Чем объект сталкивается, задаёт компонент «Коллайдер»");
    for (int k = 0; k < 6; ++k) shape_->addItem(shapeIcon(ShapeKind(k), 20), shapeName(ShapeKind(k)));
    connect(shape_, &QComboBox::currentIndexChanged, this, &SceneBuilder::onDetailsEdited);
    f->addRow("Форма", shape_);
    goalButton_ = new QPushButton("Цель IK — рука тянется сюда");
    goalButton_->setObjectName("addGoal");
    connect(goalButton_, &QPushButton::clicked, this, [this] { addGoalToSelected(); });
    f->addRow(goalButton_);
    grip_ = new QPushButton;
    grip_->setObjectName("gripToggle");
    grip_->setToolTip("Пальцы схвата этой цели: сжать — каждый палец едет до упора в предмет и жмёт с силой 2 Н, "
                      "разжать — назад. Работает и в правке, и во время игры (команда assembly_grip)");
    grip_->setVisible(false);
    connect(grip_, &QPushButton::clicked, this, &SceneBuilder::toggleGrip);
    f->addRow(grip_);
    ikView_ = new IkSolverView;
    ikView_->setVisible(false);
    f->addRow(ikView_);
    modelFile_ = new QLabel;
    modelFile_->setObjectName("roleRowLabel");
    modelFile_->setWordWrap(true);
    f->addRow(modelFile_);
    col->addLayout(f);
}

// How the surface looks: a texture and its relief - below the components, it changes nothing physical.
void SceneBuilder::buildTextureCard(QVBoxLayout* col) {
    col->addWidget(sectionTitle("Текстура"));
    texture_ = new TexturePanel;
    col->addWidget(texture_);
    connect(texture_, &TexturePanel::edited, this, &SceneBuilder::onDetailsEdited);
}

ComponentCard* SceneBuilder::componentCard(QVBoxLayout* col, RoleIcon role) {
    auto* card = new ComponentCard(roleTitle(role), roleIcon(role, 22));
    card->setToolTip(roleTip(role));
    connect(card, &ComponentCard::removeClicked, this, [this, role] { toggleRole(role); });
    cards_[int(role)] = card;
    col->addWidget(card);
    return card;
}

Entity* SceneBuilder::masterById(uint32_t id) { return entityById(id) ? entityById(masterOf(id)) : nullptr; }

FieldForm* SceneBuilder::shapeFields(ComponentCard* card, FieldForm* SceneBuilder::*slot) {
    this->*slot = new FieldForm([this, slot](bool click) { commitSelected(this->*slot, shapeDraft_, [this](uint32_t id) { return masterById(id); }, click); });
    card->body()->addWidget(this->*slot);
    return this->*slot;
}

Vec3Row* SceneBuilder::vectorField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals) {
    auto* r = new Vec3Row(min, max, step, decimals);
    connect(r, &Vec3Row::edited, this, &SceneBuilder::onDetailsEdited);
    f->addRow(label, r);
    return r;
}

void SceneBuilder::buildMaterialCards(QVBoxLayout* col) {
    ComponentCard* rigid = componentCard(col, RoleIcon::Rigid);
    noCollider_ = new InlineBanner;
    rigid->body()->addWidget(noCollider_);
    RigidRole* r = &shapeDraft_.rigid;
    shapeFields(rigid, &SceneBuilder::rigidFields_)->number("Плотность, кг/м³", 1, 30000, 50, 0, r->density).name("rigidDensity")
        .number("Трение", 0, 2, 0.05, 2, r->friction)
        .number("Упругость удара", 0, 1, 0.05, 2, r->restitution)
        .toggle("неподвижное (стена, стол)", r->fixed)
        .vector("Скорость, м/с", -100, 100, 0.5, 2, r->velocity)
        .vector("Вращение, рад/с", -200, 200, 0.5, 2, r->angularVelocity);
}

// What the body collides with, apart from what it looks like (Houdini's collision guide, Unity's Collider beside its
// MeshRenderer): Авто - the geometry itself - or a kind fitted to the geometry or sized by hand. Choosing it never
// changes the geometry; what the physics engine gets of it is ke::colliderShapes (sim/RobotWorld.h).
void SceneBuilder::buildColliderCard(QVBoxLayout* col) {
    ColliderRole* c = &shapeDraft_.collider;
    const auto manual = [c] { return c->kind != ColliderKind::Auto && !c->fitToGeometry; };
    const auto round = [c] { return c->kind == ColliderKind::Sphere || c->kind == ColliderKind::Capsule || c->kind == ColliderKind::Cylinder; };
    const auto tall = [c] { return c->kind == ColliderKind::Capsule || c->kind == ColliderKind::Cylinder; };
    std::vector<FieldForm::Tile> kinds;
    for (int k = 0; k <= int(ColliderKind::Cylinder); ++k) kinds.push_back({colliderIcon(ColliderKind(k), 28), {}, kColliderTips[k]});
    ComponentCard* card = componentCard(col, RoleIcon::Collider);
    shapeFields(card, &SceneBuilder::colliderFields_)->note([] { return QString("чем тело сталкивается; виден зелёным каркасом"); }).name("roleRowLabel")
        .tip("Как в Houdini: геометрия — то, что видно; коллайдер — то, чем тело ударяется о других.\n"
             "Физика двигает и поворачивает геометрию вместе с коллайдером, но саму геометрию не меняет.")
        .tiles(kinds, 28, c->kind).name("colliderButton")
        .note([this, c] {
            if (c->kind != ColliderKind::Auto) return colliderName(c->kind);
            const std::vector<ShapeDesc>& made = colliderOf(shapeDraft_);
            return made.empty() ? QString("Авто — по форме геометрии") : "Авто → " + collisionShapeName(made.front().kind);
        }).name("colliderKindName")
        // Taken off, the fit leaves the numbers it made (scene/Collider.h): the collider stays where it was, not 0.2 m.
        .toggle("Подогнать под геометрию", [c] { return c->fitToGeometry ? 1 : 0; },
                [this, c](int on) {
                    if (!on && c->fitToGeometry) {
                        const ColliderRole held = heldCollider(shapeDraft_, entityLocalMesh(shapeDraft_, graph_.baseDirectory));
                        if (sizedByNumbers(held.kind)) c->size = held.size, c->offset = held.offset;
                    }
                    c->fitToGeometry = on != 0;
                }).shown([c] { return c->kind != ColliderKind::Auto; })
        .tip("Размер коллайдера берётся из размеров формы; снимите, чтобы задать его руками — или «Править в виде»: точки и гизмо")
        // Each kind its own numbers: a sphere's three equal edges were three fields of which one worked.
        .vector("Размер, м", 0.005, 50, 0.05, 3, c->size).shown([=] { return manual() && c->kind == ColliderKind::Box; })
        .number("Диаметр, м", 0.005, 50, 0.01, 3, c->size.x).shown([=] { return manual() && round(); }).name("colliderDiameter")
        .number("Высота, м", 0.005, 50, 0.01, 3, c->size.y).shown([=] { return manual() && tall(); }).name("colliderHeight")
        .vector("Сдвиг, м", -50, 50, 0.05, 3, c->offset).shown(manual)
        .vector("Поворот, °", -360, 360, 5, 1, c->rotationDeg).shown(manual);
    // The collider in the view (Unity's «Edit Collider»): the gizmo and the points hold it, the model stays.
    colliderEditButton_ = new QPushButton("Править коллайдер в виде");
    colliderEditButton_->setObjectName("colliderEdit");
    colliderEditButton_->setCheckable(true);
    // On, it is lit in the collider's green, as the bar over the view and the wireframe: the mode is seen where it was entered.
    colliderEditButton_->setStyleSheet("QPushButton:checked { background: #2f7a45; color: #f0fff1; border: 1px solid #6ee882; "
                                       "border-radius: 4px; font-weight: bold; padding: 4px; }");
    colliderEditButton_->setToolTip("Гизмо и точки в виде двигают, вращают и растягивают коллайдер, а не модель: точка на грани тянет "
                                    "грань, точка на круге — диаметр, на торце — высоту. Esc — назад к телу");
    connect(colliderEditButton_, &QPushButton::toggled, this, [this](bool on) { setColliderEdit(on); });
    connect(this, &SceneBuilder::colliderEditChanged, colliderEditButton_, [this](bool on) {
        const QSignalBlocker quiet(colliderEditButton_);
        colliderEditButton_->setChecked(on);
        colliderEditButton_->setText(on ? "Готово: назад к телу (Esc)" : "Править коллайдер в виде"); // the way back where the way in was
    });
    card->body()->addWidget(colliderEditButton_);
}

// "+ Добавить компонент": the components the object does not have yet, with their icons.
void SceneBuilder::buildAddComponent(QVBoxLayout* col) {
    addComponent_ = new QPushButton("+ Добавить компонент");
    addComponent_->setObjectName("addComponent");
    addMenu_ = new QMenu(addComponent_);
    addMenu_->setObjectName("addComponentMenu");
    connect(addMenu_, &QMenu::aboutToShow, this, [this] { fillAddMenu(addMenu_); });
    addComponent_->setMenu(addMenu_);
    col->addWidget(addComponent_);
}

void SceneBuilder::fillAddMenu(QMenu* menu) {
    menu->clear();
    const Entity* e = selectedEntity();
    if (!e) return;
    const std::vector<RoleIcon> all = commonRoles(); // several selected: added to every one of them
    for (RoleIcon role : {RoleIcon::Rigid, RoleIcon::Collider}) {
        if (std::find(all.begin(), all.end(), role) != all.end()) continue;
        QAction* a = menu->addAction(roleIcon(role, 24), roleTitle(role));
        a->setToolTip(roleTip(role));
        connect(a, &QAction::triggered, this, [this, role] { toggleRole(role); });
    }
    if (!menu->isEmpty()) menu->addSeparator();
    if (selection_.size() <= 2) fillJointMenu(*e, menu); // to the world, or between the two selected
    if (selection_.size() == 1) fillSensorMenu(*e, menu);
}

// ---------------------------------------------------------------------------
// Graph <-> widgets
// ---------------------------------------------------------------------------
// The active object's values (an instance: its master's geometry and components); with several
// selected, "—" where they differ, the component cards of what all of them have, and the shape's
// part only when every selected thing is a shape.
void SceneBuilder::fillInspector() {
    const SceneObject* o = selectedObject();
    const bool device = !o && isDevice(selectedId_); // a battery or a driver: its own card
    emptyHint_->setVisible(!o && !device);
    inspectorBody_->setVisible(o != nullptr);
    deviceBody_->setVisible(device);
    if (device) fillDeviceCards();
    if (!o) return refreshBanner();
    filling_ = true;
    clearMixed(inspectorBody_);
    const Entity* own = entityById(o->id);
    Entity shown = own ? resolveInstance(graph_, *own) : Entity();
    if (!own) static_cast<SceneObject&>(shown) = *o;
    const bool shapes = own && selectedEntities().size() == selection_.size();
    object_->setObject(shown);
    sizeRow_->setVisible(shapes);
    shapePart_->setVisible(shapes);
    roleBar_->setVisible(shapes);
    if (shapes) {
        const QSignalBlocker quiet(shape_);
        shape_->setCurrentIndex(int(shown.shape));
        // "Модель" needs a file: offered only to an imported model (button "Модель" on the toolbar).
        if (auto* model = qobject_cast<QStandardItemModel*>(shape_->model()))
            model->item(int(ShapeKind::Mesh))->setEnabled(shown.shape == ShapeKind::Mesh);
        modelFile_->setVisible(shown.shape == ShapeKind::Mesh);
        modelFile_->setText("Файл: " + QFileInfo(QString::fromStdString(shown.meshFile)).fileName());
        texture_->setTexture(shown.texture, QString::fromStdString(graph_.baseDirectory), shown.size);
        size_->setValue(shown.size);
        fillDetails();
        roleBar_->setRoles(shown);
    }
    fillManyCards(*o);
    fillLightCards(*o);
    fillSensorCard(*o);
    shown_ = shown;
    if (selection_.size() > 1) markMixed();
    shown_ = readWidgets(); // what the widgets show, "—" included: an edit is what differs from it
    filling_ = false;
    refreshSections();
    refreshHeader();
    refreshBanner();
}

void SceneBuilder::fillManyCards(const SceneObject& o) {
    const bool one = selection_.size() == 1;
    const Group* g = groupById(o.id);
    const ArrayObject* a = arrayById(o.id);
    groupCard_->setVisible(one && g);
    arrayCard_->setVisible(one && a);
    if (one && g) groupPanel_->setGroup(*g, int(childrenOf(g->id).size()));
    if (one && a) arrayDraft_ = *a, arrayFields_->fill();
    const Entity* e = entityById(o.id);
    int instances = 0;
    for (const Entity& x : graph_.entities) instances += x.id != o.id && masterOf(x.id) == o.id;
    if (one && e && e->instanceOf != 0 && masterOf(e->id) != e->id) {
        const uint32_t id = e->id;
        instanceLink_->showMessage("Экземпляр: связан с «" + QString::fromStdString(entityById(masterOf(id))->name) +
                                       "» — форма и компоненты общие", "Отвязать", [this, id] { unlinkInstance(id); });
    } else if (one && e && instances > 0) {
        instanceLink_->showMessage("Образец для " + ruPlural(instances, "экземпляра", "экземпляров", "экземпляров") +
                                   ": правка формы и компонентов меняет все");
    } else {
        instanceLink_->clearMessage();
    }
}

void SceneBuilder::fillDetails() {
    const auto master = [this](uint32_t id) { return masterById(id); };
    fillSelected(rigidFields_, shapeDraft_, master);
    fillSelected(colliderFields_, shapeDraft_, master);
}

// The geometry's widgets into the entity; its components' numbers are their cards' (shapeFields).
void SceneBuilder::readDetails(Entity& e) const {
    e.shape = ShapeKind(shape_->currentIndex());
    e.size = size_->value();
}

void SceneBuilder::showTransform(const SceneObject& o) {
    filling_ = true;
    object_->setObject(o);
    const SensorObject* sensor = sensorById(o.id);
    object_->showPlace(!(sensor && findJoint(graph_, sensor->parent)));
    if (const Entity* e = entityById(o.id)) size_->setValue(entityById(masterOf(e->id))->size);
    filling_ = false;
}

void SceneBuilder::fillWorld() { worldPanel_->setWorld(graph_.world); }

// One row per object; a group's members and an array's template under it, open.
void SceneBuilder::refreshList() {
    const QSignalBlocker quiet(list_);
    const bool wasFilling = filling_;
    filling_ = true;
    list_->clear();
    std::map<uint32_t, QTreeWidgetItem*> rows;
    std::map<uint32_t, uint32_t> under; // row id -> the row it hangs under
    auto row = [&](const SceneObject& o, const QIcon& icon, const QString& text) {
        auto* item = new QTreeWidgetItem;
        item->setText(0, text);
        item->setIcon(0, icon);
        item->setData(0, Qt::UserRole, o.id);
        item->setIcon(1, controlIcon(o.visible ? ControlIcon::Visible : ControlIcon::Hidden, 18));
        item->setIcon(2, controlIcon(o.locked ? ControlIcon::Locked : ControlIcon::Unlocked, 18));
        item->setToolTip(1, "Видимость");
        item->setToolTip(2, "Замок: мышь не выделяет и не двигает");
        if (!o.visible) item->setForeground(0, QColor(120, 124, 132)); // hidden: greyed
        rows[o.id] = item;
        under[o.id] = o.parent;
    };
    for (const Group& g : graph_.groups) row(g, objectIcon(ObjectIcon::Group, 22), QString::fromStdString(g.name) + (g.glued ? " — склеено" : ""));
    std::set<uint32_t> masters; // shapes that instances share
    for (const Entity& e : graph_.entities)
        if (masterOf(e.id) != e.id) masters.insert(masterOf(e.id));
    for (const Entity& e : graph_.entities) {
        const Entity r = resolveInstance(graph_, e);
        const bool linked = masterOf(e.id) != e.id || masters.count(e.id);
        row(e, shapeIcon(r.shape, 22), (linked ? "⧉ " : "") + rolesText(r) + (isHiddenTemplate(e) ? " (образец массива)" : ""));
    }
    for (const ArrayObject& a : graph_.arrays) {
        const ObjectIcon icon = a.pattern == ArrayPattern::Grid ? ObjectIcon::ArrayGrid : a.pattern == ArrayPattern::Circle ? ObjectIcon::ArrayCircle : ObjectIcon::ArrayLine;
        row(a, objectIcon(icon, 22), objectTitle(a.id));
        if (const Entity* t = entityById(a.templateId); t && isHiddenTemplate(*t) && !findObject(graph_, t->parent)) under[t->id] = a.id;
    }
    for (const Light& l : graph_.lights) row(l, sceneIcon(lightIcon(l.kind), 22), QString::fromStdString(l.name));
    for (const Camera& c : graph_.cameras)
        row(c, sceneIcon(SceneIcon::Camera, 22), QString::fromStdString(c.name) + (c.id == throughId_ ? " — вид через неё" : c.active ? " — активная" : ""));
    for (const SensorObject& s : graph_.sensors) row(s, deviceIcon(DeviceIcon(int(s.kind)), 22), QString::fromStdString(s.name));
    addJointRows(rows, under); // between the bodies they tie, a joint's sensors under it
    for (auto& [id, item] : rows) {
        const auto parent = rows.find(under[id]);
        if (parent != rows.end() && parent->second != item) parent->second->addChild(item);
        else list_->addTopLevelItem(item);
    }
    addDeviceRows();
    list_->expandAll();
    syncListSelection();
    filling_ = wasFilling;
}

// Only the components the object has get a card; the rigid card warns when there is no collider.
void SceneBuilder::refreshSections() {
    const std::vector<RoleIcon> all = commonRoles();
    for (int k = 0; k < int(RoleIcon::Count); ++k) cards_[k]->setVisible(std::find(all.begin(), all.end(), RoleIcon(k)) != all.end());
    const Entity* e = selectedEntity() ? entityById(masterOf(selectedId_)) : nullptr;
    if (!e || selection_.size() > 1 || !e->rigid.enabled || e->collider.enabled) {
        noCollider_->clearMessage();
        return;
    }
    noCollider_->showMessage("Без коллайдера тело провалится сквозь всё.", "Добавить коллайдер",
                             [this] { toggleRole(RoleIcon::Collider); });
}

// The icon and name of the active object - or "3 объекта" -, and the components all of them have.
void SceneBuilder::refreshHeader() {
    const SceneObject* o = selectedObject();
    if (!o) return;
    const Entity* e = entityById(o->id);
    const QIcon kind = kindIcon(o->id, 44); // a light or a camera
    const QIcon icon = e ? shapeIcon(resolveInstance(graph_, *e).shape, 44)
                         : !kind.isNull() ? kind : objectIcon(groupById(o->id) ? ObjectIcon::Group : ObjectIcon::ArrayLine, 44);
    headerIcon_->setPixmap(icon.pixmap(44, 44));
    headerName_->setText(selection_.size() > 1 ? "Выбрано: " + ruPlural(int(selection_.size()), "объект", "объекта", "объектов") : objectTitle(o->id));
    QLayout* chips = headerChips_->layout();
    while (QLayoutItem* it = chips->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (RoleIcon role : commonRoles()) {
        auto* chip = new QLabel;
        chip->setPixmap(rolePixmap(role, 20));
        chip->setToolTip(roleTitle(role));
        chips->addWidget(chip);
    }
    const bool shapes = e && selectedEntities().size() == selection_.size();
    if (chips->count() == 0 && shapes) chips->addWidget(new QLabel(selection_.size() > 1 ? "общих компонентов нет — правка идёт всем сразу"
                                                                                            : "только геометрия — добавьте компонент"));
    else if (chips->count() == 0)
        chips->addWidget(new QLabel(groupById(o->id)    ? "группа"
                                    : arrayById(o->id)  ? "массив копий"
                                    : lightById(o->id)  ? "свет — для глаза, физика его не видит"
                                    : cameraById(o->id) ? "камера — через неё вид и скриншоты"
                                    : sensorById(o->id) ? (findJoint(graph_, sensorById(o->id)->parent) ? "датчик — на сочленении, считает его ход"
                                                                                                        : "датчик — едет на своём теле, читает мир как плата")
                                                        : "разные объекты"));
    static_cast<QHBoxLayout*>(chips)->addStretch(1);
}

// What does not work for the selected object as it is, in one sentence, and the button that fixes
// it (InlineBanner). Nothing is refused silently: the rigid card says itself when a body has no
// collider (refreshSections); a message about the whole scene stays until ▶.
void SceneBuilder::refreshBanner() {
    if (!stickyBanner_) banner_->clearMessage();
}

// Assign a dropped image to the exact object hit, using the inspector's single undo operation.
void SceneBuilder::assignImage(uint32_t id, const QString& file) {
    const Entity* e = entityById(id);
    if (!editing() || !e || e->locked) return;
    select(id);
    texture_->assignImage(file);
}
