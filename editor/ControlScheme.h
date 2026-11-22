#pragma once
// Which mouse button moves the camera, and how - the three schemes the editor offers.
//   Простая (the default): the left button never moves the camera - it selects, draws a selection
//     box from empty space and drags the gizmo; the right button dragged orbits around the point
//     under the cursor, clicked opens the menu, held with W A S D (Q / E down / up, Shift faster)
//     flies; the middle button pans; the wheel zooms towards the cursor.
//   СКМ и клавиши G/R/S: the middle button orbits, Shift+middle pans, Ctrl+middle zooms; G / R / S move,
//     turn and scale from the keyboard, X deletes, Shift+D duplicates.
//   Alt и полёт на ПКМ: the camera through Alt + a button; the right button looks around in place and
//     flies with W A S D.
// In every scheme Alt+left orbits, Alt+middle pans and Alt+right zooms.
#include <QString>
#include <Qt>

enum class ControlScheme { Simple, Keys, AltFly };
enum class CameraMove { None, Orbit, Pan, Dolly, Look };

// What a drag with this button (and these keys held) does to the camera; None: the button belongs to
// the tools (selection, the gizmo, grabbing a body).
CameraMove cameraMoveFor(ControlScheme s, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
// Holding the right button and pressing W A S D flies (Простая, Alt и полёт на ПКМ).
bool rightButtonFlies(ControlScheme s);
// G / R / S keyboard transforms, X deletes, Shift+D duplicates: only "СКМ и клавиши G/R/S".
bool letterKeys(ControlScheme s);
QString controlSchemeName(ControlScheme s); // "Простая", "СКМ и клавиши G/R/S", "Alt и полёт на ПКМ"
// The camera part of the status line: "ПКМ: осмотр (клик — меню) · СКМ: панорама · колесо: зум".
QString cameraHint(ControlScheme s);

// The scheme the user chose, kept in QSettings ("controls/scheme"); Простая when never chosen.
ControlScheme savedControlScheme();
void saveControlScheme(ControlScheme s);
