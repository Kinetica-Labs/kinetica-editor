// The pointer and the keyboard in the API (EditorOps.h): an agent tries the user interface itself -
// it finds where a point of the world is on the screen, moves the mouse there, clicks and presses keys
// in the 3D view - the events a hand would send, so the magnet's preview and click are tested as used.
#include "EditorOps.h"
#include "UiWindowSpace.h"

#include "Viewport.h"

#include <QCoreApplication>
#include <QCursor>
#include <QApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

namespace {

Qt::MouseButton button(const QString& name) {
    return name == "right" ? Qt::RightButton : name == "middle" ? Qt::MiddleButton : Qt::LeftButton;
}

Qt::KeyboardModifiers modifiers(const QJsonArray& names) {
    Qt::KeyboardModifiers m;
    for (const QJsonValue& n : names)
        m |= n.toString() == "shift" ? Qt::ShiftModifier : n.toString() == "ctrl" ? Qt::ControlModifier
           : n.toString() == "alt" ? Qt::AltModifier : Qt::NoModifier;
    return m;
}

// A mouse event at once (the 3D view's drags: the call answers after them), or posted (a click in the window: a button that
// opens a menu runs the menu's own loop, and the call that pressed it would not answer until the menu closed).
void send(QWidget* to, QEvent::Type type, const QPointF& at, Qt::MouseButton b, Qt::MouseButtons held, Qt::KeyboardModifiers m, bool post = false) {
    if (post) return QCoreApplication::postEvent(to, new QMouseEvent(type, at, to->mapToGlobal(at), b, held, m));
    QMouseEvent e(type, at, to->mapToGlobal(at), b, held, m);
    QCoreApplication::sendEvent(to, &e);
}

// The window the keyboard goes to: a modal dialog over the editor, else the editor's window.
QWidget* keyWindow(QWidget* main) {
    QWidget* modal = QApplication::activeModalWidget();
    return modal && modal->isVisible() ? modal : main;
}

// What has the keyboard: an open popup, else the key window's own focus - not QApplication::focusWidget(), which an
// inactive window (the unattended editor's) leaves at whatever had it last: a combo box's list after its popup closed,
// so the digits typed into a field went into a closed list.
QWidget* keyFocus(QWidget* main) {
    if (QWidget* popup = uiPopupWindow(main)) return popup;
    return keyWindow(main)->focusWidget();
}

// The focus moved inside a window as a hand moves it: Qt sends the focus-out and focus-in events only in the active window,
// and the unattended editor's is never made active - a field left by Tab kept its typed number unapplied (a spin box
// applies it on focus-out) and the next was not selected (it selects all on a Tab focus-in). Here they are sent by hand.
class FocusMove {
public:
    explicit FocusMove(QWidget* window) : window_(window), was_(window->focusWidget()) {}
    void done(Qt::FocusReason why) {
        QWidget* now = window_->focusWidget();
        if (window_->isActiveWindow() || now == was_) return; // Qt sent them itself, or nothing moved
        if (was_) { QFocusEvent out(QEvent::FocusOut, why); QCoreApplication::sendEvent(was_, &out); }
        if (now) { QFocusEvent in(QEvent::FocusIn, why); QCoreApplication::sendEvent(now, &in); }
    }
private:
    QWidget* window_;
    QPointer<QWidget> was_;
};

// A press gives the keyboard to what was pressed, as Qt does for a real one (only for real ones: an event sent by a program
// does not move the focus) - the nearest widget up from it that takes focus by a click.
void focusByClick(QWidget* pressed) {
    FocusMove move(pressed->window());
    for (QWidget* w = pressed; w; w = w->isWindow() ? nullptr : w->parentWidget())
        if (w->isEnabled() && (w->focusPolicy() & Qt::ClickFocus)) return w->setFocus(Qt::MouseFocusReason), move.done(Qt::MouseFocusReason);
}

// A key as the window system hands it to the application - the path a real key takes: the window's shortcuts are
// tried first (Space plays, W E R pick tools), then the focused widget gets it. `ru`: the Russian layout on, as a user
// typing Russian has it - the key on the QWERTY place of `latin` arrives as its Cyrillic letter, with the place's
// X11 key code and the layout's group in the native state, so Qt's shortcuts match across layouts as they do for a hand.
void windowKey(QWidget* window, QChar latin, int qtKey, Qt::KeyboardModifiers mods, bool ru, bool press, bool release, QWidget* popup = nullptr) {
    static const char* rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const int first[] = {24, 38, 52}; // X11 key codes (evdev + 8) of each row's first key
    static const char16_t cyr[] = u"ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ";
    static const char latinOrder[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
    quint32 scan = qtKey == Qt::Key_Space ? 65 : qtKey == Qt::Key_Escape ? 9 : 0;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; rows[r][i]; ++i)
            if (latin.toUpper() == QChar(rows[r][i])) scan = quint32(first[r] + i);
    int key = qtKey;
    QString text = qtKey == Qt::Key_Space ? " " : latin.isLetter() ? QString(latin.toLower()) : latin.isPrint() ? QString(latin) : QString();
    for (int i = 0; ru && latinOrder[i]; ++i)
        if (latin.toUpper() == QChar(latinOrder[i])) key = int(cyr[i]), text = QString(QChar(cyr[i])).toLower();
    QWindow* w = window->window()->windowHandle();
    const quint32 sym = text.isEmpty() ? 0 : 0x1000000u + text[0].unicode(), state = ru ? 0x2000u : 0u; // group 1 in bits 13-14
    if (mods & (Qt::ControlModifier | Qt::AltModifier)) text.clear(); // Ctrl+A selects, it does not type an "a"
    if (popup) { // a visible popup consumes keys before the main window's application shortcuts
        QKeyEvent down(QEvent::KeyPress, key, mods, text), up(QEvent::KeyRelease, key, mods, text);
        if (press) QCoreApplication::sendEvent(popup, &down);
        if (release) QCoreApplication::sendEvent(popup, &up);
        return;
    }
    if (press) QWindowSystemInterface::handleExtendedKeyEvent(w, QEvent::KeyPress, key, mods, scan, sym, state, text);
    if (release) QWindowSystemInterface::handleExtendedKeyEvent(w, QEvent::KeyRelease, key, mods, scan, sym, state, text);
    QWindowSystemInterface::flushWindowSystemEvents();
}

} // namespace

