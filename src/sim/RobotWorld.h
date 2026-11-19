#pragma once
// The running world of a robot: the scene graph (src/scene) built into the physics engine
// (src/physics) - every visible entity with a role a body of its collider's shapes, every joint the
// engine's joint of its kind - and the robot's own code that the engine does not have: a servo's
// spring and a DC motor's torque on each joint, computed at every substep from the joint's coordinate
// (JointModel.h) and put on its two links as a pair of torques or forces. A velocity motor is the
// engine's own.
//
// Three layers, as in the scene: the source (the entity's shape), the object (its pose and roles), and
// what the engine made of it (a body, a joint). The graph as authored stays as it was; `live()` is the
// same graph with every body's pose now - what the joints' and sensors' glyphs are drawn from.
#include "physics/PhysicsWorld.h"
#include "scene/SceneGraph.h"
#include "sim/Frame.h"
#include "sim/LiveSensors.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ke {

// The visible lights of a graph, posed in the world, into out.lights: the running world's frame and an
// editor's view of the graph while it is edited are lit alike.
void describeLights(const SceneGraph& g, Frame& out);

// The collider of an entity as the engine's shapes in the entity's frame (Auto when it has none).
std::vector<ShapeDesc> colliderShapes(const Entity& e, const TriMesh& localMesh);

class RobotWorld {
public:
    explicit RobotWorld(SceneGraph graph);
    // The bodies and joints in `physics`; what could not be built is said in notes().
    void build(PhysicsWorld& physics);
    // One step of dt in `substeps` substeps: before each, the drives' forces on the links.
    // A take - the Sequencer's record of a run (Unreal's Take Recorder): from now, every free body's pose on its parent
    // a frame (a dynamic body of no robot, no joint and no sequence); taken, a sequence of it - each body a track, its
    // samples thinned to the keys that give the motion back within 0.5 mm and 0.2 degrees, the first at 0 s - and the
    // recording ends. Bodies that never moved get no track.
    void startRecording(const PhysicsWorld& physics);
    bool recording() const { return recording_; }
    SequenceObject takeRecording(const std::string& name);
    void step(PhysicsWorld& physics, Real dt, int substeps);
    void fill(const PhysicsWorld& physics, Frame& out) const;
    // The probe's debug layers that are on (core/Probe.h: contacts, centres of mass, inertia axes,
    // velocities, sleep, joint frames, the bodies' boxes), drawn from the engine's states - what a
    // laboratory of the editor shows. Nothing when no layer is on.
    void drawLayers(const PhysicsWorld& physics, Real substepDt) const;

