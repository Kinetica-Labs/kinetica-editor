// The collider wireframes of the scene builder (see SceneBuilder.h and ColliderGuides.h): in edit
// mode made from the graph - each entity's collider placed where the entity stands -, while the
// scene plays at the frame's bodies - the body's pose is the collider's pose, so the barrel outline
// rolls around the sphere that it moves. The selected object always shows its collider; the
// toolbar's "Коллайдеры" shows every object's.
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "scene/Collider.h"
#include "sim/RobotWorld.h"

#include "core/Format.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>

using namespace ke;

namespace {

// What an entity's collider depends on (not its pose): its geometry and the collider's fields.
std::string colliderKey(const Entity& e, const std::string& baseDirectory) {
    const ColliderRole& c = e.collider;
    return entityGeometryKey(e, baseDirectory) + QString::asprintf(" | %d %d %d %g %g %g %g %g %g %g %g %g", int(c.enabled), int(c.kind),
                                                                   int(c.fitToGeometry), double(c.size.x), double(c.size.y), double(c.size.z),
                                                                   double(c.offset.x), double(c.offset.y), double(c.offset.z),
                                                                   double(c.rotationDeg.x), double(c.rotationDeg.y), double(c.rotationDeg.z))
                                                       .toStdString();
}

} // namespace

const std::vector<ShapeDesc>& SceneBuilder::colliderOf(const Entity& e, std::string* key) {
    auto& cached = colliders_[e.id];
    const std::string k = colliderKey(e, graph_.baseDirectory);
    if (cached.first != k) cached = {k, colliderShapes(e, entityLocalMesh(e, graph_.baseDirectory))};
    if (key) *key = k;
    return cached.second;
}

void SceneBuilder::setShowAllColliders(bool on) {
    showAllColliders_ = on;
    refreshColliderGuides();
}

void SceneBuilder::refreshColliderGuides() {
    std::vector<float> lines;
    if (!editing()) return; // play mode: the next snapshot brings them (playColliderGuides)
    for (const Entity& w : worldEntities(graph_)) { // world poses: groups composed, instances resolved, array copies
        if (!w.visible || w.locked || !w.collider.enabled || (!showAllColliders_ && !drawnSelected(w.id))) continue;
        std::string key;
        const std::vector<ShapeDesc>& shapes = colliderOf(w, &key);
        const bool moves = w.rigid.enabled && !w.rigid.fixed;
        guides_.append(key, shapes, w.position, entityRotation(w), moves ? ColliderGuides::Dynamic : ColliderGuides::Static, lines);
    }
    guides_.prune();
    emit colliderGuides(std::move(lines));
    emitShapeHandles();
}

// While the scene plays: every body's collider at the body's pose in the frame (the running world
// builds the body at its entity's pose, the collider in the entity's frame); only the bodies of the
// selected object unless all are asked for.
void SceneBuilder::playColliderGuides(const Frame& s) {
    std::vector<float> lines;
    std::map<uint32_t, const Entity*> drawn;
    const std::vector<Entity> world = worldEntities(graph_);
    for (const Entity& w : world) drawn[w.id] = &w;
    for (const Frame::Body& body : s.bodies) {
        const auto w = drawn.find(body.object);
        if (body.body < 0 || w == drawn.end() || (!showAllColliders_ && !drawnSelected(body.object))) continue;
        const Entity& e = *w->second;
        if (!e.collider.enabled || e.locked) continue;
        const ColliderGuides::State state = !body.movable ? ColliderGuides::Static
                                            : body.sleeping ? ColliderGuides::Sleeping
                                                            : ColliderGuides::Dynamic;
        std::string key;
        const std::vector<ShapeDesc>& shapes = colliderOf(e, &key);
        guides_.append(key, shapes, body.pos, body.rot, state, lines);
    }
    emit colliderGuides(std::move(lines));
}

// ---------------------------------------------------------------------------
// The collider mode: the gizmo and the points hold the collider (SceneBuilder.h setColliderEdit)
// ---------------------------------------------------------------------------
Entity* SceneBuilder::colliderEntity() { return colliderEdit_ ? masterById(colliderEdit_) : nullptr; }

