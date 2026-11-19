#pragma once
// Bounding volume hierarchy (binned SAH) over boxes, and over a triangle mesh's triangles: ray casts (the
// editor's picking of big models) and the closest point with an angle-weighted pseudo-normal.

#include "math/Math.h"
#include "core/Mesh.h"

#include <vector>

namespace ke {

struct BVHNode {
    AABB box;
    int32_t first = 0;  // interior: index of left child (right = first + 1); leaf: first primitive slot
    int32_t count = 0;  // 0 for interior nodes
    bool isLeaf() const { return count > 0; }
};

class BVH {
public:
    void build(const std::vector<AABB>& primBoxes, int maxLeafSize = 4);
    void clear() { nodes_.clear(); prims_.clear(); }
    bool empty() const { return nodes_.empty(); }

    const std::vector<BVHNode>& nodes() const { return nodes_; }
    const std::vector<uint32_t>& prims() const { return prims_; }
    AABB bounds() const { return nodes_.empty() ? AABB() : nodes_[0].box; }

private:
    void subdivide(int nodeIdx, const std::vector<AABB>& boxes, const std::vector<Vector3>& centroids, int maxLeaf, int depth);

    std::vector<BVHNode> nodes_;
    std::vector<uint32_t> prims_;
};

struct RayHit {
    Real t = kInf;
    int triangle = -1;
    Real u = 0, v = 0;
};

struct ClosestHit {
    Vector3 point;
    Vector3 normal;           // pseudo-normal at the closest feature (outward)
    Real distance = kInf; // unsigned
    Real signedDistance = kInf;
    int triangle = -1;
};

class MeshBVH {
public:
    void build(const TriMesh& mesh);
    void clear();
    bool empty() const { return tris_.empty(); }
    AABB bounds() const { return bvh_.bounds(); }
    const BVH& bvh() const { return bvh_; }

    void triangle(size_t t, Vector3& a, Vector3& b, Vector3& c) const {
        a = pos_[tris_[t][0]]; b = pos_[tris_[t][1]]; c = pos_[tris_[t][2]];
    }
    const Vector3& faceNormal(size_t t) const { return faceN_[t]; }

    bool raycast(const Vector3& origin, const Vector3& dir, Real tmax, RayHit& hit) const;
    // Nearest surface point within maxDist; false if nothing is that close.
    bool closestPoint(const Vector3& p, Real maxDist, ClosestHit& out) const;

private:
    std::vector<Vector3> pos_;
    std::vector<std::array<uint32_t, 3>> tris_;
    std::vector<Vector3> faceN_;
    std::vector<Vector3> vertexN_;  // angle weighted
    std::vector<Vector3> edgeN_;    // 3 per triangle: ab, bc, ca
    BVH bvh_;
};

} // namespace ke
