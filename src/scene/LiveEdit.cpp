// The classifier of an edit while the world plays (scene/LiveEdit.h): one section of the graph against the same section
// of the graph the world was built from, in the order the window always asked.
#include "scene/LiveEdit.h"

namespace ke {

namespace {

std::string devicesText(const SceneGraph& g, bool duties) {
    SceneGraph devices;
    devices.batteries = g.batteries;
    devices.drivers = g.drivers;
    if (!duties)
        for (DriverObject& d : devices.drivers) d.duty = 0;
    devices.controllers = g.controllers;
    devices.sensors = g.sensors;
    devices.wheels = g.wheels;
    return devices.save();
}

// The entities the world is built of, or (handles) the goals it only follows.
std::string entitiesText(const SceneGraph& g, bool handles) {
    SceneGraph entities;
    for (const Entity& e : g.entities)
        if (isHandle(e) == handles) entities.entities.push_back(e);
    return entities.save();
}

template <class Section>
bool differ(const SceneGraph& a, const SceneGraph& b, Section SceneGraph::*section) {
    SceneGraph x, y;
    x.*section = a.*section;
    y.*section = b.*section;
    return x.save() != y.save();
}

} // namespace

LiveEdit liveEdit(const SceneGraph& running, const SceneGraph& edited) {
    LiveEdit r;
    if (differ(running, edited, &SceneGraph::world)) r.reload = LiveEdit::Reload::World;
    else if (differ(running, edited, &SceneGraph::groups)) r.reload = LiveEdit::Reload::Groups;
    else if (differ(running, edited, &SceneGraph::arrays)) r.reload = LiveEdit::Reload::Arrays;
    else if (devicesText(running, false) != devicesText(edited, false)) r.reload = LiveEdit::Reload::Devices;
    else if (entitiesText(running, false) != entitiesText(edited, false)) r.reload = LiveEdit::Reload::Bodies;
    if (r.reload != LiveEdit::Reload::None) return r;
    r.duties = devicesText(running, true) != devicesText(edited, true);
    r.lights = differ(running, edited, &SceneGraph::lights) || differ(running, edited, &SceneGraph::cameras);
    r.goals = entitiesText(running, true) != entitiesText(edited, true);
    r.joints = differ(running, edited, &SceneGraph::joints);
    r.forces = differ(running, edited, &SceneGraph::forces);
    return r;
}

const char* reloadWord(LiveEdit::Reload r) {
    switch (r) {
    case LiveEdit::Reload::World: return "world";
    case LiveEdit::Reload::Groups: return "groups";
    case LiveEdit::Reload::Arrays: return "arrays";
    case LiveEdit::Reload::Devices: return "devices";
    case LiveEdit::Reload::Bodies: return "bodies";
    case LiveEdit::Reload::Joints: return "joints";
    case LiveEdit::Reload::None: break;
    }
    return "";
}

} // namespace ke
