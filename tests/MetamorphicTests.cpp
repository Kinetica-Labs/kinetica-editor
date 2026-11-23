// Metamorphic tests of the running world (src/sim): a tumbling fall onto the floor has no exact answer to check, but a
// known change of the scene must give a known change of the run - the whole scene moved along the floor, turned about y,
// mirrored, its objects made in the other order: each body ends where the same change takes it in the first run. A wrong
// frame, a hidden dependence on where the world is, on its handedness or on the order objects are made shows as a gap.
// (Metamorphic testing: Chen, Cheung, Yiu 1998 - the oracle problem of simulations.)
#include "TestRunner.h"

#include "sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace ke;

namespace {

struct Pose {
    Vector3 at, x, y; // where the body is, and its own x and y axes in the world
};

// Two boxes far apart, turned every way, dropped onto the floor: they tumble, bounce and come to rest.
SceneGraph tumbling() {
    SceneGraph g;
    const auto box = [&g](uint32_t id, const Vector3& size, const Vector3& at, const Vector3& degrees) {
        Entity e;
        e.id = id, e.name = "Брусок", e.size = size, e.position = at, e.rotationDeg = degrees;
        e.rigid.enabled = e.collider.enabled = true;
        g.entities.push_back(e);
    };
    box(1, {0.3_r, 0.1_r, 0.2_r}, {-0.6_r, 0.7_r, 0.2_r}, {20, 35, -15});
    box(2, {0.15_r, 0.25_r, 0.15_r}, {0.7_r, 0.9_r, -0.4_r}, {-30, 10, 40});
    return g;
}

// The bodies after `frames` frames, by id (1, 2): the engine's own poses - this is the checking side, it may read them.
std::vector<Pose> run(const SceneGraph& g, int frames) {
    Simulation s;
    s.load(g);
    for (int k = 0; k < frames; ++k) s.stepFrame();
    std::vector<Pose> out;
    for (uint32_t id : {1u, 2u}) {
        const BodyState b = s.physics()->state(s.robot()->bodyOf(id));
        out.push_back({b.position, b.rotation.rotate(Vector3(1, 0, 0)), b.rotation.rotate(Vector3(0, 1, 0))});
    }
    return out;
}

// The largest gap between the run of the changed scene and the first run taken by the same change: of a place, m; of an
// axis, the length of the difference of two unit vectors (≈ the angle between them, rad).
std::pair<double, double> gap(const std::vector<Pose>& got, const std::vector<Pose>& first, const std::function<Pose(const Pose&)>& change) {
    double place = 0, turn = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        const Pose want = change(first[i]);
        place = std::max(place, double(length(got[i].at - want.at)));
        turn = std::max({turn, double(length(got[i].x - want.x)), double(length(got[i].y - want.y))});
    }
    return {place, turn};
}

constexpr int kFrames = 150; // 2.5 s: fallen, bounced, at rest

// The control of the tumble's chaos: the first run with one body 10 µm higher and turned 1e-5 degrees about every axis -
// the contact's geometry itself disturbed (which corner touches first), not a shift along the endless floor. A gap of a
// change within this is chaos; a gap far above it is the change.
double controlGap() {
    const std::vector<Pose> first = run(tumbling(), kFrames);
    SceneGraph g = tumbling();
    g.entities[0].position.y += 1e-5_r;
    g.entities[0].rotationDeg = g.entities[0].rotationDeg + Vector3(1e-5_r);
    const auto [place, turn] = gap(run(g, kFrames), first, [](const Pose& p) { return p; });
    std::printf("  the control of chaos - one body 10 µm higher, turned 1e-5°: %.2g m, %.2g rad from the first run\n", place, turn);
    return std::max(place, turn);
}

} // namespace

// Moved along the floor by (0.7, 0, -0.4) m: every body ends 0.7 m along x and 0.4 m back along z, turned the same.
void testMetamorphicMoved() {
    const std::vector<Pose> first = run(tumbling(), kFrames);
    SceneGraph g = tumbling();
    const Vector3 by(0.7_r, 0, -0.4_r);
    for (Entity& e : g.entities) e.position = e.position + by;
    const auto [place, turn] = gap(run(g, kFrames), first, [&by](const Pose& p) { return Pose{p.at + by, p.x, p.y}; });
    std::printf("  the scene moved along the floor: the bodies end %.2g m and %.2g rad from where the move takes the first run's\n", place, turn);
    CHECK(place < 2e-3 && turn < 2e-3, "moved scene: a body %g m / %g rad off the moved first run", place, turn);
}

