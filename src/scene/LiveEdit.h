#pragma once
// What a world that is already built takes from an edited scene, and what it cannot: the classifier both hosts ask
// after an edit of a playing scene (the window's SceneBuilder, the windowless host's Scene). It compares the graph the
// world was built from with the edited one and names the first section that needs a rebuild, or the parts a running
// RobotWorld takes in place (setDuties, setLightsAndCameras, setGoals, setJoints). No texts for people: reloadWord is
// the API's word; each host says it in its own language.
#include "scene/SceneGraph.h"

namespace ke {

struct LiveEdit {
    // Joints is never returned by liveEdit: whether a joint's new shape can be taken is known only when
    // RobotWorld::setJoints refuses it, so the caller of setJoints reports it.
    enum class Reload { None, World, Groups, Arrays, Devices, Bodies, Joints } reload = Reload::None;
    bool duties = false, lights = false, goals = false, joints = false; // what to hand to the running world
};

LiveEdit liveEdit(const SceneGraph& running, const SceneGraph& edited);
const char* reloadWord(LiveEdit::Reload r); // "world" | "groups" | "arrays" | "devices" | "bodies" | "joints"; None: ""

} // namespace ke
