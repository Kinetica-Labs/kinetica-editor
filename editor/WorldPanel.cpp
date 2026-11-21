// The world's physics (see WorldPanel.h): ordinary language and SI units first, the engine's terms
// in tooltips and search. Fields bind to the authored WorldSettings, not to the running world (see
// SceneBuilder.cpp): a change takes effect at the next Play.
#include "WorldPanel.h"
#include "UiLanguage.h"
#include "InspectorWidgets.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
using namespace ke;

WorldPanel::WorldPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("worldPanel");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8);
    auto* title = new QLabel(uiText("Kinetica", "Физика мира")); title->setStyleSheet("font-weight: 600;"); layout->addWidget(title);
    auto* search = new QLineEdit; search->setObjectName("physicsFilter"); search->setClearButtonEnabled(true);
    search->setPlaceholderText(uiText("Kinetica", "Найти: гравитация, подшаги, CCD…"));
    search->setAccessibleName(uiText("Kinetica", "Поиск настроек физики")); layout->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, &WorldPanel::filter);
    status_ = new QLabel; status_->setWordWrap(true); status_->setObjectName("physicsEditStatus"); layout->addWidget(status_);
    controls_ = new QWidget; body_ = new QVBoxLayout(controls_); body_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(controls_); buildGravity(); buildContacts(); buildJoints(); buildAdvanced(); buildEnvironment();
    auto* reset = new QPushButton(uiText("Kinetica", "Сбросить физику")); reset->setObjectName("resetWorldPhysics");
    reset->setToolTip(uiText("Kinetica", "Вернуть земную гравитацию и стандартные настройки контактов и суставов. Ctrl+Z отменит сброс."));
    body_->addWidget(reset);
    empty_ = new QLabel(uiText("Kinetica", "Настройка не найдена. Очистите поиск или попробуйте другое название."));
    empty_->setObjectName("physicsNoMatch"); empty_->setWordWrap(true); empty_->hide();
    layout->addWidget(empty_); layout->addStretch();
    connect(reset, &QPushButton::clicked, this, [this] {
        auto w = settings(); w.gravity = {0, -9.81_r, 0}; w.physics = {}; setWorld(w); changed();
    });
    setWorld({}); setEditable(true);
}

CollapsibleSection* WorldPanel::section(const QString& title, bool open) {
    auto* s = new CollapsibleSection(title); s->setOpen(open); body_->addWidget(s); return s;
}

QWidget* WorldPanel::row(CollapsibleSection* s, const QString& label, QWidget* input, const QString& tip, const QString& aliases) {
    auto* w = new QWidget; auto* l = new QHBoxLayout(w); l->setContentsMargins(0, 0, 0, 0);
    auto* name = new QLabel(label); name->setWordWrap(true); name->setBuddy(input);
    name->setToolTip(tip); input->setToolTip(tip); input->setAccessibleName(label);
    l->addWidget(name, 1); l->addWidget(input); s->body()->addWidget(w);
    fields_.push_back({w, input, s, label + " " + aliases + " " + tip}); return w;
}

void WorldPanel::count(CollapsibleSection* s, const QString& label, const QString& name, int low, int high,
                       const QString& tip, int PhysicsSettings::* member) {
    auto* input = new QSpinBox; input->setObjectName(name); input->setRange(low, high); input->setKeyboardTracking(false);
    row(s, label, input, tip, name);
    readers_.push_back([input, member](const WorldSettings& w) { input->setValue(w.physics.*member); });
    writers_.push_back({input, [input, member](WorldSettings& w) { w.physics.*member = input->value(); }});
    connect(input, &QSpinBox::valueChanged, this, &WorldPanel::changed);
}

void WorldPanel::number(CollapsibleSection* s, const QString& label, const QString& name, double low, double high,
                        double scale, const QString& unit, const QString& tip, Real PhysicsSettings::* member) {
    auto* input = new QDoubleSpinBox; input->setObjectName(name); input->setRange(low, high);
    input->setDecimals(3); input->setSingleStep(0.1); input->setSuffix(" " + unit); input->setKeyboardTracking(false);
    row(s, label, input, tip, name);
    readers_.push_back([input, member, scale](const WorldSettings& w) { input->setValue(double(w.physics.*member) * scale); });
    writers_.push_back({input, [input, member, scale](WorldSettings& w) { w.physics.*member = Real(input->value() / scale); }});
    connect(input, &QDoubleSpinBox::valueChanged, this, &WorldPanel::changed);
}

