#pragma once
// The physics engine's settings, saved with a scene (SceneGraph.h, the scene file's physics line) and
// applied when its world is built (PhysicsWorld.h). The defaults hold a robot and a pile alike
// (docs/box3d-backend.md): an arm of six links sags 2 mm, a tower of ten cubes 0.4 mm, a pyramid of 406
// cubes stands within 1 mm. Box3D holds a joint no stiffer than a quarter of the substep's rate, so the
// joints' hertz rise with the substeps.
#include "math/Real.h"

namespace ke {

struct PhysicsSettings {
    int substeps = 32;             // per step of 1/60 s
    Real contactHertz = 240, contactDamping = 10;
    Real jointHertz = 480, jointDamping = 2;
    bool sleeping = true;          // bodies at rest stop being simulated until something touches them
    bool continuous = true;        // fast bodies do not pass through static ones
    Real linearDamping = 0, angularDamping = 0; // every body's, 1/s
};

} // namespace ke
