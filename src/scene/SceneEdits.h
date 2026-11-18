#pragma once
// The few edits every change of a scene is made of - the core of the editor's API. An object (an
// entity, a group, an array, a light, a camera, a joint, a battery, a driver, a sensor) is read and
// written as its block of the scene file (SceneGraph.cpp): the API, the file and the inspector share
// one vocabulary - "rigid density", "shape size", "link axis" - and no second schema can drift from
// the first.
//
//   getObject    the block of one object;
//   putObject    a block in: a new object (id 0 or unused), or the object of its id replaced;
//   setField     one "line key values" of an object's block (a role line missing is added);
//   removeObject an object out, with what cannot live without it (its joints and the sensors on it
//                or on them; children keep their world pose under the removed one's parent);
//   getWorld     the world's lines, and putWorld to set them;
//   diffEdits    the edits between two scenes (a change made by hand, as code), applyEdits to replay them.
//
// Each edit has an inverse - put the old block back, or remove what was put - so an undo history
// is a list of (before, after) blocks, and a button or an agent composes everything else (add a
// cube, attach to a face, duplicate) from these and one transaction.
#include "scene/SceneGraph.h"

#include <string>
#include <vector>

namespace ke {

// The block of the object with this id, as save() writes it ("" when there is none).
std::string getObject(const SceneGraph& g, uint32_t id);
// Reads one block and puts it into the graph. Returns its id (0 and `error` when the text is not
// one block, or its id belongs to an object of another kind).
uint32_t putObject(SceneGraph& g, const std::string& block, std::string& error);
// The values after `key` on the object's line that starts with the words of `line` ("rigid",
// "shape", "link", "drive rx"...) replaced by `values` (words as the file has them); the key is
// appended when the line has none, and the line itself when the object has none (a role switched
// on, a freedom driven). An empty key replaces the words right after the line's own (a shape's kind,
// a name). Returns the object's id (0 and `error` when the result does not read back).
uint32_t setField(SceneGraph& g, uint32_t id, const std::string& line, const std::string& key,
                  const std::vector<std::string>& values, std::string& error);
bool removeObject(SceneGraph& g, uint32_t id);
// The world - gravity, box - and the physics engine's settings, as the file's "world" and "physics"
// lines (both, always), and back: the text of those lines replaces them (a missing physics line: the
// defaults). False and `error` when the text is not those lines.
std::string getWorld(const SceneGraph& g);
bool putWorld(SceneGraph& g, const std::string& text, std::string& error);

// One core edit as data - what a journal holds and replays, and what an editor's console shows for a
// change made by hand (as Maya's script editor echoes a setAttr for each edit).
struct GraphEdit {
    enum class Kind { Put, Set, Remove, World };
    Kind kind = Kind::Put;
    uint32_t id = 0;                  // Set, Remove (Put: the block's own)
    std::string text;                 // Put: the block; World: the world's lines
    std::string line, key;            // Set: as setField takes them
    std::vector<std::string> values;
};
// The core edits that turn `before` into `after`: what is gone removed first (with what goes with it),
// what is new put, and what changed set word span by word span - from the first word that differs to
// the last, after the key nearest before it - where a line keeps its count of words (a moved shape:
// "position x y z"), else put whole; the world's lines when they changed. Replayed on `before`
// (applyEdits), they give `after` to the bit.
std::vector<GraphEdit> diffEdits(const SceneGraph& before, const SceneGraph& after);
bool applyEdits(SceneGraph& g, const std::vector<GraphEdit>& edits, std::string& error);

} // namespace ke
