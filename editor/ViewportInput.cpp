// The viewport's mouse and keys (see Viewport.h and ControlScheme.h): the navigation cube, the
// tools, the camera flight on W A S D, the keys of a transform in progress, the edit mode's picking,
// a click on a sensor or a body in play, making shapes with the mouse (on, off, Esc), and the line of
// the status bar that says what the buttons and keys do right now.
#include "Viewport.h"
#include "UiLanguage.h"

#include "SensorGlyphs.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QTimer>
#include <QToolTip>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using ke::Vector3;

namespace {

// A letter key by its place on the keyboard, not by the layout: with the Russian layout on, the key a
// QWERTY board prints W on arrives as Ц (QKeyEvent::key() is the layout's letter), and the flight's
// W A S D would do nothing. JCUKEN rows mapped onto QWERTY, as the keys sit.
int keyByPlace(const QKeyEvent* e) {
    static const char16_t ru[] = u"ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬ";
    static const char latin[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
    for (int i = 0; i < 26; ++i)
        if (e->key() == int(ru[i])) return int(latin[i]); // Qt's letter keys are the upper-case code points
    return e->key();
}

bool isFlyKey(int k) { return k == Qt::Key_W || k == Qt::Key_A || k == Qt::Key_S || k == Qt::Key_D || k == Qt::Key_Q || k == Qt::Key_E; }

} // namespace

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------
void Viewport::frameScene() {
    if (followedBody_) followBody(0);
    if (focusBox_.valid()) {
        camera_.frame(focusBox_, 40.0f, 24.0f);
        update();
        return;
    }
    if (!snap_ || !snap_->domain.valid()) return;
    camera_.frame(snap_->domain, 55.0f, 22.0f); // a 3/4 view of the world box
    update();
}

// Looks along ball k of the navigation cube (+X, -X, +Y, -Y, +Z, -Z) at the scene: +Y is the top view.
void Viewport::viewAlong(int k) {
    camera_.lookAlong(-NavCube::axis(k));
    update();
    updateHint();
}

// The point the camera turns around and zooms towards: what the cursor is on (an object, a body),
// else the floor, else the depth of the current target.
QVector3D Viewport::pointUnderCursor(const QPointF& pos) const {
    const Ray ray = camera_.screenRay(pos, size());
    const auto q = [](const Vector3& p) { return QVector3D(p.x, p.y, p.z); };
    if (editMode_) {
        const std::vector<PickHit> hits = pickAll(ray);
        if (!hits.empty()) return q(hits.front().point);
    } else {
        int body;
        Vector3 hit;
        if (pickAnyBody(ray, body, hit)) return q(hit);
    }
    const float toFloor = intersectAxisPlane(ray, 1, 0.0f);
    if (toFloor > 0.0f && toFloor < 20.0f * camera_.distance()) return q(ray.at(toFloor));
    const QVector3D f = camera_.forward(), o = q(ray.origin), d = q(ray.dir);
    const float t = QVector3D::dotProduct(camera_.target() - o, f) / std::max(1e-4f, QVector3D::dotProduct(d, f));
    return o + d * t;
}

ViewportTool& Viewport::toolFor(Qt::MouseButton button, const QPointF& pos) {
    if (editMode_ && createMode_) return createTool_; // making shapes: the left button draws one
    if (editMode_ && magnetMode_) return magnetTool_; // the magnet: a click puts a link on a face
    if (editMode_ || playHandleAt(pos)) return editTool_; // the scene builder's edit mode: select, gizmo, camera; a goal's gizmo in play
    if (button == Qt::LeftButton && snap_) return grabTool_; // a body of the running world
    return cameraTool_;
}

// ---------------------------------------------------------------------------
// The mouse
// ---------------------------------------------------------------------------
// The navigation cube takes a left press on it: a click on a ball looks along that axis, a drag orbits.
bool Viewport::navPress(QMouseEvent* e) {
    if (!overlayVisible_ || grabbed_ || e->button() != Qt::LeftButton) return false;
    const int h = navCube_.hit(e->position(), viewMatrix(), size());
    if (h == NavCube::kOutside) return false;
    navPressed_ = true;
    navDragged_ = false;
    navPressHit_ = h;
    navLast_ = e->pos();
    return true;
}

void Viewport::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason); // the keys (W A S D, X Y Z during a drag) come here
    if (navPress(e)) return;
    if (!toolOp_.isEmpty() && e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier)) { // a plugin's tool takes the click
        grabbed_ = &pluginTool_;
        return pluginTool_.press(*this, e);
    }
    if (e->button() == Qt::RightButton) rightHeld_ = true;
    if (e->button() == Qt::LeftButton) labPress_ = e->position(), labPressed_ = true; // a Laboratory click?
    const bool alt = e->modifiers() & Qt::AltModifier;
    if (!editMode_ && snap_ && !grabbed_ && e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        const Ray ray = camera_.screenRay(e->position(), size());
        float toGoal;
        if (const uint32_t goal = pickHandle(ray, toGoal)) { // Ctrl on a goal turns it, under its own gizmo too
            emit entityClicked(goal);
            grabbed_ = &goalTool_;
            goalTool_.begin(*this, e, goal, ray.at(toGoal));
            return updateHint();
        }
    }
    if (!editMode_ && snap_ && !grabbed_ && e->button() == Qt::LeftButton && !alt && !(e->modifiers() & Qt::ShiftModifier) &&
        !playHandleAt(e->position())) {
        const Ray ray = camera_.screenRay(e->position(), size());
        uint32_t sensor = 0;
        float nearest = 1e30f, glyph = 0, t;
        for (const ke::SensorVis& v : snap_->sensors)
            if (sensorHit(v, ray, t) && t < nearest) nearest = t, sensor = v.id, glyph = float(v.size);
        int body;
        Vector3 hit;
        float toHandle;
        const uint32_t handle = pickHandle(ray, toHandle);
        const bool onBody = pickAnyBody(ray, body, hit);
        // A glyph over a robot's link means the sensor, a see-through goal around the hand the goal. A thing lying
        // about (a dynamic body, no robot's link) is taken through them: through a glyph behind its face, through
        // a goal it lies inside (behind the goal's face by less than the goal's size); a glyph is drawn larger than
        // its sensor, so the thing's face within twice the glyph's scale behind it is not hidden by it.
        const float toBody = onBody ? ke::length(hit - ray.origin) : 1e30f;
        const ke::Frame::Body* under = onBody ? &snap_->bodies[size_t(body)] : nullptr;
        const bool thing = under && under->movable && !under->robot;
        float goalSize = 0;
        for (const ke::Frame::Body& b : snap_->bodies)
            if (handle && b.marker && b.object == handle && b.mesh) goalSize = ke::maxComp(b.mesh->bounds().extent());
        // A see-through goal before a sensor's glyph inside it is the goal: the glyphs on a gripper sit in its goal.
        const bool goalFirst = handle && toHandle <= toBody + 1e-3f && !(thing && toBody < toHandle + goalSize);
        if (sensor && !(thing && toBody < nearest + 2 * glyph) && !(goalFirst && toHandle < nearest)) emit sensorClicked(sensor);
        else if (goalFirst) { // selected, and taken: a drag moves it (Ctrl: turns it)
            emit entityClicked(handle);
            grabbed_ = &goalTool_;
            goalTool_.begin(*this, e, handle, ray.at(toHandle));
            return updateHint();
        } else if (onBody) emit bodyClicked(body);
    }
    if (!grabbed_) grabbed_ = &toolFor(e->button(), e->position());
    grabbed_->press(*this, e);
    updateHint();
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    lastMouse_ = e->position();
    if (navPressed_) {
        const QPoint d = e->pos() - navLast_;
        if (!navDragged_ && QLineF(e->position(), QPointF(navLast_)).length() < 3) return;
        navDragged_ = true;
        navLast_ = e->pos();
        camera_.rotate(float(d.x()), float(d.y()));
        update();
        return;
    }
    const int hover = navCube_.hit(e->position(), viewMatrix(), size());
    if (hover != navHover_) {
        navHover_ = hover;
        update();
    }
    if (grabbed_) grabbed_->move(*this, e);
    else if (editMode_ && createMode_) createTool_.move(*this, e); // the cross under the mouse, or a shape's height
    else if (editMode_ && magnetMode_) magnetTool_.move(*this, e); // the face under the mouse
    else if (editMode_) editTool_.move(*this, e); // hover, or a keyboard transform following the mouse
    else if (gizmoShown()) { // play: an IK goal's gizmo lights up under the mouse
        const GizmoHandle h = gizmo_.hitTest(gizmoView(), ke::Vector2(float(e->position().x()), float(e->position().y())));
        if (h != gizmo_.hover()) gizmo_.setHover(h), setCursor(h != GizmoHandle::None ? Qt::SizeAllCursor : Qt::ArrowCursor), update();
    }
    updateHint();
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (navPressed_ && e->button() == Qt::LeftButton) {
        if (!navDragged_ && navPressHit_ >= 0) viewAlong(navPressHit_);
        navPressed_ = false;
        return;
    }
    if (grabbed_) grabbed_->release(*this, e);
    if (e->button() == Qt::RightButton) stopFlying();
    if (e->buttons() == Qt::NoButton) grabbed_ = nullptr;
    if (e->button() == Qt::LeftButton && labPressed_) { // a press and a release within 4 px: a click
        labPressed_ = false;
        if (labInspect_ && QLineF(labPress_, e->position()).length() < 4) emit labClicked(e->position());
    }
    update();
    updateHint();
}

