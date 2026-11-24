// The Sequencer's preview in edit mode (SceneBuilder.h): the active sequence's poses at the playhead drawn while it is
// dragged or played, the graph put back when it ends - the panel (SequencerPanel) then makes the one edit through the
// API. The sequences themselves, their keys and the playhead as an edit are the core's (scene/Animation.h, the
// sequence_* operations).
#include "SceneBuilder.h"

#include "UiLanguage.h"

#include "core/Json.h"
#include "core/Format.h"
#include "scene/Animation.h"
#include "scene/SceneOps.h"

#include <QJsonDocument>
#include <QLocale>

void SceneBuilder::beginSequencePreview() {
    if (editing() && !previewFrom_) previewFrom_ = graph_;
}

void SceneBuilder::previewSequence(double time) {
    ke::SequenceObject* q = ke::findSequence(graph_, 0);
    if (!editing() || !previewFrom_ || !q) return;
    ke::seekSequence(graph_, *q, ke::Real(time));
    refreshView();
    refreshLightsAndCameras();
    if (const ke::SceneObject* o = selectedObject()) showTransform(*o);
}

void SceneBuilder::endSequencePreview() {
    if (!previewFrom_) return;
    graph_ = std::move(*previewFrom_);
    previewFrom_.reset();
}

// The key points: each key of the selected object's track where the world has it (its parent's pose applied), and a
// smooth key between two others its Bezier handles - the key ± its velocity × a third of the time to each neighbour, as
// the curves draw them. Not while a preview plays, not of a track of one key (no path to drag along).
std::vector<ShapeHandle> SceneBuilder::keyHandles() {
    keyPoints_.clear();
    std::vector<ShapeHandle> out;
    const ke::SequenceObject* q = ke::activeSequence(graph_);
    const ke::SceneObject* o = editing() && !previewFrom_ && selection_.size() == 1 ? ke::findObject(graph_, selectedId_) : nullptr;
    const ke::SequenceTrack* t = q && o ? ke::trackOf(*q, o->id) : nullptr;
    if (!t || t->keys.size() < 2) return out;
    ke::Vector3 at(0.0f);
    ke::Quaternion turn;
    if (const ke::SceneObject* parent = o->parent ? ke::findObject(graph_, o->parent) : nullptr) ke::worldPose(graph_, *parent, at, turn);
    const auto world = [&](const ke::Vector3& p) { return at + turn.rotate(p); };
    const auto seconds = [](ke::Real s) { return QLocale().toString(double(s), 'f', 2); };
    for (size_t i = 0; i < t->keys.size(); ++i) {
        const ke::AnimKey& k = t->keys[i];
        ShapeHandle h;
        h.free = true, h.plugin = true, h.axis = ke::Vector3(1.0f, 0.0f, 0.0f), h.centre = world(k.position);
        h.label = QString(uiText("Kinetica", "Ключ %1 с")).arg(seconds(k.time));
        out.push_back(h);
        keyPoints_.push_back({k.time, 0, 0});
        const bool smooth = k.interp == ke::AnimInterp::Smooth || k.hasVelocity;
        if (!smooth || i == 0 || i + 1 == t->keys.size()) continue; // an end's slope is none: nothing to hold
        const ke::Vector3 v = ke::keyVelocity(t->keys, i);
        for (int side : {-1, 1}) {
            const ke::Real gap = side > 0 ? t->keys[i + 1].time - k.time : k.time - t->keys[i - 1].time;
            ShapeHandle b = h;
            b.centre = world(k.position + v * (gap / 3 * ke::Real(side)));
            b.guide = {h.centre, b.centre};
            b.label = QString(uiText("Kinetica", "Наклон ключа %1 с")).arg(seconds(k.time));
            out.push_back(b);
            keyPoints_.push_back({k.time, side, gap});
        }
    }
    return out;
}

void SceneBuilder::refreshKeyPoints() {
    std::vector<KeyPoint> was = keyPoints_;
    std::vector<ke::Vector3> where;
    for (const ShapeHandle& h : keyHandles()) where.push_back(h.centre);
    const bool same = where.size() == keyPointsShown_.size() &&
                      std::equal(where.begin(), where.end(), keyPointsShown_.begin(), [](const ke::Vector3& a, const ke::Vector3& b) { return length(a - b) < 1e-6f; });
    if (same) return void(keyPoints_ = std::move(was));
    emitShapeHandles();
}

// A key point dragged: its place - or, a handle, its velocity - set by sequence_key_set itself, on the scene as it was when
// taken, while the mouse moves (no undo step); at the release once more, one undo step and the operation's line in the
// console. Esc: back as it was.
void SceneBuilder::onKeyHandle(int index, ke::Vector3 point, int phase) {
    if (index < 0 || size_t(index) >= keyPoints_.size()) return;
    const KeyPoint p = keyPoints_[size_t(index)];
    const uint32_t id = selectedId_;
    if (phase == 0) {
        componentGraph_ = graph_;
        componentDragging_ = true;
        return;
    }
    if (!componentDragging_) return;
    graph_ = componentGraph_;
    if (phase == 3) componentDragging_ = false;
    else {
        const ke::SceneObject* o = ke::findObject(componentGraph_, id);
        const ke::SequenceObject* q = ke::activeSequence(componentGraph_);
        const ke::SequenceTrack* t = q && o ? ke::trackOf(*q, id) : nullptr;
        const auto key = t ? std::find_if(t->keys.begin(), t->keys.end(), [&](const ke::AnimKey& k) { return std::fabs(k.time - p.time) < 1e-6f; })
                           : std::vector<ke::AnimKey>::const_iterator();
        if (!t || key == t->keys.end()) return;
        ke::Vector3 at(0.0f);
        ke::Quaternion turn;
        if (const ke::SceneObject* parent = o->parent ? ke::findObject(componentGraph_, o->parent) : nullptr) ke::worldPose(componentGraph_, *parent, at, turn);
        const ke::Vector3 local = turn.conjugate().rotate(point - at); // on the parent, where keys are
        const ke::Vector3 value = p.side == 0 ? local : (local - key->position) * (3 / (p.gap * ke::Real(p.side)));
        const auto vec = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
        QJsonObject args{{"id", int(id)}, {"time", double(p.time)}, {p.side == 0 ? "position" : "velocity", vec(value)}};
        ke::Value r = ke::Value::map();
        std::string error;
        if (phase == 2) componentDragging_ = false, remember();
        ke::findSceneOp("sequence_key_set")->edit(graph_, ke::parseJson(QJsonDocument(args).toJson(QJsonDocument::Compact).toStdString()), r, error);
        if (phase == 2) {
            const auto three = [](const ke::Vector3& v) { return QString("(%1, %2, %3)").arg(QString::fromStdString(ke::numberText(v.x)), QString::fromStdString(ke::numberText(v.y)), QString::fromStdString(ke::numberText(v.z))); };
            codeAction("sequence_key_set", QString("ed.sequence_key_set(id=%1, time=%2, %3=%4)").arg(id).arg(QString::fromStdString(ke::numberText(p.time)))
                                               .arg(p.side == 0 ? "position" : "velocity", three(value)), args, false);
        }
    }
    refreshView();
    refreshLightsAndCameras();
    emitShapeHandles();
    if (const ke::SceneObject* o = selectedObject()) showTransform(*o);
    emit keysMoved();
}
