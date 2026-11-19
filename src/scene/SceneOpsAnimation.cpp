// The Sequencer's operations (SceneOps.h, the work in Animation.h): a key of objects where they stand, a key removed, a
// track cleared, the playhead put at a time - the animated objects to their poses there -, the clip's loop, and the
// whole clip read back. Each change is one undo step of the client and a line of its journal.
#include "scene/SceneOps.h"

#include "scene/Animation.h"

#include <cmath>

namespace ke {

namespace {

bool timeArg(const Value& a, const SceneGraph& g, Real& t, std::string& e) {
    const double x = a["time"].number(double(g.animation.time));
    if (!std::isfinite(x) || x < 0 || x > 86400) return e = "time is seconds 0..86400", false;
    return t = Real(x), true;
}

std::vector<uint32_t> objectsArg(const Value& a) {
    std::vector<uint32_t> ids = idsArg(a);
    if (a.has("id")) ids.push_back(uint32_t(a["id"].integer()));
    return ids;
}

Value vec3(const Vector3& v) {
    Value out = Value::list();
    out.push(double(v.x)), out.push(double(v.y)), out.push(double(v.z));
    return out;
}

} // namespace

std::vector<SceneOp> animationSceneOps() {
    std::vector<SceneOp> ops;
    ops.push_back({"anim_key", "A key of each object at `time` (the playhead when none) where it stands now - its place and turn on its parent "
                               "(one at the same time replaced): the Sequencer's K", "{id?: int, ids?: [int], time?: s}", {},
                   [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Real t;
        if (!timeArg(a, g, t, e)) return false;
        const std::vector<uint32_t> ids = objectsArg(a);
        if (ids.empty()) return e = "id or ids: the objects to key", false;
        for (uint32_t id : ids)
            if (!keyObject(g, id, t)) return e = "no object " + std::to_string(id), false;
        return r.set("time", double(t)), r.set("length", double(g.animation.length())), true;
    }});
    ops.push_back({"anim_delete_key", "The object's key at `time` removed (its last: its track goes)", "{id: int, time: s}", {},
                   [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        Real t;
        if (!timeArg(a, g, t, e)) return false;
        return removeKey(g, uint32_t(a["id"].integer()), t) || (e = "no key of " + std::to_string(a["id"].integer()) + " at that time", false);
    }});
    ops.push_back({"anim_clear", "The objects' tracks removed (none named: the whole clip)", "{id?: int, ids?: [int]}", {},
                   [](SceneGraph& g, const Value& a, Value&, std::string&) { return removeTracks(g, objectsArg(a)), true; }});
    ops.push_back({"anim_seek", "The playhead at `time`: every animated object to its pose there (in a line between keys, the shortest turn)",
                   "{time: s}", {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Real t;
        if (!a.has("time")) return e = "time: where the playhead goes, s", false;
        if (!timeArg(a, g, t, e)) return false;
        applyAnimation(g, t);
        return r.set("time", double(t)), true;
    }});
    ops.push_back({"anim_set", "The clip's loop: past its last key it starts over", "{loop: bool}", {},
                   [](SceneGraph& g, const Value& a, Value&, std::string&) { return g.animation.loop = a["loop"].truth(g.animation.loop), true; }});
    ops.push_back({"anim_get", "The clip: the playhead, its length, the loop, and each track's object and keys (time s, position m and rotation "
                               "degrees on the object's parent)", "{}", [](const SceneGraph& g, const Value&, Value& r, std::string&) {
        Value tracks = Value::list();
        for (const AnimTrack& t : g.animation.tracks) {
            Value track = Value::map(), keys = Value::list();
            for (const AnimKey& k : t.keys) {
                Value key = Value::map();
                key.set("time", double(k.time)), key.set("position", vec3(k.position)), key.set("rotation", vec3(k.rotationDeg));
                keys.push(key);
            }
            const SceneObject* o = findObject(g, t.object);
            track.set("object", int(t.object)), track.set("name", o ? o->name : std::string()), track.set("keys", keys);
            tracks.push(track);
        }
        r.set("time", double(g.animation.time)), r.set("length", double(g.animation.length())), r.set("loop", g.animation.loop);
        return r.set("tracks", tracks), true;
    }, {}});
    return ops;
}

} // namespace ke
