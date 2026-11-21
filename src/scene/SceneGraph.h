#pragma once
// The scene as data: a robot and its world - bodies, joints, motors, batteries, sensors, wheels,
// controllers, IK goals, lights, cameras and forces. An editor builds it with buttons and operations,
// saves it as a small text file anyone can read (*.kscene, SceneGraph.cpp), and the running world
// (src/sim) turns it into bodies and joints of the physics engine.
//
// Edit and play: an editor edits the graph; Play builds a world from it; Stop throws the world away
// and shows the graph again. Creating a shape makes geometry only: without roles it is drawn but takes
// no part in the simulation. Many objects at once - hierarchy, instances, arrays - are in
// SceneHierarchy.cpp; an entity's mesh in EntityShapes.cpp.
#include "core/Mesh.h"
#include "core/SurfaceTexture.h"
#include "math/Math.h"
#include "physics/PhysicsSettings.h"
#include "scene/JointModel.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ke {

// Mesh: a model read from an OBJ or STL file (Entity::meshFile).
enum class ShapeKind { Box, Sphere, Cylinder, Cone, Plane, Mesh };

// A plugin's component on an object of any kind - a shape, a joint, a light (docs/contracts/editor-plugin.md): its
// type, "<plugin>.<name>", and its values as the scene file has them - a key and one word each (a number, a vector
// "x,y,z", a word). Kept word for word whether or not the plugin is there: a scene opens and
// saves the same without it.
struct PluginComponent {
    std::string type;
    std::vector<std::pair<std::string, std::string>> values;
};

// What a body collides with - its own component on the entity, apart from what the entity looks
// like: a horse model may
// collide as a capsule. The body is always DRAWN with its geometry; the collider is only for
// contacts (the editor shows it as a thin wireframe). A collider alone makes a STATIC obstacle (a
// wall, a floor); with the rigid role it is the collider of a moving body; a rigid role without
// one falls back to Auto (with a note: the editor adds a collider together with the rigid role).
// Auto: the geometry itself - a box for a box, a sphere for a sphere, a thin box for a plane, a
// the convex hull of anything else; offset and rotation are then ignored. The other kinds sit at
// `offset` / `rotationDeg` in the object's frame: Box, Sphere and Capsule sized by `size` or fitted
// to the geometry's extent along the collider's own axes; ConvexHull the hull of the geometry.
// Cylinder is a cylinder about the collider's y, diameter = x, height = y (the physics engine may
// stand it in with a many-sided hull). A body's mass is the collider's volume times the density, its
// centre of mass the collider's.
enum class ColliderKind { Auto, Box, Sphere, Capsule, ConvexHull, Cylinder };
struct ColliderRole {
    bool enabled = false;
    ColliderKind kind = ColliderKind::Auto;
    bool fitToGeometry = true;  // size from the geometry's extent (then `size` is ignored)
    Vector3 size{0.2_r};         // box: edges; sphere: diameter = x; capsule, cylinder: diameter = x, full height = y
    Vector3 offset{0.0_r};       // collider centre relative to the object's centre, in the object's frame
    Vector3 rotationDeg{0.0_r};  // collider orientation relative to the object (capsule axis = y)
    bool trigger = false;        // a zone: it touches nothing and notices what is inside it (a bin a part is to reach, a guard's area)
    std::vector<std::string> ignores; // tags: no contact between it and a body marked with any of them (a part and the robot's own cover)
};
struct RigidRole {
    bool enabled = false;
    Real density = 500;        // kg/m^3
    // Given instead of what the shape and the density make - an imported link's, a part weighed on scales; each by
    // itself, what is not given stays the shape's own:
    Real mass = 0;              // kg; 0: the shape's volume times the density (the shape's inertia is scaled with a given mass)
    bool centerGiven = false;   // the centre of mass given:
    Vector3 centerOfMass{0.0_r}; //   in the body's own frame, m
    bool inertiaGiven = false;  // the inertia tensor given, about the centre of mass in the body's axes, kg m^2:
    Vector3 inertiaDiagonal{0.0_r}; //   ixx iyy izz
    Vector3 inertiaProducts{0.0_r}; //   ixy ixz iyz
    // This body's own, apart from the world's:
    Real gravityScale = 1;      // how much of the world's gravity takes it: 0 - it floats (a balloon, a body held by thrust in a test)
    Real linearDamping = -1;    // 1/s; below 0: the world's (physics linear_damping)
    Real angularDamping = -1;   // 1/s; below 0: the world's (physics angular_damping)
    Real friction = 0.5_r;      // kinetic friction coefficient
    Real restitution = 0.2_r;   // bounciness, 0..1
    bool fixed = false;         // static: does not move (a wall, a table)
    bool kinematic = false;     // moves as it is told - by its velocity and spin, or by a sequence - and pushes what it meets;
                                // nothing pushes it: no gravity, no contact (a conveyor, a door, a lift). A fixed one stays static
    // What physics moves by forces: neither held still nor led.
    bool dynamic() const { return enabled && !fixed && !kinematic; }
    Vector3 velocity{0.0_r};     // initial, m/s
    Vector3 angularVelocity{0.0_r};
};
// What every thing in a scene has, whatever it is (a shape with roles, a camera, a light, a sensor): a name, where it is, how it is turned, its colour, and the editor's
// switches. The inspector shows this part first and always the same way; the rest depends on
// what the thing is.
struct SceneObject {
    uint32_t id = 0;            // stable handle (unique in the graph; the editor assigns it)
    std::string name;
    Vector3 position{0.0_r};
    Vector3 rotationDeg{0.0_r};  // Euler angles in degrees: x pitch, y yaw, z roll (applied Ry Rx Rz)
    Vector3 color{0.8_r, 0.8_r, 0.8_r};
    bool visible = true;        // hidden things take no part in the simulation either
    bool locked = false;        // the editor does not select or move it (a floor, a backdrop)
    // Hierarchy: the id of the parent object (0: none). position / rotationDeg are then RELATIVE to
    // the parent, and moving the parent moves the children (a group). worldPose() composes them.
    uint32_t parent = 0;
    std::vector<std::string> tags; // the words it is marked with ("target", "keep"): scene_tag puts them, scene_list {tag} finds by them
    std::vector<PluginComponent> components; // the plugins' data on it («component <type> key value…» lines), whatever it is
};

