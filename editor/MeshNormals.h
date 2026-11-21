#pragma once
// How the viewer shades a mesh. A body drawn smooth (ke::Frame::Body::smooth, a sphere) gets "auto
// smooth": every corner of a triangle takes the
// mean normal of the faces around its point that bend less than the crease angle from its own face, so
// a sphere is round and sharper edges stay sharp. Any other body keeps its facets: every corner its
// face's normal (a low-poly model smoothed looks like a blob). The same rule in edit mode and in play.
#include "core/Mesh.h"
#include "math/Vector3.h"

#include <vector>

// One normal per triangle corner: normals[3 * t + k] for corner k of triangle t. Points at the same
// place count as one point, so a seam where a mesh repeats its vertices is smoothed across too.
std::vector<ke::Vector3> autoSmoothNormals(const ke::TriMesh& mesh, float creaseDegrees = 35.0f);
// The normals a body is drawn with, laid out the same way: auto smooth when `smooth`, else facets.
std::vector<ke::Vector3> meshNormals(const ke::TriMesh& mesh, bool smooth);