void Viewport::wheelEvent(QWheelEvent* e) {
    if (followedBody_) camera_.zoom(e->angleDelta().y() / 120.0f);
    else camera_.zoomToward(pointUnderCursor(e->position()), e->angleDelta().y() / 120.0f);
    update();
}

// A double click is F: the selected object framed (on empty space the first click selected nothing,
// so the whole scene). Not on the navigation cube or a gizmo handle.
void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
    if (editMode_ && createMode_) return mousePressEvent(e); // making shapes: a quick second click is a click
    if (e->button() != Qt::LeftButton || navCube_.hit(e->position(), viewMatrix(), size()) != NavCube::kOutside) return;
    if (!editMode_) return frameScene();
    if (gizmoShown() && gizmo_.hitTest(gizmoView(), ke::Vector2(float(e->position().x()), float(e->position().y()))) != GizmoHandle::None) return;
    emit frameSelectedRequested();
}

// The keys go to the view the mouse is over: the flight's and the
// transforms' keys work without a click into the view first.
void Viewport::enterEvent(QEnterEvent* e) {
    if (window()->isActiveWindow() && !QApplication::activePopupWidget()) setFocus(Qt::MouseFocusReason);
    QOpenGLWidget::enterEvent(e);
}

void Viewport::leaveEvent(QEvent*) {
    if (navHover_ != NavCube::kOutside) navHover_ = NavCube::kOutside, update();
}