ColliderRole SceneBuilder::heldNow(const Entity& e) const {
    const bool fitted = e.collider.kind != ColliderKind::Auto && e.collider.fitToGeometry; // only a fit reads the geometry
    return heldCollider(e, fitted ? entityLocalMesh(e, graph_.baseDirectory) : TriMesh());
}

bool SceneBuilder::colliderBodyPose(Vector3& p, Quaternion& q) const {
    const SceneObject* o = findObject(graph_, colliderEdit_);
    if (!o) return false;
    worldPose(graph_, *o, p, q);
    return true;
}

// A box's six faces; a sphere's rim; a capsule's or a cylinder's rim and both ends - on the collider's own axes.
std::vector<ShapeHandle> SceneBuilder::handlesOf(const ColliderRole& c) const {
    std::vector<ShapeHandle> out;
    Vector3 p;
    Quaternion q;
    if (!sizedByNumbers(c.kind) || !colliderBodyPose(p, q)) return out;
    const Vector3 centre = p + q.rotate(c.offset);
    const Quaternion turn = q * colliderTurn(c);
    static const char* faces[3][2] = {{uiText("Kinetica", "грань +X"), uiText("Kinetica", "грань −X")}, {uiText("Kinetica", "грань +Y"), uiText("Kinetica", "грань −Y")}, {uiText("Kinetica", "грань +Z"), uiText("Kinetica", "грань −Z")}};
    auto add = [&](int dimension, int side, bool both, const char* what) {
        ShapeHandle h;
        Vector3 axis(0.0f);
        axis[dimension] = float(side);
        h.centre = centre, h.axis = turn.rotate(axis), h.reach = 0.5f * c.size[dimension];
        h.dimension = dimension, h.side = side, h.both = both, h.what = what;
        out.push_back(h);
    };
    if (c.kind == ColliderKind::Box)
        for (int k = 0; k < 3; ++k) add(k, 1, false, faces[k][0]), add(k, -1, false, faces[k][1]);
    else add(0, 1, true, uiText("Kinetica", "диаметр"));
    if (c.kind == ColliderKind::Capsule || c.kind == ColliderKind::Cylinder) add(1, 1, true, uiText("Kinetica", "высота")), add(1, -1, true, uiText("Kinetica", "высота"));
    return out;
}

void SceneBuilder::emitShapeHandles() {
    const Entity* e = colliderEntity();
    if (colliderEdit_ && (!e || !editing() || !e->collider.enabled)) { // the shape gone, the collider off, the scene playing
        colliderEdit_ = 0, colliderDrag_ = false;
        emit colliderEditChanged(false);
    }
    emit shapeHandles(colliderEdit_ ? handlesOf(heldNow(*e)) : forceById(selectedId_) ? forceHandles() : componentHandles()); // else a force's or a plugin's orange points
}

void SceneBuilder::setColliderEdit(bool on) {
    const Entity* e = on && editing() && selection_.size() == 1 ? masterById(selectedId_) : nullptr;
    if (on && (!e || !e->collider.enabled || isHandle(*e))) {
        emit statusMessage(uiText("Kinetica", "Коллайдер: выберите одно тело с коллайдером (в правке)"));
        on = false;
    } else if (on && !sizedByNumbers(heldNow(*e).kind)) {
        emit statusMessage(uiText("Kinetica", "Коллайдер повторяет геометрию (выпуклая оболочка): выберите в карточке коробку, сферу, капсулу или цилиндр — "
                           "их место и размер задаются руками"));
        on = false;
    }
    const uint32_t was = colliderEdit_;
    colliderEdit_ = on ? selectedId_ : 0;
    if (was != colliderEdit_) {
        sendViewportFlags(); // the shape see-through, its collider over it
        updateGizmoTarget();
        refreshColliderGuides();
        emitShapeHandles();
        if (on) emit statusMessage(uiText("Kinetica", "Коллайдер «") + QString::fromStdString(e->name) + uiText("Kinetica", "»: точки — размер, гизмо — место и поворот; "
                                   "модель не меняется. Esc — назад к телу"));
    }
    emit colliderEditChanged(colliderEdit_ != 0);
}