// A group: an object with no geometry of its own that other objects hang under (Ctrl+G in the
// editor). For physics its members stay separate bodies unless `glued`: then they are welded into
// one rigid body (the group's members' colliders become one compound).
struct Group : SceneObject {
    bool glued = false;
};

// A light of the scene, the three kinds every 3D package has. It is for the viewer only (the
// physics does not see it); the direction of Sun and Spot is the object's -y axis turned by its
// rotation (a light "looks down" by default).
enum class LightKind { Sun, Point, Spot };
struct Light : SceneObject {
    LightKind kind = LightKind::Point;
    Real intensity = 1.0_r;     // relative brightness (1 = the default light of the viewer)
    Real range = 5.0_r;         // Point / Spot: the distance where the light has faded out, m
    Real coneDeg = 40.0_r;      // Spot: full cone angle
    Real softnessDeg = 10.0_r;  // Spot: the soft edge of the cone
    bool shadows = false;       // the viewer may cast shadows from it (Sun first)
};

// A force of the scene: a push along its own +y axis (directional) or a burst or pull from its origin (radial), once
// (impulse, N s) or all the time (constant, N). It acts on the centre of mass of rigid bodies while the world runs
// (src/sim/RobotWorld.cpp), never as a torque. A negative strength pushes along -y, or inward.
enum class ForceShape { Directional, Radial };
enum class ForceMode { Impulse, Constant };
enum class ForceFalloff { None, Linear, InverseSquare };
// Single source of the bounds; the reader, scene_add_force and the editor's card use these numbers.
constexpr Real kForceMaxStrength = 100000_r;  // |strength|, N s or N
constexpr Real kForceMaxRadius = 1000_r;      // m
constexpr Real kForceMaxStart = 3600_r;       // s
struct Force : SceneObject {
    ForceShape shape = ForceShape::Directional;
    ForceMode mode = ForceMode::Impulse;
    Real strength = 1_r;          // N s (impulse) or N (constant); negative: along -axis / inward
    Real radius = 0_r;            // m; radial: > 0 required; directional: 0 = no limit
    ForceFalloff falloff = ForceFalloff::None;
    Real start = 0_r;             // s of world time from play; impulse only, >= 0
    std::string tag;              // tag word; "" = every dynamic body
};
// The one validation of a force: null when it is valid, else an English refusal text.
const char* forceFault(const Force& f);

// A camera of the scene: a viewpoint the viewer can look through, take screenshots and recordings
// from. Looks along the object's -z axis turned by its rotation.
// A camera is free - it looks where its turn points - or a target camera (a camera and its aim point):
// it always looks at its target, an object (its origin, wherever the running world takes it), its up the world's;
// its own turn is not used. Moved, it keeps looking at the target; the target moved, it turns after it. A target that
// is no other object is the camera's own (Entity::targetOf): a small see-through cube the gizmo moves, a handle like
// an IK goal; it goes with its camera, or when the camera is set free.
enum class CameraKind { Free, Target };
struct Camera : SceneObject {
    Real fovDeg = 50.0_r;       // vertical field of view
    Real nearClip = 0.05_r, farClip = 200.0_r; // m
    bool active = false;        // the one the viewer looks through when "through the camera" is on
    CameraKind kind = CameraKind::Free;
    uint32_t target = 0;        // a target camera's object (0, or gone: it looks 2 m ahead)
};

