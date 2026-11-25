// The collider a body is built with (sim/RobotWorld.cpp colliderShapes) against exact answers: a mesh's own collider
// thinned to its extremes along 120 directions; scene_collider's numbers per kind, each kind refusing another's; a mesh
// collider keeping its hollows - a ball rests on a channel's floor, standing or moving (convex parts), on the top of
// its hull; a solid mesh cut into convex parts (spatial/ConvexParts.h) holding its volume and leaving its corners free.
// The rest of the running world: SimTests.cpp.
#include "TestBodies.h"
#include "TestRunner.h"

#include "scene/SceneOps.h"
#include "sim/Simulation.h"
#include "spatial/ConvexParts.h"
#include "spatial/HullPoints.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace ke;

// A mesh's own collider (auto) - the UR5e base's 2427 points: the engine and the editor's collider guide get its
// extremes along 120 directions (HullPoints.h), not every point; the hull of all of them took the window 24 s to
// draw, and Box3D builds no more than 128 vertices anyway. The thinned hull reaches the mesh's box within the
// directions' spacing: 120 over the sphere lie sqrt(4 pi / 120) = 0.32 rad apart, so along any axis the hull falls
// short by R (1 - cos 0.16) = 1.3 % of the mesh's size at most.
void testSimMeshColliderThinned() {
    SceneGraph g = ur5e();
    Entity base = *std::find_if(g.entities.begin(), g.entities.end(), [](const Entity& e) { return e.id == 3; });
    base.collider.kind = ColliderKind::Auto;
    const TriMesh mesh = entityLocalMesh(base, g.baseDirectory);
    const std::vector<ShapeDesc> shapes = colliderShapes(base, mesh);
    AABB box(Vector3(1e9_r), Vector3(-1e9_r));
    for (const Vector3& p : shapes.front().points) box.expand(p);
    const Real gap = length(box.extent() - mesh.bounds().extent());
    std::printf("  the base's mesh of %zu points: a collider of %zu points, its box off the mesh's by %.2e m\n", mesh.positions.size(),
                shapes.front().points.size(), double(gap));
    CHECK(shapes.size() == 1 && shapes.front().points.size() <= size_t(kHullDirections) && gap < 0.013_r * maxComp(mesh.bounds().extent()),
          "the mesh's collider has %zu points, its box off the mesh's by %f", shapes.front().points.size(), double(gap));
}

// scene_collider holds the collider as numbers: a 0.2 x 0.4 x 0.6 m box's fitted collider becomes a sphere round its
// largest edge (radius 0.3 m), a capsule as wide as the larger of x and z (0.6 m) and as tall as y (0.4 m: no straight
// part left, half height 0), a box with its offset and turn kept; each kind refuses the other kinds' numbers, naming its
// own, and the engine's shape (colliderShapes) is exactly the numbers.
void testSceneColliderNumbers() {
    SceneGraph g;
    Entity box;
    box.id = 1, box.name = "Брус", box.shape = ShapeKind::Box, box.size = Vector3(0.2_r, 0.4_r, 0.6_r);
    box.collider.enabled = true, box.collider.kind = ColliderKind::Box; // fitted to the geometry
    g.entities.push_back(box);
    std::string error;
    Value answer;
    const auto run = [&](Value a) { a.set("id", 1); answer = Value::map(); return findSceneOp("scene_collider")->edit(g, a, answer, error); };
    const auto shape = [&] { return colliderShapes(g.entities[0], entityLocalMesh(g.entities[0])).front(); };
    Value sphere = Value::map();
    sphere.set("kind", "sphere");
    const bool round = run(sphere) && shape().kind == ShapeDesc::Kind::Sphere && std::fabs(shape().radius - 0.3_r) < 1e-6_r &&
                       !g.entities[0].collider.fitToGeometry;
    Value capsule = Value::map();
    capsule.set("kind", "capsule");
    const bool capsuled = run(capsule) && shape().kind == ShapeDesc::Kind::Capsule && std::fabs(shape().radius - 0.3_r) < 1e-6_r;
    Value taller = Value::map();
    taller.set("height", 1.0), taller.set("diameter", 0.1);
    const bool tall = run(taller) && std::fabs(shape().radius - 0.05_r) < 1e-6_r && std::fabs(shape().halfHeight - 0.45_r) < 1e-6_r &&
                      answer["height"].number() == 1.0;
    Value wrong = Value::map(), offset = Value::list(), size = Value::list();
    for (Real v : {0.0_r, 0.1_r, 0.0_r}) offset.push(v);
    for (Real v : {0.3_r, 0.2_r, 0.1_r}) size.push(v);
    wrong.set("size", size);
    const bool sizeRefused = !run(wrong) && error.find("diameter and height") != std::string::npos;
    Value boxed = Value::map();
    boxed.set("kind", "box"), boxed.set("size", size), boxed.set("offset", offset);
    const bool boxy = run(boxed) && shape().kind == ShapeDesc::Kind::Box && length(shape().halfExtents - Vector3(0.15_r, 0.1_r, 0.05_r)) < 1e-6_r &&
                      length(shape().position - Vector3(0, 0.1_r, 0)) < 1e-6_r;
    Value diameter = Value::map();
    diameter.set("diameter", 0.2);
    const bool diameterRefused = !run(diameter) && error.find("size [x, y, z]") != std::string::npos;
    std::printf("  a fitted 0.2 x 0.4 x 0.6 m collider: a sphere %s, a capsule %s, 0.1 x 1 m %s; size refused on a capsule %s, "
                "a box with size and offset %s, diameter refused on a box %s (%s)\n", round ? "r 0.3" : "no", capsuled ? "r 0.3" : "no",
                tall ? "r 0.05 h/2 0.45" : "no", sizeRefused ? "yes" : "no", boxy ? "yes" : "no", diameterRefused ? "yes" : "no", error.c_str());
    CHECK(round && capsuled && tall, "a kind's own numbers from the fitted box, then given");
    CHECK(sizeRefused && boxy && diameterRefused, "each kind takes its own numbers and names them when given another's");
}