void EditorOps::addInputOps() {
    addSync("view_project", "The pixel of the 3D view (origin top-left) where a world point is seen", "{point: [x,y,z]}",
            [this](const QJsonObject& a) {
        const QJsonArray p = a.value("point").toArray();
        const QVector3D world(float(p[0].toDouble()), float(p[1].toDouble()), float(p[2].toDouble()));
        const QVector3D ndc = view_->camera().projection(float(view_->width()) / std::max(1, view_->height()))
                                  .map(view_->camera().view().map(world));
        return ok({{"x", (ndc.x() + 1) * 0.5 * view_->width()}, {"y", (1 - ndc.y()) * 0.5 * view_->height()},
                   {"visible", std::abs(ndc.x()) <= 1 && std::abs(ndc.y()) <= 1}});
    });
    addSync("ui_pointer", "The mouse: move to a pixel (drag: with the button held), click / press / release a button there, or turn "
            "the wheel there by `notches` (positive: away from the user) - in the 3D view's pixels, or (space 'window') in the whole "
            "window's, as its screenshot shows them (an open menu included; a move there shows the tooltip at once). The user's "
            "own cursor stays where it is unless cursor: true",
            "{action: 'move'|'drag'|'click'|'press'|'release'|'wheel', x: number, y: number, notches?: int, space?: 'view'|'window', "
            "button?: 'left'|'right'|'middle', modifiers?: ['shift'|'ctrl'|'alt'], cursor?: bool}",
            [this](const QJsonObject& a) {
        static const QStringList known{"action", "x", "y", "notches", "space", "button", "modifiers", "cursor"};
        for (const QString& key : a.keys()) // a guessed name (dy, delta) was silently ignored: the wheel turned up
            if (!key.startsWith('_') && !known.contains(key))
                return error("ui_pointer has no '" + key + "'; it takes " + known.join(", "));
        QPointF at(a.value("x").toDouble(), a.value("y").toDouble());
        QWidget* to = view_;
        const bool inWindow = a.value("space").toString() == "window";
        if (inWindow) { // the widget under that pixel of the window, as a hand finds it - an open menu's first
            const QPoint global = window_->mapToGlobal(at.toPoint());
            QWidget* top = uiInputWindow(window_, global);
            const QPoint local = uiWindowLocal(top, global);
            QWidget* child = top->childAt(local);
            to = child ? child : top;
            at = to == top ? QPointF(local) : QPointF(to->mapFrom(top, local));
            if (a.value("cursor").toBool()) QCursor::setPos(global); // the real cursor only when asked: it is the user's
        }
        const QString action = a.value("action").toString("move");
        const Qt::MouseButton b = button(a.value("button").toString());
        const Qt::KeyboardModifiers m = modifiers(a.value("modifiers").toArray());
        send(to, QEvent::MouseMove, at, Qt::NoButton, action == "drag" ? Qt::MouseButtons(b) : Qt::NoButton, m);
        if (inWindow && action == "move") { // a hand resting there: the widget's tooltip, as the window shows it after a pause
            QHelpEvent tip(QEvent::ToolTip, at.toPoint(), to->mapToGlobal(at.toPoint()));
            QCoreApplication::sendEvent(to, &tip);
        }
        if (inWindow && (action == "press" || action == "click")) focusByClick(to);
        if (action == "press" || action == "click") send(to, QEvent::MouseButtonPress, at, b, b, m, inWindow);
        if (action == "release" || action == "click") send(to, QEvent::MouseButtonRelease, at, b, Qt::NoButton, m, inWindow);
        for (int i = 0, n = a.value("notches").toInt(1); action == "wheel" && i < std::abs(n); ++i) // one event a notch, as a wheel sends
            for (QWidget* w = to; w; w = w->isWindow() ? nullptr : w->parentWidget()) { // up to whoever takes it, as Qt passes a
                const QPointF here = w->mapFromGlobal(to->mapToGlobal(at));            // real wheel on (not a program's: Qt 6)
                QWheelEvent e(here, to->mapToGlobal(at), QPoint(), QPoint(0, n > 0 ? 120 : -120), Qt::NoButton, m, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(w, &e);
                if (e.isAccepted()) break;
            }
        return ok({{"widget", to->metaObject()->className()}});
    });
    addSync("ui_type", "Text typed into what has the keyboard focus - a field a click gave it to - as a person types it: each character "
            "a key press and release with its text (capitals, digits, Cyrillic, punctuation). Enter, Tab and Escape are ui_key's",
            "{text: str}", [this](const QJsonObject& a) {
        // The window's own focus: the editor is not made the active window (that would take the keyboard from the user's
        // other programs), and Qt names no application focus for an inactive window.
        QWidget* focus = keyFocus(window_);
        if (!focus) return error("nothing has the keyboard focus: click a field first");
        for (const QChar ch : a.value("text").toString()) {
            const int key = ch.isDigit() || (ch.unicode() < 128 && ch.isLetter()) ? ch.toUpper().unicode() : int(Qt::Key_unknown);
            const Qt::KeyboardModifiers m = ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
            QKeyEvent press(QEvent::KeyPress, key, m, QString(ch)), release(QEvent::KeyRelease, key, m, QString(ch));
            QCoreApplication::sendEvent(focus, &press);
            QCoreApplication::sendEvent(focus, &release);
        }
        return ok({{"focus", focus->metaObject()->className()}});
    });
    addSync("ui_key", "A key pressed and released in the 3D view: 'A', '1', 'Escape', 'Delete'... - in an open popup menu first, "
            "where Escape closes it; a key the view does not take goes on to the window's shortcuts (F frames the selection), as a real one does; "
            "action 'press' holds it down until a 'release' (W A S D fly the view while the right button is held). "
            "window: as the window system delivers a real key - the window's shortcuts first, then whatever has the focus "
            "(not the view forced); layout 'ru': the key on that QWERTY place typed with the Russian layout on",
            "{key: str, action?: 'tap'|'press'|'release', window?: bool, layout?: 'us'|'ru'}", [this](const QJsonObject& a) {
        const QKeySequence seq(a.value("key").toString());
        if (seq.isEmpty()) return error("no key '" + a.value("key").toString() + "'");
        const QKeyCombination k = seq[0];
        const QString action = a.value("action").toString("tap");
        QWidget* popup = uiPopupWindow(window_);
        if (a.value("window").toBool() || popup) { // an open visible popup always takes keys first
            const QString name = a.value("key").toString();
            FocusMove move(keyWindow(window_));
            windowKey(popup ? popup : keyWindow(window_), name.size() == 1 ? name[0] : QChar(), k.key(), k.keyboardModifiers(),
                      a.value("layout").toString() == "ru", action != "release", action != "press", popup);
            const bool back = k.key() == Qt::Key_Backtab || (k.key() == Qt::Key_Tab && (k.keyboardModifiers() & Qt::ShiftModifier));
            if (!popup) move.done(back ? Qt::BacktabFocusReason : k.key() == Qt::Key_Tab ? Qt::TabFocusReason : Qt::OtherFocusReason);
            QWidget* focus = keyFocus(window_);
            return ok({{"focus", focus ? focus->metaObject()->className() : "none"}});
        }
        FocusMove move(window_);
        view_->setFocus();
        move.done(Qt::OtherFocusReason);
        QKeyEvent over(QEvent::ShortcutOverride, k.key(), k.keyboardModifiers(), a.value("key").toString());
        QCoreApplication::sendEvent(view_, &over);
        QKeyEvent press(QEvent::KeyPress, k.key(), k.keyboardModifiers()), release(QEvent::KeyRelease, k.key(), k.keyboardModifiers());
        if (action != "release") QCoreApplication::sendEvent(view_, &press);
        if (action != "release" && !press.isAccepted()) { // not the view's key: the window's shortcut, as a hand's F frames the selection
            const QString name = a.value("key").toString();
            windowKey(window_, name.size() == 1 ? name[0] : QChar(), k.key(), k.keyboardModifiers(), a.value("layout").toString() == "ru",
                      true, action != "press");
            return ok();
        }
        if (action != "press") QCoreApplication::sendEvent(view_, &release);
        return ok();
    });
}
