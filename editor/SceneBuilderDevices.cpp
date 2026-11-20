// The electrics in the scene builder (see SceneBuilder.h): batteries and motor drivers as rows of the
// scene list - a battery, its drivers under it - and their cards. They are no SceneObjects (no pose: a
// driver is the wiring between a battery and a joint's motor), so the inspector shows their card in
// place of the object's. A driver's duty is a slider, taken by the running world at once. While the scene plays the
// cards read what the world's power does (RobotWorld::powerReading, src/sim/RobotPower.cpp): a pack's voltage,
// current and charge, a motor's voltage and current. Both are optional: a DC motor with no driver runs on its own
// line's voltage, a driver with no battery on an ideal bus. The joint's and the controller's cards
// share this place (SceneBuilderJoints.cpp, SceneBuilderControllers.cpp).
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "InspectorWidgets.h"
#include "scene/Assembly.h"
#include "sim/Simulation.h"

#include <QBoxLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QTreeWidget>


using namespace ke;

namespace {

BatteryObject* batteryIn(SceneGraph& g, uint32_t id) {
    for (BatteryObject& b : g.batteries)
        if (id != 0 && b.id == id) return &b;
    return nullptr;
}

DriverObject* driverIn(SceneGraph& g, uint32_t id) {
    for (DriverObject& d : g.drivers)
        if (id != 0 && d.id == id) return &d;
    return nullptr;
}

QFont fixedFont() { return QFontDatabase::systemFont(QFontDatabase::FixedFont); }

} // namespace

bool SceneBuilder::isDevice(uint32_t id) const {
    auto& g = const_cast<SceneGraph&>(graph_);
    return batteryIn(g, id) || driverIn(g, id) || (id != 0 && findJoint(g, id)) || controllerById(id);
}

void SceneBuilder::addDeviceRows() {
    std::map<uint32_t, QTreeWidgetItem*> packs;
    auto item = [](uint32_t id, DeviceIcon icon, const QString& text) {
        auto* row = new QTreeWidgetItem;
        row->setText(0, text);
        row->setIcon(0, deviceIcon(icon, 22));
        row->setData(0, Qt::UserRole, id);
        return row;
    };
    for (const BatteryObject& b : graph_.batteries) {
        packs[b.id] = item(b.id, DeviceIcon::Battery, QString::fromStdString(b.name) + QString(" — %1S%2P").arg(b.series).arg(b.parallel));
        list_->addTopLevelItem(packs[b.id]);
    }
    for (const ControllerObject& c : graph_.controllers)
        list_->addTopLevelItem(item(c.id, DeviceIcon::Controller, QString::fromStdString(c.name) + QString(uiText("Kinetica", " — %1 действ.")).arg(c.actions.size())));
    for (const DriverObject& d : graph_.drivers) {
        const JointObject* j = findJoint(graph_, d.joint);
        QTreeWidgetItem* row = item(d.id, DeviceIcon::Driver, QString::fromStdString(d.name) + " → " + (j ? QString::fromStdString(j->name) : QString("?")));
        auto pack = packs.find(d.battery);
        if (pack != packs.end()) pack->second->addChild(row);
        else list_->addTopLevelItem(row);
    }
}

