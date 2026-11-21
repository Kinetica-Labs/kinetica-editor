#pragma once
// The collider of a body drawn as thin guide lines over its geometry: the user sees
// WHAT the body collides with (a barrel around a sphere)
// while the geometry itself stays what it is. Lines, not a triangle wireframe: a box gets its 12
// edges, a sphere three great circles, a capsule its two rings, four side lines and the arcs of its
// caps, a convex hull its real edges - the diagonals that only split a flat face into triangles are
// left out.
//
// A collider is the physics engine's shapes in the body's frame (ke::ShapeDesc, made by
// ke::colliderShapes in sim/RobotWorld.h - exactly what the running world builds). Its lines are made
// once per collider (by a key the caller gives: what the collider depends on) and kept while it is
// asked for; every frame they are only turned and moved to where the body is (play mode) or where the
// object stands (edit mode).
#include "math/Math.h"
#include "physics/PhysicsWorld.h"

#include <QString>

#include <map>
#include <string>
#include <vector>

namespace ke { struct TriMesh; }

class ColliderGuides {
public:
    // What the collider belongs to, for its colour: a body that moves (green), a fixed obstacle
    // (grey), a body at rest that the engine has put to sleep (dim green).
    enum State { Dynamic = 0, Static = 1, Sleeping = 2 };
    // Appends the guide lines of the collider `shapes` (in the body's frame) placed at (position,
    // rotation) as pairs of points, x y z state per point (GL_LINES, the viewport's line layout; the
    // state picks the colour). `key` names the collider: equal keys, equal shapes.
    void append(const std::string& key, const std::vector<ke::ShapeDesc>& shapes, const ke::Vector3& position,
                const ke::Quaternion& rotation, State state, std::vector<float>& lines);
    // Forgets the lines of colliders not asked for since the last prune (call once per rebuild).
    void prune();
    // The lines of a collider in the body's frame (tests; also what append() places).
    const std::vector<ke::Vector3>& localLines(const std::string& key, const std::vector<ke::ShapeDesc>& shapes);

private:
    struct Entry {
        std::vector<ke::Vector3> lines; // segment end points, pairs, in the body's frame
        bool used = true;               // asked for since the last prune
    };
    std::map<std::string, Entry> cache_;
};

// A point of the collider mode (shape handles): on the collider's surface, dragged along the line from the
// collider's centre through it. A box has one on each face (the far face stays), a round collider one on its rim (the
// diameter) and a capsule or a cylinder one on each end (the height, both ends at once).
// The same points carry a plugin's fields (orange handles on a parametric object: a radius, a length, a point
// dragged where it is seen, an angle on its circle, a box's size on its faces): a line handle moves along its line, a
// free one in the plane facing the eye - or, given a normal, in that plane (a dial: around an axis).
struct ShapeHandle {
    ke::Vector3 centre, axis; // the world line it moves on: the collider's centre, outward
    float reach = 0;          // where it is on that line, metres from the centre
    int dimension = 0;        // the size it changes: x, y or z of the collider
    int side = 1;             // the face's side (+1, -1)
    bool both = false;        // both sides at once (a diameter, a height); otherwise the far side stays
    const char* what = "";    // said in the status bar: "грань +X", "диаметр", "высота"
    bool free = false;        // a point: dragged in the plane facing the eye, not along the line (centre + axis * reach is it)
    ke::Vector3 normal{0.0f}; // a free point held to the plane through it with this normal (zero: the plane facing the eye)
    std::vector<ke::Vector3> guide; // a thin line through these world points, drawn with the point (a dial's circle)
    bool plugin = false;      // a plugin's field: orange; a collider's: green
    QString label;            // a plugin's field's words (what overrides `what`)
};

// The guide lines of one shape kind in the shape's frame (pairs of points).
std::vector<ke::Vector3> boxGuide(const ke::Vector3& halfExtents);
std::vector<ke::Vector3> sphereGuide(float radius);
std::vector<ke::Vector3> capsuleGuide(float radius, float halfHeight); // axis along y
std::vector<ke::Vector3> meshEdgeGuide(const ke::TriMesh& mesh);       // edges between faces that bend
std::vector<ke::Vector3> hullGuide(const std::vector<ke::Vector3>& points); // the edges of the points' convex hull
// The lines of one shape of a collider in the body's frame (its own position and turn applied).
std::vector<ke::Vector3> shapeGuide(const ke::ShapeDesc& shape);
