// Controllers in the scene builder (see SceneBuilder.h): the scene's abstract panels (ke::ControllerObject) as rows
// of the scene list and a card - their actions with what each drives and how fast, and their bindings with
// «Назначить»: press the keys the binding is to take, one by one for a key composite (forward, back, left, right),
// each by its place on the keyboard (KeyPlace.h). A panel is the scene's data, made by the operations
// controller_add and controller_set as well; the running world does not take a panel's input yet (src/sim).
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "InspectorWidgets.h"
#include "KeyPlace.h"

#include <QApplication>
#include <QBoxLayout>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

using namespace ke;

namespace {

QString driveText(const ControllerAction& a) {
    switch (a.drive) {
    case ControlDrive::Plane: return uiText("Kinetica", "цель по полу, м/с");
    case ControlDrive::Lift: return uiText("Kinetica", "цель вверх-вниз, м/с");
    case ControlDrive::Turn: return uiText("Kinetica", "поворот цели, °/с");
    case ControlDrive::Grip: return uiText("Kinetica", "схват, предел силы Н");
    case ControlDrive::Jog: return a.dof >= DofRx ? uiText("Kinetica", "сустав, °/с") : uiText("Kinetica", "сустав, м/с");
    case ControlDrive::Duty: return uiText("Kinetica", "ШИМ драйвера");
    }
    return QString();
}

QString deviceText(const std::string& d) { return d == "keys" ? uiText("Kinetica", "клавиатура") : d == "gamepad" ? uiText("Kinetica", "геймпад") : d == "mouse" ? uiText("Kinetica", "мышь") : QString::fromStdString(d); }

QString controlText(const ControlBinding& b) {
    static const std::pair<const char*, const char*> pad[] = {
        {"left_stick", uiText("Kinetica", "левый стик")}, {"right_stick", uiText("Kinetica", "правый стик")}, {"left_x", uiText("Kinetica", "левый стик ↔")}, {"left_y", uiText("Kinetica", "левый стик ↕")},
        {"right_x", uiText("Kinetica", "правый стик ↔")}, {"right_y", uiText("Kinetica", "правый стик ↕")}, {"left_trigger", uiText("Kinetica", "курок LT")}, {"right_trigger", uiText("Kinetica", "курок RT")},
        {"left_bumper", "LB"}, {"right_bumper", "RB"}, {"left", uiText("Kinetica", "левая кнопка")}, {"right", uiText("Kinetica", "правая кнопка")}, {"middle", uiText("Kinetica", "колесо")}};
    QStringList names;
    for (const std::string& c : b.controls) {
        QString n = QString::fromStdString(c);
        for (const auto& [code, text] : pad)
            if (c == code) n = text;
        names << (n.size() == 1 ? n.toUpper() : n);
    }
    return names.join(" ") + (b.toggle ? uiText("Kinetica", " (переключатель)") : "") + (b.invert ? uiText("Kinetica", " (наоборот)") : "");
}

// What the next press of a binding being set stands for: a key composite's directions, or the one control.
QString captureStep(const ControllerAction& a, const ControlBinding& b, size_t got) {
    if (b.device != "keys") return a.value == ControlValue::Vector2 ? uiText("Kinetica", "стик") : a.value == ControlValue::Axis ? uiText("Kinetica", "ось или курок") : uiText("Kinetica", "кнопку");
    static const char* four[] = {uiText("Kinetica", "вперёд"), uiText("Kinetica", "назад"), uiText("Kinetica", "влево"), uiText("Kinetica", "вправо")};
    static const char* two[] = {uiText("Kinetica", "плюс"), uiText("Kinetica", "минус")};
    const size_t n = b.controls.size();
    if (n >= 4) return QString("%1 (%2/4)").arg(four[std::min<size_t>(got, 3)]).arg(got + 1);
    if (n == 2) return QString("%1 (%2/2)").arg(two[std::min<size_t>(got, 1)]).arg(got + 1);
    return uiText("Kinetica", "клавишу");
}

} // namespace