void SceneBuilder::buildDeviceCards(QVBoxLayout* col) {
    deviceBody_ = new QWidget;
    auto* body = new QVBoxLayout(deviceBody_);
    body->setContentsMargins(0, 0, 0, 0);
    batteryCard_ = new ComponentCard(uiText("Kinetica", "Батарея"), deviceIcon(DeviceIcon::Battery, 22));
    batteryCard_->setObjectName("batteryCard");
    batteryCard_->setToolTip(uiText("Kinetica", "Сборка ячеек 18650: схема Тевенина, заряд кончается, напряжение под нагрузкой просаживается"));
    BatteryObject& pack = batteryDraft_;
    batteryFields_ = new FieldForm([this](bool click) { commitDraft(batteryIn(graph_, selectedId_), batteryDraft_, click); });
    batteryFields_->number(uiText("Kinetica", "Последовательно"), 1, 1000, 1, 0, [&pack] { return pack.series; }, [&pack](double v) { pack.series = int(v); }).name("batterySeries")
        .number(uiText("Kinetica", "Параллельно"), 1, 1000, 1, 0, [&pack] { return pack.parallel; }, [&pack](double v) { pack.parallel = int(v); }).name("batteryParallel")
        .number(uiText("Kinetica", "Ёмкость ячейки"), 0.0001, 1000, 0.1, 4, [&pack] { return pack.capacityAh; }, [&pack](double v) { pack.capacityAh = Real(v); })
        .unit(uiText("Kinetica", " А·ч")).name("batteryCapacity").tip(uiText("Kinetica", "Ёмкость одной ячейки"))
        .number(uiText("Kinetica", "Заряд на старте"), 0, 100, 5, 1, [&pack] { return pack.soc; }, [&pack](double v) { pack.soc = Real(v); }).unit(" %", 100).name("batteryCharge")
        .number(uiText("Kinetica", "Отсечка ячейки"), 2, 4.2, 0.1, 2, [&pack] { return pack.cutoff; }, [&pack](double v) { pack.cutoff = Real(v); }).unit(uiText("Kinetica", " В")).name("batteryCutoff")
        .tip(uiText("Kinetica", "Защита отключает батарею, когда напряжение ячейки под нагрузкой падает ниже этого"));
    batteryCard_->body()->addWidget(batteryFields_);
    batteryLive_ = new QLabel;
    batteryLive_->setFont(fixedFont());
    batteryLive_->setWordWrap(true);
    batteryCard_->body()->addWidget(batteryLive_);
    connect(batteryCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    body->addWidget(batteryCard_);

    driverCard_ = new ComponentCard(uiText("Kinetica", "Драйвер мотора"), deviceIcon(DeviceIcon::Driver, 22));
    driverCard_->setObjectName("driverCard");
    driverCard_->setToolTip(uiText("Kinetica", "H-мост: подаёт на мотор долю напряжения батареи по ШИМ, знак — направление"));
    driverMount_ = new QLabel;
    driverMount_->setWordWrap(true);
    driverCard_->body()->addWidget(driverMount_);
    DriverObject& d = driverDraft_;
    driverFields_ = new FieldForm([this](bool click) { commitDraft(driverIn(graph_, selectedId_), driverDraft_, click); });
    driverFields_->slider(uiText("Kinetica", "ШИМ"), -100, 100, [&d] { return d.duty; }, [&d](double v) { d.duty = Real(v); }, 3).unit(" %", 100).name("driverDuty")
        .tip(uiText("Kinetica", "ШИМ: −100 % — полный назад, 0 — стоп, 100 % — полный вперёд"))
        .choice(uiText("Kinetica", "Батарея"), [this] {
            std::vector<FieldForm::Option> out{{uiText("Kinetica", "нет"), 0}};
            for (const BatteryObject& o : graph_.batteries) out.push_back({QString::fromStdString(o.name), double(o.id)});
            return out;
        }, [&d] { return d.battery; }, [&d](double v) { d.battery = uint32_t(v); }).name("driverBattery").tip(uiText("Kinetica", "От какой батареи питается мост"))
        .choice(uiText("Kinetica", "При ШИМ 0"), {uiText("Kinetica", "накат: ключи разомкнуты"), uiText("Kinetica", "тормоз: обмотка замкнута")}, [&d] { return d.brake; }, [&d](double v) { d.brake = v != 0; })
        .name("driverStop").tip(uiText("Kinetica", "Что делает мост при ШИМ 0 (DRV8833: оба входа в 0 или оба в 1)"))
        .number(uiText("Kinetica", "Предел тока"), 0, 100, 0.5, 2, [&d] { return d.currentLimit; }, [&d](double v) { d.currentLimit = Real(v); }).unit(uiText("Kinetica", " А")).name("driverLimit")
        .tip(uiText("Kinetica", "Предел тока мотора; 0 — без предела"))
        .number(uiText("Kinetica", "Ключи"), 0, 10, 0.01, 3, [&d] { return d.switches; }, [&d](double v) { d.switches = Real(v); }).unit(uiText("Kinetica", " Ом")).name("driverSwitches")
        .tip(uiText("Kinetica", "Сопротивление открытых ключей моста на пути тока"));
    driverCard_->body()->addWidget(driverFields_);
    driverLive_ = new QLabel;
    driverLive_->setFont(fixedFont());
    driverLive_->setWordWrap(true);
    driverCard_->body()->addWidget(driverLive_);
    connect(driverCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    body->addWidget(driverCard_);
    buildControllerCard(body);
    buildJointCard(body);
    deviceBody_->setVisible(false);
    col->addWidget(deviceBody_);
}

void SceneBuilder::fillDeviceCards() {
    const BatteryObject* b = batteryIn(graph_, selectedId_);
    const DriverObject* d = driverIn(graph_, selectedId_);
    batteryCard_->setVisible(b != nullptr);
    driverCard_->setVisible(d != nullptr);
    const bool was = filling_;
    filling_ = true;
    fillJointCard();
    fillControllerCard();
    const QString waiting = editing() ? uiText("Kinetica", "Напряжение, ток и заряд — при пуске сцены") : "…";
    if (b) {
        batteryDraft_ = *b;
        batteryFields_->fill();
        batteryLive_->setText(waiting);
    }
    if (d) {
        driverDraft_ = *d;
        const JointObject* j = findJoint(graph_, d->joint);
        driverMount_->setText("«" + QString::fromStdString(d->name) + uiText("Kinetica", "» · мотор: ") + (j ? "«" + QString::fromStdString(j->name) + "»" : QString(uiText("Kinetica", "нет сочленения"))) +
                              (batteryIn(graph_, d->battery) ? QString() : QString(uiText("Kinetica", " · батареи нет"))));
        driverFields_->fill();
        driverLive_->setText(waiting);
    }
    filling_ = was;
}

// What a battery or a driver does now, read on the simulation's thread (SceneBuilder::pollLive).
QString powerLiveText(const Simulation& sim, uint32_t id) {
    RobotWorld::PowerReading r;
    const RobotWorld* robot = sim.robot();
    if (!robot) return QString();
    SceneGraph& g = const_cast<SceneGraph&>(robot->graph());
    if (!robot->powerReading(id, r)) return driverIn(g, id) ? uiText("Kinetica", "не подключён: у сочленения на этой оси нет мотора постоянного тока") : QString();
    const bool battery = batteryIn(g, id) != nullptr;
    QString t = battery ? QString(uiText("Kinetica", "%1 В · ток %2 А · заряд %3 %")).arg(double(r.voltage), 0, 'f', 2).arg(double(r.current), 0, 'f', 3).arg(100.0 * double(r.soc), 0, 'f', 1)
                        : QString(uiText("Kinetica", "на моторе %1 В · ток %2 А")).arg(double(r.voltage), 0, 'f', 2).arg(double(r.current), 0, 'f', 3);
    if (r.spent) t += battery ? uiText("Kinetica", "\nсела: защита разомкнула шину, моторы без питания") : uiText("Kinetica", "\nбатарея села: мотор без питания");
    return t;
}
