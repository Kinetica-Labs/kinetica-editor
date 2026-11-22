// The actions on whole subtrees of the hierarchy - delete, copy, duplicate, group, ungroup, make an
// array - and the gizmo's move, turn and scale (see SceneActions.h). A subtree goes with what cannot
// live without it: the joints that lose a link, the sensors on a body or a joint. World poses are kept
// whenever an object changes parent.
#include "scene/SceneActions.h"

#include "scene/Assembly.h"

#include <algorithm>
#include <set>

namespace ke {

namespace {

bool contains(const std::vector<uint32_t>& v, uint32_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

SceneObject* objectIn(SceneGraph& g, uint32_t id) { return const_cast<SceneObject*>(findObject(g, id)); }

template <class T>
T* byId(std::vector<T>& v, uint32_t id) {
    for (T& o : v)
        if (o.id == id) return &o;
    return nullptr;
}

template <class T>
void eraseDead(std::vector<T>& v, const std::set<uint32_t>& dead) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](const T& o) { return dead.count(o.id) > 0; }), v.end());
}

// A copy of a plain object (a group, an array, a light, a camera) under `parent`, unlocked, new id.
template <class T>
uint32_t copyPlain(SceneGraph& g, std::vector<T>& v, const T& original, uint32_t parent) {
    T c = original;
    c.id = nextId(g);
    c.parent = parent;
    c.locked = false;
    v.push_back(c);
    return c.id;
}

} // namespace

uint32_t copySubtree(SceneGraph& g, uint32_t id, bool instance, uint32_t parent, std::map<uint32_t, uint32_t>* copied) {
    if (const Entity* e = byId(g.entities, id)) {
        Entity c = instance ? *e : resolveInstance(g, *e); // a copy of an instance is a shape of its own
        c.id = nextId(g);
        c.parent = parent;
        c.locked = false;
        c.instanceOf = instance ? masterOf(g, id) : 0;
        g.entities.push_back(c);
        if (copied) (*copied)[id] = c.id;
        std::vector<SensorObject> sensors; // its sensors, forces and point wheels go with it, as a body's components do
        for (const SensorObject& x : g.sensors)
            if (x.parent == id) sensors.push_back(x);
        for (SensorObject x : sensors) x.id = nextId(g), x.parent = c.id, x.name += " копия", g.sensors.push_back(x);
        std::vector<Force> forces; // a thrust of its own pushes the copy as it pushes the original
        for (const Force& f : g.forces)
            if (f.parent == id) forces.push_back(f);
        for (Force f : forces) f.id = nextId(g), f.parent = c.id, f.name += " копия", g.forces.push_back(f);
        std::vector<WheelObject> wheels;
        for (const WheelObject& w : g.wheels)
            if (w.parent == id) wheels.push_back(w);
        for (const WheelObject& w : wheels) copySubtree(g, w.id, false, c.id), objectIn(g, g.wheels.back().id)->name = w.name + " копия";
        return c.id;
    }
    if (const SensorObject* x = byId(g.sensors, id)) return copyPlain(g, g.sensors, SensorObject(*x), parent);
    if (const WheelObject* w = byId(g.wheels, id)) { // a wheel's encoders go with it
        const uint32_t copy = copyPlain(g, g.wheels, WheelObject(*w), parent);
        std::vector<SensorObject> sensors;
        for (const SensorObject& x : g.sensors)
            if (x.parent == id) sensors.push_back(x);
        for (SensorObject x : sensors) x.id = nextId(g), x.parent = copy, x.name += " копия", g.sensors.push_back(x);
        return copy;
    }
    if (const Group* o = byId(g.groups, id)) {
        const std::vector<uint32_t> kids = childrenOf(g, id); // before the copy joins the graph
        const uint32_t copy = copyPlain(g, g.groups, Group(*o), parent);
        for (uint32_t kid : kids) copySubtree(g, kid, instance, copy, copied);
        return copy;
    }
    if (const ArrayObject* a = byId(g.arrays, id)) return copyPlain(g, g.arrays, ArrayObject(*a), parent); // the copy shares the template
    if (const Light* l = byId(g.lights, id)) return copyPlain(g, g.lights, Light(*l), parent);
    if (const Force* f = byId(g.forces, id)) return copyPlain(g, g.forces, Force(*f), parent);
    if (const Camera* cam = byId(g.cameras, id)) {
        const uint32_t copy = copyPlain(g, g.cameras, Camera(*cam), parent);
        byId(g.cameras, copy)->active = false; // one active camera: the original stays it
        return copy;
    }
    return 0;
}

