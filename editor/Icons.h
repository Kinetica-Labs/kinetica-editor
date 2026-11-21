#pragma once
// Icons of the editor, drawn in code with QPainter: no image files, one consistent look (soft light
// from the top left, a thin darker outline, transparent background), crisp at any screen scale
// (each icon holds a 1x and a 2x pixmap). The shapes are shaded like small 3D objects so a beginner
// recognises "cube" and "sphere" at a glance; each component has a picture of what it makes of a shape.
#include "scene/SceneGraph.h"

#include <QIcon>
#include <QPixmap>
#include <QString>

// The components as the editor shows them, in the order of ke::Role: the rigid body and its collider
// (what it collides with - a component of its own: alone it is a fixed obstacle, with
// Rigid a moving body).
enum class RoleIcon { Rigid, Collider, Count };

// Buttons of the simulation: run, run and record (▶ with a red dot; the dot lit while it records, the two in turns blink),
// pause, stop, one
// step, back to the start, undo, redo, eye, lock,
// and the tools of the edit mode: select, move, rotate, scale.
enum class ControlIcon {
    Play, Record, Recording, Pause, Stop, Step, Reset, Undo, Redo, Visible, Hidden, Locked, Unlocked,
    ToolSelect, ToolMove, ToolRotate, ToolScale, Count
};

QIcon shapeIcon(ke::ShapeKind shape, int size = 48);
QIcon roleIcon(RoleIcon role, int size = 40);
QIcon controlIcon(ControlIcon control, int size = 32);
QPixmap rolePixmap(RoleIcon role, int size); // for small chips next to a name
// The kinds of collider as small wireframe glyphs: Авто, Коробка, Сфера, Капсула, Оболочка, Цилиндр.
QIcon colliderIcon(ke::ColliderKind kind, int size = 28);
// Many objects at once: a group (three cubes held together) and the patterns of an array - a row, a
// grid, a circle of small cubes.
enum class ObjectIcon { Group, ArrayLine, ArrayGrid, ArrayCircle, Count };
QIcon objectIcon(ObjectIcon kind, int size = 28);
// Lights and cameras of a scene: a sun, a light bulb (a lamp), a spotlight's cone, a camera.
enum class SceneIcon { Sun, Bulb, Spot, Camera, Count };
QIcon sceneIcon(SceneIcon kind, int size = 28);

// The devices of mechatronics: the sensors (in the order of ke::SensorKind), the electrics, and Sensor - any sensor, the
// toolbar's button that opens their list.
enum class DeviceIcon { Encoder, Imu, Sonar, Touch, Wrench, Effort, Stereo, Battery, Driver, Joint, Controller, Sensor, Count }; // ke::SensorKind first
QIcon deviceIcon(DeviceIcon kind, int size = 28);

// The layout buttons of the window: the left side bar, the layout menu,
// the bottom panel, the right side bar. The area is filled while it is open (the icon's On state).
// The magnet's elements (ke::kMagnetPresets): a turn, a travel, a wheel, a weld, a ball.
enum class LinkIcon { Rotary, Linear, Wheel, Weld, Ball, Count };
QIcon linkIcon(LinkIcon kind, int size = 28);
QPixmap linkPixmap(LinkIcon kind, int size);

enum class LayoutIcon { LeftBar, Customize, BottomPanel, RightBar, Count };
QIcon layoutIcon(LayoutIcon kind, int size = 20);

