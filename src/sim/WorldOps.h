#pragma once
// What the operations of a running world answer, in the core without Qt: the five operations that read or push the
// world (sim_measure, sensor_read, robot_command, controller_input, sim_impulse), the live state sim_state and sim_step
// answer, the frames sim_step takes, and the commands a plugin's tick gives back. Functions over ke::Simulation that take
// and return ke::Value; the editor's window (editor/EditorOpsSim.cpp) calls them on the simulation's thread and adds what
// needs its own thread, mode and clock. The answers are the privileged loop's truth of the engine (the observability
// contract) except sensorReadings, which is what a robot reads.
#include "core/Value.h"
#include "sim/Simulation.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ke {

// One operation of the world: its help and arguments line and its kind as ops_list shows them, and its run. `world` null: the scene is
// not built - each operation says so in its own words. `firstController` is the scene's first controller (0: none), what
// controller_input takes when no id is given; the caller reads it from its graph so that run never does. False: `error`.
struct WorldOp {
    using Run = std::function<bool(Simulation* world, uint32_t firstController, const Value& args, Value& answer, std::string& error)>;
    std::string name, help, args, kind; // kind: "query" (reads) or "window" (the running world's own state: never the authored scene)
    Run run;
};

const std::vector<WorldOp>& worldOps();
const WorldOp* worldOp(const std::string& name); // null: not one of them

// A world built from the graph, the channels of the scene before gone with it. Stop loads an empty graph without this.
void startWorld(Simulation& s, const SceneGraph& g);

// sim_step's `frames`: 1 when absent or not a whole number that fits an int, else 0..6000.
int framesArg(const Value& args);

// The live state: the time, every body's pose and velocity, every joint, every sensor's reading with its truth, the
// contacts, triggers, grips and what the world could not build. {time, frame} alone when no world is built.
Value liveState(const Simulation& s);
// liveState and what sim_step did: the frames asked, the frames stepped, whether it stopped short.
Value stepAnswer(const Simulation& s, int requested, int completed);

// What every sensor reads as the robot reads it (a list): no truth. Also what a plugin's tick gets.
Value sensorReadings(const Simulation& s);
// The moving bodies as the engine has them (a map by entity id): a physics plugin's input.
Value bodyStates(const Simulation& s);
// {id: {target | velocity | duty: number} or {force, torque: [x, y, z]}} into the world; what went wrong, "; "-joined
// in the order of the keys as text (empty: all done).
std::string applyCommands(Simulation& s, const Value& commands);

} // namespace ke