ControllerObject* SceneBuilder::controllerIn(uint32_t id) {
    for (ControllerObject& c : graph_.controllers)
        if (id != 0 && c.id == id) return &c;
    return nullptr;
}

const ControllerObject* SceneBuilder::controllerById(uint32_t id) const { return const_cast<SceneBuilder*>(this)->controllerIn(id); }

void SceneBuilder::buildControllerCard(QVBoxLayout* body) {
    controllerCard_ = new ComponentCard(uiText("Kinetica", "Пульт"), deviceIcon(DeviceIcon::Controller, 22));
    controllerCard_->setObjectName("controllerCard");
    controllerCard_->setToolTip(uiText("Kinetica", "Абстрактный пульт: действия двигают цель руки, схват, сустав или мотор; кнопки клавиатуры, "
                                "мыши и геймпада назначаются действиям"));
    controllerInfo_ = new QLabel;
    controllerInfo_->setWordWrap(true);
    controllerCard_->body()->addWidget(controllerInfo_);
    controllerActions_ = new QTableWidget(0, 3);
    controllerActions_->setObjectName("controllerActions");
    controllerActions_->setHorizontalHeaderLabels({uiText("Kinetica", "Действие"), uiText("Kinetica", "Двигает"), uiText("Kinetica", "Скорость")});
    controllerBinds_ = new QTableWidget(0, 3);
    controllerBinds_->setObjectName("controllerBinds");
    controllerBinds_->setHorizontalHeaderLabels({uiText("Kinetica", "Действие"), uiText("Kinetica", "Кнопки"), ""});
    for (QTableWidget* t : {controllerActions_, controllerBinds_}) {
        t->verticalHeader()->setVisible(false);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionMode(QAbstractItemView::NoSelection);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        t->horizontalHeader()->setStretchLastSection(true);
        t->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    }
    for (QTableWidget* t : {controllerActions_, controllerBinds_}) { // the middle column takes the width, nothing runs off the panel
        t->horizontalHeader()->setStretchLastSection(false);
        t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    controllerCard_->body()->addWidget(controllerActions_);
    controllerCard_->body()->addWidget(controllerBinds_);
    connect(controllerCard_, &ComponentCard::removeClicked, this, &SceneBuilder::removeSelected);
    body->addWidget(controllerCard_);
}

void SceneBuilder::fillControllerCard() {
    const ControllerObject* c = controllerIn(selectedId_);
    controllerCard_->setVisible(c != nullptr);
    if (!c) return;
    controllerInfo_->setText(QString::fromStdString(c->name) + uiText("Kinetica", " · «Назначить» — нажмите клавиши, которые возьмёт привязка; Esc — отмена"));
    controllerActions_->setRowCount(int(c->actions.size()));
    for (size_t k = 0; k < c->actions.size(); ++k) {
        const ControllerAction& a = c->actions[k];
        controllerActions_->setItem(int(k), 0, new QTableWidgetItem(QString::fromStdString(a.name)));
        controllerActions_->setItem(int(k), 1, new QTableWidgetItem(driveText(a)));
        auto* speed = makeSpin(0, 1000, a.drive == ControlDrive::Turn ? 5 : 0.01, a.drive == ControlDrive::Turn ? 0 : 3, "");
        speed->setValue(double(a.speed));
        connect(speed, &QDoubleSpinBox::valueChanged, this, [this, k](double v) { onControllerSpeed(k, v); });
        controllerActions_->setCellWidget(int(k), 2, speed);
    }
    fillControllerBinds(*c);
}

void SceneBuilder::fillControllerBinds(const ControllerObject& c) {
    int rows = 0;
    for (const ControllerAction& a : c.actions) rows += int(a.bindings.size());
    controllerBinds_->setRowCount(rows);
    int row = 0;
    for (size_t k = 0; k < c.actions.size(); ++k)
        for (size_t b = 0; b < c.actions[k].bindings.size(); ++b, ++row) {
            const ControlBinding& bind = c.actions[k].bindings[b];
            controllerBinds_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(c.actions[k].name)));
            auto* keys = new QTableWidgetItem(deviceIcon(DeviceIcon::Controller, 16), controlText(bind));
            keys->setToolTip(deviceText(bind.device) + ": " + controlText(bind));
            if (bind.device != "gamepad") keys->setIcon(QIcon());
            keys->setText((bind.device == "mouse" ? uiText("Kinetica", "мышь: ") : "") + controlText(bind));
            controllerBinds_->setItem(row, 1, keys);
            auto* set = new QPushButton(uiText("Kinetica", "Назначить"));
            set->setObjectName(QString("bind_%1_%2").arg(k).arg(b));
            set->setEnabled(bind.device == "keys"); // a key by a key; a pad's and the mouse's controls are named in the scene file
            connect(set, &QPushButton::clicked, this, [this, k, b, set] { startCapture(k, b, set); });
            controllerBinds_->setCellWidget(row, 2, set);
        }
}

