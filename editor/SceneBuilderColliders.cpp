// The collider wireframes of the scene builder (see SceneBuilder.h and ColliderGuides.h): in edit
// mode made from the graph - each entity's collider placed where the entity stands -, while the
// scene plays at the frame's bodies - the body's pose is the collider's pose, so the barrel outline
// rolls around the sphere that it moves. The selected object always shows its collider; the
// toolbar's "Коллайдеры" shows every object's.
#include "SceneBuilder.h"

#include "sim/RobotWorld.h"

#include <QString>

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