// An instance whose master goes keeps what it looked like: it becomes an ordinary shape.
void deleteObjects(SceneGraph& g, const std::vector<uint32_t>& ids) {
    std::set<uint32_t> dead;
    auto gone = [&](uint32_t id) {
        const std::vector<uint32_t> chain = ancestry(g, id);
        return std::any_of(chain.begin(), chain.end(), [&](uint32_t x) { return contains(ids, x); });
    };
    auto collect = [&](const auto& objects) {
        for (const auto& o : objects)
            if (gone(o.id)) dead.insert(o.id);
    };
    collect(g.entities), collect(g.groups);
    for (const ArrayObject& a : g.arrays)
        if (gone(a.id) || dead.count(a.templateId)) dead.insert(a.id);
    collect(g.lights), collect(g.cameras), collect(g.forces);
    for (const Entity& e : g.entities) // a camera's own target goes with it
        if (e.targetOf && dead.count(e.targetOf)) dead.insert(e.id);
    for (Entity& e : g.entities)
        if (!dead.count(e.id) && e.instanceOf != 0 && dead.count(masterOf(g, e.id))) e = resolveInstance(g, e), e.instanceOf = 0;
    for (const WheelObject& w : g.wheels)
        if (gone(w.id) || dead.count(w.parent)) dead.insert(w.id);
    collect(g.sensors);
    std::vector<uint32_t> joints;
    for (const JointObject& j : g.joints) joints.push_back(j.id);
    for (uint32_t id : dead) removeJointsOf(g, id); // a joint cannot live without both its links
    for (uint32_t j : joints)
        if (!findJoint(g, j)) dead.insert(j); // and a sensor not without its parent, an entity or a joint
    g.sensors.erase(std::remove_if(g.sensors.begin(), g.sensors.end(), [&](const SensorObject& s) { return dead.count(s.id) || dead.count(s.parent); }),
                    g.sensors.end());
    // A battery, a driver or a controller named (the drivers of a removed battery stay: unpowered, they say so; a
    // controller whose goal went stays, driving nothing).
    std::set<uint32_t> named(ids.begin(), ids.end());
    eraseDead(g.batteries, named), eraseDead(g.drivers, named), eraseDead(g.controllers, named);
    eraseDead(g.entities, dead), eraseDead(g.groups, dead), eraseDead(g.arrays, dead), eraseDead(g.lights, dead), eraseDead(g.cameras, dead), eraseDead(g.forces, dead);
    eraseDead(g.wheels, dead);
    eraseDead(g.sequences, named); // a sequence named; an object gone leaves its track lost (skipped)
}

std::vector<uint32_t> copyJoints(SceneGraph& g, const std::map<uint32_t, uint32_t>& copied) {
    std::vector<uint32_t> made;
    const std::vector<JointObject> joints = g.joints; // the copies join the list as it is walked
    for (const JointObject& j : joints) {
        const auto a = copied.find(j.a), b = copied.find(j.b);
        if (a == copied.end() || (j.b != 0 && b == copied.end())) continue;
        JointObject c = j;
        c.id = nextId(g), c.a = a->second, c.b = j.b ? b->second : 0, c.name += " копия";
        if (!j.b) { // its frame in the world: carried as the moving link was, original -> copy
            Vector3 originA, originB, pa, pc;
            Quaternion frameA, frameB, qa, qc;
            jointWorldFrames(g, j, originA, frameA, originB, frameB);
            worldPose(g, *findObject(g, j.a), pa, qa);
            worldPose(g, *findObject(g, c.a), pc, qc);
            const Quaternion turn = (qc * qa.conjugate()).normalized();
            c.anchorB = pc + turn.rotate(originB - pa);
            c.frameB = eulerDegrees((turn * frameB).normalized());
        }
        g.joints.push_back(c);
        made.push_back(c.id);
        std::vector<SensorObject> sensors; // an encoder or an effort sensor on it
        for (const SensorObject& x : g.sensors)
            if (x.parent == j.id) sensors.push_back(x);
        for (SensorObject x : sensors) x.id = nextId(g), x.parent = c.id, x.name += " копия", g.sensors.push_back(x);
        std::vector<DriverObject> drivers; // its motor's bridge, on the same battery
        for (const DriverObject& d : g.drivers)
            if (d.joint == j.id) drivers.push_back(d);
        for (DriverObject d : drivers) d.id = nextId(g), d.joint = c.id, d.name += " копия", g.drivers.push_back(d);
    }
    return made;
}

