#pragma once
// The operations of the API's core: every change of a scene graph and every question about it that a
// client may ask, with its name, a line of help, its arguments and its answer as JSON-shaped values
// (core/Value.h). The editor registers each of them as its own operation (undo, journal, console),
// and serves them to scripts and agents over its API - so one name means one thing everywhere, as
// Blender's operators do for its window and its scripts. What only a client has stays with the client: time (play, step), the undo history, files,
// and the window's own state (the selection, the gizmo, the view).
//
// An operation that acts on objects takes them as `ids`: the core never knows a selection, and a
// client with one fills `ids` from it when they are missing (takesIds). An action answers {"id", "ids"}
// with what a window would select after it, and {"note"} with what it switched on besides, in words.
#include "core/Value.h"
#include "scene/SceneGraph.h"

#include <functional>
#include <string>
#include <vector>

namespace ke {

struct SceneOp {
    using Query = std::function<bool(const SceneGraph& g, const Value& args, Value& answer, std::string& error)>;
    using Edit = std::function<bool(SceneGraph& g, const Value& args, Value& answer, std::string& error)>;
    std::string name, help, args; // args: the arguments as a line of help, "{target: int, face?: '+x'..'-z'}"
    Query query;                  // a question: reads the graph only
    Edit edit;                    // a change: the client's one undo step (an error leaves it to undo it)
    bool takesIds = false;        // acts on `ids`: a client fills them from its selection when missing
};

const std::vector<SceneOp>& sceneOps();
const SceneOp* findSceneOp(const std::string& name);

// The pieces of the list (SceneOps.cpp, SceneOpsActions.cpp, SceneOpsAssembly.cpp...).
std::vector<SceneOp> coreSceneOps();       // list, get, put, set, remove, the world, a batch
std::vector<SceneOp> actionSceneOps();     // add, delete, duplicate, group, roles, move, turn, scale
std::vector<SceneOp> assemblySceneOps();   // a face's place, links, ready presets, joints, sensors, power
std::vector<SceneOp> kinematicSceneOps();  // inverse kinematics: an effector to a point with a turn
std::vector<SceneOp> controllerSceneOps(); // a controller: added as a ready panel for an arm (SceneOpsControllers.cpp)

// Arguments and answers as the operations read and write them.
bool vectorArg(const Value& args, const char* key, Vector3& out); // [x, y, z]
std::vector<uint32_t> idsArg(const Value& args);                   // the list `ids`
Value vectorValue(const Vector3& v);
Value idsValue(const std::vector<uint32_t>& ids);
Value selected(const std::vector<uint32_t>& ids, uint32_t active); // {"ids": ..., "id": active}

} // namespace ke
