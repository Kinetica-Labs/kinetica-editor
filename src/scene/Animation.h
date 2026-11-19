#pragma once
// The scene's animation (SceneGraph.h, struct Animation): an object's pose at a time from its track, the clip applied
// to the scene at a time (the Sequencer's playhead), keys made from where objects stand and removed. The running world
// (src/sim) and the editor sample it alike.
#include "scene/SceneGraph.h"

namespace ke {

// The track's pose at `time` on the object's parent: in a line between two keys and the shortest turn (slerp); held
// before the first key and after the last, or - `loop` - wrapped over `length`. False: the track has no key.
bool samplePose(const AnimTrack& track, Real time, bool loop, Real length, Vector3& position, Quaternion& rotation);
const AnimTrack* trackOf(const SceneGraph& g, uint32_t object);
// Every animated object put to its pose at `time` (gone objects' tracks are left alone); the playhead there.
void applyAnimation(SceneGraph& g, Real time);
// A key of the object at `time` where it stands now (one at the same time replaced). False: no such object.
bool keyObject(SceneGraph& g, uint32_t object, Real time);
bool removeKey(SceneGraph& g, uint32_t object, Real time); // false: no key there
void removeTracks(SceneGraph& g, const std::vector<uint32_t>& objects); // their tracks (none given: all)

} // namespace ke
