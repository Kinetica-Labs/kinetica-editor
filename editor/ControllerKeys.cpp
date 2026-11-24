// A person's keys to the scene's panels (see ControllerKeys.h).
#include "ControllerKeys.h"
#include "UiLanguage.h"

#include "EditorOps.h"
#include "KeyPlace.h"
#include "SceneBuilder.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace {

std::string keyName(const QKeyEvent* e) {
    const int key = keyByPlace(e);
    return key == Qt::Key_Space ? "Space" : QKeySequence(key).toString().toStdString();
}

bool typing() {
    QWidget* w = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w) || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w);
}

const char* doing(ke::ControlDrive d) {
    switch (d) {
    case ke::ControlDrive::Plane: return uiText("Kinetica", "по полу");
    case ke::ControlDrive::Lift: return uiText("Kinetica", "вверх-вниз");
    case ke::ControlDrive::Turn: return uiText("Kinetica", "поворот");
    case ke::ControlDrive::Grip: return uiText("Kinetica", "схват");
    case ke::ControlDrive::Jog: return uiText("Kinetica", "сустав");
    case ke::ControlDrive::Duty: return uiText("Kinetica", "мотор");
    case ke::ControlDrive::Value: return uiText("Kinetica", "значение");
    }
    return "";
}

} // namespace

ControllerKeys::ControllerKeys(EditorOps* ops, SceneBuilder* builder, QObject* parent) : QObject(parent), ops_(ops), builder_(builder) {
    qApp->installEventFilter(this);
}

bool ControllerKeys::hasKeys() const {
    for (const ke::ControllerObject& c : builder_->graph().controllers)
        for (const ke::ControllerAction& a : c.actions)
            for (const ke::ControlBinding& b : a.bindings)
                if (b.device == "keys" && !b.controls.empty()) return true;
    return false;
}

bool ControllerKeys::bound(const std::string& key) const {
    for (const ke::ControllerObject& c : builder_->graph().controllers)
        for (const ke::ControllerAction& a : c.actions)
            for (const ke::ControlBinding& b : a.bindings)
                if (b.device == "keys" && std::find(b.controls.begin(), b.controls.end(), key) != b.controls.end()) return true;
    return false;
}

QString ControllerKeys::note() const {
    if (!active_) return QString();
    QStringList parts;
    for (const ke::ControllerObject& c : builder_->graph().controllers)
        for (const ke::ControllerAction& a : c.actions)
            for (const ke::ControlBinding& b : a.bindings) {
                if (b.device != "keys" || b.controls.empty()) continue;
                QStringList keys;
                for (const std::string& k : b.controls) keys << (k == "Space" ? QString(uiText("Kinetica", "пробел")) : QString::fromStdString(k));
                parts << keys.join(' ') + " — " + doing(a.drive);
            }
    return parts.join(" · ") + uiText("Kinetica", " · T — клавиши окну");
}

void ControllerKeys::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    held_.clear();
    send(); // let go: every value back to rest
    emit changed();
}

void ControllerKeys::sync() {
    const bool playing = builder_->mode() == SceneBuilder::Mode::Playing;
    const bool edited = builder_->editing();
    if (playing && !playing_ && !active_ && hasKeys()) setActive(true); // ▶ with a panel: the keys are its
    if (edited) setActive(false), toggled_.clear(), sent_.clear();
    else if (!playing && !held_.empty()) held_.clear(), send(); // paused: nothing held
    playing_ = playing;
}

// Each panel's actions from the keys held: a vector of four (up down left right), an axis of two (+ -), a button of
// one (held, or flipped by a press when a toggle). Sent where the value changed, one call a panel.
void ControllerKeys::send() {
    if (builder_->editing()) return;
    for (const ke::ControllerObject& c : builder_->graph().controllers) {
        QJsonObject values;
        for (const ke::ControllerAction& a : c.actions) {
            double x = 0, y = 0;
            bool keyed = false;
            for (const ke::ControlBinding& b : a.bindings) {
                if (b.device != "keys" || b.controls.empty()) continue;
                keyed = true;
                const auto down = [this](const std::string& k) { return held_.count(k) ? 1.0 : 0.0; };
                const std::vector<std::string>& k = b.controls;
                const double sign = b.invert ? -1 : 1;
                if (k.size() >= 4) x += sign * (down(k[3]) - down(k[2])), y += sign * (down(k[0]) - down(k[1]));
                else if (k.size() == 2) x += sign * (down(k[0]) - down(k[1]));
                else x += sign * (b.toggle ? (toggled_[std::to_string(c.id) + "/" + a.name] ? 1.0 : 0.0) : down(k[0]));
            }
            if (!keyed) continue;
            const QString text = a.value == ke::ControlValue::Vector2 ? QString("%1 %2").arg(x).arg(y) : QString::number(x);
            std::string& last = sent_[std::to_string(c.id) + "/" + a.name];
            if (last == text.toStdString() || (last.empty() && x == 0 && y == 0)) continue;
            last = text.toStdString();
            values.insert(QString::fromStdString(a.name), a.value == ke::ControlValue::Vector2 ? QJsonValue(QJsonArray{x, y}) : QJsonValue(x));
        }
        if (!values.isEmpty()) ops_->run("controller_input", QJsonObject{{"id", int(c.id)}, {"values", values}}, [](const QJsonObject&) {}, "ui");
    }
}

// Before shortcuts (ShortcutOverride): a bound key is the panel's while it is on; T flips it while the scene plays.
bool ControllerKeys::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type t = event->type();
    if ((t != QEvent::KeyPress && t != QEvent::KeyRelease && t != QEvent::ShortcutOverride) || !playing_ || typing())
        return QObject::eventFilter(watched, event);
    auto* k = static_cast<QKeyEvent*>(event);
    if (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) return QObject::eventFilter(watched, event);
    const std::string key = keyName(k);
    const bool flip = keyByPlace(k) == Qt::Key_T;
    if (!flip && !(active_ && bound(key))) return QObject::eventFilter(watched, event);
    event->accept();
    if (t == QEvent::ShortcutOverride || k->isAutoRepeat()) return true;
    if (flip) {
        if (t == QEvent::KeyPress && hasKeys()) setActive(!active_);
        return true;
    }
    if (t == QEvent::KeyPress) {
        held_.insert(key);
        for (const ke::ControllerObject& c : builder_->graph().controllers) // a toggle button flips on its press
            for (const ke::ControllerAction& a : c.actions)
                for (const ke::ControlBinding& b : a.bindings)
                    if (b.device == "keys" && b.toggle && b.controls.size() == 1 && b.controls[0] == key) {
                        bool& on = toggled_[std::to_string(c.id) + "/" + a.name];
                        on = !on;
                    }
    } else held_.erase(key);
    send();
    return true;
}
