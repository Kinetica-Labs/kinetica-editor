#pragma once
// The Sequencer's work on a sequence (SceneGraph.h, SequenceObject): a bound object's
// pose at a time, a sequence played onto the scene at its playhead, keys made from where objects stand and removed. The
// sequence holds the animation; the objects it binds never do. The running world (src/sim) and the editor sample it alike.
#include "scene/SceneGraph.h"

namespace ke {

// The pose of keys at `time` on the object's parent: from a key to the next as the key says (AnimInterp: in a line, the
// Hermite cubic, or held) and the shortest turn (slerp); held
// before the first key and after the last, or - `loop` - wrapped over `length`. False: no key.
bool samplePose(const std::vector<AnimKey>& keys, Real time, bool loop, Real length, Vector3& position, Quaternion& rotation);
// The velocity a smooth key's place goes through, m/s: its own (recorded, or set by its handle), else the slope between
// its neighbours, none at the ends. Its Bezier handles are the same line: the key ± this velocity × a third of the time
// to the neighbour key.
Vector3 keyVelocity(const std::vector<AnimKey>& keys, size_t i);
SceneObject* posedObject(SceneGraph& g, uint32_t id); // one a sequence can bind: an entity, a group, an array, a light, a camera
SequenceObject* findSequence(SceneGraph& g, uint32_t id);       // 0: the active one
const SequenceObject* activeSequence(const SceneGraph& g);      // the Sequencer's; none: nullptr
SequenceObject* playingSequence(SceneGraph& g);                 // the active one when ▶ plays it (SequenceObject::play)
const SequenceObject* playingSequence(const SceneGraph& g);
const SequenceTrack* trackOf(const SequenceObject& q, uint32_t object);
// The sequence's playhead at `time` and every object it binds put to its pose there, every keyed field set to its numbers
// there (a lost binding skipped).
void seekSequence(SceneGraph& g, SequenceObject& q, Real time);
// A field's numbers at `time` (FieldTrack): held before the first key and after the last, or - `loop` - wrapped over
// `length`; between keys in a line, by the Hermite cubic through the neighbours' slopes (smooth), or held. False: no key.
bool sampleField(const std::vector<FieldKey>& keys, Real time, bool loop, Real length, std::vector<Real>& values);
const FieldTrack* fieldTrackOf(const SequenceObject& q, uint32_t object, const std::string& line, const std::string& key);
// A key of the object's field at `time` with the numbers it holds now (one at the same time replaced). False and `error`:
// no such object or field, or a field that is no numbers (a kind, a name: nothing to go between).
bool keyField(const SceneGraph& g, SequenceObject& q, uint32_t object, const std::string& line, const std::string& key, Real time,
              std::string& error);
bool removeFieldKey(SequenceObject& q, uint32_t object, const std::string& line, const std::string& key, Real time); // its last: the track goes
// A key of the object at `time` where it stands now (one at the same time replaced). False: nothing to bind.
bool keyObject(const SceneGraph& g, SequenceObject& q, uint32_t object, Real time);
bool removeKey(SequenceObject& q, uint32_t object, Real time);            // false: no key there (its last: the track goes)
// The object's key at `time` (a microsecond either way); nullptr: none.
AnimKey* keyAt(SequenceObject& q, uint32_t object, Real time);
// The key moved to `to` (s), kept in time order; false: another key is there.
bool moveKey(SequenceObject& q, uint32_t object, Real time, Real to);
void removeTracks(SequenceObject& q, const std::vector<uint32_t>& objects); // their tracks (none named: all)
void makeActive(SceneGraph& g, uint32_t sequence);                         // it plays, no other
// Samples of a motion (a recorded run, a frame each) thinned to the keys that give it back: every sample dropped lies
// within `metres` and `degrees` of the line and the shortest turn between the keys kept around it.
std::vector<AnimKey> thinKeys(const std::vector<AnimKey>& samples, Real metres, Real degrees);

} // namespace ke
