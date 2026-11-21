// The Sequencer's tracks and the time axis both views share (SequencerViews.h). The tree is built from the open sequence
// only when it changes (a signature of its tracks and keys, and of the scene's objects an undo replaces): its rows are
// «Все дорожки», then in the order the tracks come each root of the scene's hierarchy their objects hang under, as a
// folding row, and the tracks of objects with no parent. A folded row draws the keys of all its tracks as ticks, one a
// pixel; only the rows in sight are drawn. A filter keeps the tracks whose object's name holds its words, unfolded.
#include "SequencerViews.h"
#include "UiLanguage.h"

#include "SceneBuilder.h"
#include "SequencerPanel.h"
#include "scene/Animation.h"

#include <QContextMenuEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sequencer {

// By value at every step. Written as std::clamp over temporaries - it returns a reference to one of its arguments -
// this gave 0 s for any point of a drag in the program MSVC built (Windows 10: the playhead could be clicked, not
// dragged), the right time under GCC, and the right time under MSVC too once the code around the call changed.
double Axis::time(double x) const {
    const double t = (x - kNames - kPad) * span / double(std::max(1, width - kNames - 2 * kPad));
    return t < 0 ? 0 : t > span ? span : t;
}

Axis axisOf(const ke::SequenceObject* q, int width, double atLeast) {
    return {width, std::max({5.0, std::ceil(double(q ? q->length() : 0) + 1), atLeast})};
}

// A figure every 1, 5, 10 or 30 s by how long the view is, a tick between; the last figure at the edge written before its tick.
void drawRuler(QPainter& p, const Axis& a, const QPalette& pal, int height) {
    const QColor text = pal.color(QPalette::Text), faint(text.red(), text.green(), text.blue(), 40);
    p.fillRect(QRect(kNames, 0, a.width - kNames, kRuler), pal.color(QPalette::AlternateBase));
    const double every = a.span <= 12 ? 1 : a.span <= 40 ? 5 : a.span <= 120 ? 10 : 30;
    for (int k = 0; k * every / 2 <= a.span + 1e-9; ++k) {
        const double t = k * every / 2, x = a.x(t);
        p.setPen(text);
        p.drawLine(QPointF(x, kRuler - (k % 2 ? 4 : 8)), QPointF(x, kRuler));
        if (k % 2) continue;
        const bool edge = x + 40 > a.width;
        p.drawText(edge ? QRectF(x - 63, 1, 60, kRuler - 7) : QRectF(x + 3, 1, 60, kRuler - 7), (edge ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                   QString::number(t) + uiText("Kinetica", " с"));
        p.setPen(faint);
        p.drawLine(QPointF(x, kRuler), QPointF(x, height));
    }
}

void drawPlayhead(QPainter& p, const Axis& a, double time, int height) {
    const QColor head(255, 106, 90);
    const double x = a.x(std::min(time, a.span));
    p.setPen(QPen(head, 1.6));
    p.drawLine(QPointF(x, 0), QPointF(x, height));
    p.setBrush(head);
    p.setPen(Qt::NoPen);
    const QPointF tip[3] = {{x - 6, 0}, {x + 6, 0}, {x, 9}};
    p.drawPolygon(tip, 3);
}

// Its shape says how the key goes on: a diamond in a line, a circle smoothly, a square held.
void drawKey(QPainter& p, const QPointF& c, ke::AnimInterp interp, const QColor& fill, bool picked, const QColor& outline) {
    p.setPen(QPen(picked ? outline : QColor(60, 45, 10), 1.2));
    p.setBrush(picked ? QColor(255, 236, 170) : fill);
    if (interp == ke::AnimInterp::Smooth) p.drawEllipse(c, 5.5, 5.5);
    else if (interp == ke::AnimInterp::Step) p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
    else {
        const QPointF d[4] = {{c.x(), c.y() - 6}, {c.x() + 6, c.y()}, {c.x(), c.y() + 6}, {c.x() - 6, c.y()}};
        p.drawPolygon(d, 4);
    }
}

} // namespace sequencer

using namespace sequencer;

