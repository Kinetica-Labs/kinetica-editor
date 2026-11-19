#pragma once
// Assembling a mechanism out of shapes, as the assembly of a CAD program does it: a shape put flat
// against a face of another - the editor's "magnet": the six faces of a box are the six directions
// a link can grow in - and a joint between the two at that face (a hinge, a slider, a ball, a weld).
// These are plain edits of a SceneGraph, no physics involved: the editor's buttons and an agent that
// drives the editor through its API call the same functions, and the running world (src/sim) builds
// the joints in the physics engine when the scene plays.
#include "scene/SceneGraph.h"
#include "scene/Visuals.h"

#include <memory>

namespace ke {

// A face of an entity's box: the six directions of its own frame.
enum class BoxFace { PlusX, MinusX, PlusY, MinusY, PlusZ, MinusZ };

// A shape against a face of the target: the face, its outward normal in the world, the centre of
// the face (the joint's anchor), the pose of the new shape (centred over the face, turned as the
// target) and the two directions lying in the face - the target's other two axes - a hinge turns
// about one of them (a knee) or about the normal (a wheel, a turntable).
struct FaceSnap {
    bool found = false;
    BoxFace face = BoxFace::PlusY;
    Vector3 normal{0.0_r}, anchor{0.0_r}, position{0.0_r}, rotationDeg{0.0_r};
    Vector3 across[2] = {Vector3(0.0_r), Vector3(0.0_r)};
    Real halfAcross[2] = {0, 0}; // the face's half extents along across[0], across[1] [m]
};

// The face of the target's box nearest to a world point (the point a click hit), or a face given by
// name; the shape put against it has edges `size` in its own frame. The box is the entity's
// Entity::size about its centre (a model's: its mesh's bounds). No such entity: found = false.
FaceSnap snapToFace(const SceneGraph& g, uint32_t target, const Vector3& point, const Vector3& size);
FaceSnap snapToFace(const SceneGraph& g, uint32_t target, BoxFace face, const Vector3& size);
// Where the mouse ray hit the target's surface and the outward normal of the polygon it hit (world).
// A box (and a plane) snaps to its face whose normal is nearest to the polygon's - centred on it, as
// above. A curved or modelled surface (a sphere, a cylinder's side, a model) snaps at the point hit,
// the new shape turned so that its side that would face the nearest box face faces the normal.
FaceSnap snapToSurface(const SceneGraph& g, uint32_t target, const Vector3& point, const Vector3& normal, const Vector3& size);
// The axis a joint at that face turns about: 0 and 1 across the face, 2 along its normal (the default:
// the normal of the polygon the mouse is on - a wheel, a turntable, a door on a wall).
Vector3 snapAxis(const FaceSnap& s, int axis);

// A joint between two entities (b = 0: the world) at a world point along a world direction - written
// into b's frame (JointObject). Returns its id.
uint32_t addJoint(SceneGraph& g, JointType type, uint32_t a, uint32_t b, const Vector3& worldAnchor, const Vector3& worldAxis);
// A joint put anew at a world point along a world axis, its links where they stand (the card's «Ось» and «Точка оси»).
void placeJoint(const SceneGraph& g, JointObject& j, const Vector3& worldAnchor, const Vector3& worldAxis);
const JointObject* findJoint(const SceneGraph& g, uint32_t id);
// The joints that hold the entity (as a or b) go, as when the entity is deleted. Returns how many.
int removeJointsOf(SceneGraph& g, uint32_t entity);
// A joint's frames in the world at the poses the graph has: frame a on entity a, frame b on entity b.
void jointWorldFrames(const SceneGraph& g, const JointObject& j, Vector3& originA, Quaternion& frameA, Vector3& originB, Quaternion& frameB);
// Frame b's origin and x axis in the world: where the joint is and what it turns about (slides along).
void jointWorldFrame(const SceneGraph& g, const JointObject& j, Vector3& anchor, Vector3& axis);
// The six coordinates at the graph's poses (GenericJoint.h: travel, then turns of frame a from b).
void jointCoordinates(const SceneGraph& g, const JointObject& j, Real out[6]);
// The joint's main freedom - the turn about x when free, else the first free one (-1: a weld) - and
// its coordinate: a hinge's angle [rad], a slider's travel [m].
int mainFreedom(const JointObject& j);
Real jointPosition(const SceneGraph& g, const JointObject& j);
// The kind the freedoms make: Hinge, Slider, Ball, Fixed (Generic: another mix), and the API's words -
// weld, hinge, slider, ball, cylinder (travel and turn about x), universal (turns about y and z),
// planar (travel along y and z, turn about x); setting a kind frees its freedoms and locks the rest.
JointType jointKind(const JointObject& j);
const char* jointKindWord(const JointObject& j);
bool setJointKind(JointObject& j, const std::string& word);
void setJointKind(JointObject& j, JointType kind);
// Turns (slides) entity a - and every link that hangs on it through joints, and their children -
// about frame b's axis of the freedom until its coordinate is `value` (clamped into its limits):
// forward kinematics, the joint as the editor's gizmo. Returns the value reached (0: a locked freedom).
Real poseJoint(SceneGraph& g, uint32_t joint, int dof, Real value);

// The joints as the viewer draws them (JointVis) at the poses the graph has: what an
// editor shows before the scene plays - the same glyphs as the running joints at angle 0.
std::vector<JointVis> jointVisuals(const SceneGraph& g);
// The sensors as authored, for a viewer before anything plays: each at its pose in the world, an
// encoder on a joint at the joint's frame (its x the joint's axis); a sonar's beam at its full range.
std::vector<SensorVis> sensorVisuals(const SceneGraph& g);
// A point wheel as authored, for a viewer before anything plays: its hub with the suspension fully
// extended and its frame (x forward, y up, z the axle) in the world; its tyre as a mesh - a cylinder
// about the mesh's y, turned onto the axle by wheelMeshTurn() on top of the frame.
void wheelPose(const SceneGraph& g, const WheelObject& w, Vector3& hub, Quaternion& frame);
std::shared_ptr<const TriMesh> wheelMesh(const WheelObject& w);
Quaternion wheelMeshTurn();

// A link put against a face (FaceSnap): its prototype (shape, size, components), its turn in its own
// frame on top of the snap's (a wheel: its axis y turned onto the face's normal), its place on the face
// - u, v along across[0], across[1] from the anchor [m] - and the joint that holds it: its kind, the
// axis (snapAxis), the drive and the limits of its main freedom.
struct LinkSpec {
    Entity prototype;
    Quaternion turn;
    Real u = 0, v = 0;
    JointType joint = JointType::Hinge;
    int axis = 2;
    JointDrive drive;                   // on the joint's main freedom: a spring, a velocity motor, a DC motor
    bool limited = false;
    Real lower = 0, upper = 0;
    bool flip = false;                  // the joint's axis reversed: a motor turns, a limit counts, the other way
};
// The link (a copy of the prototype: a fresh id, no parent, at rest, moving) flat against the face at
// its place, and the joint at that place. A joint needs a body on both sides: an entity made of nothing
// (geometry only) is made rigid, with its collider. Nothing is added when the snap found nothing.
struct Attached { uint32_t entity = 0, joint = 0; };
Attached attachLink(SceneGraph& g, uint32_t target, const FaceSnap& at, const LinkSpec& link);
// The simplest link: a copy of `prototype` centred on the face, joined about snapAxis(axis).
Attached attachToFace(SceneGraph& g, uint32_t target, BoxFace face, const Entity& prototype, JointType type, int axis);

// A sensor put on a body at a place found by a snap (SensorObject: a child of what carries it), named
// by its kind and number. A sonar looks out of the face: its x along the normal. An IMU lies flat on
// it as a chip on a board: its z out of the face, its x along the face's first direction across - on
// a top face at rest it reads +g on z, as an IMU on a table does. Returns its id (0: no place).
uint32_t attachSensor(SceneGraph& g, uint32_t target, const FaceSnap& at, SensorKind kind);
// An encoder on a joint's main freedom (rx of a hinge, tx of a slider), `lines` a turn (or a metre) - or
// on a point wheel (WheelObject), counting its turns.
uint32_t attachEncoder(SceneGraph& g, uint32_t joint, int lines = 500);
// A force sensor on a joint (SensorKind): a wrench - the force and torque it passes to its link a - or an
// effort - its drive's force along freedom `dof` (-1: the main one). Its id (0: no such joint).
uint32_t attachJointSensor(SceneGraph& g, uint32_t joint, SensorKind kind, int dof = -1);
// A joint's DC motor fed from a battery (AssemblyPresets.cpp): a driver at full duty, signed as the
// motor's own voltage, on the first freedom with a DC motor, from `battery` - 0: the scene's first,
// or a 3S pack of 18650 cells made when there is none - or a point wheel's motor (WheelObject). Returns
// the driver's id (0: no DC motor there).
uint32_t powerMotor(SceneGraph& g, uint32_t joint, uint32_t battery = 0);

// Ready links for building quickly (AssemblyPresets.cpp), in the order of the editor's pie menu -
// clockwise from the top; the places never change, so the hand learns them:
//   Hinge      - a copy of the target, a hinge about the polygon's normal (a robot's base, a door);
//   Elbow      - a copy, a hinge across the face (a knee, a shoulder);
//   Wheel      - a cylinder about the normal; MotorWheel - driven by an electric DC motor;
//   Shock      - a shock absorber: a slider with a spring and a damper along the direction nearest "up";
//   Car        - four shock absorbers on the face and the opposite one, a motor wheel on each;
//   Weld, Ball - a copy welded on, a copy on a ball joint.
enum class LinkPreset { Hinge, Elbow, Wheel, MotorWheel, Shock, Car, Weld, Ball, Rotary, Linear };
constexpr int kLinkPresets = 10;
// The magnet's own short list - an element per kind of motion, as scene_audit's invariants tell them apart: a turn (a
// hinge, an elbow and a wheel's axle are one rotary joint about another axis), a travel (a shock absorber is a slider
// whose servo is soft), a wheel (its own shape: a cylinder that rolls), a weld, a ball. Rotary and Linear carry a servo
// tuned to what they hold (assembly_actuator's); the motor of a wheel and the spring of a travel are choices of the
// joint's card. A car is a whole device - an asset, not a joint. The other words stay in the API (assembly_preset).
constexpr LinkPreset kMagnetPresets[] = {LinkPreset::Rotary, LinkPreset::Linear, LinkPreset::Wheel, LinkPreset::Weld, LinkPreset::Ball};
constexpr int kMagnetCount = 5;
LinkPreset magnetPreset(LinkPreset p); // the short list's element a preset is (a suggestion of the old list shown in the new)
const char* presetName(LinkPreset p);   // "Шарнир", "Колено", "Колесо"...
const char* presetWord(LinkPreset p);   // the API's word: "hinge", "elbow", "wheel"...
bool presetFromWord(const std::string& word, LinkPreset& p);
// How a preset is put: `axis` (0, 1, 2 as snapAxis; -1: the preset's own) turns a copied link's joint -
// the hinge of Hinge and Elbow, the slide of Shock (a wheel always turns about the normal); `flip`
// reverses the joint's axis; `mirror` puts the same link mirrored on the other side of the target too -
// across its middle plane parallel to the face - driven the mirrored way (Kerbal's symmetry: two legs,
// two wheels, two arms in one click). A car is mirrored already.
struct PresetOptions {
    int axis = -1;
    bool flip = false;
    bool mirror = false;
};
std::vector<Attached> applyPreset(SceneGraph& g, uint32_t target, const FaceSnap& at, LinkPreset p, const PresetOptions& o = {});
// The place mirrored across the target's middle plane parallel to the face (the opposite face of a box).
FaceSnap mirroredPlace(const SceneGraph& g, uint32_t target, const FaceSnap& at);
// Modules (AssemblyPresets.cpp): a working unit put on a body in one call - as a Prefab in Unity, a part in
// Kerbal, a component in Fusion - made of the same links, joints, sensors and electrics as by hand, so its
// physics is full. A module is placed by meaning: at a side of the body in the body's own frame - forward
// +x, up +y, left -z (ROS's x forward, y left, z up, turned to y up) - and its parts are named by that
// side ("Колесо спереди слева"), so a scene reads as what it is.
enum class Side { FrontLeft, FrontRight, RearLeft, RearRight, Left, Right };
constexpr int kSides = 6;
const char* sideName(Side s);  // "спереди слева" ... "справа"
bool sideFromWord(const std::string& word, Side& s);
struct WheelModuleOptions {
    bool suspension = true; // a shock absorber between the body and the wheel (else the wheel on the body)
    int encoderLines = 500; // a quadrature encoder on the wheel's axle (0: none)
    bool powered = true;    // a driver from the battery, at duty 0 - standing until told to go
    uint32_t battery = 0;   // 0: the scene's first, or a new 3S pack
    Real radius = 0;        // 0: 1.2 times the side's smaller half extent, as a car's
    bool point = false;     // a point wheel (WheelObject): the same spring, motor and masses, no bodies - fast
    Real rolling = 0.021_r; // a point wheel's rolling resistance (WheelObject::rolling): what the wheel of bodies'
                            // contacts lose on a hard floor, read from its motor's current
};
struct WheelModule { uint32_t wheel = 0, axle = 0, shock = 0, strut = 0, encoder = 0, driver = 0, battery = 0; };
// A wheel module at a side of `body` (a shape): a shock absorber carrying a quarter of its weight, a
// motor wheel whose positive duty drives the body forward (+x) and whose encoder counts forward as
// positive, a driver on the battery. Nothing (all 0) when `body` is no shape. A point module's wheel is
// its WheelObject (no axle, shock or strut).
WheelModule addWheelModule(SceneGraph& g, uint32_t body, Side side, const WheelModuleOptions& o = {});

// The preset most likely meant at this place - the editor's default for a plain click: on the side
// of something flat and wide (a chassis) a car - shock absorbers and motor wheels - while it carries
// nothing yet, then one motor wheel; anywhere else a hinge about the normal of the polygon under the mouse.
LinkPreset suggestPreset(const SceneGraph& g, uint32_t target, const FaceSnap& at);

} // namespace ke