// Turned a quarter about y: (x, z) -> (z, -x); a body's turn Ry Rx Rz takes the quarter into its yaw.
void testMetamorphicTurned() {
    const std::vector<Pose> first = run(tumbling(), kFrames);
    SceneGraph g = tumbling();
    const auto quarter = [](const Vector3& v) { return Vector3(v.z, v.y, -v.x); }; // Ry(+90°)
    for (Entity& e : g.entities) e.position = quarter(e.position), e.rotationDeg.y += 90;
    const auto [place, turn] = gap(run(g, kFrames), first, [&quarter](const Pose& p) { return Pose{quarter(p.at), quarter(p.x), quarter(p.y)}; });
    std::printf("  the scene turned a quarter about y: %.2g m, %.2g rad from the turned first run\n", place, turn);
    CHECK(place < 2e-3 && turn < 2e-3, "turned scene: a body %g m / %g rad off the turned first run", place, turn);
}

// Turned half round about y: (x, z) -> (-x, -z) - a rotation like the quarter, so no change of hand: beside the mirror
// it says whether a gap is the mirror's own (the solver's handedness) or any big change's.
void testMetamorphicHalfTurned() {
    const std::vector<Pose> first = run(tumbling(), kFrames);
    SceneGraph g = tumbling();
    const auto half = [](const Vector3& v) { return Vector3(-v.x, v.y, -v.z); }; // Ry(180°)
    for (Entity& e : g.entities) e.position = half(e.position), e.rotationDeg.y += 180;
    const auto [place, turn] = gap(run(g, kFrames), first, [&half](const Pose& p) { return Pose{half(p.at), half(p.x), half(p.y)}; });
    std::printf("  the scene turned half round about y: %.2g m, %.2g rad from the turned first run\n", place, turn);
    CHECK(place < 2e-3 && turn < 2e-3, "half-turned scene: a body %g m / %g rad off the turned first run", place, turn);
}

// Mirrored x -> -x: a place's x changes its sign; a turn Ry(a) Rx(b) Rz(c) becomes Ry(-a) Rx(b) Rz(-c); a body's own axes
// are the mirrored ones, its x reversed besides (a mirror image of a right-handed frame, made right-handed again). The
// flight mirrors to the bit; the contacts do not: one bounce leaves about 5 mm - a hundred times the control of chaos
// (0.04 mm) and the half turn (0.04 mm), so it is the change of hand itself, in the contact solver (where in it - the
// order of its manifold's points, its friction directions - is not checked: docs/contracts/physics.md). Held under 1 cm,
// so that a worse asymmetry is seen.
void testMetamorphicMirrored() {
    SceneGraph g = tumbling();
    const auto mirror = [](const Vector3& v) { return Vector3(-v.x, v.y, v.z); };
    for (Entity& e : g.entities) e.position = mirror(e.position), e.rotationDeg = {e.rotationDeg.x, -e.rotationDeg.y, -e.rotationDeg.z};
    const auto mirrored = [&mirror](const Pose& p) { return Pose{mirror(p.at), mirror(p.x) * -1.0_r, mirror(p.y)}; };
    const auto [flyPlace, flyTurn] = gap(run(g, 10), run(tumbling(), 10), mirrored); // 1/6 s: in the air, no contact yet
    const auto [place, turn] = gap(run(g, kFrames), run(tumbling(), kFrames), mirrored);
    std::printf("  the scene mirrored in x: in flight %.2g m, %.2g rad; after the bounces %.2g m, %.2g rad\n", flyPlace, flyTurn, place, turn);
    const double control = controlGap();
    CHECK(flyPlace == 0 && flyTurn == 0, "mirrored flight: a body %g m / %g rad off the mirrored first run", flyPlace, flyTurn);
    CHECK(place < 1e-2 && turn < 1e-2, "mirrored scene: a body %g m / %g rad off the mirrored first run (control %g)", place, turn, control);
}

// Made in the other order: the same bodies, the same run - to the bit, as the constitution's determinism asks (bodies
// that never touch each other must not feel the order they were made in).
void testMetamorphicOrder() {
    const std::vector<Pose> first = run(tumbling(), kFrames);
    SceneGraph g = tumbling();
    std::reverse(g.entities.begin(), g.entities.end());
    const auto [place, turn] = gap(run(g, kFrames), first, [](const Pose& p) { return p; });
    std::printf("  the objects made in the other order: %.2g m, %.2g rad from the first run\n", place, turn);
    CHECK(place == 0 && turn == 0, "made in the other order: a body %g m / %g rad off the first run", place, turn);
}