std::vector<uint32_t> duplicateObjects(SceneGraph& g, const std::vector<uint32_t>& roots, const Vector3* by) {
    std::vector<uint32_t> copies;
    std::map<uint32_t, uint32_t> copied;
    for (uint32_t id : roots) {
        const SceneObject* original = findObject(g, id);
        if (!original) continue;
        const AABB box = objectBounds(g, id);
        const uint32_t c = copySubtree(g, id, false, original->parent, &copied);
        SceneObject* o = objectIn(g, c);
        o->name += " копия";
        Vector3 p;
        Quaternion q;
        worldPose(g, *o, p, q);
        const Vector3 keep = o->rotationDeg;
        if (by) // where it was asked to be
            setWorldPose(g, *o, p + *by, q);
        else if (!byId(g.sensors, c)) // a sensor's copy stays on its mount; anything else goes on top of the original
            setWorldPose(g, *o, p + Vector3(0.0_r, (box.valid() ? box.extent().y : 0.2_r) + 0.02_r, 0.0_r), q);
        o->rotationDeg = keep;
        copies.push_back(c);
    }
    copyJoints(g, copied);
    return copies;
}

// Members of one parent group stay inside it; the group stands at the centre of their boxes.
uint32_t groupObjects(SceneGraph& g, const std::vector<uint32_t>& roots) {
    if (roots.empty() || !findObject(g, roots.front())) return 0;
    Group group;
    group.id = nextId(g);
    group.name = "Группа " + std::to_string(g.groups.size() + 1);
    AABB box;
    for (uint32_t id : roots) box.expand(objectBounds(g, id));
    const uint32_t parent = findObject(g, roots.front())->parent;
    const bool oneParent = std::all_of(roots.begin(), roots.end(), [&](uint32_t id) { return findObject(g, id) && findObject(g, id)->parent == parent; });
    group.parent = oneParent && findObject(g, parent) ? parent : 0;
    Vector3 first;
    Quaternion turned;
    worldPose(g, *findObject(g, roots.front()), first, turned);
    setWorldPose(g, group, box.valid() ? box.center() : first, Quaternion());
    std::vector<std::pair<Vector3, Quaternion>> poses(roots.size());
    for (size_t i = 0; i < roots.size(); ++i) worldPose(g, *findObject(g, roots[i]), poses[i].first, poses[i].second);
    g.groups.push_back(group);
    for (size_t i = 0; i < roots.size(); ++i) {
        SceneObject* o = objectIn(g, roots[i]);
        o->parent = group.id;
        setWorldPose(g, *o, poses[i].first, poses[i].second);
    }
    return group.id;
}

std::vector<uint32_t> ungroupObjects(SceneGraph& g, const std::vector<uint32_t>& groups) {
    std::vector<uint32_t> freed;
    for (uint32_t gid : groups) {
        const Group* group = byId(g.groups, gid);
        if (!group) continue;
        const uint32_t up = group->parent;
        for (uint32_t kid : childrenOf(g, gid)) {
            SceneObject* o = objectIn(g, kid);
            Vector3 p;
            Quaternion q;
            worldPose(g, *o, p, q);
            o->parent = up;
            setWorldPose(g, *o, p, q);
            freed.push_back(kid);
        }
        eraseDead(g.groups, std::set<uint32_t>{gid});
    }
    return freed;
}

