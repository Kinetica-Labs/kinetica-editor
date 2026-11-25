#pragma once
// An editor's actions as edits of the scene graph - the core every client of the API calls: the
// editor's buttons and its operations (EditorOps), a script over the API, an agent over MCP. Add a shape, a light, a camera; delete, duplicate, group, ungroup, make an array,
// show everything; give or take a role; move, turn and scale objects (what a gizmo drag does). The
// logic sits in this core and the window only calls it, so the same call makes the
// same scene to the bit whoever calls it.
//
// The objects are always named by id: which ones are selected is the client's business. New ids come
// from nextId(g), so an editor that never reuses an id keeps g.idFloor at the first one it has not
// given out. Each function is one edit; undo is the client's (it keeps the graph from before).
#include "scene/SceneGraph.h"

#include <map>

#include <functional>
#include <string>
#include <vector>

namespace ke {

// What an entity is: a moving rigid body, a collider (alone: a static obstacle), or both.
enum class Role { Rigid, Collider, Count };
const char* roleWord(Role r); // "rigid", "collider": the API's words
bool roleOfWord(const std::string& word, Role& out);
bool hasRole(const Entity& e, Role r);
void setRole(Entity& e, Role r, bool on); // the component alone, nothing else changes

// The names new objects get, in Russian as the scene file keeps them: "Куб", "Солнце".
const char* shapeTitle(ShapeKind k);
const char* lightTitle(LightKind k);
const char* forceTitle(ForceShape shape, ForceMode mode); // «Толчок», «Взрыв», «Тяга», «Притяжение»

// The hierarchy: the object, its parent, its parent's parent... (a cycle ends it); whether `id` is
// `ancestor` or inside it; the objects whose parent it is; of `ids`, those not inside another of them
// and not locked (what a gizmo, Delete and Ctrl+D act on); the master an instance takes its shape from.
std::vector<uint32_t> ancestry(const SceneGraph& g, uint32_t id);
bool isInside(const SceneGraph& g, uint32_t id, uint32_t ancestor);
std::vector<uint32_t> childrenOf(const SceneGraph& g, uint32_t id);
std::vector<uint32_t> topmost(const SceneGraph& g, const std::vector<uint32_t>& ids);
uint32_t masterOf(const SceneGraph& g, uint32_t entity);
// The object an array's copy is drawn for: its array (an ordinary id is its own; 0: no such array).
uint32_t ownerOf(const SceneGraph& g, uint32_t drawnId);
// The world box of what the object shows (its shapes, an array's copies; a light, a camera or a force: 0.3 m
// around it). Invalid when it shows nothing. entityBox: a shape's box as a viewer knows it (a model's
// mesh it has loaded); empty, the core's own estimate.
AABB objectBounds(const SceneGraph& g, uint32_t id, const std::function<AABB(const Entity&)>& entityBox = {});
AABB sceneBounds(const SceneGraph& g); // the visible shapes one can edit (not a locked floor)

// New objects: their id.
uint32_t addShape(SceneGraph& g, ShapeKind shape);  // "Куб 2": 0.8 m over a free spot of the floor
// A model's file (OBJ, STL) as a new shape - «Файл → Импорт…» and scene_import: named after the file, its largest side
// 1 m with its proportions kept, standing on the floor over a free spot. 0 and `error`, the scene untouched, when the
// file gives no triangles.
uint32_t importModel(SceneGraph& g, const std::string& file, std::string& error);
uint32_t addLight(SceneGraph& g, LightKind kind, const Vector3& at); // the sun high at a slant over `at`
uint32_t placeLight(SceneGraph& g, LightKind kind, const Vector3& position, const Vector3& target); // at `position`, shining at `target`
uint32_t addForce(SceneGraph& g, ForceShape shape, ForceMode mode, const Vector3& at); // the kind's defaults, standing at `at`
uint32_t addCamera(SceneGraph& g, const Vector3& eye, const Vector3& centre); // at `eye`, looking at `centre`
// A camera made a target camera (SceneGraph.h): it looks at object `target` always, or (0) at `point`, or (neither) at
// the point it looks at now; its stored turn follows, so set free again it keeps looking there. False: no such camera,
// or the target is no object (or the camera itself).
bool aimCamera(SceneGraph& g, uint32_t camera, uint32_t target, const Vector3* point);
bool freeCamera(SceneGraph& g, uint32_t camera); // a free camera again, looking where it looked

// The objects with everything under them, the joints that lose a link, the sensors on them; an
// array whose template goes; batteries and drivers named in `ids`.
void deleteObjects(SceneGraph& g, const std::vector<uint32_t>& ids);
// A copy of the object and all under it (sensors go with their body) under `parent`; an instance copy
// shares the master's geometry. Its id; `copied` (if given) gets each entity copied -> its copy, for copyJoints.
uint32_t copySubtree(SceneGraph& g, uint32_t id, bool instance, uint32_t parent, std::map<uint32_t, uint32_t>* copied = nullptr);
// The joints among copied links (`copied`: an entity -> its copy, the copies standing where they are to be): every
// joint whose moving link was copied and whose other one was too, or is the world, again between the copies - with
// the sensors on it and its motor's driver (on the same battery). A joint on the world hangs the copy as the original
// hangs: its frame there moves as the copied link moved from the original. The joints' copies.
std::vector<uint32_t> copyJoints(SceneGraph& g, const std::map<uint32_t, uint32_t>& copied);
// Copies of `roots` (topmost) a little above them - a sensor's copy stays on its mount -, or each `by` from its original
// in the world (zero: in its place): the copies.
std::vector<uint32_t> duplicateObjects(SceneGraph& g, const std::vector<uint32_t>& roots, const Vector3* by = nullptr);
// A group at the centre of `roots`, their world poses kept: its id (0: no roots).
uint32_t groupObjects(SceneGraph& g, const std::vector<uint32_t>& roots);
// The groups dissolved, their members kept where they stand in the world: the members.
std::vector<uint32_t> ungroupObjects(SceneGraph& g, const std::vector<uint32_t>& groups);
// Each shape the hidden template of a row of 5 copies along x, a little more than its width apart: the arrays.
std::vector<uint32_t> arrayObjects(SceneGraph& g, const std::vector<uint32_t>& shapes);
void showAll(SceneGraph& g); // every hidden object shown, not an array's template
// A role given to the entities (an instance: its master) or taken; giving one switches on what it needs
// (a rigid body its collider). What was switched on besides, in words for a person ("" - nothing).
std::string setRoles(SceneGraph& g, const std::vector<uint32_t>& entities, Role role, bool on);
// A turn by `degrees` about `axis` (normalised here) - the one way every client makes it from the two
// numbers, so a turn said as a line and the turn a hand made with the gizmo are the same bits.
Quaternion turnOf(const Vector3& axis, Real degrees);
// The gizmo's effect, from the poses and sizes of `start` (the graph when the drag began; it may be `g`
// itself): moved by `by` in the world - their angles kept as typed; turned by `turn` about `pivot`;
// scaled by the factors along their own axes, the distances inside a group or an array too.
void moveObjects(SceneGraph& g, const SceneGraph& start, const std::vector<uint32_t>& ids, const Vector3& by);
void turnObjects(SceneGraph& g, const SceneGraph& start, const std::vector<uint32_t>& ids, const Vector3& pivot, const Quaternion& turn);
void scaleObjects(SceneGraph& g, const SceneGraph& start, const std::vector<uint32_t>& ids, const Vector3& factors);

} // namespace ke
