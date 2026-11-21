// The Sequencer's operations (SceneOps.h, the work in Animation.h). A sequence is an object of the scene holding an
// animation of others (SceneGraph.h, SequenceObject): sequence_add makes one, sequence_key keys objects in it where they
// stand (no sequence yet: one is made), sequence_delete_key and sequence_clear remove keys and tracks, sequence_seek puts
// its playhead at a time - the objects it binds to their poses there -, sequence_set names it, loops it, makes it the
// one that plays, sequence_get reads them back. `sequence` absent: the active one. Each change is one undo step of the
// client and a line of its journal; a sequence is deleted as any object (scene_delete).
#include "scene/SceneOps.h"

#include "scene/Animation.h"

#include <cmath>

namespace ke {

namespace {

bool timeArg(const Value& a, Real otherwise, Real& t, std::string& e) {
    const double x = a["time"].number(double(otherwise));
    if (!std::isfinite(x) || x < 0 || x > 86400) return e = "time is seconds 0..86400", false;
    return t = Real(x), true;
}

std::vector<uint32_t> objectsArg(const Value& a) {
    std::vector<uint32_t> ids = idsArg(a);
    if (a.has("id")) ids.push_back(uint32_t(a["id"].integer()));
    return ids;
}

SequenceObject& addSequence(SceneGraph& g, const std::string& name) {
    SequenceObject q;
    q.id = nextId(g);
    q.name = name.empty() ? "Секвенция " + std::to_string(g.sequences.size() + 1) : name;
    q.active = !activeSequence(g);
    g.sequences.push_back(q);
    return g.sequences.back();
}

SequenceObject* sequenceArg(SceneGraph& g, const Value& a, std::string& e) {
    const uint32_t id = uint32_t(a["sequence"].integer());
    if (SequenceObject* q = findSequence(g, id)) return q;
    return e = id ? "no sequence " + std::to_string(id) : "no active sequence (sequence_add)", nullptr;
}

Value vec3(const Vector3& v) {
    Value out = Value::list();
    out.push(double(v.x)), out.push(double(v.y)), out.push(double(v.z));
    return out;
}

Value describe(const SceneGraph& g, const SequenceObject& q) {
    Value s = Value::map(), tracks = Value::list();
    for (const SequenceTrack& t : q.tracks) {
        Value track = Value::map(), keys = Value::list();
        for (const AnimKey& k : t.keys) {
            Value key = Value::map();
            key.set("time", double(k.time)), key.set("position", vec3(k.position)), key.set("rotation", vec3(k.rotationDeg));
            if (k.hasVelocity) key.set("velocity", vec3(k.velocity));
            key.set("interp", interpName(k.interp));
            keys.push(key);
        }
        const SceneObject* o = findObject(g, t.object);
        track.set("object", int(t.object)), track.set("name", o ? o->name : std::string()), track.set("lost", !o), track.set("keys", keys);
        tracks.push(track);
    }
    s.set("id", int(q.id)), s.set("name", q.name), s.set("active", q.active), s.set("play", q.play), s.set("time", double(q.time));
    s.set("length", double(q.length())), s.set("loop", q.loop), s.set("tracks", tracks);
    return s;
}

} // namespace

std::vector<SceneOp> animationSceneOps() {
    std::vector<SceneOp> ops;
    ops.push_back({"sequence_add", "A sequence: an animation of objects of the scene, kept apart from them (the first one made, or `active`, is the "
                                   "one that plays)", "{name?: str, active?: bool}", {}, [](SceneGraph& g, const Value& a, Value& r, std::string&) {
        const uint32_t id = addSequence(g, a["name"].text("")).id;
        if (a["active"].truth(false)) makeActive(g, id);
        return r.set("id", int(id)), true;
    }});
    ops.push_back({"sequence_key", "A key of each object at `time` (the sequence's playhead when none) where it stands now - its place and turn on "
                                   "its parent (one at the same time replaced); no sequence yet: one is made. The Sequencer's K",
                   "{ids: [int], time?: s, sequence?: int, interp?: str}", {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        const std::vector<uint32_t> ids = objectsArg(a);
        if (ids.empty()) return e = "ids: the objects to key", false;
        SequenceObject* q = !a.has("sequence") && g.sequences.empty() ? &addSequence(g, "") : sequenceArg(g, a, e);
        Real t;
        if (!q || !timeArg(a, q->time, t, e)) return false;
        AnimInterp interp = AnimInterp::Linear;
        if (a.has("interp") && !interpOf(a["interp"].text(""), interp)) return e = "interp: linear, smooth or step", false;
        for (uint32_t id : ids) {
            if (!keyObject(g, *q, id, t)) return e = "object " + std::to_string(id) + ": nothing a sequence can move", false;
            if (a.has("interp")) keyAt(*q, id, t)->interp = interp;
        }
        return r.set("sequence", int(q->id)), r.set("time", double(t)), r.set("length", double(q->length())), true;
    }, true});
    ops.push_back({"sequence_key_set", "The object's key at `time` changed: moved to new_time, its place or turn on the parent, how it goes on to "
                                       "the next key (interp: linear, smooth - the cubic through the keys -, step - held); the bound objects then "
                                       "stand where the sequence has them at its playhead. The Sequencer's curves",
                   "{id: int, time: s, sequence?: int, new_time?: s, position?: [x,y,z], rotation?: [x,y,z], interp?: str}", {},
                   [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        SequenceObject* q = sequenceArg(g, a, e);
        Real t = 0;
        if (!q || !a.has("time") || !timeArg(a, 0, t, e)) return e.empty() ? (e = "time: the key's, s", false) : false;
        const uint32_t id = uint32_t(a["id"].integer());
        AnimKey* k = keyAt(*q, id, t);
        if (!k) return e = "no key of " + std::to_string(id) + " at that time", false;
        const auto vec = [&a](const char* name, Vector3& v) {
            const std::vector<Value>& x = a[name].items();
            if (x.size() == 3) v = Vector3(Real(x[0].number(0)), Real(x[1].number(0)), Real(x[2].number(0)));
            return !a.has(name) || x.size() == 3;
        };
        if (!vec("position", k->position) || !vec("rotation", k->rotationDeg)) return e = "position and rotation: [x, y, z]", false;
        if (a.has("interp") && !interpOf(a["interp"].text(""), k->interp)) return e = "interp: linear, smooth or step", false;
        if (a.has("new_time")) {
            Value moved = Value::map();
            moved.set("time", a["new_time"]);
            Real to = 0;
            if (!timeArg(moved, 0, to, e)) return false;
            if (!moveKey(*q, id, t, to)) return e = "another key of " + std::to_string(id) + " is there", false;
        }
        seekSequence(g, *q, q->time);
        return true;
    }});
    ops.push_back({"sequence_delete_key", "The object's key at `time` removed (its last: its track goes)", "{id: int, time: s, sequence?: int}", {},
                   [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        SequenceObject* q = sequenceArg(g, a, e);
        Real t;
        if (!q || !a.has("time") || !timeArg(a, 0, t, e)) return e.empty() ? (e = "time: the key's, s", false) : false;
        return removeKey(*q, uint32_t(a["id"].integer()), t) || (e = "no key of " + std::to_string(a["id"].integer()) + " at that time", false);
    }});
    ops.push_back({"sequence_clear", "The objects' tracks removed from the sequence (none named: all of them)", "{ids?: [int], sequence?: int}", {},
                   [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        SequenceObject* q = sequenceArg(g, a, e);
        return q && (removeTracks(*q, objectsArg(a)), true);
    }});
    ops.push_back({"sequence_seek", "The sequence's playhead at `time`: every object it binds to its pose there (in a line between keys, the "
                                    "shortest turn)", "{time: s, sequence?: int}", {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        SequenceObject* q = sequenceArg(g, a, e);
        Real t;
        if (!q) return false;
        if (!a.has("time")) return e = "time: where the playhead goes, s", false;
        if (!timeArg(a, 0, t, e)) return false;
        seekSequence(g, *q, t);
        return r.set("time", double(t)), true;
    }});
    ops.push_back({"sequence_set", "The sequence's name, its loop (past its last key it starts over), whether it is the Sequencer's (active), and "
                                   "whether ▶ plays it when active (play: its bodies led along their tracks; off: shown only, the physics its own)",
                   "{sequence?: int, name?: str, loop?: bool, active?: bool, play?: bool}", {}, [](SceneGraph& g, const Value& a, Value&, std::string& e) {
        SequenceObject* q = sequenceArg(g, a, e);
        if (!q) return false;
        q->name = a["name"].text(q->name);
        q->loop = a["loop"].truth(q->loop);
        q->play = a["play"].truth(q->play);
        if (a.has("active")) a["active"].truth(false) ? makeActive(g, q->id) : void(q->active = false);
        return true;
    }});
    ops.push_back({"sequence_get", "The sequences: each one's id, name, whether it is the Sequencer's and whether ▶ plays it, its playhead, length and loop, and its tracks - the "
                                   "object bound (lost: deleted) and its keys (time s, position m and rotation degrees on its parent)",
                   "{sequence?: int}", [](const SceneGraph& g, const Value& a, Value& r, std::string&) {
        Value list = Value::list();
        const uint32_t only = uint32_t(a["sequence"].integer());
        for (const SequenceObject& q : g.sequences)
            if (!only || q.id == only) list.push(describe(g, q));
        return r.set("sequences", list), true;
    }, {}});
    return ops;
}

} // namespace ke
