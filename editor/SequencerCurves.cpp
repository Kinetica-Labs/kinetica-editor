// The Sequencer's curves (SequencerViews.h): one track - the selected object's, else the picked key's, else the first -
// as x, y, z of its place (m) or its turn (degrees, the turn's own Euler angles) against time, sampled at every second
// pixel the way the world plays it (scene/Animation.h: in a line, smooth, held). The left column switches the place and
// the turn and shows or hides an axis. A key's dot dragged: up and down its value, along its time (whichever the hand
// moves more first); released, one sequence_key_set. The values' range is fitted to the curve in sight, and held while
// dragging. The wheel zooms the time about the mouse (the tracks' too: one axis), Shift+wheel the values; the middle button
// moves both; F shows the whole again - the 3D view's own words for the same.
#include "SequencerViews.h"
#include "UiLanguage.h"

#include "SceneBuilder.h"
#include "SequencerPanel.h"
#include "scene/Animation.h"

#include <QContextMenuEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace sequencer;

namespace {

const QColor kAxes[3] = {QColor(236, 92, 92), QColor(100, 204, 112), QColor(96, 156, 244)};
const char* const kAxisNames[3] = {"x", "y", "z"};

QRectF modeBox(bool turn) { return QRectF(turn ? 92 : 8, kRuler + 30, 80, 20); }
QRectF axisBox(int c) { return QRectF(8 + c * 34, kRuler + 56, 30, 20); }

// Euler degrees as the turn they make gives them back: a curve and its keys' dots read the same angles.
ke::Vector3 ownAngles(const ke::Vector3& degrees) {
    ke::SceneObject o;
    o.rotationDeg = degrees;
    return ke::eulerDegrees(ke::objectRotation(o));
}

} // namespace

SequencerCurves::SequencerCurves(SequencerPanel* panel) : QWidget(panel), panel_(panel) {
    setObjectName("sequencerCurves");
    setMinimumHeight(kRuler + 4 * kRow);
}

uint32_t SequencerCurves::object() const {
    const ke::SequenceObject* q = ke::activeSequence(panel_->builder()->graph());
    if (!q || q->tracks.empty()) return 0;
    if (const uint32_t s = panel_->builder()->selectedId(); ke::trackOf(*q, s)) return s;
    if (const uint32_t picked = panel_->pickedObject(); ke::trackOf(*q, picked)) return picked;
    return q->tracks[0].object;
}

const ke::SequenceTrack* SequencerCurves::track() const {
    const ke::SequenceObject* q = ke::activeSequence(panel_->builder()->graph());
    return q ? ke::trackOf(*q, object()) : nullptr;
}

double SequencerCurves::valueOf(const ke::AnimKey& k, int channel) const {
    return turn_ ? double(ownAngles(k.rotationDeg)[channel]) : double(k.position[channel]);
}

double SequencerCurves::valueAt(const std::vector<ke::AnimKey>& keys, double t, int channel) const {
    ke::Vector3 p;
    ke::Quaternion r;
    ke::samplePose(keys, ke::Real(t), false, 0, p, r);
    return turn_ ? double(ke::eulerDegrees(r)[channel]) : double(p[channel]);
}

double SequencerCurves::yOf(double v) const {
    const double top = kRuler + 10, bottom = height() - 10;
    return top + (hi_ - v) / (hi_ - lo_) * (bottom - top);
}

double SequencerCurves::vAt(double y) const {
    const double top = kRuler + 10, bottom = height() - 10;
    return hi_ - (y - top) / (bottom - top) * (hi_ - lo_);
}

void SequencerCurves::fitRange(const std::vector<ke::AnimKey>& keys) {
    const Axis a = panel_->axis(width());
    double lo = 1e300, hi = -1e300;
    for (int c = 0; c < 3; ++c) {
        if (!shown_[size_t(c)]) continue;
        for (int k = 0; k <= 200; ++k) {
            const double v = valueAt(keys, a.from + a.span * k / 200, c);
            lo = std::min(lo, v), hi = std::max(hi, v);
        }
        for (const ke::AnimKey& key : keys)
            if (double(key.time) >= a.from && double(key.time) <= a.from + a.span) lo = std::min(lo, valueOf(key, c)), hi = std::max(hi, valueOf(key, c));
    }
    if (lo > hi) lo = 0, hi = 1;
    const double pad = std::max((hi - lo) * 0.12, turn_ ? 5.0 : 0.05);
    lo_ = lo - pad, hi_ = hi + pad;
}

bool SequencerCurves::dotAt(const QPointF& at, size_t& key, int& channel) const {
    const ke::SequenceTrack* t = track();
    if (!t) return false;
    const Axis a = panel_->axis(width());
    if (at.x() < kNames) return false; // a key zoomed out of sight is not under the names
    for (int c = 2; c >= 0; --c) // the one drawn last, on top, first
        for (size_t k = 0; shown_[size_t(c)] && k < t->keys.size(); ++k)
            if (std::hypot(a.x(double(t->keys[k].time)) - at.x(), yOf(valueOf(t->keys[k], c)) - at.y()) <= 7) return key = k, channel = c, true;
    return false;
}

