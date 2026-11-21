#pragma once
// The sensors of the running world, each composed of independent parts (docs/contracts/sensors.md) - no hierarchy of
// kinds of sensors:
//   - a probe of the physics (SensorProbe): what the sensor touches in the world and the true quantity there, through
//     the PhysicsWorld contract and the robot world's joints only - an inertial point, an echo, a joint's coordinate,
//     reaction or drive, a frame against another, the contacts through a face;
//   - a device of its datasheet (SensorDevice over src/devices): the truth on its own clock into what a robot reads -
//     counts of a word, a pulse's distance, a sample with its noise and offsets.
// The kind is the recipe that picks the two (makeSensor); the model is the datasheet's numbers (scene/SensorSpec.h).
// A robot's control reads `reading`; `truth`, the same quantities as the physics has them, is the privileged loop's -
// plots, checks, a laboratory - never a robot's input.
#include "physics/PhysicsWorld.h"
#include "scene/SceneGraph.h"

#include <memory>
#include <string>

namespace ke {

class RobotWorld;

// The quantities of a kind. An IMU: the specific force (a) and the angular rate (b) in its axes, m/s^2, rad/s. A wrench:
// the force (a) and torque (b) its joint passes to the link, in the joint's frame on the link, N, N m. A sonar's
// distance (m), an encoder's coordinate (rad or m), a touch pad's force (N), an effort's force or torque: value. An
// encoder's steps, a touch pad's points of contact: count.
struct SensorValues {
    Vector3 a{0.0_r}, b{0.0_r};
    Real value = 0;
    int64_t count = 0;
};

struct SensorReading : SensorValues {
    int64_t sampleNs = -1;   // when it was taken, the world's clock (-1: none yet)
    bool saturated = false;  // at its full scale; a sonar with no echo in range reads -1
    bool referenced = true;  // an encoder: its count is a coordinate only with a known zero (SensorObject::encoderReference)
    std::string problem;     // why it reads nothing ("": it reads)
};

// The physics side: after each substep h of the world, the truth now - false, and why, when there is nothing to read.
class SensorProbe {
public:
    virtual ~SensorProbe() = default;
    virtual bool sense(const PhysicsWorld& physics, const RobotWorld& robot, Real h, SensorValues& truth, std::string& problem) = 0;
};

// The datasheet side: its clock on to `untilNs`, the truth holding since it last ran, into what a robot reads.
class SensorDevice {
public:
    virtual ~SensorDevice() = default;
    virtual void run(int64_t untilNs, const SensorValues& truth, SensorReading& out) = 0;
};

class LiveSensor {
public:
    LiveSensor(const SensorObject& spec, std::unique_ptr<SensorProbe> probe, std::unique_ptr<SensorDevice> device)
        : spec_(spec), probe_(std::move(probe)), device_(std::move(device)) {}
    void step(const PhysicsWorld& physics, const RobotWorld& robot, Real h, int64_t untilNs);
    const SensorObject& spec() const { return spec_; }
    const SensorReading& reading() const { return reading_; }
    const SensorValues& truth() const { return truth_; }

private:
    SensorObject spec_;
    std::unique_ptr<SensorProbe> probe_;
    std::unique_ptr<SensorDevice> device_;
    SensorValues truth_;
    SensorReading reading_;
};

// The recipe of a scene's sensor in a built world (null, and why: none yet - a stereo camera's frame is taken on request).
std::unique_ptr<LiveSensor> makeSensor(const SensorObject& s, const SceneGraph& g, const RobotWorld& robot, std::string& why);

} // namespace ke
