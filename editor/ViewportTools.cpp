// What the mouse does in the view (see ViewportTools.h): the camera tool (orbit, pan, zoom, look
// around), the grab tool of play mode (a mouse joint on a body), the edit tool (select, box select,
// gizmo drags, Shift-clone, keyboard transforms), the assembly magnet's tool and making shapes.
#include "ViewportTools.h"

#include "ColliderGuides.h"
#include "SceneMarkers.h"
#include "Viewport.h"
#include "scene/SceneActions.h"

#include <QLineF>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

static ke::Vector2 pixel(const QMouseEvent* e) { return ke::Vector2(float(e->position().x()), float(e->position().y())); }

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------
void CameraTool::press(Viewport& v, QMouseEvent* e) {
    last_ = e->pos();
    move_ = v.cameraMoveFor(e->button(), e->modifiers());
    if (move_ == CameraMove::Orbit) pivot_ = v.followedBody() ? v.camera().target() : v.pointUnderCursor(e->position());
    if (move_ == CameraMove::Dolly) pivot_ = v.camera().target();
    v.setOrbitPivot(pivot_, move_ == CameraMove::Orbit && !v.flying()); // the mark on the point it turns around
}

void CameraTool::move(Viewport& v, QMouseEvent* e) {
    const QPoint d = e->pos() - last_;
    last_ = e->pos();
    CameraMove m = move_;
    if (v.flying() && (m == CameraMove::Orbit || m == CameraMove::Look)) m = CameraMove::Look; // W A S D held: look around
    OrbitCamera& cam = v.camera();
    if (v.followedBody()) {
        if (m==CameraMove::Orbit || m==CameraMove::Look) cam.rotate(float(d.x()),float(d.y()));
        else if (m==CameraMove::Dolly) cam.zoom(float(-d.y())/40.0f);
        v.setOrbitPivot(cam.target(),m==CameraMove::Orbit);
        v.update(); return;
    }
    switch (m) {
    case CameraMove::Orbit: cam.orbitAround(pivot_, float(d.x()), float(d.y())); break;
    case CameraMove::Look: cam.orbitAround(cam.eye(), float(d.x()), float(d.y())); break;
    case CameraMove::Pan: cam.pan(float(d.x()), float(d.y())); break;
    case CameraMove::Dolly: cam.zoomToward(pivot_, float(-d.y()) / 40.0f); break;
    case CameraMove::None: return;
    }
    v.update();
}

void CameraTool::release(Viewport& v, QMouseEvent* e) {
    if (e->buttons() != Qt::NoButton) return;
    move_ = CameraMove::None;
    v.setOrbitPivot(pivot_, false);
}

// ---------------------------------------------------------------------------
// Play mode: grab a body
// ---------------------------------------------------------------------------
void GrabTool::press(Viewport& v, QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier)) {
        Ray ray = v.camera().screenRay(e->position(), v.size());
        int body;
        ke::Vector3 hit;
        if (v.pickBody(ray, body, hit)) {
            const QVector3D f = v.camera().forward();
            planeNormal_ = ke::Vector3(f.x(), f.y(), f.z());
            planePoint_ = hit;
            grabbing_ = true;
            emit v.grabStarted(body, hit);
            return;
        }
    }
    camera_.press(v, e);
}

void GrabTool::move(Viewport& v, QMouseEvent* e) {
    if (!grabbing_) {
        camera_.move(v, e);
        return;
    }
    Ray ray = v.camera().screenRay(e->position(), v.size());
    float denom = ke::dot(ray.dir, planeNormal_);
    if (std::fabs(denom) < 1e-6f) return;
    float t = ke::dot(planePoint_ - ray.origin, planeNormal_) / denom;
    if (t <= 0) return;
    emit v.grabMoved(ray.at(t));
}

void GrabTool::release(Viewport& v, QMouseEvent* e) {
    if (grabbing_ && e->button() == Qt::LeftButton) {
        grabbing_ = false;
        emit v.grabReleased();
    }
    camera_.release(v, e);
}