namespace {

const QColor kKey(240, 192, 64), kEnd(150, 164, 190); // a key; the play range's end (the playhead is the red one)

uint64_t signatureOf(const ke::SceneGraph& g, const ke::SequenceObject* q) {
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(q ? q->id : 0), mix(g.entities.size()), mix(g.groups.size()), mix(uint64_t(reinterpret_cast<uintptr_t>(g.entities.data())));
    if (q)
        for (const ke::SequenceTrack& t : q->tracks) {
            mix(t.object), mix(t.keys.size());
            for (const ke::AnimKey& k : t.keys) mix(uint64_t(std::llround(double(k.time) * 240)) ^ (uint64_t(k.interp) << 60));
        }
    return h;
}

// The keys' times of these tracks, one a 1/240 s: sorted for a few tracks, marked on the frames for many.
std::vector<float> ticksOf(const ke::SequenceObject& q, const std::vector<size_t>& tracks) {
    std::vector<int> frames;
    if (tracks.size() > 64) {
        std::vector<char> marked(size_t(std::ceil(double(q.length()) * 240)) + 2, 0);
        for (size_t i : tracks)
            for (const ke::AnimKey& k : q.tracks[i].keys) marked[size_t(std::max(0L, std::lround(double(k.time) * 240)))] = 1;
        for (size_t f = 0; f < marked.size(); ++f)
            if (marked[f]) frames.push_back(int(f));
    } else {
        for (size_t i : tracks)
            for (const ke::AnimKey& k : q.tracks[i].keys) frames.push_back(int(std::lround(double(k.time) * 240)));
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    }
    std::vector<float> out(frames.size());
    for (size_t k = 0; k < frames.size(); ++k) out[k] = float(frames[k] / 240.0);
    return out;
}

} // namespace

SequencerTimeline::SequencerTimeline(SequencerPanel* panel, QScrollBar* scroll) : QWidget(panel), panel_(panel), scroll_(scroll) {
    setObjectName("sequencerTimeline");
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(kRuler + 3 * kRow);
    connect(scroll_, &QScrollBar::valueChanged, this, [this] { update(); });
}

void SequencerTimeline::refresh() {
    const ke::SceneGraph& g = panel_->builder()->graph();
    const uint64_t s = signatureOf(g, ke::activeSequence(g));
    if (s != signature_) signature_ = s, rebuild();
    update();
}

void SequencerTimeline::setFilter(const QString& words) {
    filter_ = words.trimmed();
    rebuild();
    update();
}

