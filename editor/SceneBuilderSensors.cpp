// Sensors in the scene builder (see SceneBuilder.h): putting one on a shape, its "Датчик" card, and
// what the selected joint reads while the scene plays.
//
// A sensor is a child of what carries it (ke::SensorObject), so it is a row under its shape in the
// list, moves with the gizmo (its pose on the shape is where it is mounted) and goes with the shape.
// Where a new one goes: an IMU flat on the shape's top (its z up, as a chip on a table); a sonar and a
// touch pad on the face that looks toward the view, the face the user is looking at; an encoder, a
// force-torque sensor and an effort sensor on the joint the shape hangs by. The menu and the API
// (assembly_sensor, assembly_encoder) make the same edit. The running world (src/sim) does not model
// the sensors' readings yet - the devices are in src/devices -, so their card says so instead of a number;
// a joint's own coordinate and effort are read live (the privileged loop's truth, SceneBuilderJoints.cpp).
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "InspectorWidgets.h"
#include "Resources.h"
#include "scene/Assembly.h"
#include "core/Units.h"
#include "scene/SensorSpec.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>

using namespace ke;

QString jointLiveText(const Simulation& sim, uint32_t id); // SceneBuilderJoints.cpp
QString powerLiveText(const Simulation& sim, uint32_t id); // SceneBuilderDevices.cpp

namespace {

// SensorKind; a stereo camera of a scene file keeps its name, but none is offered: vision is a later stage.
const char* kSensorNames[] = {QT_TRANSLATE_NOOP("Kinetica", "Энкодер"), "IMU", QT_TRANSLATE_NOOP("Kinetica", "Сонар"), QT_TRANSLATE_NOOP("Kinetica", "Касание"), QT_TRANSLATE_NOOP("Kinetica", "Сила-момент"), QT_TRANSLATE_NOOP("Kinetica", "Усилие привода"), QT_TRANSLATE_NOOP("Kinetica", "Стереокамера")};
const int kSensorKinds = 6; // the kinds a person can put: up to the effort sensor
const char* kDofWords[] = {QT_TRANSLATE_NOOP("Kinetica", "ход по x"), QT_TRANSLATE_NOOP("Kinetica", "ход по y"), QT_TRANSLATE_NOOP("Kinetica", "ход по z"), QT_TRANSLATE_NOOP("Kinetica", "поворот вокруг x"), QT_TRANSLATE_NOOP("Kinetica", "поворот вокруг y"), QT_TRANSLATE_NOOP("Kinetica", "поворот вокруг z")};

// The face of the target whose world normal is nearest to `toward`.
BoxFace faceToward(const SceneGraph& g, const Entity& e, const Vector3& toward) {
    Vector3 p;
    Quaternion q;
    worldPose(g, e, p, q);
    const Vector3 local = q.conjugate().rotate(toward);
    int best = 0;
    for (int a = 1; a < 3; ++a)
        if (std::fabs(local[a]) > std::fabs(local[best])) best = a;
    return BoxFace(2 * best + (local[best] < 0 ? 1 : 0));
}

uint32_t jointOfLink(const SceneGraph& g, uint32_t entity) {
    for (const JointObject& j : g.joints)
        if (j.a == entity) return j.id;
    return 0;
}

// What the robot reads from a sensor now, in words, and the truth of its probe beside it - the person sees both.
QString sensorLiveText(const Simulation& s, uint32_t id) {
    const LiveSensor* x = s.robot() ? s.robot()->sensor(id) : nullptr;
    if (!x) return uiText("Kinetica", "В работающем мире этого датчика нет (стереокамера снимает по запросу)");
    const SensorReading& r = x->reading();
    if (!r.problem.empty()) return uiText("Kinetica", "Не читает: ") + QString::fromStdString(r.problem);
    const auto n = [](double v, int d = 3) { return QString::number(v, 'f', d); };
    const auto vec = [&n](const Vector3& v, int d = 3) { return "(" + n(v.x, d) + ", " + n(v.y, d) + ", " + n(v.z, d) + ")"; };
    const SensorValues& t = x->truth();
    QString text;
    switch (x->spec().kind) {
    case SensorKind::Imu: text = "a " + vec(r.a) + uiText("Kinetica", " м/с²\nω ") + vec(r.b, 4) + uiText("Kinetica", " рад/с\nистина: a ") + vec(t.a) + ", ω " + vec(t.b, 4); break;
    case SensorKind::Wrench: text = "F " + vec(r.a, 2) + uiText("Kinetica", " Н\nM ") + vec(r.b) + uiText("Kinetica", " Н·м\nистина: F ") + vec(t.a, 2) + ", M " + vec(t.b); break;
    case SensorKind::Sonar: text = (r.value < 0 ? QString(uiText("Kinetica", "эха нет")) : uiText("Kinetica", "дальность ") + n(r.value, 4) + uiText("Kinetica", " м")) + uiText("Kinetica", " · истина ") + (t.value < 0 ? QString(uiText("Kinetica", "нет эха")) : n(t.value, 4) + uiText("Kinetica", " м")); break;
    case SensorKind::Touch: text = uiText("Kinetica", "сила ") + n(r.value, 2) + uiText("Kinetica", " Н, точек ") + QString::number(r.count); break;
    case SensorKind::Effort: text = uiText("Kinetica", "усилие ") + n(r.value, 4); break;
    case SensorKind::Encoder:
        text = uiText("Kinetica", "счёт ") + QString::number(r.count) + (r.referenced ? " · " + n(r.value, 5) : uiText("Kinetica", " · от старта ") + n(r.value, 5) + uiText("Kinetica", " (нуль не задан)")) +
               uiText("Kinetica", " · истина ") + n(t.value, 5);
        break;
    case SensorKind::Stereo: break;
    }
    return text + (r.saturated ? uiText("Kinetica", "\nна пределе диапазона") : QString()) + uiText("Kinetica", "\nотсчёт ") + n(double(r.sampleNs) * 1e-9, 4) + uiText("Kinetica", " с");
}

} // namespace

