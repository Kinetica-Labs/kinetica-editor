// The scene's animation (Animation.h): sampling, applying, keying.
#include "scene/Animation.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

// The turn of Euler degrees as an object's: R = Ry Rx Rz (SceneGraph.h, objectRotation).
Quaternion turnOf(const Vector3& d) {
    const Real k = kPi / 180;
    return (Quaternion::fromAxisAngle({0, 1, 0}, d.y * k) * Quaternion::fromAxisAngle({1, 0, 0}, d.x * k) *
            Quaternion::fromAxisAngle({0, 0, 1}, d.z * k)).normalized();
}

const Real kSameTime = 1e-6_r; // keys closer than a microsecond are one

} // namespace

Real Animation::length() const {
    Real last = 0;
    for (const AnimTrack& t : tracks)
        if (!t.keys.empty()) last = std::max(last, t.keys.back().time);
    return last;
}

bool samplePose(const AnimTrack& track, Real time, bool loop, Real length, Vector3& position, Quaternion& rotation) {
    const std::vector<AnimKey>& k = track.keys;
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

const AnimTrack* trackOf(const SceneGraph& g, uint32_t object) {
    for (const AnimTrack& t : g.animation.tracks)
        if (t.object == object) return &t;
    return nullptr;
}

void applyAnimation(SceneGraph& g, Real time) {
    g.animation.time = time;
    const Real length = g.animation.length();
    for (const AnimTrack& t : g.animation.tracks) {
        SceneObject* o = const_cast<SceneObject*>(findObject(g, t.object)); // the graph is ours to change
        Vector3 p;
        Quaternion q;
        if (!o || !samplePose(t, time, g.animation.loop, length, p, q)) continue;
        o->position = p;
        o->rotationDeg = eulerDegrees(q);
    }
}

bool keyObject(SceneGraph& g, uint32_t object, Real time) {
    const SceneObject* o = findObject(g, object);
    if (!o) return false;
    auto t = std::find_if(g.animation.tracks.begin(), g.animation.tracks.end(), [&](const AnimTrack& x) { return x.object == object; });
    if (t == g.animation.tracks.end()) g.animation.tracks.push_back({object, {}}), t = g.animation.tracks.end() - 1;
    const AnimKey key{time, o->position, o->rotationDeg};
    auto at = std::lower_bound(t->keys.begin(), t->keys.end(), time - kSameTime, [](const AnimKey& x, Real s) { return x.time < s; });
    if (at != t->keys.end() && std::fabs(at->time - time) <= kSameTime) *at = key;
    else t->keys.insert(at, key);
    return true;
}

bool removeKey(SceneGraph& g, uint32_t object, Real time) {
    for (AnimTrack& t : g.animation.tracks) {
        if (t.object != object) continue;
        const auto at = std::find_if(t.keys.begin(), t.keys.end(), [&](const AnimKey& k) { return std::fabs(k.time - time) <= kSameTime; });
        if (at == t.keys.end()) return false;
        t.keys.erase(at);
        if (t.keys.empty()) removeTracks(g, {object});
        return true;
    }
    return false;
}

void removeTracks(SceneGraph& g, const std::vector<uint32_t>& objects) {
    std::vector<AnimTrack>& tracks = g.animation.tracks;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const AnimTrack& t) {
        return objects.empty() || std::find(objects.begin(), objects.end(), t.object) != objects.end();
    }), tracks.end());
}

} // namespace ke