void GoalTool::begin(Viewport& v, QMouseEvent* e, uint32_t goal, const ke::Vector3& taken) {
    goal_ = goal, taken_ = taken, from_ = e->position(), moved_ = false;
    turning_ = e->modifiers() & Qt::ControlModifier;
    for (const ke::Frame::Body& b : v.snapshot()->bodies)
        if (b.marker && b.object == goal) centre_ = position_ = b.pos, turn_ = now_ = b.rot;
}

void GoalTool::move(Viewport& v, QMouseEvent* e) {
    if (!goal_ || (!moved_ && QLineF(e->position(), from_).length() < 3)) return;
    moved_ = true;
    if (turning_) {
        const QVector3D up = v.camera().up(), right = v.camera().right();
        const QPointF d = e->position() - from_;
        const ke::Real deg = ke::kPi / 360; // half a degree a pixel
        now_ = ke::Quaternion::fromAxisAngle({up.x(), up.y(), up.z()}, ke::Real(d.x()) * deg) *
               ke::Quaternion::fromAxisAngle({right.x(), right.y(), right.z()}, ke::Real(d.y()) * deg) * turn_;
    } else {
        const Ray ray = v.camera().screenRay(e->position(), v.size());
        const QVector3D f = v.camera().forward();
        const ke::Vector3 n(f.x(), f.y(), f.z());
        const float along = ke::dot(ray.dir, n);
        if (std::fabs(along) < 1e-6f) return;
        const float t = ke::dot(taken_ - ray.origin, n) / along;
        if (t <= 0) return;
        position_ = centre_ + (ray.at(t) - taken_);
    }
    emit v.goalDragged(goal_, position_, now_, false);
}

void GoalTool::release(Viewport& v, QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !goal_) return;
    if (moved_) emit v.goalDragged(goal_, position_, now_, true);
    goal_ = 0;
}

// ---------------------------------------------------------------------------
// Edit mode
// ---------------------------------------------------------------------------
void EditTool::press(Viewport& v, QMouseEvent* e) {
    if (pressTransform(v, e)) return;
    const CameraMove cam = v.cameraMoveFor(e->button(), e->modifiers());
    if (e->button() == Qt::RightButton && !(e->modifiers() & Qt::AltModifier)) { // a click (no drag) opens the menu
        rightClickPending_ = true;
        rightPressPos_ = e->position();
    }
    if (cam != CameraMove::None || e->button() != Qt::LeftButton) {
        camera_.press(v, e);
        return;
    }
    pressLeft(v, e);
}

// A keyboard transform: left confirms, right cancels. A handle drag: the right button puts it back.
bool EditTool::pressTransform(Viewport& v, QMouseEvent* e) {
    if (v.gizmo().modal()) {
        if (e->button() == Qt::LeftButton) v.finishModal();
        else if (e->button() == Qt::RightButton) v.cancelModal();
        return true;
    }
    if (v.gizmo().dragging()) {
        if (e->button() == Qt::RightButton) v.cancelGizmoDrag();
        return true;
    }
    return false;
}

void EditTool::pressLeft(Viewport& v, QMouseEvent* e) {
    pressPos_ = boxEnd_ = e->position();
    pressShift_ = e->modifiers() & Qt::ShiftModifier;
    if (v.gizmoShown()) {
        pending_ = v.gizmo().hitTest(v.gizmoView(), pixel(e));
        if (pending_ != GizmoHandle::None) return; // the drag starts once the mouse has moved 3 px
    }
    pressedEntity_ = v.pickEntity(v.camera().screenRay(e->position(), v.size()));
    clickPending_ = true;
}

void EditTool::startFloorDrag(Viewport& v, QMouseEvent* e) {
    const ke::Vector2 press(float(pressPos_.x()), float(pressPos_.y()));
    ke::Vector3 hit;
    if (!v.pickEntity(v.camera().screenRay(pressPos_, v.size()), &hit)) return;
    emit v.entityClicked(pressedEntity_); // the builder selects it and puts the gizmo on it
    v.gizmo().beginFloorDrag(v.gizmoView(), press, hit);
    floorDrag_ = true;
    virtualMouse_ = lastMouse_ = press;
    emit v.gizmoStarted();
    moveGizmo(v, e);
}

