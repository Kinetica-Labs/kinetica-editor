#pragma once
// A conservative cover of triangle surfaces, not just a cover of sampled vertices.
// Each subdivided triangle is assigned wholly to one enclosing sphere. This covers
// the surface, not the interior volume; containment and swept motion remain separate checks.
#include "core/Mesh.h"
#include "robot/ArmPlanner.h"

namespace ke {
std::vector<ArmSphere> armSurfaceCover(const TriMesh& mesh, Real cell);
}
