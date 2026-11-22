#pragma once
// Lights, cameras and forces in the 3D view. They have no geometry, so the editor draws each as a small
// wireframe and the mouse picks it by a ball around that wireframe:
//   Солнце    - a disc across its direction and an arrow along it (sunlight is parallel: where the
//               sun stands does not matter, only where it points);
//   Лампа     - a bulb of three rings; when selected, a faint circle as wide as its range;
//   Прожектор - its cone: the full cone solid, the cone where the soft edge begins faint;
//   Толчок    - an arrow along the force's own +y axis (against it for a negative strength), and, when the push
//               reaches only so far, a circle of that radius across it;
//   Взрыв     - a sphere of three rings of its radius with short arrows on the axes, out of it (into it for a
//               negative strength);
//   Момент    - three quarters of a ring about the force's own +y axis, its arrowhead the way it turns (the right
//               hand's, the thumb along +y; the other way for a negative strength), and the axis through it;
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
    enum Kind { Sun, Point, Spot, Camera, Push, Burst, Twist };
    uint32_t id = 0;
    Kind kind = Point;
    ke::Vector3 position;    // in the world
    ke::Quaternion rotation; // in the world: a light shines along -y, a camera looks along -z
    ke::Vector3 color{1.0f};
    float range = 5, coneDeg = 40, softnessDeg = 10, fovDeg = 50;
    float reach = 0; // one being placed: its cone, frame or arrow drawn this far, to where it looks (0: the marker's size);
                     // a force: the arrow's length, negative for one that pulls in (range is its radius)
    bool selected = false;
    bool idle = false;   // a force that pushes nothing as the scene stands (its shape is no moving body): drawn pale
    bool aimed = false;  // a target camera: a faint line to its target
    ke::Vector3 aim{0.0f};
};

// A force's marker, not yet posed: its kind by its shape, `range` its radius, `reach` +1 or -1 by its strength's sign.
SceneMarker forceMarker(const ke::SceneGraph& g, const ke::Force& f);
float forceArrow(const ke::SceneGraph& g, const ke::Force& f); // the signed length of a directional force's arrow, m (never under 0.15)
// The graph's visible lights, cameras and forces as markers, posed in the world.
std::vector<SceneMarker> markersOf(const ke::SceneGraph& g);
// The marker's wireframe: solid lines, and the faint ones (a range, a soft edge) apart. `aspect` is
// the width / height of the view a camera's frame is drawn with.
void markerLines(const SceneMarker& m, float aspect, std::vector<float>& solid, std::vector<float>& faint);
// Where the ray meets the ball around the marker (t along the ray); false: it misses, or it starts
// inside the ball (a camera just made where the view stands must not catch every click).
bool markerHit(const SceneMarker& m, const Ray& ray, float& t);
