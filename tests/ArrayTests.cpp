// Many objects at once, as data: hierarchy (a child of a turned group sits where the turn puts it),
// instances and arrays (a line, a grid and a circle of copies exactly where the pattern says). The
// text file keeps all of it.
#include "TestRunner.h"

#include "scene/SceneGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace ke;

namespace {

Entity boxEntity(const char* name, const Vector3& position, Real size) {
    Entity e;
    e.name = name;
    e.size = Vector3(size);
    e.position = position;
    e.rigid.enabled = e.collider.enabled = true;
    return e;
}

// A graph with a turned group holding a child, a master with a chain of instances and an array.
SceneGraph hierarchyGraph() {
    SceneGraph g;
    Group grp;
    grp.id = 10;
    grp.name = "Group";
    grp.position = {1, 0, 0};
    grp.rotationDeg = {0, 90, 0};
    grp.glued = true;
    g.groups.push_back(grp);
    Entity child = boxEntity("Child", {1, 0, 0}, 0.1_r);
    child.id = 1;
    child.parent = 10;
    g.entities.push_back(child);
    Entity master = boxEntity("Master", {0, 1, 0}, 0.2_r);
    master.id = 2;
    master.rigid.density = 700;
    master.shape = ShapeKind::Sphere;
    g.entities.push_back(master);
    Entity a = boxEntity("A", {0, 2, 0}, 0.3_r);
    a.id = 3;
    a.instanceOf = 2;
    g.entities.push_back(a);
    Entity b = boxEntity("B", {0, 3, 0}, 0.4_r);
    b.id = 4;
    b.instanceOf = 3; // a chain: B -> A -> Master
    g.entities.push_back(b);
    ArrayObject arr;
    arr.id = 20;
    arr.name = "Row";
    arr.templateId = 2;
    arr.count[0] = 5;
    arr.step = {0.25_r, 0, 0};
    arr.jitter = 0.01_r;
    arr.seed = 7;
    g.arrays.push_back(arr);
    return g;
}

} // namespace

void testHierarchyInstancesRoundTrip() {
    const SceneGraph g = hierarchyGraph();
    const std::string text = g.save();
    SceneGraph back;
    std::string error;
    const bool loaded = back.load(text, error);
    const std::string again = back.save();
    std::printf("  file: %zu bytes, %zu entities, %zu groups, %zu arrays; round trip %s\n", text.size(), back.entities.size(),
                back.groups.size(), back.arrays.size(), text == again ? "identical" : "DIFFERENT");
    CHECK(loaded, "load failed: %s", error.c_str());
    CHECK(text == again, "save -> load -> save changed the text");
    Vector3 p;
    Quaternion q;
    worldPose(g, g.entities[0], p, q); // the child at x = 1 of a group at x = 1 turned 90 deg about y
    const Vector3 e = eulerDegrees(q);
    std::printf("  child of the turned group: world (%.4f %.4f %.4f), yaw %.2f deg (expected (1 0 -1), 90)\n", p.x, p.y, p.z, e.y);
    CHECK(length(p - Vector3(1, 0, -1)) < 1e-5_r && std::fabs(e.y - 90) < 1e-3_r, "wrong world pose of a child");
    SceneObject moved = g.entities[0];
    setWorldPose(g, moved, {2, 0.5_r, -1}, q);
    Vector3 p2;
    Quaternion q2;
    worldPose(g, moved, p2, q2);
    CHECK(length(p2 - Vector3(2, 0.5_r, -1)) < 1e-5_r, "setWorldPose does not invert worldPose");
    const Entity b = resolveInstance(g, g.entities[3]);
    std::printf("  instance chain B -> A -> Master: shape %s, density %.0f, own position y %.1f, own id %u\n",
                b.shape == ShapeKind::Sphere ? "sphere" : "other", b.rigid.density, b.position.y, b.id);
    CHECK(b.shape == ShapeKind::Sphere && b.rigid.density == 700 && b.position.y == 3 && b.id == 4, "instance not resolved to its master");
    CHECK(gluedGroupOf(g, g.entities[0]) == 10 && nextId(g) == 21, "glued group or next id wrong");
}

void testExpandArrayPatterns() {
    SceneGraph g;
    g.entities.push_back(boxEntity("Template", {5, 5, 5}, 0.1_r));
    g.entities[0].id = 1;
    ArrayObject a;
    a.id = 2;
    a.templateId = 1;
    a.position = {0, 1, 0};
    a.count[0] = 5;
    a.step = {0.3_r, 0, 0.1_r};
    const std::vector<Entity> line = expandArray(g, a);
    Real lineErr = 0;
    for (size_t i = 0; i < line.size(); ++i) lineErr = std::max(lineErr, length(line[i].position - (a.position + a.step * Real(i))));
    a.pattern = ArrayPattern::Grid;
    a.count[0] = 3, a.count[1] = 2, a.count[2] = 2;
    a.step = {0.2_r, 0.3_r, 0.4_r};
    const std::vector<Entity> grid = expandArray(g, a);
    const Vector3 last = grid.empty() ? Vector3(0.0_r) : grid.back().position - a.position; // (2, 1, 1) * spacing
    a.pattern = ArrayPattern::Circle;
    a.count[0] = 8;
    a.radius = 1.5_r;
    const std::vector<Entity> circle = expandArray(g, a);
    Real radiusErr = 0;
    for (const Entity& c : circle) radiusErr = std::max(radiusErr, std::fabs(length(c.position - a.position) - a.radius));
    a.pattern = ArrayPattern::Line;
    a.count[0] = 6;
    a.jitter = 0.05_r;
    const std::vector<Entity> j1 = expandArray(g, a), j2 = expandArray(g, a);
    a.seed = 99;
    const std::vector<Entity> j3 = expandArray(g, a);
    bool same = j1.size() == j2.size(), differs = false;
    for (size_t i = 0; i < j1.size() && same; ++i) same = length(j1[i].position - j2[i].position) == 0;
    for (size_t i = 0; i < j1.size() && i < j3.size(); ++i) differs = differs || length(j1[i].position - j3[i].position) > 1e-4_r;
    std::printf("  line: %zu copies, worst spacing error %.1e m; grid: %zu copies, last at (%.2f %.2f %.2f); circle: %zu, "
                "worst radius error %.1e m; jitter same seed %s, other seed %s\n", line.size(), lineErr, grid.size(), last.x, last.y,
                last.z, circle.size(), radiusErr, same ? "same" : "DIFFERENT", differs ? "differs" : "SAME");
    CHECK(line.size() == 5 && lineErr < 1e-6_r, "line copies misplaced");
    CHECK(grid.size() == 12 && length(last - Vector3(0.4_r, 0.3_r, 0.4_r)) < 1e-6_r, "grid copies misplaced");
    CHECK(circle.size() == 8 && radiusErr < 1e-6_r, "circle copies off the radius");
    CHECK(same && differs, "the jitter must repeat with the seed and change with another");
    CHECK(isArrayCopyId(line[3].id) && arrayOfCopyId(line[3].id) == 2 && line[3].id == arrayCopyId(2, 3), "copy ids not stable");
}