void SequencerTimeline::rebuild() {
    rows_.clear();
    const ke::SceneGraph& g = panel_->builder()->graph();
    const ke::SequenceObject* q = ke::activeSequence(g);
    if (!q) return fitScroll();
    if (decidedFor_ != q->id) decidedFor_ = q->id, allOpen_ = q->tracks.size() <= 40, open_.clear(); // many tracks: folded
    std::unordered_map<uint32_t, const ke::SceneObject*> objects;
    objects.reserve(g.entities.size() + g.groups.size() + g.arrays.size() + g.lights.size() + g.cameras.size());
    const auto index = [&objects](const auto& list) {
        for (const auto& o : list) objects[o.id] = &o;
    };
    index(g.entities), index(g.groups), index(g.arrays), index(g.lights), index(g.cameras);
    const auto find = [&objects](uint32_t id) -> const ke::SceneObject* {
        const auto it = objects.find(id);
        return it == objects.end() ? nullptr : it->second;
    };
    const auto rootOf = [&find](const ke::SceneObject* o) {
        uint32_t root = 0;
        for (int depth = 0; o && o->parent && depth < 64; ++depth) root = o->parent, o = find(o->parent);
        return root;
    };
    // The tracks that pass the filter, in their order: each under its root, or loose.
    struct Branch {
        uint32_t root;
        std::vector<size_t> tracks;
    };
    struct Item {
        bool branch;
        size_t index;
    };
    std::vector<Branch> branches;
    std::vector<Item> items;
    std::vector<size_t> all;
    std::unordered_map<uint32_t, size_t> branchOf;
    for (size_t i = 0; i < q->tracks.size(); ++i) {
        const ke::SceneObject* o = find(q->tracks[i].object);
        if (!filter_.isEmpty() && !(o && QString::fromStdString(o->name).contains(filter_, Qt::CaseInsensitive))) continue;
        all.push_back(i);
        const uint32_t root = rootOf(o);
        if (!root) {
            items.push_back({false, i});
            continue;
        }
        const auto [it, fresh] = branchOf.try_emplace(root, branches.size());
        if (fresh) branches.push_back({root, {}}), items.push_back({true, it->second});
        branches[it->second].tracks.push_back(i);
    }
    const bool unfold = !filter_.isEmpty();
    const auto trackRow = [&](size_t i, int depth) {
        Row r;
        r.kind = Row::Track, r.depth = depth, r.id = q->tracks[i].object, r.track = i;
        const ke::SceneObject* o = find(r.id);
        r.lost = !o;
        r.name = o ? QString::fromStdString(o->name) : QString(uiText("Kinetica", "потерян (id %1)")).arg(r.id);
        rows_.push_back(std::move(r));
    };
    Row head;
    head.kind = Row::All, head.count = int(all.size()), head.name = uiText("Kinetica", "Все дорожки"), head.times = ticksOf(*q, all);
    rows_.push_back(std::move(head));
    if (allOpen_ || unfold)
        for (const Item& item : items) {
            if (!item.branch) {
                trackRow(item.index, 1);
                continue;
            }
            const Branch& b = branches[item.index];
            Row r;
            r.kind = Row::Group, r.depth = 1, r.id = b.root, r.count = int(b.tracks.size()), r.times = ticksOf(*q, b.tracks);
            const ke::SceneObject* o = find(b.root);
            r.name = o ? QString::fromStdString(o->name) : QString("id %1").arg(b.root);
            rows_.push_back(std::move(r));
            if (open_.count(b.root) || unfold)
                for (size_t i : b.tracks) trackRow(i, 2);
        }
    fitScroll();
}

void SequencerTimeline::fitScroll() {
    const int visible = std::max(1, (height() - kRuler) / kRow);
    scroll_->setPageStep(visible);
    scroll_->setRange(0, std::max(0, int(rows_.size()) - visible));
}

void SequencerTimeline::toggle(int row) {
    if (row < 0 || row >= int(rows_.size()) || rows_[size_t(row)].kind == Row::Track) return;
    if (rows_[size_t(row)].kind == Row::All) allOpen_ = !allOpen_;
    else if (!open_.erase(rows_[size_t(row)].id)) open_.insert(rows_[size_t(row)].id);
    rebuild();
    update();
}

int SequencerTimeline::rowAt(double y) const {
    if (y < kRuler) return -1;
    const int r = scroll_->value() + int((y - kRuler) / kRow);
    return r < int(rows_.size()) ? r : -1;
}

bool SequencerTimeline::keyAt(const QPointF& at, uint32_t& object, double& time) const {
    const int r = rowAt(at.y());
    const ke::SequenceObject* q = ke::activeSequence(panel_->builder()->graph());
    if (r < 0 || !q || rows_[size_t(r)].kind != Row::Track || rows_[size_t(r)].track >= q->tracks.size()) return false;
    const Axis a = axisOf(q, width(), panel_->atLeast());
    const ke::SequenceTrack& t = q->tracks[rows_[size_t(r)].track];
    for (const ke::AnimKey& k : t.keys)
        if (std::fabs(a.x(double(k.time)) - at.x()) <= 6) return object = t.object, time = double(k.time), true;
    return false;
}

QPointF SequencerTimeline::pointOf(uint32_t object, double time) const {
    const int first = scroll_->value();
    const Axis a = axisOf(ke::activeSequence(panel_->builder()->graph()), width(), panel_->atLeast());
    for (int r = first; r < int(rows_.size()); ++r) {
        const double y = kRuler + (r - first) * kRow;
        if (y + kRow > height()) break;
        if (rows_[size_t(r)].kind == Row::Track && rows_[size_t(r)].id == object) return QPointF(a.x(time), y + kRow / 2.0);
    }
    return QPointF(-1, -1);
}

