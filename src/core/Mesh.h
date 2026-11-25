#pragma once
// Indexed triangle mesh, procedural primitives and OBJ/STL import.

#include "math/Math.h"

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ke {

struct TriMesh {
    std::vector<Vector3> positions;
    std::vector<std::array<uint32_t, 3>> triangles;
    // A colour a triangle, linear RGB 0..1 - a model's materials (an OBJ's vertex colours "v x y z r g b"); empty: the
    // whole mesh takes its object's colour. Kept by weld and removeDegenerate; a viewer uses it only when there
    // is one a triangle (an operation that remakes the triangles drops it).
    std::vector<Vector3> triangleColors;
    bool coloured() const { return !triangleColors.empty() && triangleColors.size() == triangles.size(); }
    // A texture coordinate a triangle corner (three a triangle, in the triangles' order), an OBJ's "vt" - u right, v up, as OBJ and
    // OpenGL have them: a picture laid by the model's own unwrapping. Empty: none. Kept by weld and removeDegenerate, turned with a
    // flipped winding; a viewer uses it only when there is one a corner (an operation that remakes the triangles drops it).
    std::vector<std::array<float, 2>> uvs;
    bool textured() const { return !uvs.empty() && uvs.size() == 3 * triangles.size(); }

    bool empty() const { return triangles.empty(); }
    AABB bounds() const;
    Vector3 faceNormal(size_t t) const;   // unit normal, (0,0,0) for degenerate
    Real signedVolume() const;        // > 0 for outward oriented closed meshes
    // How far a vertex lies outside a face plane, at most over all faces, after subtracting the
    // rounding uncertainty of each face's plane (Mesh.cpp): <= 0 for an exactly convex mesh, and a
    // sliver triangle cannot report a convex mesh as concave.


    void transform(const Matrix3x3& R, const Vector3& scale, const Vector3& translation); // p' = R*(scale*p)+t
    void translate(const Vector3& t);
    void flipWinding();
    // Merge vertices closer than eps (needed for STL and for pseudo-normal SDF).
    void weld(Real eps);
    // Removes zero-area triangles.
    void removeDegenerate(Real areaEps = 1e-14_r);
    // Flips the winding if the mesh is inside-out.
    void orientOutward();
    // Uniformly scale so the largest extent equals `size` and move bounds center to `center`.
    void fitTo(const Vector3& center, Real size);
};

namespace primitives {
TriMesh box(const Vector3& halfExtents);
// Surface of revolution around the X axis; profile points are (x, radius).
TriMesh revolve(const std::vector<std::pair<Real, Real>>& profile, int segments);
TriMesh sphere(Real radius, int segments = 48, int rings = 24);
TriMesh ellipsoid(const Vector3& radii, int segments = 48, int rings = 24);
// Cylinder of given radius and length with its axis along Z.
TriMesh cylinder(Real radius, Real length, int segments = 48);
TriMesh cone(Real radius, Real length, int segments = 48); // apex pointing to -X
// Capsule with its axis along Y: a side of length 2 halfHeight and two hemispherical caps.
} // namespace primitives

// Format detected by extension (.obj, .stl). Returns false and fills `error` on failure.
bool loadMesh(const std::string& path, TriMesh& out, std::string& error);

} // namespace ke
