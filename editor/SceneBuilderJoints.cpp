// Joints as the meta-objects they are (SceneBuilder.h): in the scene's list each joint is a row under the body it
// moves - as a robot description's link owns its parent joint - with the sensors on it under it, the bodies themselves
// staying a flat list a narrow panel can read; selected, its card «Сочленение»: what it holds on what, its main
// freedom's limit and drive (none, a servo, a velocity motor, a DC motor) and, while the scene plays, its angle, its
// servo's target and its motor's torque - read from the running world (ke::RobotWorld: the engine's poses, the
// privileged loop's truth, not an encoder's count).
#include "SceneBuilder.h"

#include "Icons.h"
#include "InspectorWidgets.h"
#include "scene/Assembly.h"

#include <QBoxLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace ke;

namespace {

const double kDeg = 180.0 / 3.14159265358979323846;
const char* kAxisWords[] = {"ход по x", "ход по y", "ход по z", "поворот вокруг x", "поворот вокруг y", "поворот вокруг z"};

// What the joint turns about or slides along, in the world's words: «поворот вокруг вертикали (Y)», not the frame's own
// "x" (a person who chose "about the vertical" read "about x" and thought it wrong). Another axis: its world vector.
QString worldAxisWords(const SceneGraph& g, const JointObject& j, int dof) {
    if (dof % 3 != 0) return kAxisWords[dof];
    Vector3 anchor, axis;
    jointWorldFrame(g, j, anchor, axis);
    const bool turn = dof >= DofRx;
    const char* names[] = {"X", "вертикали (Y)", "Z"};
    for (int k = 0; k < 3; ++k)
        if (std::fabs(axis[k]) > 0.999_r) return QString(turn ? "поворот вокруг " : "ход вдоль ") + names[k] + (!turn && axis[k] < 0 ? ", к минусу" : "");
    return QString(turn ? "поворот вокруг оси (%1, %2, %3)" : "ход вдоль (%1, %2, %3)").arg(double(axis.x), 0, 'f', 2).arg(double(axis.y), 0, 'f', 2).arg(double(axis.z), 0, 'f', 2);
}

// The drive's kind as the card's list has it: 0 none, 1 servo, 2 velocity motor, 3 DC motor.
int driveKind(const JointDrive& d) { return !d.enabled ? 0 : d.constant > 0 ? 3 : d.stiffness > 0 ? 1 : 2; }

// The joint's kind in words, from its file word: hinge, slider, ball, weld...
QString kindWord(const JointObject& j) {
    const std::string w = jointKindWord(j);
    return w == "hinge" ? "шарнир" : w == "slider" ? "ползун" : w == "ball" ? "шаровой" : w == "weld" ? "сварка" : w == "cylinder" ? "цилиндрический"
         : w == "universal" ? "кардан" : w == "planar" ? "плоский" : "6 степеней";
}

QString driveWord(const JointObject& j) {
    const int dof = mainFreedom(j);
    if (dof < 0) return "сварка";
    const char* kinds[] = {"без привода", "серво", "мотор скорости", "мотор пост. тока"};
    return kindWord(j) + " · " + kinds[driveKind(j.dof[dof].drive)];
}

} // namespace

JointObject* SceneBuilder::jointById(uint32_t id) {
    for (JointObject& j : graph_.joints)
        if (id != 0 && j.id == id) return &j;
    return nullptr;
}

// The joints' rows: each under the body it moves (its link a; on the world: at the top), the sensors on it under it.
void SceneBuilder::addJointRows(std::map<uint32_t, QTreeWidgetItem*>& rows, std::map<uint32_t, uint32_t>& under) {
    for (const JointObject& j : graph_.joints) {
        auto* item = new QTreeWidgetItem;
        const int dof = mainFreedom(j);
        const bool driven = dof >= 0 && j.dof[dof].drive.enabled;
        item->setText(0, QString::fromStdString(j.name) + " — " + driveWord(j));
        item->setIcon(0, deviceIcon(driven ? DeviceIcon::Effort : DeviceIcon::Joint, 22));
        item->setData(0, Qt::UserRole, j.id);
        const SceneObject* on = findObject(graph_, j.b);
        item->setToolTip(0, "Сочленение: двигает это тело на «" + (on ? QString::fromStdString(on->name) : QString("мире")) +
                                "»; выберите — его привод и пределы");
        rows[j.id] = item;
        under[j.id] = j.a;
    }
    for (const SensorObject& s : graph_.sensors)
        if (findJoint(graph_, s.parent)) under[s.id] = s.parent;
}

