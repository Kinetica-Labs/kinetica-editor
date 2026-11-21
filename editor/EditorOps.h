#pragma once
// The editor's API: the operator registry. Every change of the scene and every action is an
// operation with a name, a line of help, its arguments and its result as JSON - as Blender's
// operators and FreeCAD's commands: a button, the magnet's click, a Python script and an agent
// over the network (EditorServer) call the same operations, so whatever one can do the others can.
//
// The core is small and every edit in it has an inverse (ke::SceneEdits): get, put, set, remove a
// block of the scene file, and a batch of them as one undo step. Around it: queries (the list of
// objects, the face a point is on), compositions (attach a link to a face with a joint), time
// (play, pause, stop, step N frames, the live state, a push on a body), the view (screenshot, frame)
// and the resource lease of an agent's session (EditorOpsRuntime.cpp). An answer is
// {"ok": true, ...} or {"ok": false, "error": "..."}; an operation that waits for the simulation
// answers when the simulation thread is done. Every call that changes something, from the window or
// from outside, is written to an append-only journal (JSON lines, as Blender's Info and Maya's script
// editor echo each action): a session can be read back, replayed, and an agent sees what a hand did.
#include "core/RuntimePolicy.h"
#include "core/Value.h"
#include "math/Quaternion.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <deque>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <vector>

class QWidget;
class SceneBuilder;
namespace ke { struct SceneGraph; struct SceneOp; }

class SimController;
class Viewport;
class EditorRuntime;

QJsonObject coreAnswerJson(const ke::Value& answer); // a core answer as the API's: its keys and ok true (EditorOpsCore.cpp)
ke::Value coreValue(const QJsonValue& v);            // a JSON value as the core's arguments take it
QJsonValue coreJson(const ke::Value& v);             // a core value as the API's JSON (ops_list's schema, the self-test group wire-parity)

class EditorOps : public QObject {
public:
    using Reply = std::function<void(const QJsonObject&)>;
    using Handler = std::function<void(const QJsonObject& args, const Reply& reply)>;

    EditorOps(SceneBuilder* builder, Viewport* view, SimController* ctrl, QWidget* window);

    // `source` says who called: "ui" (a button, the magnet) or "api" (the server). Every operation that
    // changes something - not the queries - leaves a line in the journal (EditorJournal.cpp).
    void run(const QString& name, const QJsonObject& args, const Reply& reply, const QString& source = "api");
    // An operation that answers at once (every edit and query), called by the user interface: its answer.
    QJsonObject runNow(const QString& name, const QJsonObject& args);
    QJsonArray list(const ke::Value& families = ke::Value()) const; // name, help, args, schema, kind of every operation (of these families only)
    // What brings the window up to the simulation's newest frame (MainWindow sets it).
    void setRefresh(std::function<void()> refresh) { refresh_ = std::move(refresh); }
    bool recording() const { return recording_; } // a take runs (sim_record)
    void onRecordingChanged(std::function<void()> f) { recordingChanged_ = std::move(f); } // the window's ● follows it
    // Who sees every call as it is answered - the console: who called, the operation, its arguments, its
    // answer, and whether it was a query (it changed nothing).
    using Listener = std::function<void(const QString& source, const QString& name, const QJsonObject& args, const QJsonObject& answer, bool query)>;
    // A call the window made without running it here (an edit by hand, as SceneBuilder's code): journaled
    // and shown as every other call.
    void report(const QString& source, const QString& name, const QJsonObject& args);
    void addListener(Listener l) { listeners_.push_back(std::move(l)); }
    // A plugin's commands (PluginHost): added while it runs, gone when it stops. A command's call is not a line of
    // the journal - the edits it makes through the API are, and they make one undo step.
    // `schema`: its arguments as JSON Schema (ops_list gives it; MCP makes it the tool's inputSchema).
    void addPluginOp(const QString& name, const QString& help, const QString& args, Handler run, const QJsonObject& schema = {});
    void addOp(const QString& name, const QString& help, const QString& args, Handler run) { add(name, help, args, std::move(run)); } // the plugins' own
    void removePluginOp(const QString& name);
    // Why a name is no operation now, when someone knows (a plugin's command while the plugin is down: PluginHost).
    void setUnknownHint(std::function<QString(const QString&)> hint) { unknownHint_ = std::move(hint); }
    static bool isQuery(const QString& name); // EditorJournal.cpp
    static bool windowOnly(const QString& name); // the window's state or time, no change of the scene (EditorJournal.cpp)
    // What an operation is to the one who calls it (ops_list says it; an agent's client marks its tools by it):
    //   "query"  reads - nothing a person would see changes;
    //   "edit"   changes the scene: one undo step takes it back;
    //   "window" the window's own state and time - the selection, the gizmo, the view and what is drawn over it, play,
    //            pause, a step, a hand's input -, never the scene as it is authored;
    //   "action" the rest: a file written, the journal's way back, the program stopped - and a plugin's command, which
    //            may do any of these.
    QString kind(const QString& name) const;
    // A plugin process's own wiring (plugin_register, _wait, _reply, _panel_set with its token): no one's action - out of
    // ops_list, the console's log and the journal, as MCP leaves it out of its tools (a person saw ed.plugin_register(…)).
    static bool wiring(const QString& name) {
        return name == "plugin_register" || name == "plugin_wait" || name == "plugin_reply" || name == "plugin_panel_set";
    }

