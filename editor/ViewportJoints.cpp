// A joint as what it is - a tie between two bodies -, painted over the picture: from each body's own place a dashed
// line to the joint, a pin there in the colour of the joint's kind, and under the pin of a joint that holds its body to
// the world the hatch mechanics draw for the ground. What the joint lets the bodies do - its ring, its travel, its limits
// - is its glyph, drawn in the scene's space (Viewport::drawJoints, JointGlyphs.h); this is the other half: which two
// things it holds together. The ties of the selected body are drawn brighter.
#include "Viewport.h"

#include <QPainter>

#include <algorithm>

namespace {

// By the joint's kind, in ke::JointType's order - the colours its glyph is drawn in (Viewport::drawJoints): a ball orange,
// a hinge blue, a slider green, a weld violet, a generic one yellow.
QColor kindColour(ke::JointType type) {
    static const QColor colours[] = {{255, 140, 51}, {77, 204, 255}, {102, 255, 115}, {230, 115, 255}, {255, 230, 89}, {235, 235, 235}};
    return colours[std::clamp(int(type), 0, 5)];
}

} // namespace

void Viewport::drawJointTies(QPainter& p) {
    jointTies_.clear();
    if (!snap_ || snap_->joints.empty()) return;
    const QSizeF view(width(), height());
    const QMatrix4x4 vp = camera_.projection(float(view.width() / std::max(1.0, view.height()))) * camera_.view();
    const auto seen = [&](const ke::Vector3& w, QPointF& out) {
        const QVector4D clip = vp * QVector4D(w.x, w.y, w.z, 1);
        if (clip.w() < 1e-3f) return false; // behind the eye
        out = QPointF(double(clip.x() / clip.w() + 1) * 0.5 * view.width(), double(1 - clip.y() / clip.w()) * 0.5 * view.height());
        return true;
    };
    const auto chosen = [&](uint32_t object) {
        return object && std::any_of(highlight_.begin(), highlight_.end(), [&](int body) {
            return body >= 0 && body < int(snap_->bodies.size()) && snap_->bodies[size_t(body)].object == object;
        });
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const ke::JointVis& j : snap_->joints) {
        QPointF pin, a, b;
        if (!seen(j.anchorB, pin) || !seen(j.bodyA, a) || (!j.grounded && !seen(j.bodyB, b))) continue;
        const bool bright = chosen(j.a) || chosen(j.b);
        QColor colour = kindColour(j.type);
        colour.setAlpha(bright ? 255 : 170);
        const QColor dark(10, 12, 16, bright ? 200 : 120);
        const double wide = bright ? 2.4 : 1.6;
        const auto tie = [&](const QPointF& body) { // a line with a dark edge, and a dot where it takes its body
            if (QLineF(body, pin).length() < 6) return;
            p.setPen(QPen(dark, wide + 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(body, pin);
            QPen dashed(colour, wide, Qt::CustomDashLine, Qt::RoundCap);
            dashed.setDashPattern({3.0, 2.5});
            p.setPen(dashed);
            p.drawLine(body, pin);
            p.setPen(QPen(dark, 1.2));
            p.setBrush(colour);
            p.drawEllipse(body, 3.2, 3.2);
        };
        tie(a);
        if (!j.grounded) tie(b);
        if (j.grounded) { // the ground: a bar under the pin and its hatch
            p.setPen(QPen(dark, wide + 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(pin + QPointF(-9, 9), pin + QPointF(9, 9));
            p.setPen(QPen(colour, wide, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(pin, pin + QPointF(0, 9));
            p.drawLine(pin + QPointF(-9, 9), pin + QPointF(9, 9));
            for (const double x : {-7.0, -1.0, 5.0}) p.drawLine(pin + QPointF(x, 9), pin + QPointF(x - 4, 14));
        }
        p.setPen(QPen(dark, 1.5));
        p.setBrush(colour);
        p.drawEllipse(pin, bright ? 6.0 : 5.0, bright ? 6.0 : 5.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(250, 250, 252, colour.alpha()));
        p.drawEllipse(pin, 1.8, 1.8);
        jointTies_.push_back({j.id, pin, a, j.grounded ? pin : b, j.grounded});
    }
    p.restore();
}
