#pragma once
// One frame of the running world as a viewer draws it: the bodies at their poses with their meshes, the
// joints, sensors and IK goals as glyphs, the contacts, the lights, the probe's channels and drawings,
// and the measured channels.
// Immutable once filled (Simulation::fill): the simulation thread publishes it, the window reads it.
#include "core/Mesh.h"
#include "core/Probe.h"
#include "core/SurfaceTexture.h"
#include "math/Math.h"
#include "scene/Visuals.h"
#include "sim/Channels.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ke {

struct Frame {
    uint64_t frame = 0;
    Real time = 0;
    Real stepMs = 0;      // what the last step took on the simulation thread
    AABB domain;          // the world's box: the floor at its bottom
    std::string engine;   // "Box3D 0.2.0"
    std::string sceneName; // the scene's file, as the window names it

    // A body of the scene, or an entity that is only drawn (no role: geometry; an IK goal: a marker).
    struct Body {
        uint32_t object = 0;                 // the scene's object it is (an entity, an array's copy)
        int body = -1;                       // the physics engine's body (-1: only drawn)
        Vector3 pos{0.0_r};
        Quaternion rot;
        std::shared_ptr<const TriMesh> mesh; // in the entity's own frame
        std::shared_ptr<const TriMesh> collider; // its collider as a mesh in the same frame (null: none)
        Vector3 color{0.8_r};
        SurfaceTexture texture{};
        Real opacity = 1;                    // < 1: see-through, drawn blended over the rest
        bool smooth = false;                 // drawn with smooth normals (a sphere), else faceted
        bool sleeping = false;
        bool movable = true;                 // a dynamic body the mouse may grab
        bool marker = false;                 // an editor's mark (an IK goal: intent), not matter: no sensor sees it
        bool robot = false;                  // a link of a robot (bodies tied by joints), not a thing lying about
    };
    std::vector<Body> bodies;
    std::vector<JointVis> joints;
    std::vector<SensorVis> sensors;
    std::vector<GoalVis> goals;

    struct ContactInfo {
        Vector3 position{0.0_r}, normal{0.0_r}; // normal: from body a to body b
        Real depth = 0, normalImpulse = 0;      // m; N s over the step
        int bodyA = -1, bodyB = -1;
    };
    std::vector<ContactInfo> contacts; // listed when they are read (Simulation::frameContacts, the contact-points layer)

    struct LightInfo {
        int kind = 1;                        // LightKind: 0 Sun, 1 Point, 2 Spot
        Vector3 position{0.0_r}, direction{0, -1, 0};
        Vector3 color{1.0_r};
        Real intensity = 1, range = 5, coneDeg = 40, softnessDeg = 10;
        bool shadows = false;
    };
    std::vector<LightInfo> lights;

    // The cameras the running world itself moves, where it has them: a key of the playing sequence or a moving body,
    // on the camera, on its target or up their parents. A camera not listed stands where the scene has it.
    struct CameraPose {
        uint32_t id = 0;
        Vector3 position{0.0_r};
        Quaternion rotation; // its world turn: a target camera's already looks at its target
    };
    std::vector<CameraPose> cameras;

    // A gripper on an IK goal's effector as the gripper itself reports it (Robotiq's gOBJ: from its fingers' own
    // travel, not from the physics' contacts): open, closing, holding - its fingers stopped short of closed, so
    // something is between them - or closed on nothing.
    enum class GripState { Open, Closing, Holding, Empty };
    struct GripInfo {
        uint32_t goal = 0;
        GripState state = GripState::Open;
        Real gap = 0; // how far its fingers stand from closed [m]
    };
    std::vector<GripInfo> grips;
    std::vector<std::pair<std::string, std::string>> info; // name, value: what the scene says about itself
    Probe::Snapshot probe;
    std::vector<Measurement> measurements; // the channels the simulation was asked to measure (Channels.h)
};

} // namespace ke
