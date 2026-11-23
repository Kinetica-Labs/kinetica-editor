// What the viewport draws on top of its picture while it is used, and where it looks (see Viewport.h):
//   - the play mode's frame: a border round the view while the scene runs, in the run's colour - green playing, amber
//     paused, red recording (MainWindow::syncRecord) -, so nobody edits a running scene thinking it is the one kept;
//   - the orbit pivot: while the camera orbits, a ring with a dot on the point it turns around -
//     the turn is no longer a mystery when the point is far away;
//   - following a body: the camera keeps its own orbit about a moving body of the frame - or, chasing, sits behind it
//     along its heading and catches up its turns and its centre with a lag (a game's spring arm).
#include "Viewport.h"

#include <QPainter>

#include <cmath>

namespace {

constexpr float kChaseTurn = 0.35f, kChaseMove = 0.12f; // s: the lags of the heading and of the centre

// The yaw (OrbitCamera's: the eye at cos, sin of it from the target) of an eye behind a body turned `rot`: its own x
// on the ground, reversed.
float behindYaw(const ke::Quaternion& rot) {
    const ke::Vector3 ahead = rot.rotate(ke::Vector3(1, 0, 0));
    return float(qRadiansToDegrees(std::atan2(-double(ahead.z), -double(ahead.x))));
}

float wrapped(float deg) { return deg - 360.0f * std::floor((deg + 180.0f) / 360.0f); }

} // namespace

void Viewport::setPlayFrame(bool on) {
    if (playFrame_ == on) return;
    playFrame_ = on;
    update();
}

void Viewport::setOrbitPivot(const QVector3D& pivot, bool on) {
    orbitPivot_ = pivot;
    if (orbiting_ == on && !on) return;
    orbiting_ = on;
    update();
}

void Viewport::drawPlayFrame(QPainter& p) {
    if (!playFrame_) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(runFrame_.isValid() ? runFrame_ : QColor(240, 160, 32), 2.0)); // thin: it says the mode, it does not hide the scene
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    p.restore();
}

// A dark ring under a light one (readable on any background) and a dot in the middle.
void Viewport::drawOrbitPivot(QPainter& p) {
    if (!orbiting_) return;
    const ke::Vector2 at = gizmoView().project(ke::Vector3(orbitPivot_.x(), orbitPivot_.y(), orbitPivot_.z()));
    if (at.x < 0 || at.y < 0 || at.x > width() || at.y > height()) return;
    const QPointF c(at.x, at.y);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 150), 4.0));
    p.drawEllipse(c, 9.0, 9.0);
    p.setPen(QPen(QColor(255, 255, 255, 230), 1.6));
    p.drawEllipse(c, 9.0, 9.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 196, 64));
    p.drawEllipse(c, 2.8, 2.8);
    p.restore();
}

// Viewer-only tracking uses the immutable render snapshot, never a controller observation.
// Body::pos is the solver centre of mass, not the mesh bounding-box centre.
bool Viewport::followBody(uint32_t object) {
    if (object==followedBody_) return true;
    if (!object) {
        if (followedBody_) { bodyOrbit_=camera_; bodyOrbitSaved_=true; camera_=sceneOrbit_; }
        followedBody_=0;
        stopFlying();
        update(); return true;
    }
    const ke::Frame::Body* body=nullptr;
    if (snap_) for (const auto& b:snap_->bodies) if (b.object==object && b.movable) { body=&b; break; }
    if (!body || lookingThrough()) return false;
    stopFlying();
    if (!followedBody_) sceneOrbit_=camera_;
    const QVector3D centre(body->pos.x,body->pos.y,body->pos.z);
    if (chase_) { // behind it, a little above, along its heading
        const float yaw = qDegreesToRadians(behindYaw(body->rot));
        camera_.lookAt(centre + QVector3D(2.4f * std::cos(yaw), 1.0f, 2.4f * std::sin(yaw)), centre);
        chaseHeading_ = behindYaw(body->rot), chaseTime_ = -1;
    } else if (bodyOrbitSaved_) camera_=bodyOrbit_;
    else camera_.lookAt(centre+QVector3D(1.8f,1.0f,1.8f),centre);
    followedBody_=object;
    updateBodyOrbit();
    update(); return true;
}
void Viewport::setFollow(uint32_t body) {
    const bool changed = body != followWish_;
    followWish_ = body;
    if (followedBody_ && followedBody_ != body) followBody(0);
    updateBodyOrbit(); // playing: at once
    if (changed) emit followChanged(body);
}

void Viewport::setChase(bool on) {
    if (chase_ == on) return;
    chase_ = on;
    if (const uint32_t body = followedBody_) followBody(0), followBody(body); // placed anew, behind it or as it was
}

void Viewport::updateBodyOrbit() {
    if (followWish_ && followedBody_ != followWish_ && !editMode_ && !lookThrough_) followBody(followWish_); // its ▶, or it is back
    if (!followedBody_) return;
    if (snap_) for (const auto& b:snap_->bodies) if (b.object==followedBody_ && b.movable) {
        const QVector3D centre(b.pos.x,b.pos.y,b.pos.z);
        if (!chase_) return camera_.translate(centre-camera_.target());
        const double now = double(snap_->time);
        const float dt = chaseTime_ < 0 || now < chaseTime_ ? -1.0f : float(now - chaseTime_); // back in time: a new run
        chaseTime_ = now;
        if (dt < 0) return camera_.translate(centre-camera_.target()); // the first frame, or a jump: at once
        const float turn = (1.0f - std::exp(-dt / kChaseTurn)) * wrapped(behindYaw(b.rot) - chaseHeading_);
        chaseHeading_ += turn;
        camera_.turnYaw(turn); // the heading's change only: an orbit the mouse made stays
        camera_.translate((centre-camera_.target()) * (1.0f - std::exp(-dt / kChaseMove)));
        return;
    }
    followBody(0); // removed body or reset: restore the scene view (the wish waits for the next run)
}
