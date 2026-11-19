// The Sequencer's work on a sequence (Animation.h): sampling, playing, keying.
#include "scene/Animation.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

// The turn of Euler degrees as an object's: R = Ry Rx Rz (SceneHierarchy.cpp, objectRotation).
Quaternion turnOf(const Vector3& d) { SceneObject o; o.rotationDeg = d; return objectRotation(o).normalized(); }

const Real kSameTime = 1e-6_r; // keys closer than a microsecond are one

template <class T> SceneObject* in(std::vector<T>& objects, uint32_t id) {
    for (T& o : objects)
        if (o.id == id) return &o;
    return nullptr;
}

} // namespace

Real SequenceObject::length() const {
    Real last = 0;
    for (const SequenceTrack& t : tracks)
        if (!t.keys.empty()) last = std::max(last, t.keys.back().time);
    return last;
}

bool samplePose(const std::vector<AnimKey>& k, Real time, bool loop, Real length, Vector3& position, Quaternion& rotation) {
    if (k.empty()) return false;
    if (loop && length > 0) time = time - length * std::floor(time / length);
    if (time <= k.front().time || k.size() == 1) return position = k.front().position, rotation = turnOf(k.front().rotationDeg), true;
    if (time >= k.back().time) return position = k.back().position, rotation = turnOf(k.back().rotationDeg), true;
    const auto after = std::upper_bound(k.begin(), k.end(), time, [](Real t, const AnimKey& x) { return t < x.time; });
    const AnimKey &a = *(after - 1), &b = *after;
    const Real s = (time - a.time) / (b.time - a.time);
    position = a.position + (b.position - a.position) * s;
    rotation = slerp(turnOf(a.rotationDeg), turnOf(b.rotationDeg), s);
    return true;
}

SceneObject* posedObject(SceneGraph& g, uint32_t id) {
    if (id == 0) return nullptr;
    for (SceneObject* o : {in(g.entities, id), in(g.groups, id), in(g.arrays, id), in(g.lights, id), in(g.cameras, id)})
        if (o) return o;
    return nullptr;
}

SequenceObject* findSequence(SceneGraph& g, uint32_t id) {
    for (SequenceObject& q : g.sequences)
        if (id ? q.id == id : q.active) return &q;
    return nullptr;
}

const SequenceObject* activeSequence(const SceneGraph& g) { return findSequence(const_cast<SceneGraph&>(g), 0); }

const SequenceTrack* trackOf(const SequenceObject& q, uint32_t object) {
    for (const SequenceTrack& t : q.tracks)
        if (t.object == object) return &t;
    return nullptr;
}

void seekSequence(SceneGraph& g, SequenceObject& q, Real time) {
    q.time = time;
    const Real length = q.length();
    for (const SequenceTrack& t : q.tracks) {
        SceneObject* o = posedObject(g, t.object);
        Vector3 p;
        Quaternion r;
        if (!o || !samplePose(t.keys, time, q.loop, length, p, r)) continue;
        o->position = p;
        o->rotationDeg = eulerDegrees(r);
    }
}

bool keyObject(const SceneGraph& g, SequenceObject& q, uint32_t object, Real time) {
    const SceneObject* o = posedObject(const_cast<SceneGraph&>(g), object);
    if (!o) return false;
    auto t = std::find_if(q.tracks.begin(), q.tracks.end(), [&](const SequenceTrack& x) { return x.object == object; });
    if (t == q.tracks.end()) q.tracks.push_back({object, {}}), t = q.tracks.end() - 1;
    const AnimKey key{time, o->position, o->rotationDeg};
    auto at = std::lower_bound(t->keys.begin(), t->keys.end(), time - kSameTime, [](const AnimKey& x, Real s) { return x.time < s; });
    if (at != t->keys.end() && std::fabs(at->time - time) <= kSameTime) *at = key;
    else t->keys.insert(at, key);
    return true;
}

bool removeKey(SequenceObject& q, uint32_t object, Real time) {
    for (SequenceTrack& t : q.tracks) {
        if (t.object != object) continue;
        const auto at = std::find_if(t.keys.begin(), t.keys.end(), [&](const AnimKey& k) { return std::fabs(k.time - time) <= kSameTime; });
        if (at == t.keys.end()) return false;
        t.keys.erase(at);
        if (t.keys.empty()) removeTracks(q, {object});
        return true;
    }
    return false;
}

void removeTracks(SequenceObject& q, const std::vector<uint32_t>& objects) {
    q.tracks.erase(std::remove_if(q.tracks.begin(), q.tracks.end(), [&](const SequenceTrack& t) {
        return objects.empty() || std::find(objects.begin(), objects.end(), t.object) != objects.end();
    }), q.tracks.end());
}

void makeActive(SceneGraph& g, uint32_t sequence) {
    for (SequenceObject& q : g.sequences) q.active = q.id == sequence;
}

} // namespace ke
