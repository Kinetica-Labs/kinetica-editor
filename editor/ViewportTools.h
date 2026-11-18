#pragma once
// Mouse interaction strategies for the Viewport (Strategy pattern): the viewport forwards mouse
// events to the active tool - the camera, grabbing a body in play, editing, the assembly magnet,
// making shapes - so a new tool plugs in without touching it.
// Which button moves the camera is the control scheme's business (ControlScheme.h); the tools ask
// the viewport for it, so the left button never moves the camera unless Alt is held.

#include "ControlScheme.h"
#include "Gizmo.h"
#include "OrbitCamera.h"
#include "scene/SceneGraph.h"

#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector3D>

#include <vector>

class QMouseEvent;
class Viewport;

class ViewportTool {
public:
    virtual ~ViewportTool() = default;
    virtual void press(Viewport&, QMouseEvent*) {}
    virtual void move(Viewport&, QMouseEvent*) {}
    virtual void release(Viewport&, QMouseEvent*) {}
};

// The camera: what a drag does is decided at the press from the button, the keys held and the
// scheme - orbit around the point under the cursor (it stays where it is on screen), pan, zoom, or
// look around in place. While the right button is held and W A S D fly the camera, a drag looks
// around instead of orbiting (Unity, Unreal).
class CameraTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;
    CameraMove active() const { return move_; }

private:
    QPoint last_;
    CameraMove move_ = CameraMove::None;
    QVector3D pivot_;
};

// Play mode: the left button grabs a body of the running world with a mouse joint - the cursor ray
// picks the body (its mesh), the hit point becomes the anchor, and dragging moves the target on the
// plane through the anchor facing the camera. Other buttons (and Alt+left) move the camera.
class GrabTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;

private:
    bool grabbing_ = false;
    ke::Vector3 planePoint_, planeNormal_;
    CameraTool camera_;
};

// Edit mode (the scene builder, nothing simulated):
//   left button - on a gizmo handle: drag it; a click on an object selects it (again on the same
//     spot: the one behind it; Shift or Ctrl + click adds or takes it away); a click on empty space
//     selects nothing; a drag from empty space draws a selection box (Shift adds, Ctrl takes away);
//     Shift + drag of an object slides it across the floor; Shift held when a handle drag starts
//     clones the selection (the popover asks how many); Shift pressed during a drag is "finer";
//   right button - a click opens the menu, a drag moves the camera (the scheme says how);
//   middle button, Alt + any button - the camera.
// With no button pressed the handle or object under the mouse is highlighted. During a keyboard
// transform the left button confirms it and the right one cancels it.
class EditTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;
    bool boxSelecting() const { return box_; }
    bool cloning() const { return cloning_; } // this handle drag began with Shift: it clones
    QRectF boxRect() const { return QRectF(pressPos_, boxEnd_).normalized(); }

private:
    bool pressTransform(Viewport& v, QMouseEvent* e); // a keyboard transform or a handle drag takes the press
    void pressLeft(Viewport& v, QMouseEvent* e);
    bool moveGizmo(Viewport& v, QMouseEvent* e);      // true: the gizmo took the move
    void startLeftDrag(Viewport& v, QMouseEvent* e);  // a floor slide or the selection box
    void hover(Viewport& v, QMouseEvent* e);
    void startFloorDrag(Viewport& v, QMouseEvent* e);

    GizmoHandle pending_ = GizmoHandle::None; // a handle pressed; the drag starts after 3 px
    bool floorDrag_ = false;                  // Shift+drag of an object (Shift is not "fine" then)
    bool pressShift_ = false;                 // Shift was down at the left press
    bool cloning_ = false;                    // the handle drag clones (Shift at its start)
    bool cloneShiftHeld_ = false;             // ... and that Shift is still down: not "finer" yet
    bool clickPending_ = false, rightClickPending_ = false, box_ = false;
    uint32_t pressedEntity_ = 0;              // the object under the left press (0: empty space)
    QPointF pressPos_, rightPressPos_, boxEnd_;
    ke::Vector2 virtualMouse_, lastMouse_;    // Shift slows a handle drag to a tenth of the mouse
    CameraTool camera_;
};

