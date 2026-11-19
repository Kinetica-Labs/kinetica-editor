#pragma once
// Lights and cameras in the 3D view. They have no geometry, so the editor draws each as a small
// wireframe (the way Blender and Maya do) and the mouse picks it by a ball around that wireframe:
//   Солнце    - a disc across its direction and an arrow along it (sunlight is parallel: where the
//               sun stands does not matter, only where it points);
//   Лампа     - a bulb of three rings; when selected, a faint circle as wide as its range;
//   Прожектор - its cone: the full cone solid, the cone where the soft edge begins faint;
//   Камера    - what it sees as a pyramid (the eye at the tip, the frame 0.35 m in front), with a
//               triangle over the frame's top edge that says which way is up.
// markersOf makes them from the scene graph; the viewport draws them and the window picks them.
// Lines are pairs of points in the world, x y z 0 each (the viewport's line format).
#include "OrbitCamera.h"
#include "math/Math.h"
#include "scene/SceneGraph.h"

#include <cstdint>
#include <vector>

struct SceneMarker {
    enum Kind { Sun, Point, Spot, Camera };
    uint32_t id = 0;
    Kind kind = Point;
    ke::Vector3 position;    // in the world
    ke::Quaternion rotation; // in the world: a light shines along -y, a camera looks along -z
    ke::Vector3 color{1.0f};
    float range = 5, coneDeg = 40, softnessDeg = 10, fovDeg = 50;
    float reach = 0; // one being placed: its cone, frame or arrow drawn this far, to where it looks (0: the marker's size)
    bool selected = false;
    bool aimed = false;  // a target camera: a faint line to its target and a cross there (3ds Max's camera target)
    ke::Vector3 aim{0.0f};
};

// The graph's visible lights and cameras as markers, posed in the world.
std::vector<SceneMarker> markersOf(const ke::SceneGraph& g);
// The marker's wireframe: solid lines, and the faint ones (a range, a soft edge) apart. `aspect` is
// the width / height of the view a camera's frame is drawn with.
void markerLines(const SceneMarker& m, float aspect, std::vector<float>& solid, std::vector<float>& faint);
// Where the ray meets the ball around the marker (t along the ray); false: it misses, or it starts
// inside the ball (a camera just made where the view stands must not catch every click).
bool markerHit(const SceneMarker& m, const Ray& ray, float& t);
