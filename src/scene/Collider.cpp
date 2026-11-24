// The collider as numbers (see Collider.h).
#include "scene/Collider.h"

#include <algorithm>
#include <limits>

namespace ke {

namespace {

// The box of the geometry in the collider's axes: its centre (in the entity's frame) and its edges.
void fitBox(const TriMesh& mesh, const Quaternion& turn, Vector3& centre, Vector3& size) {
    Vector3 lo(std::numeric_limits<Real>::max()), hi(-std::numeric_limits<Real>::max());
    for (const Vector3& p : mesh.positions) {
        const Vector3 q = turn.conjugate().rotate(p);
        lo = vmin(lo, q), hi = vmax(hi, q);
    }
    if (mesh.positions.empty()) lo = hi = Vector3(0.0_r);
    centre = turn.rotate((lo + hi) * 0.5_r);
    size = hi - lo;
}

} // namespace

bool sizedByNumbers(ColliderKind k) {
    return k == ColliderKind::Box || k == ColliderKind::Sphere || k == ColliderKind::Capsule || k == ColliderKind::Cylinder;
}

ColliderKind builtKind(const Entity& e) {
    switch (e.shape) {
    case ShapeKind::Box:
    case ShapeKind::Plane: return ColliderKind::Box;
    case ShapeKind::Sphere: return ColliderKind::Sphere;
    default: return ColliderKind::ConvexHull;
    }
}

Vector3 kindForm(ColliderKind k, const Vector3& extent) {
    switch (k) {
    case ColliderKind::Sphere: return Vector3(maxComp(extent));
    case ColliderKind::Capsule:
    case ColliderKind::Cylinder: {
        const Real d = std::max(extent.x, extent.z);
        return Vector3(d, extent.y, d);
    }
    default: return extent;
    }
}

Quaternion colliderTurn(const ColliderRole& c) {
    SceneObject o;
    o.rotationDeg = c.rotationDeg;
    return objectRotation(o);
}

ColliderRole heldCollider(const Entity& e, const TriMesh& local) {
    ColliderRole held = e.collider;
    held.fitToGeometry = false;
    if (e.collider.kind == ColliderKind::Auto) { // the shape itself, centred, unturned
        held.kind = builtKind(e);
        held.offset = held.rotationDeg = Vector3(0.0_r);
        held.size = e.shape == ShapeKind::Plane ? Vector3(e.size.x, 0.02_r, e.size.z) : e.shape == ShapeKind::Sphere ? Vector3(e.size.x) : e.size;
        return held;
    }
    if (!sizedByNumbers(held.kind)) return held; // a hull: the geometry's own
    Vector3 size = e.collider.size;
    if (e.collider.fitToGeometry) {
        fitBox(local, colliderTurn(e.collider), held.offset, size);
        // Fitted, a sphere takes the largest edge and a capsule the larger of x and z; a cylinder its x.
        held.size = held.kind == ColliderKind::Cylinder ? Vector3(size.x, size.y, size.x) : kindForm(held.kind, size);
        return held;
    }
    // By hand, a sphere's diameter is x, a capsule's and a cylinder's x: the other numbers made to agree.
    held.size = held.kind == ColliderKind::Box ? size : held.kind == ColliderKind::Sphere ? Vector3(size.x) : Vector3(size.x, size.y, size.x);
    return held;
}

} // namespace ke
