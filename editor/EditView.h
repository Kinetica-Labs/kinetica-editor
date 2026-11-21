#pragma once
// The scene while it is being edited: the scene graph itself, drawn as it was authored, with no
// simulation running. The editor has three layers (the user's rule):
//   SOURCES      - geometry: a box, a sphere, a model file; physics never destroys it;
//   OBJECTS      - an entity: a source with a pose and roles (what the gizmo and inspector edit);
//   META-OBJECTS - what a role makes when the scene plays: a body of the physics engine, a joint's drive.
// In edit mode only the first two exist, and this class shows them: one drawn body per visible
// entity as the simulation would build it (ke::worldEntities: poses composed with their groups',
// instances resolved to their master's geometry, every copy of every array), and the mouse ray
// tested against that same mesh, so a click selects exactly what is drawn under the cursor. The id
// of a drawn body is its entity's id, or for an array's copy ke::arrayCopyId(array, n).
// The snapshot carries the scene's lights too (ke::describeLights, sim/RobotWorld.h): the edit view is lit as the
// played scene will be. Lights and cameras themselves have no body (SceneMarkers.h draws them).
//
// Picking: the ray is taken into the entity's own frame (the mesh is cached there by shape, size
// and model file, so the 200 copies of an array share one), tested against the mesh's box first,
// then against its triangles (Moller-Trumbore; a BVH for models of more than 2000 triangles).
#include "OrbitCamera.h"
#include "scene/SceneGraph.h"
#include "sim/Simulation.h"
#include "spatial/BVH.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// One object on a ray: its id, the distance along the ray, where the ray met it, and its name (for
// the "2 / 3: Штора" hint when several objects lie on the ray).
struct PickHit {
    uint32_t id = 0;
    float t = 0;
    ke::Vector3 point;
    std::string name;
    ke::Vector3 normal{0.0f}; // of the polygon hit, in the world, facing the ray (zero: not a mesh)
};

class EditView {
public:
    // The snapshot the viewport draws in edit mode; bodyEntity[i] = the drawn id of body i.
    std::shared_ptr<ke::Frame> snapshot(const ke::SceneGraph& g, const std::string& name,
                                                 std::vector<uint32_t>& bodyEntity);
    // Every visible, unlocked drawn entity the ray passes through, nearest first.
    std::vector<PickHit> pickAll(const ke::SceneGraph& g, const Ray& ray);
    // The box in the world of an entity posed in the world (one of ke::worldEntities).
    ke::AABB worldBounds(const ke::SceneGraph& g, const ke::Entity& world);

private:
    struct Source {
        std::string key; // shape, size and model file: the cache is rebuilt when it changes
        std::shared_ptr<const ke::TriMesh> mesh;
        ke::AABB bounds;
        std::shared_ptr<ke::MeshBVH> bvh; // big models only
    };
    const Source& source(const ke::SceneGraph& g, const ke::Entity& e);
    bool raycastLocal(const Source& s, const ke::Vector3& o, const ke::Vector3& d, float& t, int* triangle = nullptr) const;

    std::map<std::string, Source> cache_; // by shape, size and model file
};

// Moller-Trumbore ray / triangle: the distance t along the ray, false if missed or behind.
bool rayTriangle(const ke::Vector3& o, const ke::Vector3& d, const ke::Vector3& a, const ke::Vector3& b,
                 const ke::Vector3& c, float& t);