// An array: one object standing for many copies of a template entity laid out in a pattern. The simulation expands it into one set of
// meta-objects per copy; the editor changes the count or the spacing with one number. The copies
// are placed relative to the array's own pose; the template itself is not simulated.
enum class ArrayPattern { Line, Grid, Circle };
struct ArrayObject : SceneObject {
    uint32_t templateId = 0;          // the entity that is copied (usually hidden, as a template)
    ArrayPattern pattern = ArrayPattern::Line;
    int count[3] = {5, 1, 1};         // Line: count[0]; Grid: nx, ny, nz; Circle: count[0]
    Vector3 step{0.3_r, 0.3_r, 0.3_r};   // Line: step between copies (a vector); Grid: spacing per axis
    Real radius = 1.0_r;              // Circle: the radius (copies around the array's y axis)
    Vector3 rotationStepDeg{0.0_r};    // each next copy turned by this much more (a spiral staircase)
    Real jitter = 0;                 // random offset of each copy, m (a fixed seed: the same every run)
    uint32_t seed = 1;
};


// A shape in the scene with its roles: a rigid body, a collider, or both (a moving body); a collider
// alone is a static obstacle. With no role at all it is geometry only: drawn, but no body, no
// collision, no part in the simulation.
struct Entity : SceneObject {
    // An instance shares the geometry and the roles of its master (the entity with this id): change
    // the density, the collider or the shape of one and all change. Only the
    // SceneObject part (name, pose, visibility...) stays its own. 0: an ordinary, independent entity.
    // resolveInstance() gives the effective entity.
    uint32_t instanceOf = 0;
    ShapeKind shape = ShapeKind::Box;
    // Full size: box edges; sphere/cylinder/cone: diameter (x) and height (y). A mesh keeps the
    // model's proportions: it is scaled uniformly so that its largest extent equals the largest
    // component of size (entityLocalMesh(e).bounds() is its real box).
    Vector3 size{0.2_r};
    std::string meshFile;       // ShapeKind::Mesh: the OBJ/STL file as written in the scene file
    Real opacity = 1;           // 1: solid; below: see-through - glass, a screen, a guard. Its look only: physics does not know
    uint32_t goalOf = 0;        // an inverse kinematics goal: this effector's frame is driven to this entity's (Kinematics.h)
    uint32_t targetOf = 0;      // a target camera's own target: this camera looks at it
    // A goal's trajectory (Kinematics.h, pathPoint): from the goal's own place through these points, world metres,
    // walked at `pathSpeed` along its length while the scene plays - from the start, or from when the path was
    // set - then held at the last point (or back to the start and round again: `pathLoop`). Its turn stays its own.
    std::vector<Vector3> path;
    std::vector<Vector3> pathTurns; // each waypoint's turn, Euler degrees as rotationDeg (empty: the goal's own all the way);
                                    // between two points the turn goes by slerp in step with the travel
    Real pathSpeed = 0.1_r;     // m/s
    bool pathLoop = false;
    bool pathSmooth = false;    // a camera's flight: a centripetal Catmull-Rom spline through the points, the turns by SQUAD
    Real pathTurnLength = 0;    // m of the path's length a radian of turn counts (0: the travel alone) - so a hand turned
                                // on the spot, as a way planned in the joints asks, takes time and is not snapped round
    SurfaceTexture texture;
    RigidRole rigid;
    ColliderRole collider;
};

// One freedom of a joint (JointModel.h): locked, limited or free, and what drives it - a spring-
// damper, a velocity motor or an electric DC motor.
struct JointFreedom {
    JointMotion motion = JointMotion::Locked;
    Real lower = 0, upper = 0;          // m or rad, when limited
    JointDrive drive;
};