QPointF SequencerTimeline::foldOf(int row) const {
    const int r = row - scroll_->value();
    if (r < 0 || row >= int(rows_.size())) return QPointF(-1, -1);
    return QPointF(8 + rows_[size_t(row)].depth * 14 + 5, kRuler + r * kRow + kRow / 2.0);
}

void SequencerTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    const QColor text = pal.color(QPalette::Text), faint(text.red(), text.green(), text.blue(), 40);
    p.fillRect(rect(), pal.color(QPalette::Base));
    p.fillRect(QRect(0, 0, kNames, height()), pal.color(QPalette::Window));
    const ke::SceneGraph& g = panel_->builder()->graph();
    const ke::SequenceObject* q = ke::activeSequence(g);
    const Axis a = axisOf(q, width(), panel_->atLeast());
    drawRuler(p, a, pal, height());
    if (const double end = endDrag_ >= 0 ? endDrag_ : double(g.world.duration); end > 0) { // the play range: a run does not go past its end
        const double x = a.x(end);
        p.fillRect(QRectF(x, 0, width() - x, height()), QColor(0, 0, 0, 80));
        p.setPen(QPen(kEnd, 2));
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
        p.fillRect(QRectF(x - 9, 3, 9, 13), kEnd); // its handle on the ruler, a flag over the range: dragged, the end moves
    }
    if (!q || q->tracks.empty()) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(kNames + kPad, kRuler + 4, width() - kNames - 2 * kPad, height() - kRuler - 8), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   uiText("Kinetica", "Выделите объект и нажмите «◆ Ключ» (I): секвенция запомнит, где он стоит в момент бегунка. Передвиньте бегунок, "
                   "переставьте объект, ещё ключ — между ключами он поедет сам. Или «▶● Запись» сверху: физика станет секвенцией."));
    }
    const bool unfold = !filter_.isEmpty();
    const uint32_t selected = panel_->builder()->selectedId();
    const int first = scroll_->value();
    for (int r = first; q && r < int(rows_.size()); ++r) {
        const int y = kRuler + (r - first) * kRow;
        if (y > height()) break;
        const Row& row = rows_[size_t(r)];
        const double mid = y + kRow / 2.0;
        if (row.kind != Row::Track) p.fillRect(QRect(0, y, width(), kRow), QColor(128, 128, 128, 30));
        else if (r % 2) p.fillRect(QRect(kNames, y, width() - kNames, kRow), QColor(128, 128, 128, 14));
        const int indent = 8 + row.depth * 14;
        if (row.kind != Row::Track) {
            const bool open = row.kind == Row::All ? allOpen_ || unfold : open_.count(row.id) || unfold;
            p.setPen(text);
            p.drawText(QRectF(indent, y, 12, kRow), Qt::AlignCenter, open ? "▾" : "▸");
        }
        QFont f = font();
        f.setBold(row.kind != Row::Track || row.id == selected);
        f.setItalic(row.lost);
        p.setFont(f);
        p.setPen(row.lost ? pal.color(QPalette::PlaceholderText) : text);
        const int left = indent + (row.kind == Row::Track ? 0 : 14);
        const QString words = row.kind == Row::Track ? row.name : QString("%1 · %2").arg(row.name).arg(row.count);
        p.drawText(QRectF(left, y, kNames - left - 4, kRow), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(words, Qt::ElideRight, kNames - left - 4));
        p.setFont(font());
        if (row.kind != Row::Track) { // the keys of its tracks, one tick a pixel
            p.setPen(QPen(QColor(kKey.red(), kKey.green(), kKey.blue(), 210), 2));
            int last = -10;
            for (float t : row.times) {
                const int x = int(a.x(double(t)));
                if (x == last) continue;
                last = x;
                p.drawLine(QPointF(x, mid - 5), QPointF(x, mid + 5));
            }
            continue;
        }
        if (row.track >= q->tracks.size()) continue;
        const ke::SequenceTrack& t = q->tracks[row.track];
        if (t.keys.size() > 1) {
            p.setPen(QPen(QColor(kKey.red(), kKey.green(), kKey.blue(), 90), 2));
            p.drawLine(QPointF(a.x(double(t.keys.front().time)), mid), QPointF(a.x(double(t.keys.back().time)), mid));
        }
        for (const ke::AnimKey& k : t.keys)
            drawKey(p, QPointF(a.x(double(k.time)), mid), k.interp, row.lost ? QColor(150, 150, 150) : kKey,
                    t.object == keyObject_ && std::fabs(double(k.time) - keyTime_) < 1e-6, text);
    }
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.drawLine(QPointF(kNames, 0), QPointF(kNames, height()));
    if (q && q->length() > 0) { // where the sequence ends
        p.setPen(QPen(faint, 1, Qt::DashLine));
        p.drawLine(QPointF(a.x(double(q->length())), kRuler), QPointF(a.x(double(q->length())), height()));
    }
    drawPlayhead(p, a, panel_->playhead(), height());
}

