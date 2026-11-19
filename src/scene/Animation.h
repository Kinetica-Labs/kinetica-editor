#pragma once
// The Sequencer's work on a sequence (SceneGraph.h, SequenceObject - a simple Unreal Level Sequence): a bound object's
// pose at a time, a sequence played onto the scene at its playhead, keys made from where objects stand and removed. The
// sequence holds the animation; the objects it binds never do. The running world (src/sim) and the editor sample it alike.
#include "scene/SceneGraph.h"

namespace ke {

// The pose of keys at `time` on the object's parent: in a line between two keys (the Hermite cubic when both carry a
// velocity) and the shortest turn (slerp); held
// before the first key and after the last, or - `loop` - wrapped over `length`. False: no key.
bool samplePose(const std::vector<AnimKey>& keys, Real time, bool loop, Real length, Vector3& position, Quaternion& rotation);
SceneObject* posedObject(SceneGraph& g, uint32_t id); // one a sequence can bind: an entity, a group, an array, a light, a camera
SequenceObject* findSequence(SceneGraph& g, uint32_t id);       // 0: the active one
const SequenceObject* activeSequence(const SceneGraph& g);      // none: nullptr
const SequenceTrack* trackOf(const SequenceObject& q, uint32_t object);
// The sequence's playhead at `time` and every object it binds put to its pose there (a lost binding skipped).
void seekSequence(SceneGraph& g, SequenceObject& q, Real time);
// A key of the object at `time` where it stands now (one at the same time replaced). False: nothing to bind.
bool keyObject(const SceneGraph& g, SequenceObject& q, uint32_t object, Real time);
bool removeKey(SequenceObject& q, uint32_t object, Real time);            // false: no key there (its last: the track goes)
void removeTracks(SequenceObject& q, const std::vector<uint32_t>& objects); // their tracks (none named: all)
void makeActive(SceneGraph& g, uint32_t sequence);                         // it plays, no other
// Samples of a motion (a recorded run, a frame each) thinned to the keys that give it back: every sample dropped lies
// within `metres` and `degrees` of the line and the shortest turn between the keys kept around it.
std::vector<AnimKey> thinKeys(const std::vector<AnimKey>& samples, Real metres, Real degrees);

} // namespace ke
