#pragma once
// A solid mesh as convex parts - what a moving body of any shape collides by: an engine collides convex shapes, and a
// mesh's own triangles only on a body that stands (sim/RobotWorld.cpp colliderShapes). Approximate convex decomposition
// the way V-HACD does it (Mamou & Ghorbel 2009; Unreal's, Isaac Sim's, Bullet's): the solid filled with voxels and cut by
// axis planes - the most concave part first, at the plane whose two pieces' hulls hold the least volume, each piece
// split into its connected ones. A part is cut no more when its hull stands out into the air no farther than `concavity`
// from the solid - CoACD's concavity (Wei et al. 2022): the hull's surface against the shape, 5 % of the mesh's
// diagonal there too - or when the parts number `hulls`. A part is given by the mesh's own surface points in it, so the voxels' steps never reach the
// collider. The same mesh gives the same parts on every compiler; the last meshes' parts are kept, so a model is cut once.
// The points an engine takes of each part: spatial/HullPoints.h.
#include "core/Mesh.h"

#include <vector>

namespace ke {

struct ConvexPartsOptions {
    int resolution = 64;     // voxels along the mesh's longest side
    int hulls = 32;          // parts at most
    Real concavity = 0.05_r; // how far a part's hull may stand out into the air from the solid, a share of the mesh's diagonal
};

// The points of each part, in the mesh's frame, each to be taken as its convex hull: one part - the mesh's own points -
// for a convex mesh, none for an empty one.
std::vector<std::vector<Vector3>> convexParts(const TriMesh& mesh, const ConvexPartsOptions& options = {});

// The convex hull of points (incremental, O(n^2): meant for a few hundred) as its triangles (three corners each,
// counter-clockwise seen from outside), their outward unit normals and offsets - a point p is inside when
// dot(normal, p) <= offset for every one - and its volume; empty for points that lie in a plane.
struct ConvexHull {
    std::vector<Vector3> triangles;
    std::vector<Vector3> normals;
    std::vector<Real> offsets;
    Real volume = 0;
};
ConvexHull convexHull(const std::vector<Vector3>& points);

} // namespace ke