// A joint of the scene: entity `a` held to entity `b` (0: the world) - the generic six-freedom joint
// (JointModel.h). It is a frame
// on each link: where it sits and how it is turned, in that link's own frame (b = 0: the world's).
// The six freedoms are frame a's travel along frame b's x, y, z and its turns about them; each is
// locked, limited or free and may be driven. A hinge is the joint with the turn about x free, a
// slider with the travel along x, a ball with the three turns, a weld with none (jointKind in
// Assembly.h); any other mix - a cylinder joint, a universal joint, a shock absorber with a spring -
// is the same object. The coordinates are zero when the frames coincide, whatever the links' poses:
// turning a link in the editor sets the angle, and the limits stay where they were. The two links do
// not collide with each other unless `collide` - linked parts touch at the joint by design.
struct JointObject {
    uint32_t id = 0;                    // in the same id space as the objects (nextId)
    std::string name;
    uint32_t a = 0, b = 0;              // entity ids: the moving link and what it hangs on (0: world)
    Vector3 anchorA{0.0_r}, anchorB{0.0_r}; // the frames' origins in a's / b's frame, m
    Vector3 frameA{0.0_r}, frameB{0.0_r};   // the frames' turns, Euler degrees as SceneObject::rotationDeg
    JointFreedom dof[6];                // travel x, y, z; turn x, y, z (JointDof)
    bool collide = false;
    std::vector<std::string> tags; // the words it is marked with ("target", "keep"): scene_tag puts them, scene_list {tag} finds by them
    std::vector<PluginComponent> components; // the plugins' data on it («component <type> key value…» lines), whatever it is
};

// A battery of the scene (electro/Battery.h): a pack of cells - an equivalent circuit with its own
// state of charge, which the drivers' motors drain while the scene plays.
struct BatteryObject {
    uint32_t id = 0;
    std::string name;
    std::vector<std::string> tags; // the words it is marked with ("target", "keep"): scene_tag puts them, scene_list {tag} finds by them
    std::vector<PluginComponent> components; // the plugins' data on it («component <type> key value…» lines), whatever it is
    int series = 1, parallel = 1;
    Real capacityAh = 2.5_r;            // of one cell [A h]
    Real r0 = 0.03_r, r1 = 0.015_r;     // of one cell [Ohm]
    Real c1 = 2000.0_r;                 // of one cell [F]
    Real cutoff = 3.0_r;                // per cell [V]
    Real soc = 1.0_r;                   // the state of charge the scene starts with
};

// A motor driver (an H-bridge) between a battery and a joint's DC motor: it
// switches the battery's bus onto the motor with the duty `duty` (-1..1, the sign the direction). The
// motor's constant and winding resistance are the joint's dcmotor line; the driver gives its voltage
// (with no driver, the motor runs on its line's own voltage, an ideal supply). At a duty of 0 the
// bridge stops as `brake` says (DRV8833: both inputs low or high): its switches open - the winding
// carries nothing, the wheel coasts - or the winding shorted through them - it brakes.
struct DriverObject {
    uint32_t id = 0;
    std::string name;
    std::vector<std::string> tags; // the words it is marked with ("target", "keep"): scene_tag puts them, scene_list {tag} finds by them
    std::vector<PluginComponent> components; // the plugins' data on it («component <type> key value…» lines), whatever it is
    uint32_t joint = 0;                 // the joint whose freedom `dof` has the DC motor
    int dof = DofRx;
    uint32_t battery = 0;
    Real duty = 0;
    Real switches = 0.1_r;              // the bridge's on-resistance, both switches in the path [Ohm]
    Real currentLimit = 0;              // [A], 0: none
    bool brake = false;                 // at duty 0: shorted (brake), not open (coast)
};