// A collider of the geometry's own triangles keeps its hollows: a ball dropped into a U-shaped channel - one model of
// three slabs, a floor and two walls - comes to rest on the channel's floor (0.25 m up); the convex hull of the same
// model is a solid block, and the ball rests on its top (0.8 m up).
void testSimMeshCollider() {
    const std::string obj = (std::filesystem::temp_directory_path() / "ke-channel-test.obj").string();
    {
        std::ofstream out(obj);
        int base = 1;
        const auto slab = [&out, &base](Vector3 lo, Vector3 hi) { // a box, its faces counter-clockwise from outside
            for (int k = 0; k < 8; ++k)
                out << "v " << (k & 1 ? hi.x : lo.x) << " " << (k & 2 ? hi.y : lo.y) << " " << (k & 4 ? hi.z : lo.z) << "\n";
            const int f[12][3] = {{0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4}, {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
            for (const auto& t : f) out << "f " << base + t[0] << " " << base + t[1] << " " << base + t[2] << "\n";
            base += 8;
        };
        slab({-1, 0, -0.5_r}, {1, 0.1_r, 0.5_r});
        slab({-1, 0.1_r, -0.5_r}, {-0.9_r, 0.65_r, 0.5_r});
        slab({0.9_r, 0.1_r, -0.5_r}, {1, 0.65_r, 0.5_r});
    }
    Real channelMass = 0;
    const auto rest = [&obj, &channelMass](ColliderKind kind, bool moving) {
        SceneGraph g;
        g.world.physics.sleeping = false;
        Entity channel;
        channel.id = 1, channel.name = "Желоб", channel.shape = ShapeKind::Mesh, channel.meshFile = obj;
        channel.size = {2, 0.65_r, 1}, channel.position = {0, 0.325_r, 0}; // the model fitted to its own extent: as it is
        channel.collider.enabled = true, channel.collider.kind = kind;
        channel.rigid.enabled = moving;
        g.entities.push_back(channel);
        Entity ball = solid(2, "Шар", Vector3(0.3_r), {0, 1.2_r, 0}, false);
        ball.shape = ShapeKind::Sphere;
        g.entities.push_back(ball);
        if (moving) g.entities.push_back(solid(3, "Пол", {6, 0.2_r, 6}, {0, -0.1_r, 0}, true));
        Simulation s;
        s.load(g);
        for (int k = 0; k < 150; ++k) s.stepFrame();
        if (moving) channelMass = s.physics()->state(s.robot()->bodyOf(1)).mass;
        return s.physics()->state(s.robot()->bodyOf(2)).position.y;
    };
    const Real inside = rest(ColliderKind::Mesh, false), onTop = rest(ColliderKind::ConvexHull, false), moving = rest(ColliderKind::Mesh, true);
    const Real parts = rest(ColliderKind::ConvexParts, false); // asked for on a body that stands: convex parts too
    // scene_collider kind mesh on the moving channel: cut at once - 3 parts, or one hull when hulls is 1; the numbers kept
    // in the file's collider line and read back.
    SceneGraph held;
    Entity channel;
    channel.id = 1, channel.name = "Желоб", channel.shape = ShapeKind::Mesh, channel.meshFile = obj, channel.size = {2, 0.65_r, 1};
    channel.rigid.enabled = true;
    held.entities.push_back(channel);
    const auto collider = [&held](int hulls) {
        Value a = Value::map(), answer = Value::map();
        a.set("id", 1), a.set("kind", "mesh"), a.set("hulls", hulls), a.set("concavity", 0.03);
        std::string error;
        return findSceneOp("scene_collider")->edit(held, a, answer, error) ? answer["hulls_made"].integer() : -1;
    };
    const long long three = collider(8), one = collider(1);
    Value standing = Value::map(), standingAnswer = Value::map();
    std::string error;
    held.entities[0].rigid.enabled = false; // convex_parts on a body that stands: cut all the same
    standing.set("id", 1), standing.set("kind", "convex_parts"), standing.set("hulls", 8);
    const bool partsStanding = findSceneOp("scene_collider")->edit(held, standing, standingAnswer, error) && standingAnswer["hulls_made"].integer() == 3 &&
                               held.entities[0].collider.kind == ColliderKind::ConvexParts;
    SceneGraph partsBack;
    const bool partsKept = partsBack.load(held.save(), error) && partsBack.entities[0].collider.kind == ColliderKind::ConvexParts;
    held.entities[0].rigid.enabled = true, held.entities[0].collider.kind = ColliderKind::Mesh, held.entities[0].collider.hulls = 1;
    SceneGraph back;
    const bool kept = back.load(held.save(), error) && back.entities[0].collider.kind == ColliderKind::Mesh && back.entities[0].collider.hulls == 1 &&
                      std::fabs(back.entities[0].collider.concavity - 0.03_r) < 1e-6_r;
    std::filesystem::remove(obj);
    CHECK(three == 3 && one == 1 && kept, "scene_collider kind mesh: %lld parts with hulls 8, %lld with hulls 1, the numbers %s in the file",
          three, one, kept ? "kept" : "lost");
    CHECK(std::fabs(parts - 0.25_r) < 0.02_r && partsStanding && partsKept,
          "convex_parts on a standing channel: the ball rests %f m up (its floor), 3 parts cut, the kind read back from the file", double(parts));
    const Real solidMass = 500 * (2 * 0.1_r * 1 + 2 * 0.1_r * 0.55_r * 1); // the three slabs at 500 kg/m^3 (the hull would weigh 650)
    std::printf("  a ball in a U channel: a mesh collider %.3f m up (0.25, its floor), its hull %.3f m (0.8, the top); the channel moving, "
                "as convex parts: %.3f m, %.1f kg (the slabs %.1f kg)\n", double(inside), double(onTop), double(moving), double(channelMass), double(solidMass));
    CHECK(std::fabs(inside - 0.25_r) < 0.02_r, "a mesh collider: the ball rests %f m up, not on the channel's floor", double(inside));
    CHECK(std::fabs(onTop - 0.8_r) < 0.02_r, "the hull of the same model: the ball rests %f m up, not on its top", double(onTop));
    CHECK(std::fabs(moving - 0.25_r) < 0.02_r, "a moving channel's mesh collider (convex parts): the ball rests %f m up, not on its floor", double(moving));
    CHECK(std::fabs(channelMass / solidMass - 1) < 0.1_r, "the moving channel weighs %f kg, its slabs %f kg", double(channelMass), double(solidMass));
}

// A solid mesh cut into convex parts (spatial/ConvexParts.h): an L of two boxes becomes parts whose hulls hold its volume
// within 10 % and leave the inner corner free; a box stays one part, its own points; the same mesh gives the same parts.
void testConvexParts() {
    const auto join = [](TriMesh a, const TriMesh& b) {
        const uint32_t base = uint32_t(a.positions.size());
        a.positions.insert(a.positions.end(), b.positions.begin(), b.positions.end());
        for (auto t : b.triangles) a.triangles.push_back({t[0] + base, t[1] + base, t[2] + base});
        return a;
    };
    const auto slab = [](Vector3 lo, Vector3 hi) {
        TriMesh m = primitives::box((hi - lo) * 0.5_r);
        m.translate((hi + lo) * 0.5_r);
        return m;
    };
    const TriMesh ell = join(slab({0, 0, 0}, {1, 0.2_r, 0.5_r}), slab({0, 0.2_r, 0}, {0.2_r, 1, 0.5_r})); // volume 0.18 m^3
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<std::vector<Vector3>> parts = convexParts(ell);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    Real volume = 0;
    bool cornerFree = true; // the point (0.45, 0.45, 0.25) - in the L's hull, out of the L - is in no part's hull
    for (const std::vector<Vector3>& p : parts) {
        const ConvexHull h = convexHull(p);
        volume += h.volume;
        bool in = !h.normals.empty();
        for (size_t f = 0; f < h.normals.size(); ++f) in = in && dot(h.normals[f], Vector3(0.45_r, 0.45_r, 0.25_r)) <= h.offsets[f];
        cornerFree = cornerFree && !in;
    }
    const std::vector<std::vector<Vector3>> again = convexParts(ell), box = convexParts(slab({0, 0, 0}, {1, 2, 3}));
    std::printf("  an L of two boxes: %zu convex parts holding %.4f m^3 (the L 0.18, its hull 0.34), cut in %.1f ms; a box: %zu part\n",
                parts.size(), double(volume), ms, box.size());
    CHECK(parts.size() >= 2 && std::fabs(volume / 0.18_r - 1) < 0.1_r && cornerFree, "an L: %zu parts holding %f m^3, the inner corner %s",
          parts.size(), double(volume), cornerFree ? "free" : "filled");
    CHECK(box.size() == 1 && box[0].size() == primitives::box(Vector3(0.5_r)).positions.size(), "a box is one part of its own points, not %zu parts",
          box.size());
    bool same = again.size() == parts.size();
    for (size_t i = 0; same && i < parts.size(); ++i)
        same = again[i].size() == parts[i].size() &&
               std::equal(parts[i].begin(), parts[i].end(), again[i].begin(), [](const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; });
    CHECK(same, "the same mesh gives the same parts");
}
