// The list of tests, in the order they run. KE_TEST=<substring> runs only the matching ones.
#include "TestRunner.h"
#include "Tests.h"

#include "core/Probe.h"

#include <new>

// The allocation census: the test program replaces the global operator new so that every heap
// allocation counts into Probe::allocations. The core itself never touches the allocator; only the
// program that wants the number counts.
void* operator new(std::size_t n) {
    ke::Probe::allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    ke::Probe::allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return operator new(n, t); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

int main() {
    run("math: vectors, matrices, quaternions, N x N solvers", testMath);
    run("math: mesh primitives are closed and have their volumes", testPrimitives);
    run("math: BVH queries against brute force", testBVH);
    run("probe: channels, counters, timers, debug drawing", testProbe);
    run("core: units as data (UCUM) and fields as data - their schema, their check, a key's suffix against its unit", testUnitsAndFields);
    run("runtime: bounded pipeline admission", testRuntimePolicyBounds);
    run("runtime: configuration is transactional", testRuntimePolicyTransactional);
    run("runtime: monotonic idle lease and explicit stop", testRuntimePolicyLease);
    run("electro: a battery at a constant current lasts Q / I and starts at OCV - R0 I", testBatteryConstantDischarge);
    run("electro: a battery's polarisation follows the RC exponential exactly", testBatteryPolarisation);
    run("electro: a battery's energy books balance, charging and discharging", testBatteryEnergyBooks);
    run("electro: a pack of cells scales in series and in parallel", testBatteryPack);
    run("devices: a quadrature encoder counts floor(theta 4N / 2 pi), its edges timed to the nanosecond", testEncoderQuadrature);
    run("devices: an IMU's rate, full scale, counts and filter from its datasheet", testImuSpec);
    run("devices: an IMU's noise and offsets against the statistics they are drawn from", testImuNoise);
    run("devices: a sonar's jitter against the normal it is drawn from", testSonarNoise);
    run("devices: a sensor's datasheet from its model's table row, kept in the scene file, set by the API", testSensorDatasheet);
    run("physics: a falling ball's speed is g t, its height on the parabola within the integrator's error", testPhysicsFreeFall);
    run("physics: a torque spins a box at tau t / I - computed from its volume, and given", testPhysicsTorque);
    run("physics: a pendulum's period is the compound pendulum's", testPhysicsPendulum);
    run("physics: a hinge's limit and motor act the way our coordinate counts", testPhysicsHingeConventions);
    run("physics: a ray meets the nearest body, or the one past an ignored body", testPhysicsRays);
    run("physics: a box at rest is carried by contacts summing to m g", testPhysicsWeightOnContacts);
    run("physics: a hanging bob's joint pulls it up with m g", testPhysicsJointReaction);
    run("physics: a body coasting on a hinge keeps its spin", testPhysicsHingeCoasts);
    run("physics: a welded cantilever sags less with stiffer joints and more substeps", testPhysicsCantileverSag);
    run("physics: rest - a tower of ten cubes stands", testPhysicsTower);
    run("physics: rest - a pyramid of 406 cubes stands", testPhysicsPyramid);
    run("physics: the bodies of one group never touch each other", testPhysicsGroups);
    run("physics: the mouse's hand holds a body at its spring's sag g / w^2 and pulls no more than it may", testPhysicsHand);
    run("physics: two worlds built alike stay equal to the bit", testPhysicsRepeatable);
    run("physics: the same world on 1, 2, 4 and the machine's threads - 300 tumbling boxes equal to the bit", testPhysicsThreadsDeterministic);
    run("world: a box of the scene falls and rests on the floor", testSimBoxRestsOnFloor);
    run("world: a servo reaches its target and droops by m g d / k under gravity", testSimServoHoldsAndDroops);
    run("world: a DC motor spins up to U / K with the time constant I R / K^2", testSimDcMotorSpinsUp);
    run("world: the channels - energy, height, speed, contacts, a joint's coordinate, target and effort", testSimChannels);
    run("world: the mouse's hand holds a body at its target, its weight carried, and lets it fall", testSimGrab);
    run("world: the probe's debug layers - contacts and centres of mass drawn when on, nothing when off", testSimDebugLayers);
    run("world: a stiff servo on a light link brings the hand to its IK goal (the damper implicit)", testSimStiffServoOnLightLink);
    run("world: the UR5e of examples/ holds its gripper on its IK goal, its links still", testSimUr5eHolds);
    run("world: the mouse tugs the UR5e by a finger - the arm follows; let go, it comes back along its servos' profile, no bounce", testSimTugUr5e);
    run("world: a mesh's own collider takes its extremes along 120 directions, not its every point", testSimMeshColliderThinned);
    run("world: assembly_goal_pose puts the UR5e's goal at a pose at once - the arm gets there and settles", testSimGoalPoseSettles);
    run("world: the sensors read the physics through their probes and datasheets - 1 g at rest, none falling, a weight, a wall, a turn past pi", testSimSensors);
    run("scene graph: a robot's file comes back to the bit", testSceneGraphRoundTrip);
    run("scene graph: the physics line - left out at the defaults, kept when set, refused out of range", testScenePhysicsLine);
    run("ablation: a role of other physics is refused with its line and word", testSceneRefusesOtherPhysics);
    run("scene graph: the file does not depend on the locale (decimal comma)", testSceneGraphLocale);
    run("scene graph: groups, instances and arrays round trip; a child of a turned group", testHierarchyInstancesRoundTrip);
    run("array: line, grid and circle copies where the pattern puts them, seeded jitter", testExpandArrayPatterns);
    run("actions: a group keeps its members' world poses, ungroup gives them back", testActionsGroupKeepsPoses);
    run("actions: delete takes the subtree, its joints and sensors; a copy carries its sensors", testActionsDeleteAndDuplicate);
    run("actions: the gizmo's move, turn and scale; an array of a shape", testActionsTransforms);
    run("actions: the core's operations by name - questions, edits, answers, a batch", testSceneOpsRegistry);
    run("scene: a target camera looks at its object wherever either goes; set free it keeps looking there", testTargetCamera);
    run("scene: the Sequencer's clip - poses between keys, held, looped; keys, the file, the playhead", testAnimationClip);
    run("assembly: six-freedom joints in the scene file come back to the bit", testSceneJointsRoundTrip);
    run("assembly: a shape snapped to each face of a turned box touches it exactly", testSnapToFace);
    run("assembly: the core edits put, get, set, remove and their inverses", testSceneEditsCore);
    run("assembly: posing a joint turns the chain that hangs on it, within its limits", testPoseJoint);
    run("assembly: the world through the edits core, a joint's label follows its kind", testWorldAndKindEdits);
    run("assembly: a stereo camera with its housing looks where it is told, its eyes before its front, welded to what moves", testStereoCameraHousing);
    run("assembly: the scene in words - the tree of joints, axes in the carrier's frame, drivers, sensors", testDescribeScene);
    run("assembly: inverse kinematics - a planar 2R by the law of cosines, a 3R's point and turn, a point out of reach", testInverseKinematics);
    run("assembly: an IK goal moved by the operations drives the servos", testIkGoal);
    run("assembly: can the arm reach - asked without moving it; two fingers driven in one command", testCanReachAndDrives);
    run("assembly: a way re-planned on the walk starts where the goal is, not at the last path's end", testGoalPathStart);
    run("assembly: a turn on the spot walked as length takes its time and is kept in the file", testGoalPathTurnLength);
    run("assembly: a camera's flight - a spline through the points at constant speed, the turns by SQUAD", testSmoothPath);
    run("assembly: a joint's drive by its fields - a servo's target changed keeps its stiffness, a line with a key twice refused", testAssemblyDrive);
    std::printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", g_failures);
    return g_failures;
}
