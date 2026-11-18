#pragma once
// A whole arm's way among what its eyes see, planned in its joints: the arm drawn as spheres on its links (the hand,
// its fingers and what it holds among them), free where every sphere's centre stands farther from what its eyes saw
// (VoxelMap's distance field) than its radius and a margin; a way between two sets of coordinates found by RRT-Connect
// (Kuffner & LaValle 2000, "RRT-Connect: An efficient approach to single-query path planning": two trees, from the
// start and from the goals, each growing toward a random point and the other trying to reach what it grew), then
// shortened by cutting corners where a straight stretch in the joints is free (Geraerts & Overmars 2007). A way in the
// joints is a pose of the whole arm at every point - where the hand is and how it is turned, the elbow and the wrist
// with it - so a hand turned to pass a slot, under a beam or over a wall, comes out of it as it does in MoveIt with OMPL
// and FCL (Sucan et al. 2012; Pan et al. 2012) or cuRobo (Sundaralingam et al. 2023), whose arms are spheres too.
// Deterministic: the same model, map, start and seed, the same way to the bit.
#include "math/Quaternion.h"
#include "math/Vector3.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ke {

class ArmSelfCollisionIndex;

struct ArmSphere {
    Vector3 centre{0.0_r}; // in its link's frame, m
    Real radius = 0;
};

// One hinge of the chain: its parent link (-1: the fixed base), frame b in the parent's frame and frame a in its own
// link's frame (origins and turns); the coordinate turns frame a about frame b's x, QA = QB Rx(q) - 0 where the frames
// coincide (GenericJoint's frameCoordinates) - the origins staying together.
struct ArmJoint {
    int parent = -1;
    Vector3 originB{0.0_r}, originA{0.0_r};
    Quaternion turnB, turnA;
    Real lower = -kPi, upper = kPi;  // rad
    std::vector<ArmSphere> spheres; // its link's, and what hangs on it
};

struct ArmPose {
    Vector3 position{0.0_r};
    Quaternion turn;
};

// A separately identified collision body, attached to a moving link or the fixed base (-1).
// Keeping fingers and payload separate avoids inheriting a whole wrist's contact exceptions.
struct ArmCollisionBody {
    int link = -1;
    std::vector<ArmSphere> spheres;
};

struct ArmModel {
    ArmPose base;                 // the frame the first joint's b is given in (the fixed base's link)
    std::vector<ArmJoint> joints; // base to tip
    ArmPose effector;             // the effector's frame in the last link's (a hand welded on the wrist's flange)
    std::vector<ArmCollisionBody> collisionBodies;
    // The fixed base's cover in the base frame, as coarse as the links': what a camera's self mask takes as the arm's own
    // (armCaptureMask). It never moves, so no way is checked with it against the world.
    std::vector<ArmSphere> fixedSpheres;
    // Explicit body-index pairs only: neither adjacency nor overlap implies permission.
    std::vector<std::pair<size_t, size_t>> allowedCollisionPairs;
    std::vector<uint32_t> collisionEntities; // the scene entity of each collision body, when the model was built from one
    // Body pairs that come within a self margin in most of the arm's coordinates - its built-in neighbours, found by
    // sampling (armNearPairs) as the MoveIt Setup Assistant samples its default collision matrix: they must not overlap,
    // but no margin is asked of them. Bodies on one link never move apart and are such neighbours by construction.
    std::vector<std::pair<size_t, size_t>> nearPairs;
};

// Minimum signed sphere-pair separation for explicit collision bodies. Empty model: +infinity.
// Invalid topology, pose, sphere, or exception fails closed (-infinity). No environment query.
Real armSelfClearance(const ArmModel& model, const std::vector<Real>& q, std::pair<size_t, size_t>* nearest = nullptr);

// The pairs of checked collision bodies closer than `margin` in more than half of `samples` random coordinates within
// the joints' limits (ke::Random, `seed`): ArmModel::nearPairs for that margin.
std::vector<std::pair<size_t, size_t>> armNearPairs(const ArmModel& model, Real margin, int samples = 200, uint32_t seed = 7);

// Spheres over points of a link's surface (in its frame): the points sorted into cubic cells `step` on a side, each
// cell a sphere about the middle of its points reaching the farthest - hugging the surface within half a cell's
// diagonal, where one sphere a slice across a link swelled to its joint's housing. A shell: anything that comes inside
// crossed it first, and a stretch is checked finer than the spheres are wide.
std::vector<ArmSphere> armSpheres(const std::vector<Vector3>& points, Real step);

// The links' poses at coordinates q (one a joint), base to tip - forward kinematics in closed form.
void armPoses(const ArmModel& m, const std::vector<Real>& q, std::vector<ArmPose>& links);
ArmPose effectorPose(const ArmModel& m, const std::vector<Real>& q);

// Inverse kinematics on the model for a pose: q moved from where it is toward the effector at `target`, within the
// joints' limits (ArmIk.h's solver, task Pose; defined in ArmIk.cpp). True when within 0.5 mm and 0.01 rad.
bool armReach(const ArmModel& m, const ArmPose& target, std::vector<Real>& q, int iterations = 150);

