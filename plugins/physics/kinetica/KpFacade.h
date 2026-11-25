#pragma once
// kinetica-physics (extern/kinetica-physics) behind a facade of plain types - the Facade pattern. The engine names its
// headers as Kinetica names its own (<math/Quaternion.h>), so the two never meet in one file: KpFacade.cpp alone
// includes the engine and is built as a library of its own (CMakeLists.txt, ke_kp_facade); KineticaWorld.cpp - the
// boundary src/physics/PhysicsWorld.h on it - sees only this. The facade speaks the boundary's language: a body by its
// own frame and its shapes in it, numbered as they were added; a joint by its frames on the two bodies, its coordinate
// frame a relative to frame b. What the engine sees otherwise - a body by its centre of mass and principal axes, a joint
// the other way round - stays inside.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kpf {

struct Vec { double x = 0, y = 0, z = 0; };
struct Turn { double w = 1, x = 0, y = 0, z = 0; }; // a rotation: a unit quaternion

// A part of a body in the body's frame: a convex shape, or a mesh's triangles (a static body's only).
struct Shape {
    enum class Kind { Box, Sphere, Capsule, Hull, Mesh } kind = Kind::Box;
    Vec half{0.5, 0.5, 0.5};                        // a box's half extents
    double radius = 0.5, halfHeight = 0;            // a sphere's; a capsule's, along its own y
    std::vector<Vec> points;                        // a hull's points; a mesh's vertices
    std::vector<std::array<uint32_t, 3>> triangles; // a mesh's, counter-clockwise seen from outside
    Vec position;
    Turn rotation;
    double friction = 0.5, restitution = 0;
};

// A body as the boundary describes it: its frame's pose, how it moves, its mass from the shapes or given.
struct Body {
    enum class Kind { Static, Kinematic, Dynamic } kind = Kind::Dynamic;
    Vec position;
    Turn rotation;
    Vec velocity, spin; // of the centre of mass, world
    double density = 1000;
    bool massGiven = false, centreGiven = false, inertiaGiven = false;
    double mass = 0;
    Vec centre; // the centre of mass, the body's frame
    std::array<std::array<double, 3>, 3> inertia{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; // about the centre, the body's axes
    double linearDamping = 0, angularDamping = 0, gravityScale = 1;
    // Who it does not touch: a body of one group > 0 never touches another of it; a and b do not touch when the
    // ignores of one hold a bit of the other's marks.
    int group = 0;
    uint64_t marks = 0, ignores = 0;
};

struct State {
    Vec position;
    Turn rotation; // the body's frame
    Vec centre;    // the centre of mass, world
    Vec velocity, spin;
    double mass = 0; // 0: static or kinematic
    std::array<std::array<double, 3>, 3> inertia{}; // about the centre, world axes
    bool awake = true;
};

struct Hit { int body = -1; double distance = 0; Vec point, normal; };
// A touching point after a step: bodies a and b, the normal from a to b, how deep, the normal impulse of the step.
struct Contact { int a = -1, b = -1; Vec point, normal; double depth = 0, impulse = 0; };

// A joint between body a and body b (-1: the world) by its frames on them; its coordinate is frame a relative to frame
// b. Hinge: a turn about the frames' x; Slider: a travel along x; Ball: three turns; Fixed: none.
struct Joint {
    enum class Kind { Hinge, Slider, Ball, Fixed } kind = Kind::Hinge;
    int a = -1, b = -1;
    Vec anchorA, anchorB; // in each body's frame, the world's for -1
    Turn frameA, frameB;
    bool limited = false;
    double lower = 0, upper = 0;
    bool motor = false;
    double speed = 0, maxForce = 0;
};
// The mouse's hand: a spring of `hertz` and `dampingRatio` from `point` on a moving body to a target led by moveHand.
struct Hand { int body = -1; Vec point; double hertz = 7.5, dampingRatio = 1; };

struct Settings {
    int substeps = 4;
    double contactHertz = 30, contactDamping = 10;
    bool sleeping = true;
};

class World {
public:
    World(const Settings& settings, const Vec& gravity);
    ~World();
    static std::string engine();

    int addBody(const Body& body, const std::vector<Shape>& shapes, std::string& error); // -1 and why when it cannot be made
    void removeBody(int body); // the engine keeps every body it was given: a removed one stays, shapeless and still
    bool alive(int body) const;
    State state(int body) const;
    void setVelocity(int body, const Vec& velocity, const Vec& spin);
    void setPose(int body, const Vec& position, const Turn& rotation);
    void wake(int body);
    void applyForce(int body, const Vec& force, const Vec& point);
    void applyTorque(int body, const Vec& torque);
    void applyImpulse(int body, const Vec& impulse, const Vec& point);
    void applyAngularImpulse(int body, const Vec& impulse);
    void moveKinematic(int body, const Vec& position, const Turn& rotation, double dt);

    int addJoint(const Joint& joint, std::string& error);
    void removeJoint(int joint); // the engine keeps every joint: a removed one is left between still bodies, holding nothing
    bool jointAlive(int joint) const;
    void setMotor(int joint, bool on, double speed, double maxForce);
    int addHand(const Hand& hand, std::string& error);
    void moveHand(int joint, const Vec& target, double dt);

    bool castRay(const Vec& from, const Vec& to, Hit& hit, int ignore) const;
    std::vector<Contact> contacts(int body) const; // -1: the world's, each pair once
    void setGravity(const Vec& gravity);
    void step(double dt, int substeps);

private:
    struct Inside; // the engine's world and each body's place in it
    std::unique_ptr<Inside> in_;
};

} // namespace kpf
