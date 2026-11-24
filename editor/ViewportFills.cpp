// The filled pictures of view_draw (Viewport::Drawing::fill): a plugin's or an agent's flat outlines - a formula's
// glyphs, a label's plate, an arrowhead - painted over the picture after the scene. A picture is put by its space: in
// the world on its own plane, in the view's perspective; on the screen at a point of the world, facing the viewer; in
// the frame at its place. One that belongs to an object goes where the object goes, and a click on it takes the object.
// They are not in the depth buffer: a body in front does not hide one.
#include "Viewport.h"

#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace {

// A path's units a unit of the picture: a curve mapped by a perspective is cut into chords in the path's own units, so
// they are small.
constexpr double kUnits = 1000;

// The picture's plane into the screen: a path's point (u right, v down) is the world's at + (u X - v Y) * metres, then
// the view's perspective - one projective map. False: a corner of its box is behind the eye.
bool planeToScreen(const QMatrix4x4& vp, const ke::Vector3& at, const ke::Quaternion& turn, float scale, const QRectF& box, const QSizeF& view, QTransform& out) {
    const auto clip = [&](const ke::Vector3& v, float w) { return vp * QVector4D(v.x, v.y, v.z, w); };
    const float metres = scale / float(kUnits);
    const QVector4D a = clip(turn.rotate(ke::Vector3(metres, 0, 0)), 0), b = clip(turn.rotate(ke::Vector3(0, -metres, 0)), 0), c = clip(at, 1);
    const auto x = [&](const QVector4D& v) { return double(v.x() + v.w()) * 0.5 * view.width(); };
    const auto y = [&](const QVector4D& v) { return double(v.w() - v.y()) * 0.5 * view.height(); };
    out = QTransform(x(a), y(a), double(a.w()), x(b), y(b), double(b.w()), x(c), y(c), double(c.w()));
    for (const QPointF& corner : {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()})
        if (double(a.w()) * corner.x() + double(b.w()) * corner.y() + double(c.w()) < 1e-3) return false;
    return true;
}

} // namespace

std::shared_ptr<const Viewport::Fill> Viewport::fillOf(const QStringList& rows, QString& error, double edge) {
    auto fill = std::make_shared<Fill>();
    QPainterPathStroker pen; // an edge is the outline stroked twice as wide: half of it lies outside the part
    pen.setWidth(2 * edge * kUnits);
    pen.setJoinStyle(Qt::RoundJoin);
    int numbers = 0;
    for (const QString& row : rows) {
        const QStringList words = row.split(' ', Qt::SkipEmptyParts);
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        int i = 0;
        bool fine = !words.isEmpty() && words.first() == "M";
        const auto point = [&](QPointF& out) {
            bool x = false, y = false;
            if (i + 1 >= words.size()) return false;
            out = QPointF(words[i].toDouble(&x) * kUnits, -words[i + 1].toDouble(&y) * kUnits); // y up in the picture, down in the painter
            i += 2, numbers += 2;
            return x && y && std::isfinite(out.x()) && std::isfinite(out.y()) && std::fabs(out.x()) < 1e7 && std::fabs(out.y()) < 1e7;
        };
        while (fine && i < words.size()) {
            const QString op = words[i++];
            QPointF a, b, c;
            if (op == "M") fine = point(a), path.moveTo(a);
            else if (op == "L") fine = point(a), path.lineTo(a);
            else if (op == "C") fine = point(a) && point(b) && point(c), path.cubicTo(a, b, c);
            else if (op == "Z") path.closeSubpath();
            else fine = false;
        }
        if (!fine) return error = "fill: each outline is a row of M x y, L x y, C x1 y1 x2 y2 x y and Z, beginning with M", nullptr;
        fill->box = fill->box.united(path.boundingRect());
        if (edge > 0) fill->edges.push_back(pen.createStroke(path));
        fill->parts.push_back(std::move(path));
    }
    if (numbers > 400000) return error = "fill: at most 200000 points a drawing", nullptr;
    return fill;
}