void SceneBuilder::buildJointCard(QVBoxLayout* body) {
    jointCard_ = new ComponentCard("Сочленение", deviceIcon(DeviceIcon::Joint, 22));
    jointCard_->setObjectName("jointCard");
    jointCard_->setToolTip("Сочленение держит звено на теле; его главная степень свободы — поворот шарнира или ход ползуна");
    jointMount_ = new QLabel;
    jointMount_->setWordWrap(true);
    jointCard_->body()->addWidget(jointMount_);
    // The joint's main freedom and its drive; a turn in degrees on the card (5° a step), radians in the file.
    const auto free = [this] { return mainFreedom(jointDraft_) >= 0; };
    const auto f = [this]() -> JointFreedom& { return jointDraft_.dof[std::max(mainFreedom(jointDraft_), 0)]; };
    const auto d = [f]() -> JointDrive& { return f().drive; };
    const auto kind = [d] { return driveKind(d()); };
    const auto per = [this](const char* turn, const char* travel, bool angle = true) {
        return [=] { return mainFreedom(jointDraft_) >= DofRx ? FieldForm::Unit{turn, angle ? kDeg : 1, angle ? 5.0 : 0} : FieldForm::Unit{travel, 1, angle ? 0.01 : 0}; };
    };
    const auto when = [kind](std::vector<int> kinds) { return [=] { return std::count(kinds.begin(), kinds.end(), kind()) > 0; }; };
    jointFields_ = new FieldForm([this](bool click) { commitDraft(jointById(selectedId_), jointDraft_, click); });
    // Where the joint is and which way its axis points, in the world: a click that set it in the wrong place is mended
    // here (a newcomer's pendulum turned through its post, with nowhere to move the axis).
    const auto frame = [this](Vector3& at, Vector3& axis) { jointWorldFrame(graph_, jointDraft_, at, axis); };
    jointFields_->choice("Ось", {"X мира", "Y мира (вертикаль)", "Z мира", "своя"}, [=] {
        Vector3 at, axis;
        frame(at, axis);
        for (int k = 0; k < 3; ++k)
            if (std::fabs(std::fabs(axis[k]) - 1) < 1e-3f) return double(k);
        return 3.0;
    }, [=](double k) {
        if (k > 2) return;
        Vector3 at, axis, along(0.0f);
        frame(at, axis);
        along[int(k)] = 1;
        placeJoint(graph_, jointDraft_, at, along);
    }).name("jointAxis").shown(free).tip("Ось поворота шарнира или хода ползуна в мире; звенья остаются где стоят");
    for (int k = 0; k < 3; ++k)
        jointFields_->number(QString("Точка оси %1").arg(QChar('X' + k)), -1e4, 1e4, 0.01, 3, [=] {
            Vector3 at, axis;
            frame(at, axis);
            return double(at[k]);
        }, [=](double v) {
            Vector3 at, axis;
            frame(at, axis);
            at[k] = Real(v);
            placeJoint(graph_, jointDraft_, at, axis);
        }).unit([] { return FieldForm::Unit{" м"}; }).name(QString("jointAnchor%1").arg(QChar('X' + k))).tip("Где на звеньях стоит ось сочленения, в мире");
    jointFields_->toggle("Предел", [=] { return f().motion == JointMotion::Limited; }, [=](double on) { f().motion = on ? JointMotion::Limited : JointMotion::Free; })
        .shown(free).tip("Ограничить ход: звено упирается, как у настоящего робота по паспорту");
    for (Real JointFreedom::*end : {&JointFreedom::lower, &JointFreedom::upper})
        jointFields_->number(end == &JointFreedom::lower ? "От" : "До", -1e4, 1e4, 5, 2, [=] { return f().*end; }, [=](double v) { f().*end = Real(v); })
            .unit(per(" °", " м")).shown([=] { return free() && f().motion == JointMotion::Limited; });
    jointFields_->choice("Привод", {"нет", "серво: к цели", "мотор скорости", "мотор постоянного тока"}, kind, [=](double k) {
        JointDrive& drive = d(); // a kind picked starts from its usual numbers
        drive.enabled = k != 0;
        drive.stiffness = k == 1 ? (drive.stiffness > 0 ? drive.stiffness : 2000) : 0;
        drive.damping = k == 1 && drive.damping <= 0 ? 50 : drive.damping;
        drive.constant = k == 3 ? (drive.constant > 0 ? drive.constant : 0.05_r) : 0;
    }).shown(free).tip("Привод: сила по степени свободы. Серво и мотор постоянного тока считает код робота, мотор скорости — физический движок");
    const auto field = [&](const char* label, double min, double max, double step, int decimals, Real JointDrive::*v) -> FieldForm& {
        return jointFields_->number(label, min, max, step, decimals, [=] { return d().*v; }, [=](double x) { d().*v = Real(x); });
    };
    field("Цель", -1e5, 1e5, 5, 3, &JointDrive::target).unit(per(" °", " м")).shown(when({1}));
    field("Скорость", -1e5, 1e5, 1, 3, &JointDrive::targetVelocity).unit(per(" °/с", " м/с")).shown(when({1, 2}));
    field("Жёсткость", 0, 1e7, 100, 1, &JointDrive::stiffness).unit(per(" Н·м/рад", " Н/м", false)).shown(when({1}));
    field("Демпфирование", 0, 1e6, 10, 2, &JointDrive::damping).unit(per(" Н·м·с/рад", " Н·с/м", false)).shown(when({1, 2}));
    jointFields_->number("Предел силы", 0, 1e7, 10, 1, [=] { return std::isfinite(d().maxForce) ? d().maxForce : 0; },
                         [=](double v) { d().maxForce = v > 0 ? Real(v) : std::numeric_limits<Real>::infinity(); })
        .unit(per(" Н·м (0 — без предела)", " Н (0 — без предела)", false)).shown(when({1, 2}));
    field("Напряжение", -1e3, 1e3, 1, 2, &JointDrive::voltage).unit([] { return FieldForm::Unit{" В"}; }).shown(when({3}));
    field("Обмотка", 1e-4, 1e4, 0.1, 3, &JointDrive::resistance).unit([] { return FieldForm::Unit{" Ом"}; }).shown(when({3}));
    field("Константа K", 0, 100, 0.01, 4, &JointDrive::constant).shown(when({3}));
    jointCard_->body()->addWidget(jointFields_);
    jointLive_ = new QLabel;
    jointLive_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    jointLive_->setWordWrap(true);
    jointCard_->body()->addWidget(jointLive_);
    connect(jointCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    body->addWidget(jointCard_);
}

void SceneBuilder::fillJointCard() {
    const JointObject* j = jointById(selectedId_);
    jointCard_->setVisible(j != nullptr);
    if (!j) return;
    jointDraft_ = *j;
    const int dof = std::max(mainFreedom(*j), 0);
    const SceneObject* a = findObject(graph_, j->a);
    const SceneObject* b = findObject(graph_, j->b);
    jointMount_->setText("«" + QString::fromStdString(j->name) + "» · " + worldAxisWords(graph_, *j, dof) + " · держит «" + (a ? QString::fromStdString(a->name) : "?") +
                         "» на " + (b ? "«" + QString::fromStdString(b->name) + "»" : QString("мире")) + (mainFreedom(*j) < 0 ? " · сварено, свободы нет" : ""));
    jointFields_->fill();
    jointLive_->setText(editing() ? "Угол, цель и момент — при пуске сцены" : "…");
}

// What a joint does now, read on the simulation's thread: its coordinate, its servo's target, its drive's effort.
QString jointLiveText(const Simulation& sim, uint32_t id) {
    const RobotWorld* robot = sim.robot();
    const JointObject* j = robot ? robot->jointSpec(id) : nullptr;
    Real x[6];
    if (!j || !robot->jointCoordinates(*sim.physics(), id, x)) return j || (robot && findJoint(robot->graph(), id)) ? "не построено (нет тел)" : QString();
    const int dof = std::max(mainFreedom(*j), 0);
    const bool turn = dof >= DofRx;
    const double unit = turn ? kDeg : 1.0;
    const JointDrive& d = j->dof[dof].drive;
    QString t = QString(turn ? "угол %1 °" : "ход %1 м").arg(double(x[dof]) * unit, 0, 'f', 3);
    if (d.enabled && d.stiffness > 0 && d.constant == 0) t += QString(" · цель %1").arg(double(d.target) * unit, 0, 'f', 3);
    if (d.enabled) t += QString(turn ? "\nмомент мотора %1 Н·м" : "\nсила мотора %1 Н").arg(double(robot->jointEffort(*sim.physics(), id)), 0, 'f', 3);
    return t;
}