struct ArmPlanOptions {
    Real margin = 0.01_r;     // kept from everything, m
    Real selfMargin = 0;      // kept between the arm's own bodies but its near pairs (ArmModel::nearPairs), m
    // The index of the arm's own collisions a planner of the same model and self margin made (ArmPlanner::selfIndex):
    // taken as it is, not built again - a segment guard makes a planner every new map, inside a millisecond's budget.
    std::shared_ptr<const ArmSelfCollisionIndex> selfIndex;
    Real step = 0.3_r;        // a tree's longest growth, rad (the largest joint's turn)
    Real resolution = 0.01_r; // a stretch checked so that no sphere moves more than this between two checks, m
    int iterations = 6000;    // random points tried
    int shortcuts = 800;      // corners tried to cut (a stretch's check is cheap; RRT's ways wander metres)
    int optimize = 40;        // rounds of the way's optimisation (0: none)
    Real wanted = 0.05_r;     // the clearance the optimisation pushes the arm out to, m
    uint32_t seed = 1;
};

class ArmPlanner {
public:
    // clearance(p): the distance from p to the nearest thing seen, m (VoxelMap::clearance).
    // The field must remain immutable during each call. Optional gradient returns the
    // same distance and its world-space derivative, enabling analytic joint derivatives.
    ArmPlanner(const ArmModel& model, std::function<Real(const Vector3&)> clearance, const ArmPlanOptions& options = ArmPlanOptions(),
               std::function<Real(const Vector3&, Vector3&)> gradient = {});
    std::shared_ptr<const ArmSelfCollisionIndex> selfIndex() const { return selfCollision_; } // for ArmPlanOptions::selfIndex
    // The least of (distance - radius) over every sphere at q, m; and the sphere it is (link, index) when asked.
    Real clearance(const std::vector<Real>& q, int* link = nullptr, ArmSphere* where = nullptr) const; // where: that sphere in the world
    // Within the margin of everything; near the start (0.3 rad of it) a start closer than the margin is let out
    // as long as the arm comes no closer than it stood (90 % of its clearance; touching, 2 mm deeper at most) - the
    // arm stands where it stands, and a check of the way's rest must judge it by the same rule as its plan, or every
    // way planned out of a tight spot is "blocked" at once and the arm re-plans round and round.
    // Explicit own-body geometry must also have positive separation, with no start-escape allowance.
    // An own-body refusal reports pair separation; environment checks stop at the first blocking sphere.
    // Optional environment witness: that sphere's clearance on refusal,
    // the global minimum only when free. A refusal witness is NOT the global minimum; when the
    // callback returns a lower bound, the witness is a lower bound for that checked sphere only.
    // Invalid coordinates/nonfinite samples fail closed (witness = -infinity).
    // room: how far any point of the arm may move from q before it can break a margin (0 when not free, or under the
    // near-start allowance).
    bool free(const std::vector<Real>& q, Real* witness = nullptr, Real* room = nullptr) const;
    void setStart(const std::vector<Real>& start) const; // the start that rule is about (plan sets it)
    bool freeStretch(const std::vector<Real>& a, const std::vector<Real>& b) const; // checked every `resolution`
    // The way from start to the nearest of the goals reached first: coordinates base to tip at each corner, start and
    // goal included; false when the trees do not meet within the iterations (or the start or every goal is not free).
    // A collision-checked direct edge takes precedence (nearest in maximum joint displacement),
    // with grown=0. It is not expanded by the optional clearance optimizer.
    bool plan(const std::vector<Real>& start, const std::vector<std::vector<Real>>& goals, std::vector<std::vector<Real>>& way,
              int* grown = nullptr) const;
    void shorten(std::vector<std::vector<Real>>& way) const;
    // The way as the least of an action - its smoothness, the sum of its steps squared, and the arm's nearness, the sum
    // over its spheres of (wanted - clearance)^2 where they come closer than `wanted` - by steps down the gradient
    // (CHOMP: Ratliff, Zucker, Bagnell & Srinivasa 2009, "CHOMP: Gradient optimization techniques for efficient motion
    // planning"): RRT's way, wandering and grazing what it passes, resampled to ~120 points and pulled straight and out to
    // a few centimetres of room, so a voxel flickering on a beam's edge no longer closes it. Kept only if free all along.
    void optimize(std::vector<std::vector<Real>>& way) const;
    Real nearness(const std::vector<Real>& q, std::vector<Real>* gradient = nullptr) const; // arm cost and optional joint derivative
    Real reach() const { return reach_; }
    struct Timing { double shortenMs = 0, optimizeMs = 0; }; // the last plan's: its way shortened, then optimised (an instrument)
    const Timing& timing() const { return timing_; }

private:
    ArmModel model_;
    std::shared_ptr<const ArmSelfCollisionIndex> selfCollision_, selfOverlap_; // with the self margin; overlap only
    std::function<Real(const Vector3&)> clearance_;
    std::function<Real(const Vector3&, Vector3&)> gradient_;
    ArmPlanOptions options_;
    Real reach_ = 1; // the farthest sphere's edge from the first joint, m: a turn of 1 rad moves no sphere farther
    std::vector<Real> lever_; // each joint's: how far a turn of 1 rad of it alone moves any point of the arm, at most, m
    void levers(const std::vector<ArmPose>& links);
    mutable std::vector<Real> start_;    // the way's start (plan), and how close it stood to anything
    mutable Real startClearance_ = kInf, startSelf_ = kInf;
    mutable Timing timing_;
};

} // namespace ke
