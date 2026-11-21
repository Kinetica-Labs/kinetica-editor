#pragma once
// Inverse kinematics on a scene's joints (Kinematics.cpp): the coordinates of a chain - the joints from the
// first body that hangs on nothing (or on a fixed one) down to an end effector - that put the effector's frame
// at a target point with a target turn (a quaternion, as a basis of three axes R in SO(3)). A numerical solve,
// damped least squares on the exact geometric Jacobian, the error the travel to the point and the turn's logarithm log(R_target R_effector^T); the
// joints' limits held by forward kinematics (poseJoint). A target out of reach gives the nearest pose and
// says so. Deterministic: the same scene and target, the same coordinates to the bit.
#include "scene/SceneGraph.h"

#include <string>
#include <vector>

namespace ke {

struct IkLink {
    uint32_t joint = 0;
    int dof = 0;        // its main freedom (JointDof): a hinge's turn, a slider's travel
    Real coordinate = 0;
};

struct IkResult {
    std::vector<IkLink> links;   // base to tip, with the coordinates found
    Real positionError = 0;      // m, left at the end
    Real angleError = 0;         // rad, left at the end
    int iterations = 0;
    bool reached = false;        // within the tolerances
    // The method at work: the residual |e| the least squares minimise - the travel and the turn weighted to
    // metres - before each round, the first the error it started from; and how far the final pose is from a
    // singularity, Yoshikawa's manipulability w = sqrt(det(J^T J)) (sqrt(det(J J^T)) for a redundant arm) of
    // the task's Jacobian: the volume the joints' unit speeds sweep - zero where the arm loses a direction.
    std::vector<Real> residual;
    Real manipulability = 0;
};

// The joints from the effector up to the body the chain hangs from (weld joints skipped: no freedom).
std::vector<IkLink> ikChain(const SceneGraph& g, uint32_t effector);
// The chain's coordinates for the effector entity's frame at `position` turned by `rotation` (null: the point
// only - an arm of fewer than six freedoms cannot be asked both); `g` posed so.
IkResult solveIk(SceneGraph& g, uint32_t effector, const Vector3& position, const Quaternion* rotation, Real positionTolerance = 1e-4_r,
                 Real angleTolerance = 1e-3_r, int iterations = 200);

// The coordinates as the targets of the chain's servos, solved from where they are sent (pose: from the links as
// they stand, posed there too); `undriven` gets the chain's joints with no servo. And the same for a goal entity (Entity::goalOf): its effector to its pose -
// the point and the turn for a chain of six freedoms or more, the point only for fewer.
IkResult reachTargets(SceneGraph& g, uint32_t effector, const Vector3& position, const Quaternion* rotation, bool pose,
                      std::vector<uint32_t>* undriven = nullptr);
IkResult reachGoal(SceneGraph& g, uint32_t goal, std::vector<uint32_t>* undriven = nullptr);
// A goal for an effector: a see-through cube at its pose, a third larger than the link, named after it - its id
// (0: not an effector).
// Where a goal's path (Entity::path) has taken it `elapsed` seconds after it began: arc length pathSpeed * elapsed
// along the polyline from the goal's own place through the waypoints (looped: and back), held at the end. `done`: past it.
Vector3 pathPoint(const Entity& goal, Real elapsed, bool* done = nullptr);
// The same with its turn: from the goal's own toward each waypoint's (Entity::pathTurns) by slerp along each stretch
// (smooth: SQUAD along the spline); and the length a round of the path walks.
void pathPose(const Entity& goal, Real elapsed, Vector3& position, Quaternion& turn, bool* done = nullptr);
Real pathLength(const Entity& goal);
uint32_t addGoal(SceneGraph& g, uint32_t effector);

} // namespace ke
