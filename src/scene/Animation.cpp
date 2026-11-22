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

const char* interpName(AnimInterp i) { return i == AnimInterp::Smooth ? "smooth" : i == AnimInterp::Step ? "step" : "linear"; }

bool interpOf(const std::string& name, AnimInterp& i) {
    for (AnimInterp x : {AnimInterp::Linear, AnimInterp::Smooth, AnimInterp::Step})
        if (name == interpName(x)) return i = x, true;
    return false;
}

Vector3 keyVelocity(const std::vector<AnimKey>& k, size_t i) {
    if (k[i].hasVelocity) return k[i].velocity;
    if (i == 0 || i + 1 >= k.size()) return Vector3(0.0_r);
    return (k[i + 1].position - k[i - 1].position) / (k[i + 1].time - k[i - 1].time);
}

bool samplePose(const std::vector<AnimKey>& k, Real time, bool loop, Real length, Vector3& position, Quaternion& rotation) {
    if (k.empty()) return false;
    if (loop && length > 0) time = time - length * std::floor(time / length);
    if (time <= k.front().time || k.size() == 1) return position = k.front().position, rotation = turnOf(k.front().rotationDeg), true;
    if (time >= k.back().time) return position = k.back().position, rotation = turnOf(k.back().rotationDeg), true;
    const auto after = std::upper_bound(k.begin(), k.end(), time, [](Real t, const AnimKey& x) { return t < x.time; });
    const size_t i = size_t(after - k.begin()) - 1;
    const AnimKey &a = k[i], &b = k[i + 1];
    if (a.interp == AnimInterp::Step) return position = a.position, rotation = turnOf(a.rotationDeg), true;
    const Real h = b.time - a.time, s = (time - a.time) / h;
    if (a.interp == AnimInterp::Smooth) { // Hermite: the cubic with the keys' velocities at its ends
        const Real s2 = s * s, s3 = s2 * s;
        position = a.position * (2 * s3 - 3 * s2 + 1) + keyVelocity(k, i) * (h * (s3 - 2 * s2 + s)) + b.position * (3 * s2 - 2 * s3) +
                   keyVelocity(k, i + 1) * (h * (s3 - s2));
    } else
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

SequenceObject* playingSequence(SceneGraph& g) {
    SequenceObject* q = findSequence(g, 0);
    return q && q->play ? q : nullptr;
}

const SequenceObject* playingSequence(const SceneGraph& g) { return playingSequence(const_cast<SceneGraph&>(g)); }

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

AnimKey* keyAt(SequenceObject& q, uint32_t object, Real time) {
    for (SequenceTrack& t : q.tracks)
        if (t.object == object)
            for (AnimKey& k : t.keys)
                if (std::fabs(k.time - time) <= kSameTime) return &k;
    return nullptr;
}

bool moveKey(SequenceObject& q, uint32_t object, Real time, Real to) {
    AnimKey* k = keyAt(q, object, time);
    if (!k || (std::fabs(to - time) > kSameTime && keyAt(q, object, to))) return false;
    k->time = to;
    for (SequenceTrack& t : q.tracks)
        if (t.object == object) std::stable_sort(t.keys.begin(), t.keys.end(), [](const AnimKey& x, const AnimKey& y) { return x.time < y.time; });
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

// Greedy: from the last key kept, the span grows while the samples inside it stay on the line; the sample before the
// one that breaks it is kept. A span is at most 240 samples (4 s of frames): the check stays linear in the run.
std::vector<AnimKey> thinKeys(const std::vector<AnimKey>& s, Real metres, Real degrees) {
    if (s.size() <= 2) return s;
    const Real cosHalf = std::cos(degrees * kPi / 360);
    const auto holds = [&](size_t from, size_t to) {
        const std::vector<AnimKey> line{s[from], s[to]};
        for (size_t k = from + 1; k < to; ++k) {
            Vector3 p;
            Quaternion q;
            samplePose(line, s[k].time, false, 0, p, q);
            const Quaternion r = turnOf(s[k].rotationDeg);
            const Real dot = std::fabs(q.w * r.w + q.x * r.x + q.y * r.y + q.z * r.z);
            if (length(p - s[k].position) > metres || dot < cosHalf) return false;
        }
        return true;
    };
    std::vector<AnimKey> out{s.front()};
    size_t from = 0;
    for (size_t to = 2; to < s.size(); ++to)
        if (to - from > 240 || !holds(from, to)) out.push_back(s[to - 1]), from = to - 1;
    out.push_back(s.back());
    return out;
}

} // namespace ke
