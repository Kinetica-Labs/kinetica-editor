#pragma once
// The boundary to the physics engine: everything a robot asks of rigid-body physics, in the core's own
// types - bodies made of convex shapes with a given or computed mass, the joints every engine has, a
// velocity motor, forces, rays and contacts. The rest of the core and the editor see only this; one
// implementation of it, a plugin (plugins/physics/box3d, the only place that includes the engine): another
// engine is another plugin, and the tests (tests/PhysicsTests.cpp) say whether it holds (docs/contracts/physics.md). What a robot does beyond it - a servo's spring, a DC motor's torque, an encoder's count - is
// the robot's own code (src/sim), the same over any engine.
//
// Bodies and joints are handles: small integers, stable while the object lives, never reused within a
// world. Poses are the body's frame (its origin, not its centre of mass); forces and torques apply
// over the next step.
#include "math/Math.h"
#include "physics/PhysicsSettings.h"
#include "scene/JointModel.h"

#include <memory>
#include <string>
#include <vector>

namespace ke {

// A convex part of a body, in the body's frame. Capsule: two half-spheres of `radius` at +-halfHeight
// along the shape's y and the cylinder between; Hull: the convex hull of the points (the engine may
// thin them to its own limit of vertices).
struct ShapeDesc {
    enum class Kind { Box, Sphere, Capsule, Hull };
    Kind kind = Kind::Box;
    Vector3 halfExtents{0.5_r};
    Real radius = 0.5_r, halfHeight = 0;
    std::vector<Vector3> points;
    Vector3 position{0.0_r};
    Quaternion rotation;
    Real friction = 0.5_r, restitution = 0;
};

// A body: static (the floor, a table), kinematic (moved by hand) or dynamic. Its mass is the shapes'
// volume times `density`, or - an imported link's - given: the mass, the centre of mass in the body's
// frame and the inertia tensor about it in the body's axes.
struct BodyDesc {
    enum class Type { Static, Kinematic, Dynamic };
    Type type = Type::Dynamic;
    Vector3 position{0.0_r};
    Quaternion rotation;
    Vector3 velocity{0.0_r}, angularVelocity{0.0_r}; // of the centre of mass; world
    Real density = 1000;                              // kg/m^3
    bool massGiven = false;
    Real mass = 0;
    Vector3 centre{0.0_r};
    Matrix3x3 inertia = Matrix3x3::identity();
    Real linearDamping = 0, angularDamping = 0;       // 1/s
    int group = 0;                                    // > 0: the bodies of one group never touch each other (one robot's links)
};

struct BodyState {
    Vector3 position{0.0_r};      // the body's frame
    Quaternion rotation;
    Vector3 centre{0.0_r};        // the centre of mass, world
    Vector3 velocity{0.0_r}, angularVelocity{0.0_r}; // of the centre of mass; world
    Real mass = 0;                // 0: static or kinematic
    Matrix3x3 inertia = Matrix3x3::zero(); // about the centre of mass, world axes [kg m^2]; zero: static or kinematic
    bool awake = true;
};

// A joint between body a and body b (-1: the world), by its frames on them - their origins and turns
// in each body's frame (the world's for -1), as JointModel.h has them: the coordinates are frame a
// relative to frame b. Hinge: a turn about the frames' x; Slider: a travel along x; Ball: three turns;
// Fixed: none. Limits: Hinge [rad], Slider [m]; Ball: a cone of `upper` radians about x. A velocity
// motor the engine holds on a Hinge or a Slider: `motorSpeed` with at most `maxMotorForce` (N m or N).
struct JointDesc {
    JointType type = JointType::Hinge;
    int a = -1, b = -1;
    Vector3 anchorA{0.0_r}, anchorB{0.0_r};
    Quaternion frameA, frameB;
    bool limited = false;
    Real lower = 0, upper = 0;
    bool motor = false;
    Real motorSpeed = 0, maxMotorForce = 0;
    bool collide = false;         // the two bodies collide with each other
};

// The mouse's hand (Box2D's mouse joint): a soft spring of `hertz` and `dampingRatio` from `point` (world, on
// dynamic body `body`) to a target that starts there, solved together with the contacts, pulling with at most
// `maxForce` N; the body's turn is braked with at most `maxTorque` N m. It is a joint: moveHand leads its
// target, removeJoint lets go, reaction() is its pull on the body.
struct HandDesc {
    int body = -1;
    Vector3 point{0.0_r};
    Real hertz = 7.5_r, dampingRatio = 1, maxForce = 0, maxTorque = 0;
};

struct BodyHit {
    int body = -1;
    Real distance = 0;
    Vector3 point{0.0_r}, normal{0.0_r};
};

// A contact point after a step: between bodies a and b, the normal from a to b, how deep, and the
// normal impulse of the whole step (over the step's length: the mean normal force).
struct ContactPoint {
    int a = -1, b = -1;
    Vector3 point{0.0_r}, normal{0.0_r};
    Real depth = 0, normalImpulse = 0;
};

class PhysicsWorld {
public:
    virtual ~PhysicsWorld() = default;
    virtual std::string engine() const = 0; // "Box3D 0.2.0"

    // A body of convex shapes (none: a body of no collision). -1 and `error` when it cannot be made.
    virtual int addBody(const BodyDesc& body, const std::vector<ShapeDesc>& shapes, std::string& error) = 0;
    virtual void removeBody(int body) = 0; // its joints go with it
    virtual bool alive(int body) const = 0;
    virtual BodyState state(int body) const = 0;
    virtual void setVelocity(int body, const Vector3& velocity, const Vector3& angularVelocity) = 0;
    virtual void setPose(int body, const Vector3& position, const Quaternion& rotation) = 0;
    virtual void wake(int body) = 0;
    virtual void applyForce(int body, const Vector3& force, const Vector3& point) = 0; // world, at a world point
    virtual void applyTorque(int body, const Vector3& torque) = 0;
    virtual void applyImpulse(int body, const Vector3& impulse, const Vector3& point) = 0;

    virtual int addJoint(const JointDesc& joint, std::string& error) = 0;
    virtual void removeJoint(int joint) = 0;
    virtual bool jointAlive(int joint) const = 0;
    virtual void setMotor(int joint, bool on, Real speed, Real maxForce) = 0;
    virtual Real motorForce(int joint) const = 0; // what the motor put out over the last step, N m or N
    // What the joint passes to body a over the last step, world, at frame a's origin: N and N m.
    virtual void reaction(int joint, Vector3& force, Vector3& torque) const = 0;
    virtual int addHand(const HandDesc& hand, std::string& error) = 0;
    virtual void moveHand(int joint, const Vector3& target, Real dt) = 0; // the target walks there over the next dt seconds

    // The nearest body a ray from `from` to `to` meets, but `ignore`; false: none.
    virtual bool castRay(const Vector3& from, const Vector3& to, BodyHit& hit, int ignore = -1) const = 0;
    virtual std::vector<ContactPoint> contacts() const = 0;
    virtual std::vector<ContactPoint> contactsOf(int body) const = 0; // one body's, at the cost of its own contacts only

    virtual void setGravity(const Vector3& gravity) = 0;
    // One step of dt seconds in `substeps` substeps (the settings' when 0).
    virtual void step(Real dt, int substeps = 0) = 0;
};

// A world of the engine: defined by the one physics plugin built in (plugins/physics/<engine>).
std::unique_ptr<PhysicsWorld> makePhysicsWorld(const PhysicsSettings& settings, const Vector3& gravity);

} // namespace ke