void SceneBuilder::onControllerSpeed(size_t action, double speed) {
    ControllerObject* c = controllerIn(selectedId_);
    if (filling_ || !c || action >= c->actions.size()) return;
    remember(true); // a number dragged: one undo step
    controllerIn(selectedId_)->actions[action].speed = Real(speed);
    applyEdit(0);
}

void SceneBuilder::startCapture(size_t action, size_t binding, QPushButton* button) {
    if (capture_.button) finishCapture(false);
    const ControllerObject* c = controllerIn(selectedId_);
    if (!c || action >= c->actions.size() || binding >= c->actions[action].bindings.size()) return;
    capture_ = BindCapture{c->id, action, binding, {}, button};
    button->setText(uiText("Kinetica", "Нажмите: ") + captureStep(c->actions[action], c->actions[action].bindings[binding], 0));
    qApp->installEventFilter(this);
}

// A key (by its place on the keyboard): kept; the last one needed writes the binding.
void SceneBuilder::captureControl(const std::string& control) {
    const ControllerObject* c = controllerIn(capture_.controller);
    if (!c || !capture_.button) return finishCapture(false);
    const ControllerAction& a = c->actions[capture_.action];
    const ControlBinding& b = a.bindings[capture_.binding];
    capture_.got.push_back(control);
    const size_t need = b.device == "keys" ? b.controls.size() : 1;
    if (capture_.got.size() < need) return capture_.button->setText(uiText("Kinetica", "Нажмите: ") + captureStep(a, b, capture_.got.size()));
    finishCapture(true);
}

void SceneBuilder::finishCapture(bool keep) {
    qApp->removeEventFilter(this);
    const BindCapture done = capture_;
    capture_ = BindCapture();
    ControllerObject* c = controllerIn(done.controller);
    if (keep && c) {
        remember();
        controllerIn(done.controller)->actions[done.action].bindings[done.binding].controls = done.got;
        applyEdit(0);
    }
    if (controllerIn(selectedId_)) fillControllerBinds(*controllerIn(selectedId_));
}

// While a binding is being set the keys are its (Esc gives up); everything else goes on as it would.
bool SceneBuilder::eventFilter(QObject* watched, QEvent* event) {
    if (!capture_.button || (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)) return QObject::eventFilter(watched, event);
    auto* k = static_cast<QKeyEvent*>(event);
    event->accept();
    if (event->type() == QEvent::ShortcutOverride || k->isAutoRepeat()) return true;
    const ControllerObject* c = controllerIn(capture_.controller);
    const bool keys = c && c->actions[capture_.action].bindings[capture_.binding].device == "keys";
    if (k->key() == Qt::Key_Escape || !keys) return finishCapture(false), true;
    const int key = keyByPlace(k);
    const std::string name = key == Qt::Key_Space ? "Space" : QKeySequence(key).toString().toStdString();
    if (!name.empty()) captureControl(name);
    return true;
}
