#pragma once
// A scene described in words - what a person sees in the editor, for a person or a language model that
// builds by text: the bodies as a tree of what hangs on what through joints (a chassis, its shock
// absorbers, their wheels), each with its shape, size, mass and roles; each joint's kind, what moves
// and what drives it (a spring, a DC motor and its driver's duty); the sensors and where they look in
// their carrier's frame (forward +x, up +y, left -z: the modules' convention, Assembly.h); the
// batteries. Numbers are rounded for reading, written with a '.' whatever the locale. The scene file
// stays the exact record; this is its summary.
#include "scene/SceneGraph.h"

#include <string>

namespace ke {

std::string describeScene(const SceneGraph& g);

} // namespace ke
