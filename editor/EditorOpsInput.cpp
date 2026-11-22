// The pointer and the keyboard in the API (EditorOps.h): an agent tries the user interface itself -
// it finds where a point of the world is on the screen, moves the mouse there, clicks and presses keys
// in the 3D view - the events a hand would send, so the magnet's preview and click are tested as used.
#include "EditorOps.h"
#include "SceneBuilder.h"
#include "UiLanguage.h"
#include "UiWindowSpace.h"

#include "Viewport.h"

#include <QCoreApplication>
#include <QCursor>
#include <QAction>
#include <QApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocale>
#include <QMenu>
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

// Whether an action's place is one a hand reaches: a widget of the window, or a menu hung (through menus) on one - a context
// menu built for one right click and kept is not, as Qt's shortcut map has it.
bool reachable(QWidget* window, QWidget* w, int depth = 0) {
    if (auto* menu = qobject_cast<QMenu*>(w)) {
        for (QObject* o : menu->menuAction()->associatedObjects())
            if (auto* up = qobject_cast<QWidget*>(o); up && depth < 8 && reachable(window, up, depth + 1)) return true;
        return false;
    }
    return w == window || (window->isAncestorOf(w) && w->isVisible());
}

// The window's shortcut for a key, as Qt's shortcut map finds it - which it does only in the active window, and the
// unattended editor's never is: a tester's Space, Esc, F and Ctrl+Z did nothing (a trial). An enabled action with the key
// among its shortcuts, in a place a hand reaches; two such: neither, as Qt calls it ambiguous.
QAction* windowShortcut(QWidget* window, const QKeySequence& key) {
    QAction* found = nullptr;
    for (QAction* a : window->findChildren<QAction*>()) {
        if (!a->isEnabled() || a->shortcutContext() == Qt::WidgetShortcut || !a->shortcuts().contains(key)) continue;
        bool placed = false;
        for (QObject* o : a->associatedObjects())
            if (auto* w = qobject_cast<QWidget*>(o)) placed = placed || reachable(window, w);
        if (!placed || a == found) continue;
        if (found) return nullptr;
        found = a;
    }
    return found;
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
    if (press && !window->isActiveWindow()) { // the focused widget first (a field takes its letters), then the window's shortcut
        QWidget* focus = window->focusWidget() ? window->focusWidget() : window;
        QKeyEvent over(QEvent::ShortcutOverride, key, mods, text);
        over.ignore();
        QCoreApplication::sendEvent(focus, &over);
        if (QAction* a = over.isAccepted() ? nullptr : windowShortcut(window, QKeySequence(QKeyCombination(mods, Qt::Key(qtKey))))) {
            a->trigger();
            return;
        }
    }
    if (press) QWindowSystemInterface::handleExtendedKeyEvent(w, QEvent::KeyPress, key, mods, scan, sym, state, text);
    if (release) QWindowSystemInterface::handleExtendedKeyEvent(w, QEvent::KeyRelease, key, mods, scan, sym, state, text);
    QWindowSystemInterface::flushWindowSystemEvents();
}

} // namespace