// A controller of the scene (src/sim): an operator's panel as an abstract thing - named actions, each with a value (a button, an axis, a 2-D vector)
// and one thing of the scene it drives at a speed, and bindings saying which controls of which device give the value.
// Which device feeds it is up to the bindings: the keyboard, a game controller, the mouse - several at once - or none, a script or a trained policy setting the values itself. The bindings are the
// client's to read (the editor turns its keys and pad into values); the core takes only the values
// (the operation controller_input), so a demonstration records actions and a policy
// plays them back through the same panel, as one object.
//
// What an action drives (`drive`), at a value of 1 (an axis's or a button's first component, a vector's two):
//   plane - an IK goal across the floor, value (x, y) along the world's +x and -z (a client turns its view's
//           forward and right into these), `speed` m/s;   lift - the goal up, m/s;   turn - the goal about the
//           vertical, degrees/s;   grip - the fingers of the goal's effector (sliders, as assembly_grip) closed that
//           part of their travel, `speed` the force cap N;   jog - a joint's freedom `dof`: its servo's target moved,
//           m/s or degrees/s;   duty - a motor driver's duty, `speed` times the value.
enum class ControlValue { Button, Axis, Vector2 };
enum class ControlDrive { Plane, Lift, Turn, Grip, Jog, Duty };
struct ControlBinding {
    std::string device;                // "keys", "gamepad", "mouse"
    // keys: one key (a button), the positive and the negative key (an axis), up down left right (a vector);
    // gamepad: left_stick, right_stick (a vector), left_x, left_y, right_x, right_y, left_trigger, right_trigger
    // (an axis), a, b, x, y, left_bumper, right_bumper, start, back (a button); mouse: left, right, middle
    std::vector<std::string> controls;
    bool invert = false;               // the value's sign turned
    bool toggle = false;               // a press flips the value between 0 and 1 (a button's binding)
};
struct ControllerAction {
    std::string name;                  // one word: the action's name in the file, in controller_input, in a recording
    ControlValue value = ControlValue::Axis;
    ControlDrive drive = ControlDrive::Plane;
    uint32_t target = 0;               // the IK goal (plane, lift, turn, grip), the joint (jog), the driver (duty)
    int dof = DofRx;                   // jog: the joint's freedom
    Real speed = 0.1_r;                // at a value of 1, as `drive` says
    std::vector<ControlBinding> bindings;
};
struct ControllerObject {
    uint32_t id = 0;
    std::string name;
    std::vector<std::string> tags; // the words it is marked with ("target", "keep"): scene_tag puts them, scene_list {tag} finds by them
    std::vector<PluginComponent> components; // the plugins' data on it («component <type> key value…» lines), whatever it is
    std::vector<ControllerAction> actions;
};

// A sensor of the scene (src/devices): an object with no geometry riding on the link it is mounted on, a child of it - reading what a
// microcontroller would. Its pose relative to the parent is where it is mounted, its x axis what it
// measures along (an encoder's shaft). The parent is an entity or a joint:
//   - on a joint, it reads the joint's freedom `dof` - the joint's own coordinate;
//   - on an entity, it reads coordinate `dof` of its frame on the parent against the same frame
//     fixed to `against` (0: the world) where the two were when it began to read: a measuring wheel on the floor, a turntable, a tilt against
//     the world - no joint needed.
// Both go through frameCoordinates (JointModel.h), so an encoder on a joint starting at zero and one
// on the joint's two links counts the same. A parent removed takes its sensors with it; `against`
// removed leaves the sensor reading nothing and saying why.
//
// An IMU (devices/Imu.h) rides on an entity and reads against the world only - an accelerometer and a
// gyroscope measure against an inertial frame: the specific force at its point and the angular rate, in
// its axes (its pose on the parent is the chip's axes). A sonar (devices/Sonar.h) sends its beam along
// its x axis and hears the nearest echo off the rigid bodies and the walls and floor of the scene's box -
// its carrier left out, as the beam starts on its face. An IMU or a sonar with parent 0 is fixed in the
// world, at its own pose.
//
// The kind says what a sensor measures, not which unit it is: the unit is its datasheet, `spec` - the
// numbers a kind's model needs (rate, range, noise, offsets, mass...; their keys, units and the values of
// a sensor no model was given: scene/SensorSpec.h) - and `model`, the name of the table's row they were
// copied from (data/sensors/<kind>.csv). The numbers are the sensor's own: a scene reads the same with
// no table at hand, and a number typed over the model's makes the unit what was typed.
//
// The force sensors read what crosses a boundary: touch, force, torque and actuator effort.
// A touch pad lies on a face of an entity (its x out of the face, as a sonar's beam) and reads the
// normal force of the contacts through that face - the points whose outward normal lies within 45
// degrees of its x: a fingertip's pad, a bumper, a load cell under a foot, a limit switch by a threshold.
// A wrench sensor sits on a joint and reads the force and the torque the joint passes from b to a, in
// the joint's frame on a about its anchor - a six-axis force-torque sensor at a robot's wrist (ATI's
// and Robotiq's), a load cell inline. An effort sensor reads a joint's drive along one freedom: the
// force or torque its motor puts out (positive pushes the coordinate up) - a servo's current sense.
// A touch pad and an effort sensor read the physics engine's forces of each substep: ideal readings, no
// datasheet's noise yet; a wrench sensor has a datasheet (devices/ForceTorque.h).
//
// A stereo camera is one frame with two pinhole cameras on it (src/sim): its x the optical axis, the
// left camera b/2 along its +y, the right b/2 along -y, both looking along x, width x height pixels over a
// horizontal field of view - 65 degrees and 10 cm by default, as a ZED (an Intel RealSense D435: 87, 5 cm); between them a projector of a
// fixed pattern of dots, as the D435's, so a plain surface still has something to match. It is read on
// request (a frame rendered by rays, not every substep): the two images, lit by the scene's lights with their
// shadows, and the true depth and body of every pixel for checking - the depth a robot gets it must match.
enum class SensorKind { Encoder, Imu, Sonar, Touch, Wrench, Effort, Stereo };
struct SensorObject : SceneObject {
    SensorKind kind = SensorKind::Encoder;
    std::string model;                  // the catalog's model the datasheet was copied from ("": its own numbers)
    std::map<std::string, Real> spec;   // the datasheet by key (scene/SensorSpec.h); a key not here: the field's value
    int lines = 500;                    // an encoder: per turn for a turn (rx, ry, rz), per metre for a travel (tx...)
    bool hasEncoderReference = false;   // explicit calibrated coordinate when count=0; never inferred from physics
    Real encoderReference = 0;          // rad for rotational DOFs, m for translational DOFs
    int dof = DofRx;
    uint32_t against = 0;               // an encoder on an entity: what it is measured against (0: the world)
    bool noise = true;                  // the unit's noise and offsets as its datasheet has them (off: an ideal reference)
    uint32_t seed = 0;                  // of its noise and offsets: the same seed, the same unit (0: its id)
    int width = 320, height = 240;      // a stereo camera: its images, pixels
    Real fovDeg = 65;                   // ... its horizontal field of view
    Real baseline = 0.10_r;             // ... the distance between its two cameras, m (a ZED-like rig: 65 degrees, 10 cm)
    bool projector = true;              // ... its pattern of dots on
};

