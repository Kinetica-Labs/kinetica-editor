#pragma once
// A joint of a robot as data: six freedoms between two links, each locked, limited or free, and what
// drives it (USD Physics' D6, PhysX's D6, Unity's ConfigurableJoint). Each link carries a frame - an
// origin and a turn in the link's own frame (the world's for no link) - and the six freedoms are
// frame a's motion relative to frame b: travel along b's x, y, z and turns about them. A hinge is the
// joint with only the turn about x free, a slider with only the travel along x, a ball with the three
// turns, a weld with none. The scene keeps joints in this form (SceneGraph.h, JointObject); the
// physics engine builds the nearest joint it has (src/physics), and the robot's own code drives it.
//
// Coordinates: travel d_k = (pA - pB) . e_k with e_k b's axes in the world; the turn of frame a
// relative to frame b, q = QB^-1 QA, split into a twist about x, 2 atan2(q_x, q_w), and the swing
// that remains, whose y and z angles are 2 atan2(s_y, s_w) and 2 atan2(s_z, s_w) (the swing-twist
// split of PhysX's D6).
//
// A drive: a spring-damper k (x_t - x) + c (v_t - v) up to maxForce - a large stiffness is a servo,
// neither stiffness nor damping a velocity motor. An electric DC motor instead: the Lorentz force on
// the winding gives a torque K I, its turning induces the back EMF K omega, so I = (U - K omega) / R
// and the torque (K / R)(U - K omega) - with the current and the power to read back.
#include "math/Math.h"

#include <limits>

namespace ke {

// The named joints: what a set of freedoms looks like (Generic: any other mix).
enum class JointType { Ball, Hinge, Slider, Fixed, Generic };

enum class JointMotion { Locked, Limited, Free };

// The degrees of freedom by index: travel along x, y, z, then the turns about x, y, z of frame b.
enum JointDof { DofTx = 0, DofTy, DofTz, DofRx, DofRy, DofRz };

struct JointDrive {
    bool enabled = false;
    Real stiffness = 0;        // N/m, N m/rad
    Real damping = 0;          // N s/m, N m s/rad
    Real target = 0;           // m, rad
    Real targetVelocity = 0;   // m/s, rad/s
    Real maxForce = std::numeric_limits<Real>::infinity(); // N, N m
    // An electric DC motor instead (when constant > 0): voltage U [V], winding resistance R [Ohm] and
    // the motor constant K [N m/A = V s/rad] - the same in SI for the torque and the back EMF.
    Real voltage = 0, resistance = 1, constant = 0;
};

// The six coordinates of frame a (origin oA, turn QA) relative to frame b (oB, QB) in the world:
// travel along b's x, y, z, then the twist about b's x and the swing's y and z angles (above).
void frameCoordinates(const Vector3& oA, const Quaternion& QA, const Vector3& oB, const Quaternion& QB, Real out[6]);

} // namespace ke