void EditorOps::addInputOps() {
    addSync("view_draw", "Lines, points and words over the 3D view in world metres - a plugin's or an agent's picture (a planned "
                         "path, a force, a sensor's ray, a label), by key: the same key again replaces it, clear takes it off (a key ending "
                         "in / - all under it; none - all). circle {centre: [x,y,z], radius: m, axis?: [x,y,z] (y if none)} - a ring "
                         "about the axis; box {centre: [x,y,z], size: [x,y,z]} - its twelve edges along the world's axes: a gizmo's "
                         "ring and bounds. size: a point's width, px (6) - and a label's letters, pt, when larger than plain ones. "
                         "fill: a flat picture, filled - closed outlines, each a row 'M x y L x y C x1 y1 x2 y2 x y Z' in its own units (x right, "
                         "y up), painted over the scene. space world (the default): on its own plane at `at`, turned by `turn` (Euler degrees), a "
                         "unit `scale` metres (1); label: on the screen at the world's point `at`, facing the viewer; screen: in the frame at "
                         "`place`, fractions of the view from its left top - in both a unit is `scale` of the view's height. object: the picture "
                         "is that object's and goes where it goes - one in the world lies on it (`at` and `turn` in the object's own frame), a label "
                         "hangs at `at` from the object's place along the world's axes, above a body however it tumbles; a click on it selects the object. "
                         "stroke: an edge of that colour round every outline, `stroke_width` of the picture's units wide (0.05) - letters read on any background. Drawn while "
                         "the scene plays: gone at Stop. Not a scene edit: no undo, no journal",
            "{key?: str, line?: [[x,y,z]], segments?: [[x,y,z]], points?: [[x,y,z]], circle?: {}, box?: {}, text?: str, at?: [x,y,z], "
            "colour?: [r,g,b], size?: number, fill?: [str], space?: 'world'|'label'|'screen', turn?: [x,y,z], place?: [x, y], scale?: number, "
            "object?: int, stroke?: [r,g,b], stroke_width?: number, clear?: bool}",
            [this](const QJsonObject& a) {
        const QString key = a.value("key").toString();
        if (a.value("clear").toBool()) return ok({{"cleared", view_->clearDrawings(key)}});
        if (key.isEmpty()) return error("key: the drawing's name - the same key again replaces it");
        const auto point = [](const QJsonValue& v) {
            const QJsonArray p = v.toArray();
            return ke::Vector3(float(p.at(0).toDouble()), float(p.at(1).toDouble()), float(p.at(2).toDouble()));
        };
        Viewport::Drawing d;
        size_t many = 0;
        for (const char* list : {"line", "segments", "points"}) {
            const QJsonArray given = a.value(list).toArray();
            many += size_t(given.size());
            for (const QJsonValue& v : given) {
                if (v.toArray().size() != 3) return error(QString(list) + ": each point is [x, y, z]");
                const ke::Vector3 p = point(v);
                if (list[0] == 'p') d.points.push_back(p);
                else (list[0] == 'l' ? d.strip : d.segments).insert((list[0] == 'l' ? d.strip : d.segments).end(), {p.x, p.y, p.z, 0.0f});
            }
        }
        if (many > 200000) return error("at most 200000 points a drawing");
        if (a.contains("circle")) { // a ring: 48 sides, closed
            const QJsonObject c = a.value("circle").toObject();
            const float radius = float(c.value("radius").toDouble());
            ke::Vector3 about = c.contains("axis") ? point(c.value("axis")) : ke::Vector3(0.0f, 1.0f, 0.0f);
            if (c.value("centre").toArray().size() != 3 || !(radius > 0) || ke::length(about) < 1e-6f)
                return error("circle: {centre: [x, y, z], radius: metres, axis?: [x, y, z]}");
            if (!d.strip.empty()) return error("circle or line: one line a drawing");
            about = ke::normalize(about);
            const ke::Vector3 centre = point(c.value("centre"));
            const ke::Vector3 u = ke::normalize(ke::cross(about, std::fabs(about.x) < 0.9f ? ke::Vector3(1.0f, 0.0f, 0.0f) : ke::Vector3(0.0f, 1.0f, 0.0f)));
            const ke::Vector3 w = ke::cross(about, u);
            for (int k = 0; k <= 48; ++k) {
                const float t = float(k) * 2 * float(ke::kPi) / 48;
                const ke::Vector3 p = centre + (u * ke::cos(t) + w * ke::sin(t)) * radius;
                d.strip.insert(d.strip.end(), {p.x, p.y, p.z, 0.0f});
            }
        }
        if (a.contains("box")) { // the twelve edges: from each corner along each axis it is the low end of
            const QJsonObject b = a.value("box").toObject();
            if (b.value("centre").toArray().size() != 3 || b.value("size").toArray().size() != 3)
                return error("box: {centre: [x, y, z], size: [x, y, z]}");
            const ke::Vector3 centre = point(b.value("centre")), half = point(b.value("size")) * 0.5f;
            for (int corner = 0; corner < 8; ++corner)
                for (int axis = 0; axis < 3; ++axis) {
                    if (corner >> axis & 1) continue;
                    for (int end : {corner, corner | 1 << axis}) {
                        const ke::Vector3 p(centre.x + (end & 1 ? half.x : -half.x), centre.y + (end & 2 ? half.y : -half.y),
                                            centre.z + (end & 4 ? half.z : -half.z));
                        d.segments.insert(d.segments.end(), {p.x, p.y, p.z, 0.0f});
                    }
                }
        }
        d.play = !builder_->editing();
        if (d.segments.size() % 8 != 0) return error("segments: points in pairs - a start and an end each");
        d.text = a.value("text").toString();
        if (a.contains("at")) d.at = point(a.value("at"));
        if (a.contains("colour")) {
            const QJsonArray c = a.value("colour").toArray();
            d.colour = QColor::fromRgbF(float(std::clamp(c.at(0).toDouble(), 0.0, 1.0)), float(std::clamp(c.at(1).toDouble(), 0.0, 1.0)),
                                        float(std::clamp(c.at(2).toDouble(), 0.0, 1.0)));
        }
        d.size = float(std::clamp(a.value("size").toDouble(6), 1.0, 64.0));
        if (a.contains("fill")) {
            QStringList rows;
            for (const QJsonValue& row : a.value("fill").toArray()) rows << row.toString();
            QString why;
            const double edge = a.contains("stroke") ? a.value("stroke_width").toDouble(0.05) : 0.0;
            if (edge < 0 || edge > 10) return error("stroke_width: from 0 to 10 of the picture's units");
            if (!(d.fill = Viewport::fillOf(rows, why, edge))) return error(why);
            if (const QJsonArray c = a.value("stroke").toArray(); c.size() == 3)
                d.stroke = QColor::fromRgbF(float(std::clamp(c.at(0).toDouble(), 0.0, 1.0)), float(std::clamp(c.at(1).toDouble(), 0.0, 1.0)),
                                            float(std::clamp(c.at(2).toDouble(), 0.0, 1.0)));
            else if (a.contains("stroke")) return error("stroke: [r, g, b], each from 0 to 1");
            const QString space = a.value("space").toString("world");
            if (space != "world" && space != "label" && space != "screen") return error("space: world, label or screen");
            d.space = space == "world" ? Viewport::Drawing::Space::World : space == "label" ? Viewport::Drawing::Space::Label : Viewport::Drawing::Space::Screen;
            d.scale = float(a.value("scale").toDouble(1));
            if (!(d.scale > 0) || d.scale > 1000) return error("scale: above 0, at most 1000");
            if (a.contains("turn")) { // as an object's rotation in the scene file
                ke::SceneObject turned;
                turned.rotationDeg = point(a.value("turn"));
                d.turn = ke::objectRotation(turned);
            }
            if (const QJsonArray xy = a.value("place").toArray(); a.contains("place") && xy.size() != 2) return error("place: [x, y], fractions of the view from its left top");
            else if (xy.size() == 2) d.place = QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
            d.object = uint32_t(a.value("object").toInt());
            if (d.object && !findObject(builder_->graph(), d.object)) return error(QString("no object %1").arg(d.object));
        }
        if (d.strip.empty() && d.segments.empty() && d.points.empty() && d.text.isEmpty() && !d.fill)
            return error("nothing to draw: line, segments, points, circle, box, text or fill");
        view_->setDrawing(key, std::move(d));
        return ok({{"key", key}, {"drawings", int(view_->drawings())}});
    });
    // The ruler of the tool shelf (MainWindow::buildToolShelf) is this operation: a drag gives it its two points.
    addSync("view_measure", "The ruler: a line in the view from a world point to another, its length over its middle; answers the length "
            "and the difference along x, y and z (metres). None of the two: the line off. Changes nothing of the scene",
            "{point?: [x,y,z], to?: [x,y,z]}", [this](const QJsonObject& a) {
        static const QString key = "editor/ruler";
        if (!a.contains("point") && !a.contains("to")) return ok({{"cleared", view_->clearDrawings(key)}});
        const QJsonArray f = a.value("point").toArray(), t = a.value("to").toArray();
        if (f.size() != 3 || t.size() != 3) return error("point and to: two world points [x, y, z]");
        const ke::Vector3 from(float(f.at(0).toDouble()), float(f.at(1).toDouble()), float(f.at(2).toDouble())),
            to(float(t.at(0).toDouble()), float(t.at(1).toDouble()), float(t.at(2).toDouble())), by = to - from;
        const double length = double(ke::length(by));
        Viewport::Drawing d;
        d.colour = QColor(255, 214, 90);
        d.strip = {from.x, from.y, from.z, 0.0f, to.x, to.y, to.z, 0.0f};
        d.points = {from, to};
        d.text = QLocale().toString(length, 'f', 3) + uiText("Kinetica", " м");
        d.at = (from + to) * 0.5f;
        d.play = !builder_->editing();
        view_->setDrawing(key, std::move(d));
        return ok({{"length", length}, {"by", QJsonArray{double(by.x), double(by.y), double(by.z)}}});
    });
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
