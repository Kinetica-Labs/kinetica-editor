#pragma once
// The scene as a document: the graph, its undo history, the counter of object ids and the one transaction every edit
// goes through (Document.cpp). What a window or a script adds around it - the typing merge of the hand's input, the
// selection, files, the built world - is the host's. The answer of edit_undo / edit_redo is built here too.
#include "core/Value.h"
#include "scene/SceneGraph.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ke {

// Whole-graph snapshots, newest last, at most 100: what Ctrl+Z walks back through.
class History {
public:
    void push(const SceneGraph& before); // a new step: redo is gone, the oldest past the limit is forgotten
    bool undo(SceneGraph& current);      // current goes to redo, the last step comes back; false: no step
    bool redo(SceneGraph& current);
    SceneGraph pop();                    // the last step taken back and handed over (a failed edit); empty graph: none
    void cut(size_t keep);               // the steps above the first `keep` go, redo with them
    size_t steps() const { return past_.size(); }
    size_t forgotten() const { return forgotten_; } // the oldest steps dropped from a full history
    bool canRedo() const { return !redo_.empty(); }

private:
    std::vector<SceneGraph> past_, redo_;
    size_t forgotten_ = 0;
};

// The scene of a new document: the floor alone (a collider, locked: clicks on it select nothing).
SceneGraph floorScene(uint32_t floorId);

// What edit_undo / edit_redo answer, without "ok": undone / redone, the ids that changed (a joint gone with its link
// too), whether the world did, and how many steps are left.
Value historyAnswer(const SceneGraph& before, const SceneGraph& after, bool undo, bool done, size_t steps);

class Document {
public:
    // Takes the step before a change; true when it made one. Default: a push of the graph.
    using Remember = std::function<bool()>;

    SceneGraph graph;
    History history;

    uint32_t newId() { return next_++; } // never reused within a session: not by a new scene, an open, an undo
    uint32_t nextIdToGive() const { return next_; }
    void setNextId(uint32_t next) { next_ = next; } // a replayed session gives the ids it gave: the one way the counter goes down
    void adoptIds(); // the counter past every id of the graph; an entity with id 0 or a repeated one gets a new id

    bool transact(const std::function<bool(SceneGraph&, std::string&)>& change, std::string& error, const Remember& remember = {});
    void newScene(const Remember& remember = {}); // a step when there is something to keep, then the floor alone
    void load(SceneGraph g, const Remember& remember = {}); // a step, then g in its place
    bool undo() { return history.undo(graph); }
    bool redo() { return history.redo(graph); }

private:
    bool take(const Remember& remember);
    uint32_t next_ = 1;
};

} // namespace ke