    const SceneGraph& graph() const { return graph_; }
    const SceneGraph& live() const { return live_; }
    uint32_t objectIdOfBody(int body) const; // the entity a body is (0: none)
    int bodyOf(uint32_t entity) const;       // -1: none
    // A joint's six coordinates now (JointModel.h: frame a against frame b) from its links' poses in the
    // engine; false when it was not built. The truth of the engine, for plots and checks - a robot reads
    // its joints through encoders.
    bool jointCoordinates(const PhysicsWorld& physics, uint32_t joint, Real x[6]) const;
    // What the drive of its main freedom put out at the last substep: a servo's or a DC motor's force
    // (N m or N, our code), a velocity motor's (the engine's). 0: no drive.
    Real jointEffort(const PhysicsWorld& physics, uint32_t joint) const;
    const JointObject* jointSpec(uint32_t joint) const; // the joint as the world runs it (null: none)
    // The load a joint passes to its link a over the last substep, in the joint's frame on a about its anchor: N, N m.
    bool jointWrench(const PhysicsWorld& physics, uint32_t joint, Vector3& force, Vector3& torque) const;
    // The scene's sensors as the world runs them (LiveSensors.h), sampled after every substep on the world's clock:
    // what a robot reads, and the truth beside it for the privileged loop. Null: none (a stereo camera's is on request).
    const LiveSensor* sensor(uint32_t id) const;
    const std::vector<std::unique_ptr<LiveSensor>>& sensors() const { return sensors_; }
    int64_t clockNs() const { return clockNs_; } // the world's clock, whole nanoseconds
    // The joints edited while the world runs (an IK goal dragged, a servo's target typed): a joint built as
    // before takes its new drive - target, gains, motor - at once. False, and nothing changed, when the
    // joints are no longer the same (one added or removed, its kind, links, frames, limits or motion
    // changed): the world has to be built again.
    bool setJoints(PhysicsWorld& physics, const std::vector<JointObject>& joints);
    // The lights and cameras edited while the world runs: the physics never sees them, the next frame shows them.
    void setLightsAndCameras(const std::vector<Light>& lights, const std::vector<Camera>& cameras) {
        graph_.lights = live_.lights = lights;
        graph_.cameras = live_.cameras = cameras;
    }
    // The IK goals (Entity::goalOf) as they are now: made, moved or removed while the world runs. A goal is
    // intent, not matter - no body of the engine: the frame draws it as a mark, the goal channels read it.
    void setGoals(const std::vector<Entity>& goals);
    // While the world runs an IK goal leads its effector in a straight line, as a robot controller's linear move
    // (UR's movel): a carried pose walks from where the effector stands toward the goal at kToolSpeed and
    // kToolTurn at most, and every frame the chain's servos are sent the coordinates for it, solved from the links
    // as they stand. A jump of the targets to the goal's solution would swing the heavy links the shortest way of
    // the joints - a wide arc through whatever stands near.
    static constexpr Real kToolSpeed = 0.4_r, kToolTurn = 1.5_r; // m/s, rad/s
    // A servo's setpoint relaxes to its target, as a robot controller's motion profile: it walks there at the joint's
    // top speed at most (UR5e's 180 deg/s; a gripper's 0.15 m/s) and eases in over kServoSettle. Pushed off by more
    // than the spring answers at full force (maxForce / k), it yields to stay that far - a slip, not a wound-up
    // spring: let go, the joint comes back along the profile. Without it a servo whose damper alone eats its whole
    // force at a fifth of a rad/s ran full force one way, then the other - the arm bounced at 4.6 Hz, the wrists whipped.
    static constexpr Real kServoTurnSpeed = kPi, kServoTravelSpeed = 0.15_r, kServoSettle = 0.2_r; // rad/s, m/s, s
    // The mouse's hand (an editor's grab): body `body` held at `point` (world, on the body) by the engine's mouse
    // joint (PhysicsWorld::addHand, Box3D samples' tuning: 7.5 Hz, critically damped, at most 100 weights, its
    // spin braked) solved with the contacts; its weight carried at the held point, so it hangs at the cursor, not
    // g / w^2 = 4.4 mm under it. A robot's link is pulled as the whole robot: the spring as stiff and as strong as
    // for the robot's mass (Box3D scales a spring by the held body's mass - by a light finger the arm's motors
    // would hardly feel it). The target moves with setGrabTarget. A poke of the privileged loop, not a robot's
    // actuator.
    void grab(PhysicsWorld& physics, int body, const Vector3& point);
    void setGrabTarget(const Vector3& target) { grabTarget_ = target; }
    void releaseGrab(PhysicsWorld& physics);
    int grabbed() const { return grabBody_; }
    const std::vector<std::string>& notes() const { return notes_; }

private:
    struct LiveJoint {
        JointObject spec;
        int joint = -1, a = -1, b = -1; // the engine's joint and bodies (b -1: the world)
        int dof = -1;                   // its main freedom
        bool ownDrive = false;          // a servo or a DC motor: our code drives it
        mutable Real effort = 0;        // what our drive put out at the last substep
        mutable Real setpoint = std::numeric_limits<Real>::quiet_NaN(); // a servo's, on its way to the target (kServoSettle)
    };
    void setDrive(PhysicsWorld& physics, LiveJoint& live, const JointObject& j);
    bool coordinates(const PhysicsWorld& physics, const LiveJoint& j, Real c[6]) const;
    void buildJoint(PhysicsWorld& physics, const JointObject& j);
    void drive(PhysicsWorld& physics, const LiveJoint& j, Real h) const; // h: the substep
    void readPoses(const PhysicsWorld& physics);
    void leadGoals(PhysicsWorld& physics, Real dt);
    static std::unordered_set<uint32_t> animatedEntities(const SceneGraph& g);
    void sampleTake(const PhysicsWorld& physics);
    void leadAnimation(PhysicsWorld& physics, Real time, Real h);
    std::shared_ptr<const TriMesh> meshOf(const Entity& e) const;

    SceneGraph graph_, live_;
    std::vector<Entity> flat_;                       // worldEntities(graph_): world poses, instances, array copies
    std::unordered_map<uint32_t, int> bodyOfEntity_; // entity id -> body
    std::vector<uint32_t> entityOfBody_;             // body -> entity id (0: none)
    std::vector<LiveJoint> joints_;
    std::vector<std::unique_ptr<LiveSensor>> sensors_;
    SceneGraph posed_;                                 // the graph at the clip's time (Animation.h)
    std::vector<std::pair<uint32_t, int>> animated_;   // the clip's entities and their kinematic bodies
    std::vector<uint32_t> free_;                       // the entities on free dynamic bodies: what a take records
    bool recording_ = false;
    int64_t takeFromNs_ = 0;
    std::vector<std::vector<AnimKey>> take_;           // a sample a frame, an entity of free_ each
    int64_t clockNs_ = 0;
    std::unordered_map<uint32_t, int> robotOf_;      // entities tied by joints - robots' links - and their robot (robotGroups)
    std::unordered_map<uint32_t, std::pair<Vector3, Quaternion>> carried_; // goal id -> the pose its effector is led to now
    std::vector<std::string> notes_;
    int grabBody_ = -1, grabJoint_ = -1;                    // the held body and the hand's joint
    Vector3 grabLocal_{0.0_r}, grabTarget_{0.0_r};          // the held point in the body's frame; where it is led
    void carryGrab(PhysicsWorld& physics) const;
    mutable std::map<std::string, std::shared_ptr<const TriMesh>> meshes_; // by entityGeometryKey
};

} // namespace ke