void SequencerTimeline::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPointF at = e->position();
    const ke::SceneGraph& g = panel_->builder()->graph();
    if (const double end = double(g.world.duration); end > 0 && at.y() < kRuler
        && std::fabs(at.x() + 3 - axisOf(ke::activeSequence(g), width(), panel_->atLeast()).x(end)) <= 8) {
        endDrag_ = end; // the play range's end taken by its handle
        return;
    }
    const int r = rowAt(at.y());
    uint32_t object = 0;
    double t = 0;
    if (r >= 0) {
        const Row& row = rows_[size_t(r)];
        if (row.kind != Row::Track && at.x() < 8 + row.depth * 14 + 14) return toggle(r);
        if (keyAt(at, object, t)) {
            keyObject_ = object, keyTime_ = t;
            panel_->builder()->select(object);
            update();
            return;
        }
        if (at.x() < kNames) {
            if (row.id && !row.lost) panel_->builder()->select(row.id);
            keyObject_ = 0, keyTime_ = -1;
            update();
            return;
        }
    }
    keyObject_ = 0, keyTime_ = -1;
    if (at.x() >= kNames) panel_->scrub(axisOf(ke::activeSequence(panel_->builder()->graph()), width(), panel_->atLeast()).time(at.x()));
}

void SequencerTimeline::mouseMoveEvent(QMouseEvent* e) {
    if (endDrag_ >= 0) { // to a twentieth of a second, never before a tenth
        const double t = axisOf(ke::activeSequence(panel_->builder()->graph()), width(), panel_->atLeast()).time(e->position().x());
        endDrag_ = std::max(0.1, std::round(t * 20) / 20);
        return update();
    }
    if (panel_->scrubbing()) panel_->scrub(axisOf(ke::activeSequence(panel_->builder()->graph()), width(), panel_->atLeast()).time(e->position().x()));
}

void SequencerTimeline::mouseReleaseEvent(QMouseEvent*) {
    if (endDrag_ >= 0) {
        const double end = endDrag_;
        endDrag_ = -1;
        return panel_->setEnd(end);
    }
    if (panel_->scrubbing()) panel_->endScrub();
}

void SequencerTimeline::mouseDoubleClickEvent(QMouseEvent* e) {
    uint32_t object = 0;
    double t = 0;
    const int r = rowAt(e->position().y());
    if (keyAt(e->position(), object, t)) panel_->seek(t);
    else if (r >= 0 && rows_[size_t(r)].kind != Row::Track && e->position().x() < kNames) toggle(r);
}

void SequencerTimeline::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && keyObject_) {
        const QJsonObject a{{"id", int(keyObject_)}, {"time", keyTime_}};
        keyObject_ = 0, keyTime_ = -1;
        panel_->run("sequence_delete_key", a);
    } else if (e->key() == Qt::Key_I) panel_->keySelected();
    else QWidget::keyPressEvent(e);
}

void SequencerTimeline::contextMenuEvent(QContextMenuEvent* e) {
    uint32_t object = 0;
    double t = 0;
    if (keyAt(e->pos(), object, t)) panel_->keyMenu(object, t, e->globalPos());
}

void SequencerTimeline::wheelEvent(QWheelEvent* e) { scroll_->setValue(scroll_->value() - e->angleDelta().y() / 40); }

void SequencerTimeline::resizeEvent(QResizeEvent*) { fitScroll(); }
