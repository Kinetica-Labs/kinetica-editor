#pragma once
// The collider as numbers (Collider.cpp): what an entity collides with, resolved - Авто read as the kind it builds, a
// fitted collider as the size, centre and turn the geometry gives it - so a hand or an agent holds it and changes one
// number of it without the rest jumping (scene_collider, the editor's collider mode). The running world builds from
// the same numbers (sim/RobotWorld.cpp colliderShapes).
#include "scene/SceneGraph.h"

namespace ke {

// Box, sphere, capsule, cylinder are sized by numbers; Авто and a convex hull follow the geometry.
bool sizedByNumbers(ColliderKind k);

// The kind Авто builds: a box for a box or a plane, a sphere for a sphere, a hull of the mesh otherwise.
ColliderKind builtKind(const Entity& e);

// The collider as the running world builds it, as numbers: the kind resolved, the fit undone into size, offset and turn,
// the size in its kind's own form - a sphere's diameter in x, y and z; a capsule's and a cylinder's diameter in x and z,
// the full height in y; a box's edges. `local`: the entity's mesh in its own frame (entityLocalMesh).
ColliderRole heldCollider(const Entity& e, const TriMesh& local);

// A box-like extent as the kind's own form: a sphere round the largest edge, a capsule and a cylinder round the larger
// of x and z, as tall as y; a box as given.
Vector3 kindForm(ColliderKind k, const Vector3& extent);

// The turn of a collider (its rotationDeg) as a quaternion, in the entity's frame.
Quaternion colliderTurn(const ColliderRole& c);

} // namespace ke