// A wheel as a point of its body - the fast model of a wheel module (Assembly.h): no body of
// its own, a ray from where its suspension is mounted down to the ground. Its pose on the parent (the
// chassis, an entity) is that mount: its -y the suspension's line, its x the way it rolls, its z the
// axle. The suspension is a spring and a damper along the line, `rest` long fully extended; the wheel
// spins about its axle with its own inertia (a disc: m r^2 / 2), driven by a DC motor (the joint's
// motor: K, R, its own U when no driver feeds it) - `flip` reverses which way positive turns; the tyre
// pushes along the ground no more than `friction` times its load. Its mass and the unsprung mass that
// rides with it are its body's to carry: added to the body's mass and inertia as points at the hub. A
// driver may feed it as it feeds a joint, an encoder may count its turns. The running world runs it
// (src/sim).
struct WheelObject : SceneObject {
    Real radius = 0.09_r, width = 0.045_r, mass = 1.0_r, friction = 1.0_r; // m, m, kg, the tyre's mu
    Real unsprung = 0;                  // what moves with the wheel but does not spin (a strut, a hub) [kg]
    Real rolling = 0;                   // the tyre's rolling resistance coefficient: a torque C_rr N r against the spin
    Real rest = 0.1_r;                  // the suspension's length fully extended [m], ray: rest + radius
    Real stiffness = 1500.0_r, damping = 60.0_r; // N/m, N s/m
    Real voltage = 0, resistance = 1, constant = 0; // the DC motor (constant 0: none, the wheel rolls free)
    bool flip = false;
};

struct WorldSettings {
    Vector3 gravity{0, -9.81_r, 0};
    Vector3 size{4, 3, 4};      // the box everything lives in, m (floor at y = 0)
    PhysicsSettings physics;
};

// A sequence - the Sequencer's (scene/Animation.h): an object of the scene that holds an
// animation of others, never written into them. Its tracks bind objects by id (a group, a robot's root, a body, a camera,
// a light), each a list of keys - the object's pose on its parent at a time. Between two keys the place goes in a line
// and the turn the shortest way (slerp); before the first and after the last the pose holds, or the sequence loops over
// its length; between two keys that both carry a velocity (a recorded run's) the place follows the cubic with those
// velocities at its ends (Hermite: a fall's parabola, a slide's braking, exactly). A scene holds as many as it likes -
// scenarios, recorded runs. The active one is the Sequencer's (shown, scrubbed, keyed); ▶ plays it only when it is set
// to play: its bodies are then kinematic ones led along their tracks, pushing what they meet. A
// take of a run comes not playing - the next ▶ is physics again -; set to play, it replays the run as an animation. An
// object deleted leaves its track lost, skipped.
// How a key goes on to the next one (key interpolation): in a line; smoothly - the cubic
// (Hermite) through the keys with the key's velocity at it, a recorded key's own or, set by hand, the slope from the
// key before to the key after (a first and a last key: none, the motion starts and stops softly); or held, a step.
enum class AnimInterp : uint8_t { Linear, Smooth, Step };
struct AnimKey {
    Real time = 0;              // s
    Vector3 position{0.0_r};    // as SceneObject::position
    Vector3 rotationDeg{0.0_r}; // as SceneObject::rotationDeg
    Vector3 velocity{0.0_r};    // m/s on the parent, when hasVelocity
    bool hasVelocity = false;   // a recorded key: the motion's velocity there
    AnimInterp interp = AnimInterp::Linear; // to the next key
};
const char* interpName(AnimInterp i);                  // "linear", "smooth", "step"
bool interpOf(const std::string& name, AnimInterp& i); // false: no such
struct SequenceTrack {
    uint32_t object = 0;        // the bound object
    std::vector<AnimKey> keys;  // by time, one a time
};
struct SequenceObject : SceneObject {
    std::vector<SequenceTrack> tracks;
    Real time = 0;              // the playhead, s
    bool loop = false;
    bool active = false;        // the Sequencer's (one at most)
    bool play = true;           // ▶ plays it when it is the active one: its bodies led along their tracks
    Real length() const;        // the last key's time of any track (0: none)
};