void WorldPanel::flag(CollapsibleSection* s, const QString& label, const QString& name, const QString& tip,
                      bool PhysicsSettings::* member) {
    auto* input = new QCheckBox; input->setObjectName(name); row(s, label, input, tip, name);
    readers_.push_back([input, member](const WorldSettings& w) { input->setChecked(w.physics.*member); });
    writers_.push_back({input, [input, member](WorldSettings& w) { w.physics.*member = input->isChecked(); }});
    connect(input, &QCheckBox::toggled, this, &WorldPanel::changed);
}

void WorldPanel::buildGravity() {
    auto* s = section(uiText("Kinetica", "Гравитация"), true);
    auto* g = new QDoubleSpinBox; g->setObjectName("gravityStrength"); g->setRange(0, 1000);
    g->setDecimals(3); g->setSuffix(uiText("Kinetica", " м/с²")); g->setKeyboardTracking(false);
    row(s, uiText("Kinetica", "Ускорение падения"), g, uiText("Kinetica", "Земля: 9,81 м/с². 0 — невесомость. Направление задаётся ниже."), uiText("Kinetica", "gravity гравитация"));
    readers_.push_back([g](const WorldSettings& w) { g->setValue(double(length(w.gravity))); });
    writers_.push_back({g, [g](WorldSettings& w) {
        const Real len = length(w.gravity); w.gravity = (len > 0 ? w.gravity / len : Vector3(0, -1, 0)) * Real(g->value());
    }});
    connect(g, &QDoubleSpinBox::valueChanged, this, &WorldPanel::changed);
    auto* presets = new QWidget; auto* h = new QHBoxLayout(presets); h->setContentsMargins(0, 0, 0, 0);
    const char* names[] = {uiText("Kinetica", "Земля"), uiText("Kinetica", "Луна"), uiText("Kinetica", "Невесомость")}; const Real values[] = {9.81_r, 1.62_r, 0};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(names[i]); b->setObjectName(QString("gravityPreset%1").arg(i)); h->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, v = values[i]] {
            auto w = settings(); w.gravity = {0, -v, 0}; setWorld(w); changed();
        });
    }
    s->body()->addWidget(presets);
    auto* axes = new Vec3Row(-1000, 1000, 0.1, 3); axes->setObjectName("gravityVector");
    row(s, uiText("Kinetica", "Вектор X / Y / Z"), axes, uiText("Kinetica", "Y направлен вверх: отрицательный Y тянет вниз. Одна гравитация для всей сцены."), uiText("Kinetica", "gravity vector направление"));
    connect(axes, &Vec3Row::edited, this, [this, axes] {
        auto w = settings(); w.gravity = axes->value(); setWorld(w); changed();
    });
    readers_.push_back([axes](const WorldSettings& w) { axes->setValue(w.gravity); });
}

void WorldPanel::buildContacts() {
    auto* s = section(uiText("Kinetica", "Контакты твёрдых тел"), true);
    count(s, uiText("Kinetica", "Подшагов на кадр"), "substeps", 1, 64,
          uiText("Kinetica", "Кадр 1/60 с разбивается на меньшие шаги; приводы суставов считаются на каждом. Больше подшагов — "
          "жёстче контакты и суставы, но медленнее."), &PhysicsSettings::substeps);
    number(s, uiText("Kinetica", "Жёсткость контакта"), "contactHertz", 1, 10000, 1, uiText("Kinetica", "Гц"),
           uiText("Kinetica", "Частота пружины, которой движок выталкивает тела друг из друга (soft step). Выше — меньше проваливание, "
           "но не выше четверти частоты подшагов."), &PhysicsSettings::contactHertz);
    number(s, uiText("Kinetica", "Затухание контакта"), "contactDamping", 0, 100, 1, "",
           uiText("Kinetica", "Безразмерное затухание этой пружины: 1 — критическое, больше — вязче."), &PhysicsSettings::contactDamping);
    flag(s, uiText("Kinetica", "Непрерывные столкновения (CCD)"), "continuous",
         uiText("Kinetica", "Быстрое тело не пролетает сквозь неподвижное между положениями шага."), &PhysicsSettings::continuous);
}

