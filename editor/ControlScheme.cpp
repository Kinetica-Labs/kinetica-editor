// The control schemes (see ControlScheme.h): one table per scheme, Alt + a button the same in all.
#include "ControlScheme.h"
#include "UiLanguage.h"

#include <QSettings>

CameraMove cameraMoveFor(ControlScheme s, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    if (modifiers & Qt::AltModifier) { // Alt + a button: the camera in every scheme
        if (button == Qt::LeftButton) return CameraMove::Orbit;
        if (button == Qt::MiddleButton) return CameraMove::Pan;
        if (button == Qt::RightButton) return CameraMove::Dolly;
        return CameraMove::None;
    }
    switch (s) {
    case ControlScheme::Simple:
        if (button == Qt::MiddleButton) return CameraMove::Pan;
        if (button == Qt::RightButton) return CameraMove::Orbit;
        break;
    case ControlScheme::Keys:
        if (button == Qt::MiddleButton) {
            if (modifiers & Qt::ShiftModifier) return CameraMove::Pan;
            if (modifiers & Qt::ControlModifier) return CameraMove::Dolly;
            return CameraMove::Orbit;
        }
        break;
    case ControlScheme::AltFly:
        if (button == Qt::MiddleButton) return CameraMove::Pan;
        if (button == Qt::RightButton) return CameraMove::Look;
        break;
    }
    return CameraMove::None;
}

bool rightButtonFlies(ControlScheme s) { return s != ControlScheme::Keys; }

bool letterKeys(ControlScheme s) { return s == ControlScheme::Keys; }

QString controlSchemeName(ControlScheme s) {
    switch (s) {
    case ControlScheme::Simple: return uiText("Kinetica", "Простая");
    case ControlScheme::Keys: return uiText("Kinetica", "СКМ и клавиши G/R/S");
    case ControlScheme::AltFly: return uiText("Kinetica", "Alt и полёт на ПКМ");
    }
    return {};
}

QString cameraHint(ControlScheme s) {
    switch (s) {
    case ControlScheme::Simple: return uiText("Kinetica", "ПКМ: осмотр (клик — меню, +WASD — полёт) · СКМ: панорама · колесо: зум");
    case ControlScheme::Keys: return uiText("Kinetica", "СКМ: вращать · Shift+СКМ: панорама · Ctrl+СКМ, колесо: зум · ПКМ: меню");
    case ControlScheme::AltFly: return uiText("Kinetica", "Alt+ЛКМ: вращать · СКМ: панорама · Alt+ПКМ, колесо: зум · ПКМ: осмотр, +WASD — полёт");
    }
    return {};
}

ControlScheme savedControlScheme() {
    const int v = QSettings().value("controls/scheme", 0).toInt();
    return v >= 0 && v <= 2 ? ControlScheme(v) : ControlScheme::Simple;
}

void saveControlScheme(ControlScheme s) { QSettings().setValue("controls/scheme", int(s)); }