struct SceneGraph {
    WorldSettings world;
    std::vector<Entity> entities;
    std::vector<Group> groups;
    std::vector<ArrayObject> arrays;
    std::vector<Light> lights;
    std::vector<Camera> cameras;
    std::vector<JointObject> joints;
    std::vector<BatteryObject> batteries;
    std::vector<DriverObject> drivers;
    std::vector<SensorObject> sensors;
    std::vector<Force> forces;
    std::vector<WheelObject> wheels;
    std::vector<ControllerObject> controllers;
    std::vector<SequenceObject> sequences;
    // Not saved: the folder relative mesh files are found in (the scene file's own folder). load()
    // keeps it as it was.
    std::string baseDirectory;
    // Not saved: the smallest id nextId() gives - an editor that never reuses an id within a session
    // (an undo would otherwise bring back an object whose id a new one took). load() keeps it.
    uint32_t idFloor = 0;

    // Text format, one line per fact, readable and diff-able (see SceneGraph.cpp for a sample).
    std::string save() const;
    bool load(const std::string& text, std::string& error);
};
// The world's "world" and "physics" lines, both always (save() leaves out a physics line of defaults).
std::string worldText(const WorldSettings& w);

// Every list of objects a scene holds, kind by kind in the file's order: `f` is called with each (the lists hold
// different types, so `f` takes any of them). A kind added to the graph is added here once, and whatever walks all
// the objects - their tags, their components, scene_list - has it.
template <class Graph, class F>
void eachObjectList(Graph& g, F&& f) {
    f(g.entities), f(g.groups), f(g.arrays), f(g.lights), f(g.cameras), f(g.joints), f(g.batteries), f(g.drivers), f(g.sensors), f(g.forces), f(g.wheels),
        f(g.controllers), f(g.sequences);
}

// ---------------------------------------------------------------------------
// Many objects at once (SceneHierarchy.cpp): hierarchy, instances, arrays. None of these call the
// physics engine; the running world and an editor use them alike.
// ---------------------------------------------------------------------------
// The object with this id - an entity, a group, an array, a light, a camera, a force or a sensor (null: none).
const SceneObject* findObject(const SceneGraph& g, uint32_t id);
// The tags of an object of any kind - a shape, a joint, a battery, a sequence (nullptr: no such object). A tag is one
// word (tagWord): letters, digits and - _ . : of any alphabet, no spaces or quotes, 48 bytes at most.
std::vector<std::string>* objectTags(SceneGraph& g, uint32_t id);
const std::vector<std::string>* objectTags(const SceneGraph& g, uint32_t id);
bool tagWord(const std::string& word);

