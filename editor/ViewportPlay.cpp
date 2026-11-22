// What the viewport draws on top of its picture while it is used, and where it looks (see Viewport.h):
//   - the play mode's frame: an amber border round the view while the scene plays, so nobody edits
//     a running scene thinking it is the one that will be kept;
//   - the orbit pivot: while the camera orbits, a ring with a dot on the point it turns around -
//     the turn is no longer a mystery when the point is far away;
//   - following a body: the camera keeps its own orbit about a moving body of the frame.
#include "Viewport.h"

#include <QPainter>

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
    p.setPen(QPen(QColor(240, 160, 32), 4.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(2, 2, -2, -2));
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
    if (bodyOrbitSaved_) camera_=bodyOrbit_;
    else camera_.lookAt(centre+QVector3D(1.8f,1.0f,1.8f),centre);
    followedBody_=object;
    updateBodyOrbit();
    update(); return true;
}
void Viewport::updateBodyOrbit() {
    if (!followedBody_) return;
    if (snap_) for (const auto& b:snap_->bodies) if (b.object==followedBody_ && b.movable) {
        const QVector3D centre(b.pos.x,b.pos.y,b.pos.z);
        camera_.translate(centre-camera_.target());
        return;
    }
    followBody(0); // removed body or reset: restore the scene view
}