QPointF SequencerCurves::pointOf(size_t key, int channel) const {
    const ke::SequenceTrack* t = track();
    if (!t || key >= t->keys.size()) return QPointF(-1, -1);
    const Axis a = panel_->axis(width());
    return QPointF(a.x(double(t->keys[key].time)), yOf(valueOf(t->keys[key], channel)));
}

void SequencerCurves::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    const QColor text = pal.color(QPalette::Text), faint(text.red(), text.green(), text.blue(), 40);
    p.fillRect(rect(), pal.color(QPalette::Base));
    p.fillRect(QRect(0, 0, kNames, height()), pal.color(QPalette::Window));
    const Axis a = panel_->axis(width());
    drawRuler(p, a, pal, height());
    const ke::SequenceTrack* t = track();
    if (!t) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(kNames + kPad, kRuler + 4, width() - kNames - 2 * kPad, 40), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   uiText("Kinetica", "Здесь кривые дорожки: выделите объект, у которого есть ключи"));
        drawPlayhead(p, a, panel_->playhead(), height());
        return;
    }
    const std::vector<ke::AnimKey>& keys = drag_ != Drag::None ? draft_ : t->keys;
    if (drag_ == Drag::None && !valuesHeld_) fitRange(keys);

    // The left column: whose curves, the place or the turn, the axes shown.
    const ke::SceneObject* o = ke::findObject(panel_->builder()->graph(), t->object);
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(text);
    p.drawText(QRectF(8, kRuler + 4, kNames - 12, 22), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(o ? QString::fromStdString(o->name) : QString(uiText("Kinetica", "потерян")), Qt::ElideRight, kNames - 12));
    p.setFont(font());
    for (bool turn : {false, true}) {
        const QRectF box = modeBox(turn);
        p.setPen(Qt::NoPen);
        p.setBrush(turn == turn_ ? pal.color(QPalette::Highlight) : QColor(128, 128, 128, 40));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(turn == turn_ ? pal.color(QPalette::HighlightedText) : text);
        p.drawText(box, Qt::AlignCenter, turn ? uiText("Kinetica", "Поворот, °") : uiText("Kinetica", "Место, м"));
    }
    for (int c = 0; c < 3; ++c) {
        const QRectF box = axisBox(c);
        p.setPen(QPen(kAxes[c], 1.4));
        p.setBrush(shown_[size_t(c)] ? QColor(kAxes[c].red(), kAxes[c].green(), kAxes[c].blue(), 70) : Qt::transparent);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(shown_[size_t(c)] ? text : pal.color(QPalette::PlaceholderText));
        p.drawText(box, Qt::AlignCenter, kAxisNames[c]);
    }
    if (const QRectF hint(8, kRuler + 84, kNames - 16, height() - kRuler - 88); hint.height() > 40) { // how to look closer
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(hint, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   uiText("Kinetica", "Колесо — время, Shift+колесо — значения, средняя кнопка — сдвиг, F — всё"));
    }
    p.setClipRect(QRectF(kNames, kRuler, width() - kNames, height() - kRuler)); // zoomed: what is outside stays outside

    // The values' grid, its figures at the plot's right edge (the view runs a second past the last key).
    for (int k = 0; k <= 4; ++k) {
        const double v = lo_ + (hi_ - lo_) * k / 4, y = yOf(v);
        p.setPen(faint);
        p.drawLine(QPointF(kNames, y), QPointF(width(), y));
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(width() - 74, y - 14, 70, 13), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', turn_ ? 0 : 2)); // past the last key
    }
    for (int c = 0; c < 3; ++c) {
        if (!shown_[size_t(c)]) continue;
        QPainterPath curve;
        for (double x = kNames + kPad; x <= width() - kPad; x += 2) {
            const QPointF at(x, yOf(valueAt(keys, a.time(x), c)));
            if (curve.elementCount() == 0) curve.moveTo(at); // isEmpty() is true after a lone moveTo
            else curve.lineTo(at);
        }
        p.setPen(QPen(kAxes[c], 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve);
        for (size_t k = 0; k < keys.size(); ++k)
            drawKey(p, QPointF(a.x(double(keys[k].time)), yOf(valueOf(keys[k], c))), keys[k].interp, kAxes[c],
                    long(k) == dragKey_ && c == dragChannel_, text);
    }
    p.setClipping(false);
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.drawLine(QPointF(kNames, 0), QPointF(kNames, height()));
    drawPlayhead(p, a, panel_->playhead(), height());
}

