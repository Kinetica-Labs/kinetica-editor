#pragma once
// Channels: the physical quantities the plots draw - of the whole scene, of one body, of a group of
// bodies, of a joint and of an IK goal. A channel is a number with a name card: a stable ASCII id
// ("rigid/kinetic energy", the Probe key and the CSV column), a unit ("Дж") and a name for people
// ("Твёрдые тела: энергия движения").
//
// Nothing is measured unless asked. Simulation::channels says what to measure after every frame; while
// it is empty a frame does not touch any of this, and measuring never changes the world (it only reads
// the physics engine's states and the robot's joints):
//
//   sim.channels.scene = true;                                  the scene's energy, momentum ...
//   sim.channels.objects.push_back({42, "Куб"});                entity 42's body: height, speed, contacts ...
//   sim.channels.joints.push_back({7, "Плечо"});                joint 7: each freedom's coordinate, target, effort
//   sim.stepFrame();
//   for (const Measurement& m : sim.measurements()) ...         every value with its unit
//
// These are the privileged loop's numbers (the observability contract): true poses and speeds of the
// engine, for plots and verification - never a robot's input. Its sensors read through src/devices.
#include "math/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ke {

class Simulation;

// Where a channel belongs in a viewer's list of channels.
enum class ChannelGroup { Scene, Object, Group };

// The name card of a channel.
struct ChannelInfo {
    std::string id;   // ASCII "part/quantity": the Probe key and the CSV column
    std::string unit; // "Дж", "м/с", "Н", ... (UTF-8)
    std::string name; // for people, in Russian: «Твёрдые тела: энергия движения»
    ChannelGroup group = ChannelGroup::Scene;
};

struct Measurement {
    ChannelInfo info;
    double value = 0;
};

// A body of the scene by the entity it is (Entity::id; an array's copy by its copy id). Its label prefixes
// the channel ids: "Куб" -> "Куб/speed".
struct ObjectRef {
    uint32_t entity = 0;
    std::string label;
};

// Several bodies measured as one: their total mass, centre of mass, momentum and energy.
struct GroupRef {
    std::string label;
    std::vector<ObjectRef> members;
};

// A joint by its scene id: every freedom that is not locked - its coordinate, a servo's target, the
// drive's effort. "Плечо/rz coordinate".
struct JointChannelRef {
    uint32_t joint = 0;
    std::string label;
};

// An IK goal (Entity::goalOf) by its scene id: how far its effector is from it now - the live miss, the
// solver's residual and the servos' lag together.
struct GoalChannelRef {
    uint32_t goal = 0;
    std::string label;
};

// What to measure after every frame. Empty: nothing is measured.
struct ChannelRequests {
    bool scene = false;
    std::vector<ObjectRef> objects;
    std::vector<GroupRef> groups;
    std::vector<JointChannelRef> joints;
    std::vector<GoalChannelRef> goals;
    bool empty() const { return !scene && objects.empty() && groups.empty() && joints.empty() && goals.empty(); }
};

// The scene: energy (of motion, of rotation, of height above the floor), momentum and angular momentum of
// the moving bodies, the deepest contact. A static body neither moves nor stores energy.
std::vector<Measurement> measureScene(const Simulation& sim);
// One body: mass, centre of mass, height, velocity, momentum, energy; its spin, sleep and the normal force
// of its contacts (the impulses of the last step over its length; a sleeping body holds its weight).
std::vector<Measurement> measureObject(const Simulation& sim, const ObjectRef& object);
std::vector<Measurement> measureGroup(const Simulation& sim, const GroupRef& group);
std::vector<Measurement> measureJoint(const Simulation& sim, const JointChannelRef& joint);
std::vector<Measurement> measureGoal(const Simulation& sim, const GoalChannelRef& goal);
// Everything `requests` asks for, in order: scene, objects, groups, joints, goals.
std::vector<Measurement> measureAll(const Simulation& sim, const ChannelRequests& requests);

// The name cards of the scene channels (a viewer's list before anything was measured), and the card of
// any scene channel by its id (nullptr: not one of them).
const std::vector<ChannelInfo>& sceneChannelCatalog();
const ChannelInfo* describeChannel(const std::string& id);

} // namespace ke