    // The journal as a way back (EditorJournal.cpp), as a database's log with checkpoints: every line of a
    // session carries its id; a snapshot - the whole scene, the next id to give - starts the session and
    // follows what cannot be replayed from its arguments (a file opened, a new scene, an undo, a stop after
    // play, an unknown change) and every so many core edits. The scene of any moment of any session is its
    // last snapshot before that moment with the core edits after it replayed (they are deterministic). A
    // session that did not end and whose lock no process holds crashed: the window offers it back.
    void startSession();       // its line, its lock, a snapshot of the scene as it is now
    void endSession();         // the window closes cleanly: its last line
    void setSnapshotEvery(int edits) { snapshotEvery_ = edits; }
    void setJournalFile(const QString& file) { journalFile_ = file; }
    const QString& session() const { return session_; }
    QString journalFile();     // KE_JOURNAL, else <app data>/ops-journal.jsonl
    QJsonArray sessions();     // every session: id, start, end, lines, edits, crashed
    // The scene of `session` after its line `line` (-1: its last), as one undo step: "" or why it failed.
    QString restore(const QString& session, int line);

    static QJsonObject ok(QJsonObject fields = {});
    static QJsonObject error(const QString& why);

private:
    struct Op { QString help, args; Handler run; ke::Value schema; };
    // An edit of the graph: its answer, {"ok": false} to undo it (and the whole batch it is in).
    using GraphEdit = std::function<QJsonObject(ke::SceneGraph&, const QJsonObject& args)>;
    void add(const QString& name, const QString& help, const QString& args, Handler run);
    void addSync(const QString& name, const QString& help, const QString& args, std::function<QJsonObject(const QJsonObject&)> run);
    // One of the operations the windowless host has too: its help and arguments line are ke::hostOps()'s.
    void addHost(const char* name, Handler run);
    void addHostSync(const char* name, std::function<QJsonObject(const QJsonObject&)> run);
    // An edit is an operation of its own (one undo step) and a step of scene_batch.
    QJsonObject transact(const std::function<QJsonObject(ke::SceneGraph&)>& change);
    void addSceneOps();     // EditorOps.cpp: the list, a block, a new scene, undo and redo
    void addSimOps();       // EditorOpsSim.cpp: play, pause, stop, step, the live state, the world's operations
    void addRuntimeOps();   // EditorOpsRuntime.cpp: the lease, its limits, stop, quit
    void addStepOp();
    void addRecordOp();                      // EditorOpsRecord.cpp: sim_record, the Sequencer's take
    void finishTake(const Reply& reply);     // the take into the scene as a new sequence (reply may be empty)
    void addStateOp();
    ke::RuntimePolicy::Usage runtimeUsage() const;
    void stopRuntime(const QString& reason, std::function<void()> done);
    void addNoteOp();       // EditorOpsSim.cpp: an agent's line of reasoning in the console
    void addViewOps();      // EditorOpsSim.cpp: screenshot, frame, the buttons of the window
    void addInputOps();     // EditorOpsInput.cpp: a world point's pixel, the mouse and keys in the 3D view
    void addJournalOps();   // EditorJournal.cpp: the journal's last lines
    void addCoreOps();      // EditorOpsCore.cpp: the core's operations (ke::sceneOps) - queries, edits, actions, assembly
    void addPreviewOps();   // EditorOpsAssembly.cpp: what a ready link would add, as the window draws its ghost
    void addStateOps();     // EditorOpsState.cpp: the window's own state - the selection, the gizmo's tool, the view
    ke::Value withWindow(const ke::SceneOp& op, const QJsonObject& args); // the selection as ids, a light's place, a camera's eye
    void afterAction(const ke::Value& answer);                            // select what it answered, say its note
    QString selectArgs(const QJsonObject& args);
    void journal(const QString& source, const QString& name, const QJsonObject& args, const QJsonObject& answer);
    void writeLine(QJsonObject line, bool keep = true);
    void snapshot(const QString& why);
    std::vector<QJsonObject> sessionLines(const QString& session);

    SceneBuilder* builder_;
    Viewport* view_;
    EditorRuntime* runtime_ = nullptr;
    SimController* ctrl_;
    QWidget* window_;
    std::map<QString, Op> ops_;
    std::set<QString> pluginOps_;
    std::function<QString(const QString&)> unknownHint_;
    QString unknown(const QString& name) const;
    std::function<void()> refresh_;
    bool recording_ = false;
    std::function<void()> recordingChanged_;
    void setRecording(bool on) {
        recording_ = on;
        if (recordingChanged_) recordingChanged_();
    }
    std::vector<Listener> listeners_;
    std::deque<QJsonObject> recent_; // the journal's last lines (the file keeps them all)
    QString journalFile_;
    QString session_;
    std::shared_ptr<class QLockFile> lock_;
    int snapshotEvery_ = 100, editsSinceSnapshot_ = 0;
    bool replaying_ = false, playing_ = false;
};