// Where the object is now: a body of the frame the view shows (in play, where the world has it), else as the scene has it.
bool Viewport::objectPose(uint32_t id, ke::Vector3& position, ke::Quaternion& rotation) const {
    if (snap_)
        for (const ke::Frame::Body& b : snap_->bodies)
            if (b.object == id) return position = b.pos, rotation = b.rot, true;
    return objectPose_ && objectPose_(id, position, rotation);
}

void Viewport::drawOverlayFills(QPainter& p) {
    fillsDrawn_.clear();
    if (drawings_.empty()) return;
    const QSizeF view(width(), height());
    const QMatrix4x4 vp = camera_.projection(float(view.width() / std::max(1.0, view.height()))) * camera_.view();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto& [key, d] : drawings_) {
        if (!d.fill || d.fill->parts.empty()) continue;
        ke::Vector3 at = d.at;
        ke::Quaternion turn = d.turn;
        if (d.object) { // the object's: one in the world lies on it, a label hangs from its place along the world's axes
            ke::Vector3 where;
            ke::Quaternion how;
            if (!objectPose(d.object, where, how)) continue; // the object is gone: nothing to draw it on
            at = where + (d.space == Drawing::Space::World ? how.rotate(d.at) : d.at), turn = how * d.turn;
        }
        QTransform to;
        if (d.space == Drawing::Space::World) {
            if (!planeToScreen(vp, at, turn, d.scale, d.fill->box, view, to)) continue;
        } else {
            QPointF on(d.place.x() * view.width(), d.place.y() * view.height());
            if (d.space == Drawing::Space::Label) {
                const QVector4D clip = vp * QVector4D(at.x, at.y, at.z, 1);
                if (clip.w() < 1e-3f) continue; // behind the eye
                on = QPointF(double(clip.x() / clip.w() + 1) * 0.5 * view.width(), double(1 - clip.y() / clip.w()) * 0.5 * view.height());
            }
            const double unit = double(d.scale) * view.height() / kUnits;
            to = QTransform().translate(on.x(), on.y()).scale(unit, unit);
        }
        const QRectF& box = d.fill->box;
        const QPolygonF drawn = to.map(QPolygonF(QVector<QPointF>{box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()}));
        if (!drawn.boundingRect().intersects(QRectF(QPointF(0, 0), view))) continue; // out of the view
        p.setPen(Qt::NoPen);
        p.setBrush(d.stroke);
        for (const QPainterPath& edge : d.fill->edges) p.drawPath(to.map(edge)); // all the edges first: none lies over a neighbour's fill
        p.setBrush(d.colour);
        for (const QPainterPath& part : d.fill->parts) p.drawPath(to.map(part)); // each by itself: a root's roof laps over its sign
        if (d.leader)
            paintLeader(p, d, drawn.boundingRect(), [&](const ke::Vector3& w, QPointF& out) {
                const QVector4D clip = vp * QVector4D(w.x, w.y, w.z, 1);
                if (clip.w() < 1e-3f) return false; // behind the eye
                return out = QPointF(double(clip.x() / clip.w() + 1) * 0.5 * view.width(), double(1 - clip.y() / clip.w()) * 0.5 * view.height()), true;
            });
        fillsDrawn_.push_back({key, d.object, drawn, at});
    }
    p.restore();
}

QPolygonF Viewport::fillBox(const QString& key) const {
    for (const FillDrawn& f : fillsDrawn_)
        if (f.key == key) return f.box;
    return {};
}

// What a click along the ray takes, nearest first: the object of a filled picture under it before everything - the
// picture is painted over everything.
std::vector<PickHit> Viewport::pickAll(const Ray& ray) const {
    std::vector<PickHit> hits = picker_ ? picker_(ray) : std::vector<PickHit>();
    if (!editMode_) return hits;
    const ke::Vector2 at = gizmoView().project(ray.at(1.0f));
    for (const FillDrawn& f : fillsDrawn_)
        if (f.object && f.box.containsPoint(QPointF(double(at.x), double(at.y)), Qt::OddEvenFill) &&
            std::none_of(hits.begin(), hits.end(), [&](const PickHit& h) { return h.t == 0.0f && h.id == f.object; }))
            hits.insert(hits.begin(), PickHit{f.object, 0.0f, f.anchor, std::string()});
    return hits;
}