// The tables of sensor models: the program's (data/sensors next to it when installed, else the source tree's) and
// the person's own (<app data>/sensors, its rows over the program's), read at start and again when one changes.
void SceneBuilder::loadSensorCatalog() {
    const QString data = resourceDir("data");
    const QStringList dirs{data + "/sensors", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/sensors"};
    std::string problems;
    sensorCatalog().clear();
    for (const QString& dir : dirs)
        for (SensorKind kind : kTableKinds) {
            QFile file(dir + "/" + sensorKindKey(kind) + ".csv");
            if (file.open(QIODevice::ReadOnly)) problems += sensorCatalog().read(kind, file.readAll().toStdString(), file.fileName().toStdString());
        }
    if (!problems.empty()) emit statusMessage(uiText("Kinetica", "Таблицы датчиков: ") + QString::fromStdString(problems).trimmed().replace('\n', "; "));
    if (!catalogWatch_) {
        catalogWatch_ = new QFileSystemWatcher(this);
        connect(catalogWatch_, &QFileSystemWatcher::fileChanged, this, &SceneBuilder::loadSensorCatalog);
        connect(catalogWatch_, &QFileSystemWatcher::directoryChanged, this, &SceneBuilder::loadSensorCatalog);
    }
    for (const QString& dir : dirs)
        if (QDir(dir).exists() && !catalogWatch_->directories().contains(dir)) catalogWatch_->addPath(dir);
    for (const std::string& f : sensorCatalog().files())
        if (!catalogWatch_->files().contains(QString::fromStdString(f))) catalogWatch_->addPath(QString::fromStdString(f)); // an editor saves anew
    if (sensorById(selectedId_)) fillInspector();
}

SensorObject* SceneBuilder::sensorById(uint32_t id) {
    for (SensorObject& s : graph_.sensors)
        if (id != 0 && s.id == id) return &s;
    return nullptr;
}

void SceneBuilder::addSensor(SensorKind kind) {
    const Entity* e = selectedEntity();
    if (!e) return;
    const Entity target = *e;
    const Vector3 eye = viewEye_ ? viewEye_() : worldPositionOf(target.id) + Vector3(1.0f, 1.0f, 1.0f);
    uint32_t made = 0;
    std::string error;
    transact([&](SceneGraph& g, std::string& err) {
        if (kind == SensorKind::Encoder) made = attachEncoder(g, jointOfLink(g, target.id));
        else if (kind == SensorKind::Wrench || kind == SensorKind::Effort) made = attachJointSensor(g, jointOfLink(g, target.id), kind);
        else {
            const Vector3 toward = kind == SensorKind::Imu ? Vector3(0.0f, 1.0f, 0.0f) : normalize(eye - worldPositionOf(target.id));
            made = attachSensor(g, target.id, snapToFace(g, target.id, faceToward(g, target, toward), Vector3(0.0f)), kind);
        }
        if (!made) err = uiText("Kinetica", "некуда поставить датчик");
        return made != 0;
    }, error);
    if (!made) return emit statusMessage(QString::fromStdString(error));
    setSelected(made);
    const char* said[] = {uiText("Kinetica", "Энкодер сел на сочленение: считает его поворот"), uiText("Kinetica", "IMU лёг на верх тела: в покое покажет +g по z, как микросхема на столе"),
                          uiText("Kinetica", "Сонар смотрит из грани к виду; повернуть — гизмо вращения (E)"),
                          uiText("Kinetica", "Касание легло на грань, обращённую к виду: сила контактов через неё, Н"),
                          uiText("Kinetica", "Сила-момент сел на сочленение тела: что оно передаёт телу, в его осях"), uiText("Kinetica", "Усилие привода сочленения: сила или момент его мотора")};
    if (int(kind) < kSensorKinds) emit statusMessage(said[int(kind)]);
}

QString SceneBuilder::sensorMisfit(SensorKind kind) const {
    const Entity* e = nullptr;
    for (const Entity& x : graph_.entities)
        if (x.id == selectedId_) e = &x;
    if (!e || selection_.size() != 1) return uiText("Kinetica", "Выберите одно тело: датчик встанет на него или на его сочленение");
    const bool onJoint = kind == SensorKind::Encoder || kind == SensorKind::Wrench || kind == SensorKind::Effort;
    if (onJoint && !jointOfLink(graph_, e->id)) return uiText("Kinetica", "У «") + QString::fromStdString(e->name) + uiText("Kinetica", "» нет сочленения: этому датчику нечего мерить");
    return QString();
}

const std::vector<QAction*>& SceneBuilder::sensorActions() {
    if (!sensorActions_.empty()) return sensorActions_;
    for (int k = 0; k < kSensorKinds; ++k) {
        auto* a = new QAction(deviceIcon(DeviceIcon(k), 24), uiText("Kinetica", kSensorNames[k]), this);
        a->setObjectName(QString("addSensor%1").arg(k));
        const SensorKind kind = SensorKind(k);
        connect(a, &QAction::triggered, this, [this, kind] { addSensor(kind); });
        sensorActions_.push_back(a);
    }
    return sensorActions_;
}

void SceneBuilder::updateSensorActions() {
    const char* tips[] = {uiText("Kinetica", "Квадратурный энкодер на сочленение тела: фронты A/B/Z и счёт"), uiText("Kinetica", "Акселерометр и гироскоп на верх тела: модель из таблицы data/sensors/imu.csv"),
                          uiText("Kinetica", "Ультразвуковой дальномер из грани, обращённой к виду: модель из таблицы data/sensors/sonar.csv"), uiText("Kinetica", "Подушка касания на грани, обращённой к виду: сила контактов через неё"),
                          uiText("Kinetica", "Шестиосевой датчик силы и момента на сочленении тела, как в запястье робота"),
                          uiText("Kinetica", "Сила или момент мотора сочленения тела, как датчик тока сервы")};
    for (int k = 0; k < int(sensorActions().size()); ++k) {
        const QString misfit = sensorMisfit(SensorKind(k));
        sensorActions_[size_t(k)]->setEnabled(misfit.isEmpty());
        sensorActions_[size_t(k)]->setToolTip(misfit.isEmpty() ? tips[k] : misfit);
    }
}

// The "Датчики" part of "+ Добавить компонент" for the selected shape.
void SceneBuilder::fillSensorMenu(const Entity& e, QMenu* menu) {
    menu->addSection(uiText("Kinetica", "Электрика"));
    uint32_t motor = 0;
    for (const JointObject& j : graph_.joints)
        for (int k = 0; k < 6 && j.a == e.id && !motor; ++k)
            if (j.dof[k].drive.enabled && j.dof[k].drive.constant > 0) motor = j.id;
    QAction* power = menu->addAction(deviceIcon(DeviceIcon::Driver, 24), uiText("Kinetica", "Питание от батареи"));
    power->setEnabled(motor != 0);
    power->setToolTip(motor ? uiText("Kinetica", "Драйвер мотора от батареи сцены (нет батареи — появится 3S): ШИМ, ток и заряд вместо идеального источника")
                            : uiText("Kinetica", "Нужен мотор постоянного тока на сочленении тела — заготовка «Мотор-колесо»"));
    connect(power, &QAction::triggered, this, [this, motor] {
        uint32_t made = 0;
        std::string error;
        transact([&](SceneGraph& g, std::string&) { return (made = powerMotor(g, motor)) != 0; }, error);
        if (made) {
            setSelected(made);
            emit statusMessage(uiText("Kinetica", "Мотор питается от батареи: ШИМ — в карточке драйвера, двигается и на ходу"));
        }
    });
    menu->addSection(uiText("Kinetica", "Датчики"));
    const bool joint = jointOfLink(graph_, e.id) != 0;
    const char* tips[] = {uiText("Kinetica", "Квадратурный энкодер на сочленение, которым держится это тело: фронты A/B/Z и счёт"),
                          uiText("Kinetica", "Акселерометр и гироскоп: частота, диапазон, шум и смещения — из паспорта модели (data/sensors/imu.csv)"),
                          uiText("Kinetica", "Ультразвуковой дальномер из грани, обращённой к виду: луч, дальность и точность — из паспорта модели"),
                          uiText("Kinetica", "Подушка на грани, обращённой к виду: нормальная сила контактов через неё — палец захвата, бампер, весы под ногой"),
                          uiText("Kinetica", "Шестиосевой датчик силы и момента на сочленении тела, как в запястье робота: вес груза, удар, касание стола"),
                          uiText("Kinetica", "Сила или момент мотора сочленения, как датчик тока сервы: чем тело давит, сколько держит")};
    for (int k = 0; k < kSensorKinds; ++k) { // SensorKind: an encoder, a wrench and an effort need the joint the body hangs on
        const bool onJoint = k == 0 || k == 4 || k == 5;
        QAction* a = menu->addAction(deviceIcon(DeviceIcon(k), 24), uiText("Kinetica", kSensorNames[k]));
        a->setToolTip(onJoint && !joint ? uiText("Kinetica", "У тела нет сочленения: датчику нечего мерить") : tips[k]);
        a->setEnabled(!onJoint || joint);
        const SensorKind kind = SensorKind(k);
        connect(a, &QAction::triggered, this, [this, kind] { addSensor(kind); });
    }
}

void SceneBuilder::buildSensorCard(QVBoxLayout* col) {
    sensorCard_ = new ComponentCard(uiText("Kinetica", "Датчик"), deviceIcon(DeviceIcon::Sonar, 22));
    sensorCard_->setObjectName("sensorCard");
    sensorCard_->setToolTip(uiText("Kinetica", "Датчик едет на своём теле: его поза на теле — место крепления, ось x — ось измерения"));
    sensorMount_ = new QLabel;
    sensorMount_->setWordWrap(true);
    sensorCard_->body()->addWidget(sensorMount_);
    SensorObject& s = sensorDraft_;
    const auto is = [&s](std::vector<SensorKind> kinds) { return [&s, kinds] { return std::count(kinds.begin(), kinds.end(), s.kind) > 0; }; };
    const auto encoder = is({SensorKind::Encoder}), noisy = is({SensorKind::Imu, SensorKind::Sonar, SensorKind::Wrench}); // a datasheet's
    const auto rotary = [&s] { return s.dof >= DofRx; };
    sensorFields_ = new FieldForm([this](bool click) { commitDraft(sensorById(selectedId_), sensorDraft_, click); });
    FieldForm& f = *sensorFields_;
    f.number(uiText("Kinetica", "Штрихов"), 1, 1e6, 1, 0, [&s] { return s.lines; }, [&s](double v) { s.lines = int(v); }).name("sensorLines").shown(encoder)
        .label([rotary] { return rotary() ? uiText("Kinetica", "Штрихов/оборот (PPR)") : uiText("Kinetica", "Штрихов/метр"); }).tip(uiText("Kinetica", "Штрихов диска на оборот (у линейного — на метр): шагов в 4 раза больше"));
    // The count-zero reference: a calibration or a home position the person gives - never the true angle read now.
    f.toggle(uiText("Kinetica", "Известна координата при нулевом счёте"), [&s] { return s.hasEncoderReference; }, [&s](double on) { s.hasEncoderReference = on != 0; })
        .name("encoderReferenced").shown(encoder).tip(uiText("Kinetica", "Задайте калибровку или домашнюю позицию. Текущий истинный угол не считывается"));
    f.number(uiText("Kinetica", "Координата нуля"), -1e9, 1e9, 0.001, 9, [&s] { return s.encoderReference; }, [&s](double v) { s.encoderReference = Real(v); })
        .name("encoderReference").unit([rotary] { return FieldForm::Unit{rotary() ? uiText("Kinetica", " рад") : uiText("Kinetica", " м")}; }).shown([&s, encoder] { return encoder() && s.hasEncoderReference; });
    f.note([&s, rotary] {
        const double counts = 4.0 * s.lines, step = (rotary() ? 2 * 3.14159265358979323846 : 1) / counts;
        return QString(uiText("Kinetica", "%1 отсчётов/%2 · шаг %3 %4. Разрешение не равно точности; шум и потеря импульсов пока не моделируются."))
            .arg(counts, 0, 'g', 9).arg(rotary() ? uiText("Kinetica", "оборот (CPR)") : uiText("Kinetica", "метр")).arg(step, 0, 'g', 6).arg(rotary() ? uiText("Kinetica", "рад") : uiText("Kinetica", "м"));
    }).shown(encoder);
    QStringList dofs;
    for (const char* w : kDofWords) dofs << uiText("Kinetica", w);
    f.choice(uiText("Kinetica", "Мерит"), dofs, [&s] { return s.dof; }, [&s](double v) { s.dof = int(v); }).name("sensorDof").shown(is({SensorKind::Encoder, SensorKind::Effort}));
    f.choice(uiText("Kinetica", "Относительно"), [this, &s] {
        std::vector<FieldForm::Option> out{{uiText("Kinetica", "мир"), 0}};
        for (const Entity& e : graph_.entities)
            if (e.id != s.parent) out.push_back({QString::fromStdString(e.name), double(e.id)});
        return out;
    }, [&s] { return s.against; }, [&s](double v) { s.against = uint32_t(v); })
        .name("sensorAgainst").shown([this, &s, encoder] { return encoder() && !findJoint(graph_, s.parent); })
        .tip(uiText("Kinetica", "Относительно чего мерить поворот или ход: другого тела или мира"));
    // The datasheet: a model of the kind's table, its numbers into the sensor, each to be typed over.
    const auto models = [&s] { return sensorCatalog().models(s.kind); };
    f.choice(uiText("Kinetica", "Модель"), [&s, models] {
        std::vector<FieldForm::Option> out;
        for (const SensorModel* m : models())
            out.push_back({QString::fromStdString(m->name) + (m->name == s.model && !specMatches(s, *m) ? uiText("Kinetica", " (числа изменены)") : ""), double(out.size())});
        if (!sensorCatalog().find(s.kind, s.model)) out.push_back({s.model.empty() ? QString(uiText("Kinetica", "свои числа")) : QString::fromStdString(s.model) + uiText("Kinetica", " (нет в таблице)"), -1});
        return out;
    }, [&s, models] {
        const auto all = models();
        const auto at = std::find_if(all.begin(), all.end(), [&s](const SensorModel* m) { return m->name == s.model; });
        return at == all.end() ? -1.0 : double(at - all.begin());
    }, [&s, models](double k) {
        if (k >= 0 && size_t(k) < models().size()) applyModel(s, *models()[size_t(k)]);
    }).name("sensorModel").shown([&s] { return !specFields(s.kind).empty(); })
        .tip(uiText("Kinetica", "Модель — строка таблицы data/sensors/<вид>.csv: её числа из техописания переносятся в датчик, каждое можно поправить"));
    for (SensorKind kind : {SensorKind::Imu, SensorKind::Sonar, SensorKind::Wrench})
        for (const Field& field : specFields(kind)) {
            const std::string key = field.key;
            f.number(uiText("Kinetica", field.label.c_str()), field.min, field.max, field.step, field.decimals, [&s, key] { return double(specValue(s, key)); },
                     [&s, key](double v) { s.spec[key] = Real(v); })
                .unit(QString::fromStdString(" " + (UiLanguage::active() == "en" ? field.unit : unitText(field.unit))))
                .name(QString::fromStdString("spec_" + key)).shown([&s, kind] { return s.kind == kind; });
        }
    f.note([&s] {
        const SensorModel* m = sensorCatalog().find(s.kind, s.model);
        return m && !m->source.empty() ? uiText("Kinetica", "Источник: ") + QString::fromStdString(m->source) : QString();
    }).name("roleRowLabel").shown([&s] { return !specFields(s.kind).empty(); });
    f.toggle(uiText("Kinetica", "Шум и смещения по паспорту"), [&s] { return s.noise; }, [&s](double on) { s.noise = on != 0; }).name("sensorNoise").shown(noisy)
        .label([&s] { return s.kind == SensorKind::Sonar ? uiText("Kinetica", "Дрожание по паспорту") : uiText("Kinetica", "Шум и смещения по паспорту"); })
        .tip(uiText("Kinetica", "Выключено — идеальный датчик: только для эталона и сравнения, управление робота так не читает"));
    f.number(uiText("Kinetica", "Зерно шума"), 0, 1e6, 1, 0, [&s] { return s.seed; }, [&s](double v) { s.seed = uint32_t(v); }).name("sensorSeed").shown(noisy)
        .tip(uiText("Kinetica", "Зерно шума: тот же номер — тот же экземпляр датчика. 0 — по id датчика"));
    sensorCard_->body()->addWidget(sensorFields_);
    sensorLive_ = new QLabel;
    sensorLive_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    sensorLive_->setWordWrap(true);
    sensorCard_->body()->addWidget(sensorLive_);
    connect(sensorCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    col->addWidget(sensorCard_);
    liveTimer_ = new QTimer(this);
    liveTimer_->setInterval(250);
    connect(liveTimer_, &QTimer::timeout, this, &SceneBuilder::pollLive);
    liveTimer_->start();
}

void SceneBuilder::fillSensorCard(const SceneObject& o) {
    const SensorObject* s = sensorById(o.id);
    sensorCard_->setVisible(s && selection_.size() == 1);
    if (!s) return;
    sensorDraft_ = *s;
    const JointObject* j = findJoint(graph_, s->parent);
    const SceneObject* carrier = findObject(graph_, s->parent);
    const QString on = j ? uiText("Kinetica", "на сочленении «") + QString::fromStdString(j->name) + "»" : carrier ? uiText("Kinetica", "на «") + QString::fromStdString(carrier->name) + "»"
                     : s->parent == 0 ? QString(uiText("Kinetica", "закреплён в мире")) : QString(uiText("Kinetica", "носитель удалён"));
    sensorMount_->setText(QString(uiText("Kinetica", kSensorNames[int(s->kind)])) + " · " + on + (j ? QString(uiText("Kinetica", " — место задаёт сочленение")) : QString()));
    sensorCard_->setIcon(deviceIcon(DeviceIcon(int(s->kind)), 22));
    sensorFields_->fill();
    sensorLive_->setText(editing() ? uiText("Kinetica", "Показания — при пуске сцены: что читает робот и истина физики рядом") : "…");
}

// Four times a second while the scene plays: the selected joint's coordinate, target and effort, the selected
// battery's or driver's voltage and current, or the selected sensor's reading, read on the simulation's thread.
void SceneBuilder::pollLive() {
    const uint32_t id = selectedId_;
    const bool joint = findJoint(graph_, id) != nullptr;
    const bool battery = std::any_of(graph_.batteries.begin(), graph_.batteries.end(), [id](const BatteryObject& b) { return b.id == id; });
    const bool driver = std::any_of(graph_.drivers.begin(), graph_.drivers.end(), [id](const DriverObject& d) { return d.id == id; });
    if ((!joint && !battery && !driver && !sensorById(id)) || editing() || selection_.size() != 1) return;
    QPointer<SceneBuilder> self(this);
    QPointer<QLabel> shown(joint ? jointLive_ : battery ? batteryLive_ : driver ? driverLive_ : sensorLive_);
    const bool power = battery || driver;
    ctrl_->post([self, shown, id, joint, power](Simulation& s) {
        const QString text = joint ? jointLiveText(s, id) : power ? powerLiveText(s, id) : sensorLiveText(s, id);
        QMetaObject::invokeMethod(self, [self, shown, id, text] {
            if (self && shown && self->selectedId_ == id && !text.isEmpty()) shown->setText(text);
        }, Qt::QueuedConnection);
    });
}