void SequencerCurves::wheelEvent(QWheelEvent* e) {
    const int d = e->angleDelta().y() ? e->angleDelta().y() : e->angleDelta().x(); // Shift turns some wheels sideways
    if (d == 0 || e->position().x() < kNames) return;
    const double factor = std::pow(0.85, d / 120.0);
    if (e->modifiers() & Qt::ShiftModifier) { // the values, about the one under the mouse
        const double at = vAt(e->position().y());
        lo_ = at - (at - lo_) * factor, hi_ = at + (hi_ - at) * factor, valuesHeld_ = true;
        return update();
    }
    panel_->zoomTime(factor, panel_->axis(width()).time(e->position().x()));
}

void SequencerCurves::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) return void(panAt_ = e->position()); // the middle button moves the view
    if (e->button() != Qt::LeftButton) return;
    const QPointF at = e->position();
    for (bool turn : {false, true})
        if (modeBox(turn).contains(at)) return turn_ = turn, update();
    for (int c = 0; c < 3; ++c)
        if (axisBox(c).contains(at)) return shown_[size_t(c)] = !shown_[size_t(c)], update();
    size_t key = 0;
    int channel = 0;
    if (const ke::SequenceTrack* t = track(); t && dotAt(at, key, channel)) {
        draft_ = t->keys, dragKey_ = long(key), dragChannel_ = channel, pressAt_ = at, drag_ = Drag::None;
        return update();
    }
    if (at.x() >= kNames) panel_->scrub(panel_->axis(width()).time(at.x()));
}

void SequencerCurves::mouseMoveEvent(QMouseEvent* e) {
    const Axis a = panel_->axis(width());
    if (panAt_.x() >= 0) { // the content follows the hand
        const QPointF d = e->position() - panAt_;
        panAt_ = e->position();
        if (d.y() != 0) {
            const double dv = d.y() * (hi_ - lo_) / std::max(1.0, double(height() - kRuler - 20));
            lo_ += dv, hi_ += dv, valuesHeld_ = true;
        }
        if (d.x() != 0) panel_->panTime(-d.x() * a.span / double(std::max(1, width() - kNames - 2 * kPad)));
        return update();
    }
    if (panel_->scrubbing()) return panel_->scrub(a.time(e->position().x()));
    const ke::SequenceTrack* t = track();
    if (dragKey_ < 0 || !t || size_t(dragKey_) >= draft_.size()) return;
    const QPointF d = e->position() - pressAt_;
    if (drag_ == Drag::None) {
        if (std::hypot(d.x(), d.y()) < 4) return;
        drag_ = std::fabs(d.x()) > std::fabs(d.y()) ? Drag::Time : Drag::Value;
    }
    ke::AnimKey& k = draft_[size_t(dragKey_)];
    if (drag_ == Drag::Value) {
        const ke::Real v = ke::Real(vAt(e->position().y()));
        if (turn_) {
            ke::Vector3 angles = ownAngles(t->keys[size_t(dragKey_)].rotationDeg);
            angles[dragChannel_] = v;
            k.rotationDeg = angles;
        } else
            k.position[dragChannel_] = v;
    } else { // between its neighbours, a millisecond off them
        const double lo = dragKey_ > 0 ? double(draft_[size_t(dragKey_) - 1].time) + 1e-3 : 0;
        const double hi = size_t(dragKey_) + 1 < draft_.size() ? double(draft_[size_t(dragKey_) + 1].time) - 1e-3 : 86400;
        k.time = ke::Real(std::clamp(a.time(e->position().x()), lo, hi));
    }
    update();
}

void SequencerCurves::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) return void(panAt_ = QPointF(-1, -1));
    if (panel_->scrubbing()) return panel_->endScrub();
    const ke::SequenceTrack* t = track();
    if (dragKey_ < 0 || !t || size_t(dragKey_) >= t->keys.size() || drag_ == Drag::None) {
        dragKey_ = -1, drag_ = Drag::None;
        return update();
    }
    const ke::AnimKey &was = t->keys[size_t(dragKey_)], &now = draft_[size_t(dragKey_)];
    QJsonObject args{{"id", int(t->object)}, {"time", double(was.time)}};
    const auto list = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
    if (drag_ == Drag::Time) args.insert("new_time", double(now.time));
    else if (turn_) args.insert("rotation", list(now.rotationDeg));
    else args.insert("position", list(now.position));
    dragKey_ = -1, drag_ = Drag::None;
    panel_->run("sequence_key_set", args); // the graph changes: t, was and now are not read after
}

void SequencerCurves::contextMenuEvent(QContextMenuEvent* e) {
    size_t key = 0;
    int channel = 0;
    if (const ke::SequenceTrack* t = track(); t && dotAt(e->pos(), key, channel))
        panel_->keyMenu(t->object, double(t->keys[key].time), e->globalPos());
}
