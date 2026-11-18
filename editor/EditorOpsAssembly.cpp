// What a ready link would add, without adding it (EditorOps.h): the new objects' blocks and their
// joints as the viewer draws them - the window's ghost under the magnet, and an agent's look before it
// puts the link (the putting itself, and every other assembly operation, is the core's: ke::sceneOps).
// Faces are "+x" ... "-z" of the target's own frame.
#include "EditorOps.h"

#include "JointGlyphs.h"
#include "SceneBuilder.h"
#include "scene/Assembly.h"
#include "scene/SceneEdits.h"

using namespace ke;

namespace {

const char* kFaces[] = {"+x", "-x", "+y", "-y", "+z", "-z"};

bool vector(const QJsonValue& v, Vector3& out) {
    const QJsonArray a = v.toArray();
    if (a.size() != 3) return false;
    out = Vector3(Real(a[0].toDouble()), Real(a[1].toDouble()), Real(a[2].toDouble()));
    return true;
}

bool face(const QJsonValue& v, BoxFace& out) {
    for (int f = 0; f < 6; ++f)
        if (v.toString() == kFaces[f]) return out = BoxFace(f), true;
    return false;
}

// The place the arguments name: a face by name, or the polygon hit (point and normal), or the face
// nearest a point.
FaceSnap placeFrom(const SceneGraph& g, const QJsonObject& a, const Vector3& size) {
    const uint32_t target = uint32_t(a.value("target").toInt());
    BoxFace f;
    Vector3 point, normal;
    if (face(a.value("face"), f)) return snapToFace(g, target, f, size);
    if (!vector(a.value("point"), point)) return FaceSnap();
    return vector(a.value("normal"), normal) ? snapToSurface(g, target, point, normal, size) : snapToFace(g, target, point, size);
}

const Entity* entityOf(const SceneGraph& g, const QJsonObject& a) {
    for (const Entity& o : g.entities)
        if (o.id == uint32_t(a.value("target").toInt())) return &o;
    return nullptr;
}

PresetOptions presetOptions(const QJsonObject& a) {
    PresetOptions o;
    o.axis = a.value("axis").toInt(-1);
    o.flip = a.value("flip").toBool();
    o.mirror = a.value("mirror").toBool();
    return o;
}

// What a preset would add, without adding it: the new objects' blocks of the scene file and their
// joints as the viewer draws them (world anchor and axis, the angle's zero direction, limits, size).
QJsonObject previewJson(const SceneGraph& g, const Entity& target, const QJsonObject& a, LinkPreset p) {
    SceneGraph copy = g;
    const FaceSnap at = placeFrom(copy, a, target.size);
    const size_t jointsBefore = copy.joints.size();
    QJsonArray blocks, joints;
    for (const Attached& l : applyPreset(copy, target.id, at, p, presetOptions(a)))
        blocks.append(QString::fromStdString(getObject(copy, l.entity)));
    const std::vector<JointVis> vis = jointVisuals(copy);
    for (size_t k = jointsBefore; k < vis.size(); ++k) joints.append(jointVisJson(vis[k], jointKindWord(copy.joints[k])));
    return QJsonObject{{"ok", !blocks.isEmpty()}, {"blocks", blocks}, {"joints", joints}, {"face", kFaces[int(at.face)]}};
}

} // namespace

void EditorOps::addPreviewOps() {
    addSync("assembly_preview", "What assembly_preset would add, without adding it: the new objects' blocks and their joints in the world",
            "{target: int, preset: str, face?|point?+normal?, axis?, flip?, mirror?}", [this](const QJsonObject& a) {
        LinkPreset p;
        const Entity* e = entityOf(builder_->graph(), a);
        if (!e || !presetFromWord(a.value("preset").toString().toStdString(), p)) return error("needs a shape's id and a known preset");
        const QJsonObject preview = previewJson(builder_->graph(), *e, a, p);
        return preview.value("ok").toBool() ? preview : error("no place on the shape");
    });
}