std::vector<uint32_t> arrayObjects(SceneGraph& g, const std::vector<uint32_t>& shapes) {
    std::vector<uint32_t> arrays;
    for (uint32_t id : shapes) {
        const Entity* e = byId(g.entities, id);
        const bool hiddenTemplate = e && !e->visible && std::any_of(g.arrays.begin(), g.arrays.end(), [&](const ArrayObject& a) { return a.templateId == id; });
        if (!e || hiddenTemplate) continue;
        const AABB box = objectBounds(g, id);
        ArrayObject a;
        a.id = nextId(g);
        a.templateId = id;
        a.name = "Массив: " + e->name;
        a.parent = e->parent;
        a.color = e->color;
        a.pattern = ArrayPattern::Line;
        a.step = Vector3((box.valid() ? box.extent().x : 0.2_r) + 0.05_r, 0.0_r, 0.0_r);
        a.count[0] = 5;
        Vector3 p;
        Quaternion q;
        worldPose(g, *e, p, q);
        setWorldPose(g, a, p, Quaternion());
        byId(g.entities, id)->visible = false; // the template: the array draws it
        g.arrays.push_back(a);
        arrays.push_back(a.id);
    }
    return arrays;
}

Quaternion turnOf(const Vector3& axis, Real degrees) { return Quaternion::fromAxisAngle(normalize(axis), degrees * (kPi / 180.0_r)); }

void moveObjects(SceneGraph& g, const SceneGraph& start, const std::vector<uint32_t>& ids, const Vector3& by) {
    for (uint32_t id : ids) {
        SceneObject* o = objectIn(g, id);
        const SceneObject* s = findObject(start, id);
        if (!o || !s) continue;
        Vector3 p;
        Quaternion q;
        worldPose(start, *s, p, q);
        const Vector3 keep = s->rotationDeg;
        setWorldPose(g, *o, p + by, q);
        o->rotationDeg = keep; // a move never turns: the angles stay exactly as typed
    }
}

void turnObjects(SceneGraph& g, const SceneGraph& start, const std::vector<uint32_t>& ids, const Vector3& pivot, const Quaternion& turn) {
    for (uint32_t id : ids) {
        SceneObject* o = objectIn(g, id);
        const SceneObject* s = findObject(start, id);
        if (!o || !s) continue;
        Vector3 p;
        Quaternion q;
        worldPose(start, *s, p, q);
        setWorldPose(g, *o, pivot + turn.rotate(p - pivot), (turn * q).normalized());
    }
}

// Every shape inside the object (for a shape: the geometry it shows, an instance's master) gets its
// size from `start` times the factors; inside a group or an array the distances grow by them too.
void scaleObjects(SceneGraph& g, const SceneGraph& start, const std::vector<uint32_t>& ids, const Vector3& f) {
    for (uint32_t id : ids) {
        if (ArrayObject* a = byId(g.arrays, id)) {
            for (const ArrayObject& s : start.arrays)
                if (s.id == id) a->step = s.step * f, a->radius = s.radius * std::max(f.x, f.z);
            continue;
        }
        std::vector<uint32_t> sized; // a master shared by several instances is scaled once
        for (const Entity& e : g.entities) {
            if (!isInside(g, e.id, id)) continue;
            Entity* geometry = byId(g.entities, masterOf(g, e.id));
            const auto s = std::find_if(start.entities.begin(), start.entities.end(), [&](const Entity& x) { return x.id == geometry->id; });
            if (s == start.entities.end() || contains(sized, geometry->id)) continue;
            geometry->size = vmax(s->size * f, Vector3(0.005_r));
            sized.push_back(geometry->id);
        }
        if (!byId(g.groups, id)) continue;
        for (uint32_t kid : childrenOf(g, id)) // the members' places in the group
            if (const SceneObject* s = findObject(start, kid)) objectIn(g, kid)->position = s->position * f;
    }
}

} // namespace ke