// The left button went 4 px from where it was pressed: on an object with Shift a floor slide, from
// empty space the selection box.
void EditTool::startLeftDrag(Viewport& v, QMouseEvent* e) {
    clickPending_ = false;
    if (pressedEntity_ && (e->modifiers() & Qt::ShiftModifier)) startFloorDrag(v, e);
    else if (!pressedEntity_) box_ = true;
}

bool EditTool::moveGizmo(Viewport& v, QMouseEvent* e) {
    const bool snap = e->modifiers() & Qt::ControlModifier;
    if (v.gizmo().modal()) {
        emit v.gizmoMoved(v.gizmo().drag(v.gizmoView(), pixel(e), snap));
        v.update();
        return true;
    }
    if (pending_ != GizmoHandle::None) { // a pressed handle: a click does not nudge the object
        if (QLineF(e->position(), pressPos_).length() < 3) return true;
        const ke::Vector2 press(float(pressPos_.x()), float(pressPos_.y()));
        v.gizmo().begin(pending_, v.gizmoView(), press);
        pending_ = GizmoHandle::None;
        floorDrag_ = false;
        virtualMouse_ = lastMouse_ = press;
        cloning_ = cloneShiftHeld_ = pressShift_ && v.editMode(); // Shift at the start: a clone, as in 3ds Max (not of a goal in play)
        emit v.gizmoStarted();
        if (cloning_) emit v.cloneDragStarted();
    }
    if (!v.gizmo().dragging()) return false;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    if (!shift) cloneShiftHeld_ = false; // let go: pressed again, it means "finer"
    const float speed = shift && !floorDrag_ && !cloneShiftHeld_ ? 0.1f : 1.0f;
    virtualMouse_ += (pixel(e) - lastMouse_) * speed;
    lastMouse_ = pixel(e);
    emit v.gizmoMoved(v.gizmo().drag(v.gizmoView(), virtualMouse_, snap));
    v.update();
    return true;
}

void EditTool::move(Viewport& v, QMouseEvent* e) {
    if (moveGizmo(v, e)) return;
    if (rightClickPending_ && (QLineF(e->position(), rightPressPos_).length() > 4 || v.flying())) rightClickPending_ = false;
    if (box_) {
        boxEnd_ = e->position();
        v.update();
        return;
    }
    if (clickPending_ && (e->buttons() & Qt::LeftButton)) {
        if (QLineF(e->position(), pressPos_).length() > 4) startLeftDrag(v, e);
        return;
    }
    if (e->buttons() != Qt::NoButton) {
        camera_.move(v, e);
        return;
    }
    hover(v, e);
}

void EditTool::hover(Viewport& v, QMouseEvent* e) {
    const GizmoHandle h = v.gizmoShown() ? v.gizmo().hitTest(v.gizmoView(), pixel(e)) : GizmoHandle::None;
    if (h != v.gizmo().hover()) {
        v.gizmo().setHover(h);
        v.setCursor(h != GizmoHandle::None ? Qt::SizeAllCursor : Qt::ArrowCursor);
        v.update();
    }
    v.setHoveredEntity(h == GizmoHandle::None ? v.pickEntity(v.camera().screenRay(e->position(), v.size())) : 0);
}

void EditTool::release(Viewport& v, QMouseEvent* e) {
    camera_.release(v, e);
    if (e->button() == Qt::RightButton) {
        if (rightClickPending_ && !v.flewThisPress()) emit v.editContextMenu(e->position());
        rightClickPending_ = false;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (pending_ != GizmoHandle::None) { // pressed and released on a handle without moving: a click through it -
        pending_ = GizmoHandle::None;    // the gizmo sits on the selection, so clicking there again walks the ray on
        if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) v.clickSelect(e->position(), false);
        return;
    }
    if (v.gizmo().dragging() && !v.gizmo().modal()) {
        v.gizmo().end();
        floorDrag_ = false;
        const bool cloned = cloning_;
        cloning_ = false;
        emit v.gizmoFinished();
        if (cloned) emit v.cloneDragFinished(e->position()); // the popover opens at the cursor
        v.update();
        return;
    }
    if (box_) {
        box_ = false;
        emit v.boxSelected(boxRect(), e->modifiers());
        v.update();
        return;
    }
    const bool toggle = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
    if (clickPending_ && toggle && pressedEntity_) emit v.entityToggled(pressedEntity_);
    else if (clickPending_ && !toggle) v.clickSelect(e->position(), false);
    clickPending_ = false;
}