void SceneBuilder::beginColliderDrag() {
    Entity* e = colliderEntity();
    if (!e || colliderDrag_) return;
    flushCode();
    remember();
    colliderGraph_ = graph_;
    colliderStart_ = heldNow(*e);
    colliderHandles_ = handlesOf(colliderStart_);
    colliderBodyPose(colliderBodyAt_, colliderBodyTurn_);
    colliderDrag_ = true;
}

void SceneBuilder::showCollider(const ColliderRole& c) {
    Entity* e = colliderEntity();
    if (!e) return;
    e->collider = c;
    e->collider.enabled = true;
    refreshColliderGuides();
}

// A point: a face moves and the far one stays (the centre goes half the way), a diameter or a height grows both ways.
void SceneBuilder::onShapeHandle(int index, Vector3 point, int phase) {
    if (!colliderEdit_) return forceById(selectedId_) ? onForceHandle(index, point, phase) : onComponentHandle(index, point, phase); // a force's point, a plugin's field's
    if (phase == 0) return beginColliderDrag();
    if (!colliderDrag_ || index < 0 || index >= int(colliderHandles_.size())) return;
    if (phase == 3) return finishColliderDrag(true);
    const ShapeHandle& h = colliderHandles_[size_t(index)];
    const float reach = dot(point - h.centre, h.axis);
    ColliderRole c = colliderStart_;
    const float minimum = 0.005f;
    if (h.both) {
        const float full = std::max(minimum, 2.0f * reach);
        if (c.kind == ColliderKind::Sphere) c.size = Vector3(full);
        else if (h.dimension == 0) c.size.x = c.size.z = full;
        else c.size.y = full;
    } else {
        const float was = colliderStart_.size[h.dimension], full = std::max(minimum, reach + 0.5f * was);
        Vector3 along(0.0f);
        along[h.dimension] = float(h.side) * 0.5f * (full - was);
        c.size[h.dimension] = full;
        c.offset = colliderStart_.offset + colliderTurn(colliderStart_).rotate(along);
    }
    showCollider(c);
    updateGizmoTarget();
    if (phase == 2) finishColliderDrag(false);
}

void SceneBuilder::finishColliderDrag(bool cancelled) {
    if (!colliderDrag_) return;
    colliderDrag_ = false;
    const Entity* e = colliderEntity();
    const Entity* before = findEntity(colliderGraph_, e ? e->id : 0);
    const auto same = [](const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    const bool changed = e && before && (!same(e->collider.size, colliderStart_.size) || !same(e->collider.offset, colliderStart_.offset) ||
                                         !same(e->collider.rotationDeg, colliderStart_.rotationDeg) || e->collider.kind != colliderStart_.kind);
    if (cancelled || !changed) { // put back, and no undo step for nothing
        graph_ = colliderGraph_;
        doc_.history.pop();
        updateHistoryActions();
    }
    refreshColliderGuides();
    updateGizmoTarget();
    fillInspector();
    if (cancelled || !changed) return;
    QJsonObject args;
    const QString py = colliderLine(args);
    codeAction("scene_collider", py, args, false);
}

QString SceneBuilder::colliderLine(QJsonObject& args) const {
    const Entity* e = findEntity(graph_, masterOf(colliderEdit_));
    if (!e) return {};
    const ColliderRole& c = e->collider;
    const char* kind = c.kind == ColliderKind::Sphere ? "sphere" : c.kind == ColliderKind::Capsule ? "capsule" : c.kind == ColliderKind::Cylinder ? "cylinder" : "box";
    args["id"] = int(colliderEdit_), args["kind"] = kind;
    QString py = QString("ed.scene_collider(id=%1, kind=\"%2\", ").arg(colliderEdit_).arg(kind);
    const auto number = [](float v) { return QString::fromStdString(numberText(v)); };
    const auto json = [](const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
    if (c.kind == ColliderKind::Box) args["size"] = json(c.size), py += "size=" + tuple(c.size);
    else args["diameter"] = double(c.size.x), py += "diameter=" + number(c.size.x);
    if (c.kind == ColliderKind::Capsule || c.kind == ColliderKind::Cylinder) args["height"] = double(c.size.y), py += ", height=" + number(c.size.y);
    args["offset"] = json(c.offset), args["rotation"] = json(c.rotationDeg);
    return py + ", offset=" + tuple(c.offset) + ", rotation=" + tuple(c.rotationDeg) + ")";
}