// ---------------------------------------------------------------------------
// The keys
// ---------------------------------------------------------------------------
namespace {
// The magnet's keys: 1-8 a ready link, 0 back to the suggested one, A the axis, Tab flips it, M
// mirrors, Esc out of the magnet.
bool isMagnetKey(const QKeyEvent* k) {
    const int key = k->key();
    return (k->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier &&
           ((key >= Qt::Key_0 && key <= Qt::Key_5) || key == Qt::Key_A || key == Qt::Key_Tab || key == Qt::Key_M ||
            key == Qt::Key_Escape);
}
} // namespace

void Viewport::keyPressEvent(QKeyEvent* e) {
    if (!toolOp_.isEmpty() && e->key() == Qt::Key_Escape) return setPluginTool({}, {});
    if (editMode_ && createMode_ && e->key() == Qt::Key_Escape) { // the thing begun dropped; nothing begun: the mode left
        if (!createTool_.cancel(*this)) setCreateMode(false);
        return updateHint();
    }
    if (editMode_ && createMode_ && e->key() == Qt::Key_Backspace) return createTool_.back(*this), updateHint();
    if (editMode_ && magnetMode_ && !rightHeld_ && isMagnetKey(e)) return emit magnetKey(e->key());
    if (gizmo_.modal() && modalKey(e)) return updateHint();
    if (editTool_.draggingHandle() && e->key() == Qt::Key_Escape) return editTool_.cancelHandle(*this), updateHint();
    if (gizmo_.dragging()) return dragKey(e), updateHint(); // a handle drag: Esc cancels, X Y Z lock an axis
    if (flyKey(e, true)) return;
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    if (editMode_ && letterKeys(scheme_) && mods == Qt::NoModifier) { // G move, R turn, S scale
        if (e->key() == Qt::Key_G) return startModal(GizmoMode::Translate);
        if (e->key() == Qt::Key_R) return startModal(GizmoMode::Rotate);
        if (e->key() == Qt::Key_S) return startModal(GizmoMode::Scale);
    }
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::keyReleaseEvent(QKeyEvent* e) {
    if (!flyKey(e, false)) QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::focusOutEvent(QFocusEvent* e) {
    stopFlying();
    QOpenGLWidget::focusOutEvent(e);
}

// During a handle drag: Esc puts the object back; X / Y / Z switch the drag to that axis, Shift+X /
// Y / Z to the plane across it (axis locking).
void Viewport::dragKey(QKeyEvent* e) {
    const int k = e->key();
    if (k == Qt::Key_Escape) return cancelGizmoDrag();
    if (k != Qt::Key_X && k != Qt::Key_Y && k != Qt::Key_Z) return;
    const int axis = k - Qt::Key_X;
    const GizmoHandle axes[3] = {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ};
    const GizmoHandle planes[3] = {GizmoHandle::PlaneYZ, GizmoHandle::PlaneXZ, GizmoHandle::PlaneXY};
    gizmo_.switchHandle((e->modifiers() & Qt::ShiftModifier) ? planes[axis] : axes[axis], gizmoView());
    const bool snap = e->modifiers() & Qt::ControlModifier;
    emit gizmoMoved(gizmo_.drag(gizmoView(), ke::Vector2(float(lastMouse_.x()), float(lastMouse_.y())), snap));
    update();
}

// W A S D while the right button is held: fly. Returns true when the key was a flight key.
bool Viewport::flyKey(QKeyEvent* e, bool down) {
    if (followedBody_) return false;
    if (!rightHeld_ || !rightButtonFlies(scheme_) || !isFlyKey(keyByPlace(e))) return false;
    if (e->isAutoRepeat()) return true;
    if (down) flyKeys_.insert(keyByPlace(e));
    else flyKeys_.erase(keyByPlace(e));
    if (down && !flyTimer_->isActive()) {
        flyClock_.start();
        flyTimer_->start();
    }
    flew_ = flew_ || down;
    updateHint();
    return true;
}

// One step of the flight: forward / back (W / S), sideways (A / D), down / up (Q / E); Shift is 4x.
void Viewport::flyTick() {
    if (flyKeys_.empty() || !rightHeld_) return flyTimer_->stop();
    const float dt = std::min(0.05f, float(flyClock_.restart()) * 1e-3f);
    const bool fast = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    const float speed = std::max(1.0f, 0.6f * camera_.distance()) * (fast ? 4.0f : 1.0f);
    const auto held = [this](int k) { return flyKeys_.count(k) ? 1.0f : 0.0f; };
    const QVector3D step = camera_.forward() * (held(Qt::Key_W) - held(Qt::Key_S)) + camera_.right() * (held(Qt::Key_D) - held(Qt::Key_A)) +
                           QVector3D(0, 1, 0) * (held(Qt::Key_E) - held(Qt::Key_Q));
    camera_.translate(step * (speed * dt));
    update();
}

void Viewport::stopFlying() {
    rightHeld_ = false;
    flyKeys_.clear();
    flyTimer_->stop();
    flew_ = false;
}

bool Viewport::event(QEvent* e) {
    // Tab is the magnet's flip, not the next widget's focus (QWidget::event takes it before keyPressEvent).
    if (e->type() == QEvent::KeyPress && editMode_ && magnetMode_ && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Tab) {
        emit magnetKey(Qt::Key_Tab);
        return true;
    }
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        const bool flight = rightHeld_ && rightButtonFlies(scheme_) && isFlyKey(keyByPlace(k));
        const bool magnet = editMode_ && magnetMode_ && !rightHeld_ && isMagnetKey(k);
        const bool create = editMode_ && createMode_ && (k->key() == Qt::Key_Escape || k->key() == Qt::Key_Backspace);
        const bool tool = !toolOp_.isEmpty() && k->key() == Qt::Key_Escape;
        if (gizmo_.dragging() || flight || magnet || create || tool) { // X, Esc, digits, W A S D... belong to the drag, not to the window's shortcuts
            e->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

// ---------------------------------------------------------------------------
// Edit mode: picking, the gizmo, keyboard transforms
// ---------------------------------------------------------------------------
void Viewport::setEditMode(bool on) {
    if (!on) cancelModal(), setCreateMode(false);
    editMode_ = on;
    setMouseTracking(true); // the navigation cube lights up under the mouse in every mode
    if (!on) {
        gizmo_.setHover(GizmoHandle::None);
        hoveredEntity_ = 0;
        hoverBodies_.clear();
        ghostBodies_.clear();
    }
    update();
    updateHint();
}

bool Viewport::playHandleAt(const QPointF& pos) const {
    return !editMode_ && gizmoShown() && gizmo_.hitTest(gizmoView(), ke::Vector2(float(pos.x()), float(pos.y()))) != GizmoHandle::None;
}

void Viewport::setGizmoTarget(bool visible, const Vector3& position, const ke::Quaternion& rotation) {
    gizmoVisible_ = visible;
    gizmo_.setTarget(position, rotation);
    update();
}

// ---------------------------------------------------------------------------
// The collider mode's points (ColliderGuides.h ShapeHandle)
// ---------------------------------------------------------------------------
void Viewport::setShapeHandles(std::vector<ShapeHandle> handles) {
    handles_ = std::move(handles);
    if (hoveredHandle_ >= int(handles_.size())) hoveredHandle_ = -1;
    update();
    updateHint();
}

QPointF Viewport::pixelOf(const Vector3& world, bool* inFront) const {
    const QVector4D clip = camera_.projection(float(width()) / float(std::max(1, height()))) * camera_.view() * QVector4D(world.x, world.y, world.z, 1);
    if (inFront) *inFront = clip.w() > 1e-6f;
    const float w = std::fabs(clip.w()) > 1e-9f ? clip.w() : 1e-9f;
    return QPointF((clip.x() / w + 1) * 0.5 * width(), (1 - clip.y() / w) * 0.5 * height());
}

int Viewport::shapeHandleAt(const QPointF& pos) const {
    int best = -1;
    double nearest = 9.0;
    for (size_t i = 0; i < handles_.size(); ++i) {
        bool front = false;
        const QPointF p = pixelOf(handles_[i].centre + handles_[i].axis * handles_[i].reach, &front);
        if (const double d = QLineF(p, pos).length(); front && d <= nearest) nearest = d, best = int(i);
    }
    return best;
}

// The point of the handle's line nearest the mouse ray, as metres from its centre: t = (b (d.w) - a.w) / (1 - b^2) for the
// line c + a t and the ray o + d s (a, d unit, w = c - o, b = a.d). Looking along the line, it stays where it is.
float Viewport::reachAlong(const ShapeHandle& h, const QPointF& pos) const {
    const Ray ray = camera_.screenRay(pos, size());
    const Vector3 w = h.centre - ray.origin;
    const float b = ke::dot(h.axis, ray.dir), across = 1.0f - b * b;
    if (across < 1e-4f) return h.reach;
    return (b * ke::dot(ray.dir, w) - ke::dot(h.axis, w)) / across;
}

void Viewport::setPluginTool(const QString& op, const QString& label, bool drags) {
    if (op == toolOp_) return;
    toolOp_ = op, toolLabel_ = label, toolDrags_ = drags;
    if (!createMode_ && !magnetMode_) setCursor(op.isEmpty() ? Qt::ArrowCursor : Qt::CrossCursor);
    emit pluginToolChanged(op);
    updateHint();
}

// What a click there is on: the nearest object (edit: the scene's, play: the bodies'), else the floor. A surface with no
// polygon's normal (a box, a sphere) gets it from two hits a pixel aside on the same object; none: towards the eye.
bool Viewport::surfaceAt(const QPointF& pos, Vector3& point, Vector3& normal, uint32_t& object) const {
    const auto hitAt = [this](const QPointF& at, Vector3& p, Vector3* n) -> uint32_t {
        const Ray ray = camera_.screenRay(at, size());
        if (editMode_) {
            const std::vector<PickHit> hits = pickAll(ray);
            if (hits.empty()) return 0;
            p = hits.front().point;
            if (n) *n = hits.front().normal;
            return hits.front().id;
        }
        int body;
        if (!snap_ || !pickAnyBody(ray, body, p)) return 0;
        return std::max<uint32_t>(1, snap_->bodies[size_t(body)].object);
    };
    const Ray ray = camera_.screenRay(pos, size());
    normal = Vector3(0.0f);
    object = hitAt(pos, point, &normal);
    if (object) {
        if (ke::length(normal) < 0.5f) {
            Vector3 a, b;
            if (hitAt(pos + QPointF(1, 0), a, nullptr) == object && hitAt(pos + QPointF(0, 1), b, nullptr) == object)
                normal = ke::cross(a - point, b - point);
            normal = ke::length(normal) > 1e-9f ? ke::normalize(normal) : -ray.dir;
        }
        if (ke::dot(normal, ray.dir) > 0) normal = -normal;
        return true;
    }
    const float toFloor = intersectAxisPlane(ray, 1, 0.0f);
    if (toFloor <= 0.0f || toFloor > 50.0f * camera_.distance()) return false;
    point = ray.at(toFloor), normal = Vector3(0, 1, 0);
    return true;
}

void Viewport::clickTool(const QPointF& pos) {
    Vector3 point, normal;
    uint32_t object = 0;
    if (surfaceAt(pos, point, normal, object)) emit toolClicked(toolOp_, point, normal, object);
}

// The second point is what is under the mouse at the release; over nothing - the sky - where its ray meets the plane of
// the surface pressed.
void Viewport::dragTool(const QPointF& from, const QPointF& to) {
    Vector3 point, normal, end, endNormal;
    uint32_t object = 0, endObject = 0;
    if (!surfaceAt(from, point, normal, object)) return;
    if (!surfaceAt(to, end, endNormal, endObject)) {
        const Ray ray = camera_.screenRay(to, size());
        const float across = ke::dot(ray.dir, normal);
        if (std::fabs(across) < 1e-4f) return;
        end = ray.at(ke::dot(point - ray.origin, normal) / across), endObject = 0;
    }
    emit toolDragged(toolOp_, point, normal, object, end, endObject);
}

Vector3 Viewport::handlePoint(const ShapeHandle& h, const QPointF& pos) const {
    if (!h.free) return h.centre + h.axis * reachAlong(h, pos);
    const Vector3 at = h.centre + h.axis * h.reach;
    const QVector3D f = camera_.forward();
    const Vector3 n = ke::length(h.normal) > 0.5f ? h.normal : Vector3(f.x(), f.y(), f.z()); // its own plane, or the eye's
    const Ray ray = camera_.screenRay(pos, size());
    const float across = ke::dot(ray.dir, n);
    if (std::fabs(across) < 1e-4f) return at;
    return ray.at(ke::dot(at - ray.origin, n) / across);
}

void Viewport::setHoveredHandle(int i) {
    if (i == hoveredHandle_) return;
    hoveredHandle_ = i;
    setCursor(i >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

uint32_t Viewport::pickEntity(const Ray& ray, Vector3* hit) const {
    const std::vector<PickHit> hits = pickAll(ray);
    if (hits.empty()) return 0;
    if (hit) *hit = hits.front().point;
    return hits.front().id;
}

void Viewport::setHoveredEntity(uint32_t id) {
    if (id == hoveredEntity_) return;
    hoveredEntity_ = id;
    emit entityHovered(id);
}

// The objects on the ray, nearest first. Clicking the same spot again within 1.5 s (the camera
// unmoved) takes the next one. A hint at the cursor says "2 / 3: name".
uint32_t Viewport::clickSelect(const QPointF& pos, bool next) {
    const std::vector<PickHit> hits = pickAll(camera_.screenRay(pos, size()));
    std::vector<uint32_t> ids;
    for (const PickHit& h : hits) ids.push_back(h.id);
    const bool sameSpot = lastClickTime_.isValid() && lastClickTime_.elapsed() < 1500 && QLineF(pos, lastClickPos_).length() <= 4 &&
                          camera_.view() == lastClickView_;
    int i = 0;
    if (!ids.empty() && ids == lastClickHits_ && lastClickIndex_ >= 0 && (sameSpot || next)) i = (lastClickIndex_ + 1) % int(ids.size());
    else if (next && ids.size() > 1) i = 1;
    lastClickPos_ = pos;
    lastClickTime_.start();
    lastClickView_ = camera_.view();
    lastClickHits_ = ids;
    lastClickIndex_ = ids.empty() ? -1 : i;
    if (ids.size() > 1)
        QToolTip::showText(mapToGlobal(pos.toPoint()) + QPoint(16, 8), QString("%1 / %2: %3").arg(i + 1).arg(ids.size()).arg(QString::fromStdString(hits[size_t(i)].name)), this);
    else
        QToolTip::hideText();
    const uint32_t id = ids.empty() ? 0 : ids[size_t(i)];
    emit entityClicked(id);
    return id;
}

void Viewport::startModal(GizmoMode m) {
    if (!editMode_ || createMode_ || !gizmoVisible_ || gizmo_.dragging()) return;
    const QPointF p = mapFromGlobal(QCursor::pos());
    gizmo_.beginModal(m, gizmoView(), ke::Vector2(float(p.x()), float(p.y())));
    typed_.clear();
    emit gizmoStarted();
    update();
    updateHint();
}

void Viewport::finishModal() {
    if (!gizmo_.modal()) return;
    gizmo_.end();
    typed_.clear();
    emit gizmoFinished();
    update();
    updateHint();
}

void Viewport::cancelModal() {
    if (!gizmo_.modal()) return;
    gizmo_.end();
    typed_.clear();
    emit gizmoCancelled();
    update();
    updateHint();
}

void Viewport::cancelGizmoDrag() {
    if (!gizmo_.dragging() || gizmo_.modal()) return;
    gizmo_.end();
    emit gizmoCancelled();
    update();
    updateHint();
}

void Viewport::modalDrag() {
    const QPointF p = mapFromGlobal(QCursor::pos());
    const bool snap = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    emit gizmoMoved(gizmo_.drag(gizmoView(), ke::Vector2(float(p.x()), float(p.y())), snap));
    update();
}

// During a keyboard transform: X / Y / Z limit it to an axis, digits type the amount, Enter
// confirms, Esc cancels; every other key is swallowed.
bool Viewport::modalKey(QKeyEvent* e) {
    const int k = e->key();
    if (k == Qt::Key_Escape) cancelModal();
    else if (k == Qt::Key_Return || k == Qt::Key_Enter) finishModal();
    else if (k == Qt::Key_X || k == Qt::Key_Y || k == Qt::Key_Z) {
        gizmo_.constrainModal(k - Qt::Key_X, gizmoView());
        modalDrag();
    } else if (k == Qt::Key_Backspace || (e->text().size() == 1 && QString("0123456789.,-").contains(e->text()))) {
        if (k == Qt::Key_Backspace) typed_.chop(1);
        else typed_ += e->text() == "," ? QString(".") : e->text();
        bool ok = false;
        const float value = typed_.toFloat(&ok);
        gizmo_.setTyped(ok, value);
        modalDrag();
    }
    return true;
}

// ---------------------------------------------------------------------------
// What the buttons and keys do right now (the status bar)
// ---------------------------------------------------------------------------
QString Viewport::mouseHint() const {
    if (navPressed_ || navHover_ != NavCube::kOutside) return uiText("Kinetica", "Клик по шарику оси: вид вдоль неё (Y — сверху) · тяните: вращать вид");
    if (rightHeld_ && rightButtonFlies(scheme_) && (flew_ || !flyKeys_.empty()))
        return uiText("Kinetica", "Полёт: W A S D — вперёд, влево, назад, вправо · Q / E — вниз / вверх · Shift — быстрее · мышь — осмотреться");
    if (!toolOp_.isEmpty())
        return "«" + toolLabel_ + (toolDrags_ ? uiText("Kinetica", "»: тяните от точки до точки — обе уходят плагину · щелчок — одна точка · Esc — выход · ")
                                              : uiText("Kinetica", "»: щелчок по объекту или полу — точка, нормаль и объект уходят плагину · Esc — выход · ")) + cameraHint(scheme_);
    if (!editMode_ && gizmoShown() && !gizmo_.dragging() && gizmo_.hover() != GizmoHandle::None)
        return uiText("Kinetica", "Тяните: цель IK, рука идёт за ней моторами · ") + cameraHint(scheme_);
    if (!editMode_ && !gizmo_.dragging()) return uiText("Kinetica", "ЛКМ по телу: схватить и тащить · ") + cameraHint(scheme_);
    if (createMode_) return createTool_.hint(createWhat_);
    if (gizmo_.modal()) return uiText("Kinetica", "X / Y / Z: ось (ещё раз — своя) · число: точно · Enter или ЛКМ: принять · Esc или ПКМ: отмена");
    if (gizmo_.dragging() && editTool_.cloning())
        return uiText("Kinetica", "Клонирование: отпустите — спросим, сколько копий · X / Y / Z: ось · Ctrl: шаг · Esc или ПКМ: отмена");
    if (gizmo_.dragging()) return uiText("Kinetica", "X / Y / Z: только по оси · Shift+X / Y / Z: в плоскости · Ctrl: шаг · Shift: точнее · Esc или ПКМ: отмена");
    if (editTool_.boxSelecting()) return uiText("Kinetica", "Отпустите — выбрать всё в рамке · Shift: добавить · Ctrl: убрать");
    if (!handles_.empty() && !colliderMode()) { // a plugin's orange points: what they are, in its words
        if (editTool_.draggingHandle()) return QString(uiText("Kinetica", "Тяните: %1 · Esc или ПКМ: отмена")).arg(editTool_.draggedHandle().label);
        if (hoveredHandle_ >= 0) return QString(uiText("Kinetica", "Тяните точку: %1")).arg(handles_[size_t(hoveredHandle_)].label);
    }
    if (colliderMode()) { // the collider's points before the gizmo's words: they are what the mode is for
        if (editTool_.draggingHandle()) return QString(uiText("Kinetica", "Тяните: %1 коллайдера · Esc или ПКМ: отмена")).arg(editTool_.draggedHandle().what);
        if (hoveredHandle_ >= 0) {
            const ShapeHandle& h = handles_[size_t(hoveredHandle_)];
            return QString(uiText("Kinetica", "Тяните точку: %1 коллайдера%2")).arg(h.what, h.both ? "" : uiText("Kinetica", " · противоположная грань стоит на месте"));
        }
        if (gizmoShown() && gizmo_.hover() != GizmoHandle::None) {
            const char* verb = gizmo_.mode() == GizmoMode::Rotate ? uiText("Kinetica", "вращать") : gizmo_.mode() == GizmoMode::Scale ? uiText("Kinetica", "растянуть") : uiText("Kinetica", "двигать");
            return QString(uiText("Kinetica", "Тяните: %1 коллайдер, модель стоит · Ctrl: шаг · Shift во время драга: точнее · X / Y / Z: ось")).arg(verb);
        }
        return uiText("Kinetica", "Коллайдер: точки — размер, гизмо — место и поворот (W E R) · модель не меняется · Esc — назад к телу");
    }
    if (gizmoShown() && gizmo_.hover() != GizmoHandle::None) {
        const char* verb = gizmo_.mode() == GizmoMode::Rotate ? uiText("Kinetica", "вращать — отпустите у точки, и угол станет ровно 90°") :
                           gizmo_.mode() == GizmoMode::Scale ? uiText("Kinetica", "масштабировать") : uiText("Kinetica", "двигать");
        return QString(uiText("Kinetica", "Тяните: %1 · Shift+тяните: клонировать · Ctrl: шаг · Shift во время драга: точнее · X / Y / Z: ось")).arg(verb);
    }
    if (hoveredEntity_)
        return QString(uiText("Kinetica", "ЛКМ: выбрать · ")) + (gizmo_.mode() == GizmoMode::Translate ? uiText("Kinetica", "тяните: двигать по полу · ") : uiText("Kinetica", "Shift+тяните: двигать по полу · ")) +
               uiText("Kinetica", "Shift или Ctrl+ЛКМ: добавить / убрать · двойной клик: показать · ") + cameraHint(scheme_);
    return uiText("Kinetica", "ЛКМ: выбрать · тяните с пустого места: рамка · ") + cameraHint(scheme_) + uiText("Kinetica", " · F: показать");
}

void Viewport::updateHint() {
    const QString hint = mouseHint();
    if (hint == lastHint_) return;
    lastHint_ = hint;
    emit hintChanged(hint);
}

void Viewport::setMagnetMode(bool on) {
    if (on) setCreateMode(false);
    magnetMode_ = on;
    if (!on) magnetGhost_.clear(), magnetJoint_.clear();
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void Viewport::setMagnetPreview(std::vector<float> ghost, std::vector<float> joint) {
    magnetGhost_ = std::move(ghost);
    magnetJoint_ = std::move(joint);
    update();
}

void Viewport::setCreateMode(bool on, const CreateWhat& what) {
    if (on) setPluginTool({}, {}); // drawing shapes ends a plugin's tool
    if (!on && !createMode_) return;
    createMode_ = on;
    createWhat_ = what;
    createTool_.reset();
    if (on) gizmo_.setHover(GizmoHandle::None), setHoveredEntity(0);
    if (!magnetMode_) setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
    updateHint();
}