// ---------------------------------------------------------------------------
// MagnetTool
// ---------------------------------------------------------------------------
void MagnetTool::press(Viewport& v, QMouseEvent* e) {
    clickPending_ = e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier);
    pressPos_ = e->position();
    if (clickPending_) emit v.magnetPressed(e->position(), e->modifiers());
    else camera_.press(v, e);
}

// With the left button held the mouse picks a sector of the pie; otherwise it hovers (the preview) or
// moves the camera.
void MagnetTool::move(Viewport& v, QMouseEvent* e) {
    if (clickPending_ && (e->buttons() & Qt::LeftButton)) return emit v.magnetDragged(e->position());
    if (e->buttons() != Qt::NoButton) return camera_.move(v, e);
    emit v.magnetMoved(e->position());
}

void MagnetTool::release(Viewport& v, QMouseEvent* e) {
    if (clickPending_ && e->button() == Qt::LeftButton) emit v.magnetReleased(e->position());
    else camera_.release(v, e);
    clickPending_ = false;
}

// ---------------------------------------------------------------------------
// Making shapes, lights and cameras
// ---------------------------------------------------------------------------
namespace {

constexpr float kGrid = 0.05f;  // Ctrl: the start point and the sizes in steps of 5 cm
constexpr float kLeast = 0.01f; // no side thinner than 1 cm
const ke::Vector3 kUp(0.0f, 1.0f, 0.0f);

// A size dragged out (its sign: the side the drag went), snapped with Ctrl, never below the least.
float measure(float d, bool snap) {
    const float m = std::max(snap ? snapTo(std::fabs(d), kGrid) : std::fabs(d), snap ? kGrid : kLeast);
    return d < 0 ? -m : m;
}

ke::Vector3 millimetres(const ke::Vector3& v) { // the call's numbers: (0.4, 0.1, -0.25), not 0.40012345
    const auto mm = [](float x) { return std::round(x * 1000.0f) / 1000.0f; };
    return ke::Vector3(mm(v.x), mm(v.y), mm(v.z));
}

void snapFlat(ke::Vector3& p) { p.x = snapTo(p.x, kGrid), p.z = snapTo(p.z, kGrid); }

float plateThickness() { // a plane's thickness as the core makes it
    ke::SceneGraph g;
    ke::addShape(g, ke::ShapeKind::Plane);
    return g.entities.back().size.y;
}

bool isRound(ke::ShapeKind s) { return s == ke::ShapeKind::Sphere || s == ke::ShapeKind::Cylinder || s == ke::ShapeKind::Cone; }
bool isPlaced(const CreateWhat& w) { return w.kind != CreateWhat::Kind::Shape; } // a light or a camera: a place, no size
bool isCamera(const CreateWhat& w) { return w.kind == CreateWhat::Kind::Camera; }

QString titleOf(const CreateWhat& w) {
    if (isCamera(w)) return "Камера";
    return w.kind == CreateWhat::Kind::Light ? QString(ke::lightTitle(w.light)) : QString(ke::shapeTitle(w.shape));
}

QString metres(float x) { return QString::number(double(x), 'f', 2); }

// Where a light or a camera looks: the nearest surface under the cursor (any side), else the floor,
// else 3 m along the ray (the sky).
ke::Vector3 aimAt(Viewport& v, const QPointF& pos) {
    const Ray ray = v.camera().screenRay(pos, v.size());
    const std::vector<PickHit> hits = v.pickAll(ray);
    const auto nearest = std::min_element(hits.begin(), hits.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    if (nearest != hits.end()) return nearest->point;
    ke::Vector3 at;
    return rayPlane(ray, ke::Vector3(0.0f), kUp, at) ? at : ray.origin + ray.dir * 3.0f;
}

// A light or a camera being placed, drawn as its own marker will be (SceneMarkers.h) - made by the core as
// the click will make it - with the spot's cone, the camera's frame and the sun's arrow reaching where it
// looks: the patch of light on the floor, what the camera will see there.
void placedMarker(std::vector<float>& out, const CreateWhat& w, const ke::Vector3& position, const ke::Vector3& target, float aspect) {
    ke::SceneGraph g;
    if (w.kind == CreateWhat::Kind::Camera) ke::addCamera(g, position, target);
    else ke::placeLight(g, w.light, position, target);
    for (SceneMarker m : markersOf(g)) {
        m.reach = ke::length(target - position);
        markerLines(m, aspect, out, out);
    }
}

void segment(std::vector<float>& out, const ke::Vector3& a, const ke::Vector3& b) { out.insert(out.end(), {a.x, a.y, a.z, 0.0f, b.x, b.y, b.z, 0.0f}); }

} // namespace

std::vector<CreateTool::Step> CreateTool::stepsOf(const CreateWhat& w) const {
    if (w.kind == CreateWhat::Kind::Shape) {
        if (w.shape == ke::ShapeKind::Sphere || w.shape == ke::ShapeKind::Plane) return {Step::Place, Step::Base};
        return {Step::Place, Step::Base, Step::Height};
    }
    if (w.kind == CreateWhat::Kind::Light && w.light == ke::LightKind::Point) return {Step::Place, Step::Height};
    return {Step::Place, Step::Height, Step::Aim};
}

void CreateTool::reset() {
    step_ = 0;
    armed_ = rightClick_ = false;
    height_ = 0;
    ghost_.clear();
}

// The top of an object under the cursor (the nearest hit, its face looking mostly up), else the floor.
bool CreateTool::surfaceAt(Viewport& v, const QPointF& pos, ke::Vector3& at) const {
    const Ray ray = v.camera().screenRay(pos, v.size());
    const std::vector<PickHit> hits = v.pickAll(ray);
    const auto nearest = std::min_element(hits.begin(), hits.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    if (nearest != hits.end() && nearest->normal.y > 0.7f) return at = nearest->point, true;
    return rayPlane(ray, ke::Vector3(0.0f), kUp, at);
}

// A left press: the first one starts the thing on the surface; any later one ends the step it is in.
void CreateTool::press(Viewport& v, QMouseEvent* e) {
    const bool alt = e->modifiers() & Qt::AltModifier, snap = e->modifiers() & Qt::ControlModifier;
    if (e->button() == Qt::RightButton && !alt) rightClick_ = true, rightPos_ = e->position(); // a click drops, a drag looks
    if (e->button() != Qt::LeftButton || alt) return camera_.press(v, e);
    armed_ = true;
    pressPos_ = lastPos_ = e->position();
    if (step_ > 0) return confirm(v, e->position(), snap);
    if (!surfaceAt(v, e->position(), start_)) return void(armed_ = false);
    if (snap) snapFlat(start_);
    end_ = target_ = start_;
    height_ = 0;
    step_ = 1;
    risePos_ = e->position();
    track(v, e->position(), snap);
}

void CreateTool::move(Viewport& v, QMouseEvent* e) {
    if (rightClick_ && (QLineF(e->position(), rightPos_).length() > 4 || v.flying())) rightClick_ = false;
    if (e->buttons() != Qt::NoButton && !(armed_ && (e->buttons() & Qt::LeftButton))) return camera_.move(v, e);
    lastPos_ = e->position();
    track(v, e->position(), e->modifiers() & Qt::ControlModifier);
}

// A release after a drag ends the step the drag was in (press, drag, release: a footprint at once); after a
// click nothing - the next click ends it.
void CreateTool::release(Viewport& v, QMouseEvent* e) {
    camera_.release(v, e);
    if (e->button() == Qt::RightButton) {
        if (rightClick_ && !v.flewThisPress() && !cancel(v)) v.setCreateMode(false);
        rightClick_ = false;
        return;
    }
    if (e->button() != Qt::LeftButton || !armed_) return;
    armed_ = false;
    if (step_ > 0 && QLineF(e->position(), pressPos_).length() > 4) confirm(v, e->position(), e->modifiers() & Qt::ControlModifier);
}

bool CreateTool::back(Viewport& v) {
    if (step_ == 0) return false;
    if (--step_ == 0) reset();
    else if (stepsOf(v.createWhat())[size_t(step_)] == Step::Base) height_ = 0;
    track(v, lastPos_, false);
    return true;
}

bool CreateTool::cancel(Viewport& v) {
    if (step_ == 0) return false;
    reset();
    track(v, lastPos_, false);
    return true;
}

// A click on the spot where the step began gives it no size: the step waits.
bool CreateTool::done(const CreateWhat& w) const {
    const ke::Vector3 d = end_ - start_;
    switch (stepsOf(w)[size_t(step_)]) {
    case Step::Base: return isRound(w.shape) ? std::hypot(d.x, d.z) >= kLeast : std::fabs(d.x) >= kLeast && std::fabs(d.z) >= kLeast;
    case Step::Height: return height_ >= kLeast;
    case Step::Aim: return ke::length(target_ - position_) >= kLeast;
    default: return true;
    }
}

void CreateTool::confirm(Viewport& v, const QPointF& pos, bool snap) {
    const CreateWhat what = v.createWhat();
    track(v, pos, snap);
    if (!done(what)) return;
    if (size_t(step_) + 1 < stepsOf(what).size()) {
        ++step_;
        risePos_ = pos;
        return track(v, pos, snap);
    }
    const ke::Vector3 position = position_, size = size_, target = millimetres(target_);
    reset();
    track(v, pos, snap); // the cross for the next one
    emit v.created(what, position, size, target);
}

// The step now to the cursor: the footprint on the plane of the first click, the height on the vertical
// through its foot (the point of it nearest to the cursor's ray; looking straight down, the mouse's way
// up the screen), where a light looks. Before the first click: the cross where it would start.
void CreateTool::track(Viewport& v, const QPointF& pos, bool snap) {
    const CreateWhat what = v.createWhat();
    const Ray ray = v.camera().screenRay(pos, v.size());
    ghost_.clear();
    v.update();
    const auto cross = [&](const ke::Vector3& at, float pixels) {
        const float r = pixels * v.gizmoView().worldPerPixel(at);
        segment(ghost_, at - ke::Vector3(r, 0.0f, 0.0f), at + ke::Vector3(r, 0.0f, 0.0f));
        segment(ghost_, at - ke::Vector3(0.0f, 0.0f, r), at + ke::Vector3(0.0f, 0.0f, r));
    };
    if (step_ == 0) {
        ke::Vector3 at;
        if (!surfaceAt(v, pos, at)) return;
        if (snap) snapFlat(at);
        cross(at, 10.0f);
        segment(ghost_, at, at + kUp * (10.0f * v.gizmoView().worldPerPixel(at)));
        return;
    }
    const Step step = stepsOf(what)[size_t(step_)];
    ke::Vector3 hit;
    float s = 0;
    if (step == Step::Base && rayPlane(ray, start_, kUp, hit)) end_ = hit;
    if (step == Step::Height) {
        const ke::Vector3 foot = isPlaced(what) ? start_ : end_;
        height_ = std::fabs(ray.dir.y) < 0.97f && closestOnAxis(ray, foot, kUp, s) ? s : float(risePos_.y() - pos.y()) * v.gizmoView().worldPerPixel(foot);
    }
    if (step == Step::Aim) {
        target_ = aimAt(v, pos);
        if (snap) snapFlat(target_);
    }
    if (isPlaced(what)) { // a stem from the spot, its marker where it stands looking down (raised) or where it aims
        const float up = std::max(height_, 0.0f);
        position_ = millimetres(start_ + kUp * (snap ? snapTo(up, kGrid) : up));
        cross(start_, 6.0f);
        segment(ghost_, start_, position_);
        if (ke::length(target_ - position_) >= kLeast) placedMarker(ghost_, what, position_, target_, float(v.width()) / float(std::max(1, v.height())));
        if (step == Step::Aim) segment(ghost_, position_, target_), cross(target_, 8.0f);
        return;
    }
    const ke::ShapeKind shape = what.shape;
    const ke::Vector3 d = end_ - start_;
    const float tall = measure(std::max(height_, 0.0f), snap);
    ke::Vector3 centre = start_;
    if (isRound(shape)) {
        const float r = measure(std::hypot(d.x, d.z), snap);
        size_ = ke::Vector3(2 * r, shape == ke::ShapeKind::Sphere ? 2 * r : tall, 2 * r);
    } else {
        const float dx = measure(d.x, snap), dz = measure(d.z, snap);
        size_ = ke::Vector3(std::fabs(dx), shape == ke::ShapeKind::Plane ? plateThickness() : tall, std::fabs(dz));
        centre += ke::Vector3(0.5f * dx, 0.0f, 0.5f * dz);
    }
    size_ = millimetres(size_);
    position_ = millimetres(centre + kUp * (0.5f * size_.y)); // standing on the surface
    ke::Entity e;
    e.shape = shape;
    e.size = size_;
    for (const ke::Vector3& p : meshEdgeGuide(ke::entityLocalMesh(e))) ghost_.insert(ghost_.end(), {p.x + position_.x, p.y + position_.y, p.z + position_.z, 0.0f});
}

QString CreateTool::sizeText(const CreateWhat& w) const {
    if (step_ == 0) return {};
    const Step step = stepsOf(w)[size_t(step_)];
    if (isPlaced(w)) {
        const QString high = "высота " + metres(position_.y - start_.y) + " м";
        const ke::Vector3 to = target_ - position_;
        const float far = ke::length(to);
        if (step != Step::Aim || far < kLeast) return high;
        if (w.kind == CreateWhat::Kind::Light && w.light == ke::LightKind::Sun)
            return high + ", " + QString::number(qRound(std::asin(std::clamp(-to.y / far, -1.0f, 1.0f)) * 57.29578f)) + "° к горизонту";
        return high + ", до цели " + metres(far) + " м";
    }
    const QString base = isRound(w.shape) ? "радиус " + metres(0.5f * size_.x) + " м" : metres(size_.x) + " × " + metres(size_.z) + " м";
    return step == Step::Height ? base + ", высота " + metres(size_.y) + " м" : base;
}

// «Куб · шаг 2/3 ●●○ · противоположный угол, щелчок — дальше · 0.40 × 0.30 м · Backspace — шаг назад ...»
QString CreateTool::hint(const CreateWhat& w) const {
    const std::vector<Step> steps = stepsOf(w);
    const Step step = steps[size_t(step_)];
    const bool round = !isPlaced(w) && isRound(w.shape);
    QString doing;
    switch (step) {
    case Step::Place:
        doing = isPlaced(w)                        ? "щёлкните место на полу или на теле"
                : w.shape == ke::ShapeKind::Sphere ? "щёлкните центр шара"
                : round                            ? "щёлкните центр основания"
                                                   : "щёлкните первый угол основания (или сразу тяните)";
        break;
    case Step::Base: doing = round ? "радиус" : "противоположный угол"; break;
    case Step::Height: doing = "высота — ведите мышь вверх"; break;
    case Step::Aim: doing = isCamera(w) ? "куда смотрит — наведите на точку" : "куда светит — наведите на точку"; break;
    }
    if (step != Step::Place) doing += size_t(step_) + 1 == steps.size() ? ", щелчок — готово" : ", щелчок — дальше";
    QString dots;
    for (size_t k = 0; k < steps.size(); ++k) dots += int(k) <= step_ ? QString("●") : QString("○");
    QString text = QString("%1 · шаг %2/%3 %4 · %5").arg(titleOf(w)).arg(step_ + 1).arg(steps.size()).arg(dots, doing);
    const QString size = sizeText(w);
    if (!size.isEmpty()) text += " · " + size;
    return text + (step_ == 0 ? " · Esc — выход" : " · Backspace — шаг назад · Esc — отмена") + " · Ctrl — сетка 5 см";
}