void WorldPanel::buildJoints() {
    auto* s = section(uiText("Kinetica", "Суставы"), true);
    number(s, uiText("Kinetica", "Жёсткость суставов"), "jointHertz", 1, 10000, 1, uiText("Kinetica", "Гц"),
           uiText("Kinetica", "Частота, с которой движок держит звенья вместе: выше — меньше провисание и перебег предела. "
           "Движок держит не жёстче четверти частоты подшагов."), &PhysicsSettings::jointHertz);
    number(s, uiText("Kinetica", "Затухание суставов"), "jointDamping", 0, 100, 1, "",
           uiText("Kinetica", "Безразмерное затухание удержания суставов: 1 — критическое."), &PhysicsSettings::jointDamping);
}

void WorldPanel::buildAdvanced() {
    auto* s = section(uiText("Kinetica", "Покой и затухание"), false);
    flag(s, uiText("Kinetica", "Усыплять покоящиеся тела"), "sleeping",
         uiText("Kinetica", "Покоящееся тело перестаёт рассчитываться, пока его не коснутся."), &PhysicsSettings::sleeping);
    number(s, uiText("Kinetica", "Линейное затухание"), "linearDamping", 0, 100, 1, uiText("Kinetica", "1/с"), uiText("Kinetica", "Демпфирование движения каждого тела; рассеивает энергию."),
           &PhysicsSettings::linearDamping);
    number(s, uiText("Kinetica", "Угловое затухание"), "angularDamping", 0, 100, 1, uiText("Kinetica", "1/с"), uiText("Kinetica", "Демпфирование вращения каждого тела; рассеивает энергию."),
           &PhysicsSettings::angularDamping);
}

void WorldPanel::buildEnvironment() {
    auto* s = section(uiText("Kinetica", "Размер мира"), false);
    auto* size = new Vec3Row(0.5, 50, 0.5, 1); size->setObjectName("worldSize");
    row(s, uiText("Kinetica", "Мир, м"), size, uiText("Kinetica", "Коробка, в которой стоит сцена: пол на высоте 0 под её серединой, по ней кадрируется вид."),
        uiText("Kinetica", "domain размер коробка пол"));
    readers_.push_back([size](const WorldSettings& w) { size->setValue(w.size); });
    writers_.push_back({size, [size](WorldSettings& w) { w.size = size->value(); }});
    connect(size, &Vec3Row::edited, this, &WorldPanel::changed);
}

WorldSettings WorldPanel::settings() const {
    return world_;
}
void WorldPanel::changed() {
    for (const auto& [input, write] : writers_)
        if (input == sender()) write(world_);
    emit settingsEdited(world_);
}
void WorldPanel::setWorld(const WorldSettings& w) {
    world_ = w;
    std::vector<std::unique_ptr<QSignalBlocker>> blockers;
    for (const Field& f : fields_) blockers.push_back(std::make_unique<QSignalBlocker>(f.input));
    for (const auto& read : readers_) read(w);
}
void WorldPanel::setEditable(bool editable) {
    controls_->setEnabled(editable);
    status_->setText(editable ? uiText("Kinetica", "Для всей сцены. Сохраняется в файл; Ctrl+Z отменяет изменение.")
                              : uiText("Kinetica", "Настройки сцены. Нажмите Стоп, чтобы изменить их и применить при следующем Пуске."));
}
void WorldPanel::filter(const QString& query) {
    const QString q = query.trimmed();
    std::map<CollapsibleSection*, bool> matches;
    for (const auto& f : fields_) {
        const bool match = q.isEmpty() || f.search.contains(q, Qt::CaseInsensitive);
        f.row->setVisible(match); matches[f.section] |= match;
    }
    for (const auto& [s, match] : matches) {
        if (!q.isEmpty() && !folded_.count(s)) folded_[s] = s->isOpen();
        s->setVisible(match);
        if (!q.isEmpty()) s->setOpen(true);
        else if (folded_.count(s)) s->setOpen(folded_[s]);
    }
    const bool any = std::any_of(matches.begin(), matches.end(), [](const auto& entry) { return entry.second; });
    empty_->setVisible(!any);
    if (q.isEmpty()) folded_.clear();
}
