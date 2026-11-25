// What the 3D view draws for the Laboratory (see Viewport.h and LabPanel.h):
//   - the ring round the thing the Laboratory's card shows (a contact point, a body's centre), so the
//     numbers in the card and the place in the picture are tied together at a glance;
//   - the contact points of the frame as dots a few pixels wide, seen through the bodies: what is
//     drawn is what a click can pick;
//   - the words the debug layers write into the world (ke::Probe labels), each at its point, light
//     letters on a dark halo so they read on any background.
#include "Viewport.h"

#include "core/Probe.h"

#include <QPainter>
#include <QPainterPath>

void Viewport::setLabMarker(bool on, const ke::Vector3& at) {
    labMarker_ = on;
    labMarkerAt_ = at;
    update();
}

// A ring of two strokes (a dark one under a light one) with a small cross: visible on a white body
// and on a black floor alike.
void Viewport::drawLabMarker(QPainter& p) {
    if (!labMarker_) return;
    const ke::Vector2 at = gizmoView().project(labMarkerAt_);
    if (at.x < 0 || at.y < 0 || at.x > width() || at.y > height()) return;
    const QPointF c(at.x, at.y);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 170), 4.5));
    p.drawEllipse(c, 12.0, 12.0);
    p.setPen(QPen(QColor(86, 180, 233), 2.0));
    p.drawEllipse(c, 12.0, 12.0);
    p.drawLine(c + QPointF(-4, 0), c + QPointF(4, 0));
    p.drawLine(c + QPointF(0, -4), c + QPointF(0, 4));
    p.restore();
}

// The frame's contact points while the probe's layer "ContactPoints" is on: a dot a few pixels wide on
// a dark rim, seen through the bodies. A crowd of them (a heap of bodies) is left out.
void Viewport::drawContactDots(QPainter& p) {
    if (!ke::Probe::layerOn(ke::DrawLayer::ContactPoints) || snap_->contacts.empty() || snap_->contacts.size() > kContactDotsMost) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 180), 1.5));
    p.setBrush(QColor(255, 77, 51));
    for (const auto& c : snap_->contacts) {
        const ke::Vector2 at = gizmoView().project(c.position);
        if (at.x < 0 || at.y < 0 || at.x > width() || at.y > height()) continue;
        p.drawEllipse(QPointF(at.x, at.y), 3.5, 3.5);
    }
    p.restore();
}

// Every label the probe's layers wrote this frame, at its point in the world (skipped when off the
// view): light letters on a dark halo, readable on any background.
void Viewport::drawProbeLabels(QPainter& p) {
    if (snap_->probe.labels.empty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPointSizeF(8.5);
    p.setFont(f);
    for (const ke::Probe::Label& label : snap_->probe.labels) {
        const ke::Vector2 at = gizmoView().project(label.at);
        if (at.x < 0 || at.y < 0 || at.x > width() || at.y > height()) continue;
        QPainterPath text;
        text.addText(QPointF(at.x + 6, at.y - 6), f, QString::fromStdString(label.text));
        p.setPen(QPen(QColor(0, 0, 0, 200), 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(text);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(235, 238, 245));
        p.drawPath(text);
    }
    p.restore();
}