// The assembly magnet (edit mode; the window answers its signals): with no button pressed the face under the mouse shows
// where a new link would go; the left button pressed there opens the pie of ready links, the drag
// picks one, the release puts it (a release without a drag: the suggested one). The other buttons
// (and Alt + left) move the camera.
class MagnetTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;

private:
    bool clickPending_ = false; // the left button went down on the magnet's business
    QPointF pressPos_;
    CameraTool camera_;
};

// What the creation mode makes: a shape of «Фигура ▾», a light of «Свет ▾» or a camera.
struct CreateWhat {
    enum class Kind { Shape, Light, Camera };
    Kind kind = Kind::Shape;
    ke::ShapeKind shape = ke::ShapeKind::Box;
    ke::LightKind light = ke::LightKind::Point;
};

// Making things one after another with the mouse (edit mode), step by step as 3ds Max's Create panel,
// each step named in the view's hint («Куб · шаг 2/3 ●●○ · противоположный угол»):
//   a box, a plane: a corner, the opposite corner, (the box) its height;
//   a cylinder, a cone: the centre, the radius, the height; a sphere: the centre, the radius;
//   a lamp: its place, its height; the sun, a spot, a camera: the place, the height, where it looks.
// A place starts on the surface under the cursor - the top of an object (its face looks mostly up),
// else the floor y = 0 - where a small cross waits. Each step ends with a click, or with the release
// of a drag begun at the step before (press, drag, release: the footprint at once). The height follows
// the mouse along the vertical through its foot as the view shows it. The thing being made is drawn as
// a wireframe with its size next to the cursor; Ctrl snaps to 5 cm; Backspace goes a step back; Esc or
// a right click drops the thing begun, and with nothing begun leaves the mode. The middle button,
// Alt + any button and a right drag move the camera. Done, the viewport emits `created` and the window
// makes it with one API call (one undo step, one console line).
class CreateTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;
    void reset();                                              // waiting for the first click, nothing drawn
    bool back(Viewport& v);                                    // Backspace: a step back (false: nothing begun)
    bool cancel(Viewport& v);                                  // Esc: the thing begun dropped (false: nothing begun)
    const std::vector<float>& ghost() const { return ghost_; } // line pairs x y z 0: the thing, or the start's cross
    QString hint(const CreateWhat& what) const;                // the step, what the mouse does now, the keys
    QString sizeText(const CreateWhat& what) const;            // the size so far in metres (empty: nothing begun)

private:
    enum class Step { Place, Base, Height, Aim }; // the first click; the footprint; the height; where it looks
    std::vector<Step> stepsOf(const CreateWhat& what) const;
    bool surfaceAt(Viewport& v, const QPointF& pos, ke::Vector3& at) const; // a top face or the floor
    bool done(const CreateWhat& what) const;                // the step now has a size worth keeping
    void confirm(Viewport& v, const QPointF& pos, bool snap); // the step ends there: the next one, or made
    void track(Viewport& v, const QPointF& pos, bool snap);   // the step now to the cursor, the ghost

    int step_ = 0;                            // index into stepsOf: 0 waits for the first click
    ke::Vector3 start_{0.0f}, end_{0.0f};     // the first click on the surface; the footprint's far point
    ke::Vector3 target_{0.0f};                // where a light or a camera looks
    float height_ = 0;
    bool armed_ = false, rightClick_ = false; // a left press of this tool; a right press that may be a click
    QPointF pressPos_, risePos_, rightPos_, lastPos_;
    ke::Vector3 position_{0.0f}, size_{0.0f}; // the shape as it stands now: its centre and size; a light's place
    std::vector<float> ghost_;
    CameraTool camera_;
};
