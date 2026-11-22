#pragma once
// A scene with no window: the scene document, its undo history, a physics world built from it when asked, and the
// operations that read, edit and run it - the same operations the editor's window answers (ops_list names them), with
// the same names, fields, answers and texts of errors, because what decides an answer lives once in the core (ArgsSchema,
// Document, LiveEdit, WorldOps, sceneOps). The window keeps what needs a clock, a thread, a screen or a person at the
// mouse; this host is single-threaded and answers in place. A C++ program links kecore and calls it; kinetica-core and
// Python's ke.Scene() put the same calls on a pipe.
//
// One thread per Scene. The sensor catalog and the probe's channels are the process's, so one Scene per process is the
// supported use (two share them).
//
// How it differs from the window, each with its reason (docs/contracts/operations.md, «Хозяева операций»):
//  - no sim_play and no sim_pause: there is no clock, time moves by sim_step;
//  - sim_state has no frame_timing: it is the renderer's and the wall clock's bookkeeping; step_ms stays and is the
//    machine's timing, not a result;
//  - nothing is selected: an operation that acts on objects gets the `ids` it is given, and refuses with its own words
//    when there are none; a light with no `at` and a camera with no `eye` take the centre of the scene;
//  - a relative path is relative to the folder the process was started in;
//  - a change of a joint's shape while the world is built stops the world at once and answers play_stopped with reload
//    "joints"; the window answers until_stop and stops a frame later;
//  - files are read and written here with the Russian texts of errors below; the window's are Qt's;
//  - there is no journal: a script is its own record (docs/constitution.md, Art. II.6).
#include "core/Value.h"
#include "scene/Document.h"
#include "scene/LiveEdit.h"
#include "scene/SceneOps.h"
#include "sim/Simulation.h"
#include "sim/WorldOps.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ke {

// The protocol's version, in ops_list's answer (protocol_version): what a client built for it may call. It grows by one
// with each release whose names grew - names only grow (docs/contracts/names.md) - so a client built for N works with
// every program of N and later; a program before the field is 0.
constexpr int kProtocolVersion = 1;

// An operation of the host itself, with its help, arguments and kind as ops_list shows them: written once, here; the
// window registers its own handlers under these lines.
struct HostOp {
    std::string name, help, args, kind;
};

const std::vector<HostOp>& hostOps();
const HostOp* hostOp(const std::string& name); // null: not one of them
bool editsGraph(const std::string& name); // an operation that changes the graph (the world built from it must classify the change)

// What ops_list answers for one operation, and whether a name is in the `families` asked for (a list of words, each the
// text before the first '_' of the names wanted; none: every name).
Value opsListEntry(const std::string& name, const std::string& help, const std::string& args, const std::string& kind, const Value& schema);
bool inFamilies(const std::string& name, const Value& families);

// What an edit that the world still running took only until Stop says in words.
const char* untilStopNote();

class Scene {
public:
    // The sensor tables are read from <dataDir>/sensors/*.csv; "" is no tables, on purpose. A folder that is not there
    // throws std::runtime_error.
    explicit Scene(const std::string& dataDir);

    // The answer of an operation: its keys and "ok": true, or {"ok": false, "error": ...}. Never throws: an exception
    // inside an operation is an answer, and the graph is as it was.
    Value call(const std::string& op, const Value& args = Value::map());

    PlayMode mode() const { return doc_.mode; }
    const SceneGraph& graph() const { return doc_.graph; }
    const Simulation* simulation() const { return doc_.mode == PlayMode::Edit ? nullptr : sim_.get(); } // null unless built

private:
    struct Op {
        const SceneOp* core = nullptr;
        const WorldOp* world = nullptr;
        std::string help, args, kind;
        Value schema; // made at the first call: null until then
    };
    Value run(const std::string& name, const Value& args);
    bool hostCall(const std::string& name, const Value& args, Value& answer, std::string& error);
    LiveEdit::Reload takeInWorld();
    void stopWorld(bool keepEdits);
    const Value& schemaOf(Op& op);

    Document doc_;
    std::unique_ptr<Simulation> sim_;
    std::map<std::string, Op> ops_;
};

} // namespace ke
