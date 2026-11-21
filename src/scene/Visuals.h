#pragma once
// What a viewer draws of a robot besides its bodies: the joints as gizmos, the sensors as glyphs, the
// IK goals on their paths. The graph gives them at its own poses while it is edited (Assembly.h:
// jointVisuals, sensorVisuals); the running world at the poses its bodies have now (src/sim).
#include "math/Math.h"
#include "scene/JointModel.h"

#include <cstdint>

namespace ke {

struct JointVis {
    uint32_t id = 0;         // the joint's own (0: a ghost of one not made yet)
    JointType type = JointType::Hinge;
    Vector3 anchorA, anchorB, axis;
    // What it ties, for the tie drawn from each body to the joint: the two bodies' own places. `grounded`: b is the
    // world, and bodyB is not used.
    uint32_t a = 0, b = 0;
    Vector3 bodyA, bodyB;
    bool grounded = false;
    Vector3 reference{1.0_r, 0.0_r, 0.0_r};
    Real angle = 0;          // hinge [rad]
    bool servo = false;      // its main freedom driven by a servo: then `target` is where it is sent [rad or m]
    Real target = 0;
    bool limited = false;
    Real lower = 0, upper = 0;
    Real size = 0.05_r;
    // The generic joint as a gizmo (JointModel.h): both frames in the world, and per freedom (travel
    // x y z, turn x y z of frame b) its motion, limits, coordinate now and whether driven.
    bool generic = false;
    Quaternion frameA{}, frameB{};
    JointMotion motion[6] = {JointMotion::Locked, JointMotion::Locked, JointMotion::Locked,
                             JointMotion::Locked, JointMotion::Locked, JointMotion::Locked};
    Real lowers[6] = {0, 0, 0, 0, 0, 0}, uppers[6] = {0, 0, 0, 0, 0, 0}, coordinates[6] = {0, 0, 0, 0, 0, 0};
    bool driven[6] = {false, false, false, false, false, false};
};

// The sensors (SensorObject) as glyphs: an encoder's ring about its axis, an IMU's chip and axes, a
// sonar's beam as long as its echo - where they are now, and what they read.
struct SensorVis {
    uint32_t id = 0;
    int kind = 0;               // SensorKind: 0 encoder, 1 IMU, 2 sonar, 3 touch, 4 wrench, 5 effort, 6 stereo
    Vector3 position{0.0_r};
    Quaternion rotation{};      // the sensor's frame: its x the measured axis or the beam
    Real reading = 0;           // an encoder's turn [rad] or travel [m]; a sonar's distance (-1: no echo)
    Real size = 0.03_r;         // the glyph's scale [m]
    Real fovDeg = 0, baseline = 0, aspect = 0; // a stereo camera: its horizontal field of view, the distance between its lenses, height / width
};

// An IK goal that walks a path (Entity::path), where the world has taken it now.
struct GoalVis {
    uint32_t id = 0;
    Vector3 position{0.0_r};
    Quaternion rotation{};
};

} // namespace ke