// A key of the scene file under the name the format has for it today: one of its earlier names (anchorA) comes
// back as the present one (anchor_a), any other word as it is - what a script or a journal written before names
// still means the same.
std::string presentKey(const std::string& key);
// The plugins' components of an object of any kind (nullptr: no such object).
std::vector<PluginComponent>* objectComponents(SceneGraph& g, uint32_t id);
const std::vector<PluginComponent>* objectComponents(const SceneGraph& g, uint32_t id);
const Entity* findEntity(const SceneGraph& g, uint32_t id); // only an entity (null: none, or another kind)
// Any object's orientation from its rotationDeg (Ry Rx Rz), and back: Euler degrees of a rotation.
Quaternion objectRotation(const SceneObject& o);
Vector3 eulerDegrees(const Quaternion& q);
// Where an object is in the world: its pose composed with its parents'. A missing parent or a
// cycle in the parent links ends the chain there (that object counts as a root).
void worldPose(const SceneGraph& g, const SceneObject& o, Vector3& position, Quaternion& rotation);
// The inverse: sets o's relative pose so that its world pose is (position, rotation).
void setWorldPose(const SceneGraph& g, SceneObject& o, const Vector3& position, const Quaternion& rotation);
// Visible, and so are all its parents.
bool effectivelyVisible(const SceneGraph& g, const SceneObject& o);
// The nearest glued group above the object (0: none): its rigid members are one body.
uint32_t gluedGroupOf(const SceneGraph& g, const SceneObject& o);
// An instance as it acts: the geometry and roles of its root master (instanceOf followed to the end)
// with its own SceneObject part (name, pose, visibility, id). Not an instance, or a missing master:
// the entity itself.
Entity resolveInstance(const SceneGraph& g, const Entity& e);
// The copies an array stands for, posed in the WORLD (parent 0), each with the template's geometry
// and roles, visible as the array is. Copy n of the array with id A has the id arrayCopyId(A, n):
// stable across rebuilds as long as the array keeps its id (ids of arrays up to 32767, 65536 copies).
std::vector<Entity> expandArray(const SceneGraph& g, const ArrayObject& a);
uint32_t arrayCopyId(uint32_t arrayId, int copy);
bool isArrayCopyId(uint32_t id);
uint32_t arrayOfCopyId(uint32_t id);
// Every entity the simulation builds, in the world: the graph's entities (instances resolved,
// parents composed, visibility inherited) followed by the copies of every array. worldEntity: one of them.
Entity worldEntity(const SceneGraph& g, const Entity& e);
std::vector<Entity> worldEntities(const SceneGraph& g);
// A fresh id: the largest over the objects and the joints, plus one.
uint32_t nextId(const SceneGraph& g);

// Lights and cameras in the world (their parents included). A light shines along its -y axis and a
// camera looks along its -z axis, both turned by the object's world rotation; up is the camera's +y.
Vector3 lightDirection(const SceneGraph& g, const Light& l);
// The direction of a directional force: the +y axis turned by the object's world rotation.
Vector3 forceAxis(const Quaternion& q);
void cameraFrame(const SceneGraph& g, const Camera& c, Vector3& eye, Vector3& forward, Vector3& up);
// A handle: an IK goal or a camera's own target - intent, not matter: no body, no roles, drawn see-through, moved by
// the gizmo while the world runs, never seen by a robot's sensors.
inline bool isHandle(const Entity& e) { return e.goalOf != 0 || e.targetOf != 0; }
// Where a target camera looks: its object's origin in the world (a free camera, or its target gone: 2 m ahead).
// `objectAt`, when given, says where an object is now (the running world's pose; false: where the graph has it).
Vector3 cameraTarget(const SceneGraph& g, const Camera& c, const std::function<bool(uint32_t, Vector3&)>& objectAt = {});
// A camera's frame looking at `target` from `eye`, its up the world's (straight up or down: its z): its world turn.
Quaternion lookTurn(const Vector3& eye, const Vector3& target);

// ---------------------------------------------------------------------------
// The shape of an entity as a mesh (EntityShapes.cpp) - the same mesh the physics engine gets, so a
// viewer can draw geometry-only entities and the whole scene while it is being edited.
// ---------------------------------------------------------------------------
// The entity's orientation: yaw about y, then pitch about x, then roll about z (degrees).
Quaternion entityRotation(const Entity& e);
// The shape as a closed mesh in the entity's own frame, at its size, centred on the origin. A model
// file is read once and cached; an unreadable file gives an empty mesh (and `error`, if asked).
TriMesh entityLocalMesh(const Entity& e, const std::string& baseDirectory = std::string(), std::string* error = nullptr);
// The graph's relative file paths (models, textures) rewritten for a scene file in `directory`, so that they find the same
// files from there ("Save as" into another folder), and baseDirectory set to it. Absolute paths stay as they are.
void rebaseFiles(SceneGraph& g, const std::string& directory);
// What the entity's mesh depends on - the shape, its size (a model's largest extent: it keeps its
// proportions), the model's file - as text: equal texts, equal meshes (a cache's key).
std::string entityGeometryKey(const Entity& e, const std::string& baseDirectory = std::string());
// The same mesh posed in the world: turned by the rotation and moved to the position.
// No role at all: drawn, but not part of the simulation.
bool entityIsGeometryOnly(const Entity& e);
// What a rigid body of this shape weighs, kg: the mass given to it (rigid mass) - the engine's own number -, else an
// estimate: its density over the drawn shape's volume (a model: its box; the engine counts the collider's). What
// tunes a servo or a spring to a body asks here. An instance is resolved first (resolveInstance): its role is its master's.
Real bodyMass(const Entity& e);

} // namespace ke
