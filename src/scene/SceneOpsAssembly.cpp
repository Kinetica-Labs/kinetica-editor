// The assembly operations of the core (SceneOps.h, the work in Assembly.h): where a link goes against a
// face, the ready links (presets) and the one a click most likely means, putting one; a link attached to
// a face with a joint (the magnet's click), a joint between two shapes, a sensor on a face, an encoder on
// a joint, a joint's motor powered from a battery. Faces are "+x" ... "-z" of the target's own frame,
// or their sides front, rear, top, bottom, right, left; joint kinds the scene file's words.
#include "scene/SceneOps.h"

#include "scene/Assembly.h"
#include "scene/CameraHousing.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

const char* kFaces[] = {"+x", "-x", "+y", "-y", "+z", "-z"};

// A face by its axis ("+x" ... "-z") or by its side in the body's own frame, as the modules name sides:
// front +x, rear -x, top +y, bottom -y, right +z, left -z.
bool face(const Value& v, BoxFace& out) {
    static const char* sides[] = {"front", "rear", "top", "bottom", "right", "left"};
    for (int f = 0; f < 6; ++f)
        if (v.text("") == kFaces[f] || v.text("") == sides[f]) return out = BoxFace(f), true;
    return false;
}

const Entity* entityOf(const SceneGraph& g, const Value& a, const char* key = "target") {
    const uint32_t id = uint32_t(a[key].integer());
    for (const Entity& e : g.entities)
        if (e.id == id) return &e;
    return nullptr;
}

// The place the arguments name: a face by name, or the polygon hit (point and normal), or the face
// nearest a point.
FaceSnap placeFrom(const SceneGraph& g, const Value& a, const Vector3& size) {
    const uint32_t target = uint32_t(a["target"].integer());
    BoxFace f;
    Vector3 point, normal;
    if (face(a["face"], f)) return snapToFace(g, target, f, size);
    if (!vectorArg(a, "point", point)) return FaceSnap();
    return vectorArg(a, "normal", normal) ? snapToSurface(g, target, point, normal, size) : snapToFace(g, target, point, size);
}

// A joint's kind by the API's word (weld, hinge, slider, ball, cylinder, universal, planar; none: a hinge).
bool kindOk(const Value& v) {
    JointObject probe;
    return v.isNull() || setJointKind(probe, v.text(""));
}

void setKind(SceneGraph& g, uint32_t joint, const Value& v) {
    if (v.isNull()) return;
    for (JointObject& j : g.joints)
        if (j.id == joint) setJointKind(j, v.text(""));
}

PresetOptions presetOptions(const Value& a) {
    PresetOptions o;
    o.axis = a["axis"].integer(-1);
    o.flip = a["flip"].truth();
    o.mirror = a["mirror"].truth();
    return o;
}

// The new link: the target's shape, size and components (its own size if asked), moving even when the
// target is fixed, at rest, named "Звено N", in the next colour of a small palette.
Entity linkLike(const SceneGraph& g, const Entity& target, const Value& a) {
    static const Vector3 palette[] = {{0.85_r, 0.55_r, 0.25_r}, {0.3_r, 0.6_r, 0.9_r}, {0.45_r, 0.8_r, 0.45_r}, {0.85_r, 0.4_r, 0.55_r}, {0.75_r, 0.75_r, 0.3_r}};
    Entity e = resolveInstance(g, target);
    vectorArg(a, "size", e.size);
    e.rigid.fixed = false;
    e.rigid.velocity = e.rigid.angularVelocity = Vector3(0.0_r);
    const auto links = std::count_if(g.entities.begin(), g.entities.end(), [](const Entity& o) { return o.name.rfind("Звено", 0) == 0; });
    e.name = a.has("name") ? a["name"].text("") : "Звено " + std::to_string(links + 1);
    e.color = palette[links % 5];
    return e;
}

Value snapValue(const FaceSnap& s) {
    Value r = Value::map(), across = Value::list();
    across.push(vectorValue(s.across[0])), across.push(vectorValue(s.across[1]));
    r.set("face", kFaces[int(s.face)]), r.set("anchor", vectorValue(s.anchor)), r.set("normal", vectorValue(s.normal));
    r.set("position", vectorValue(s.position)), r.set("rotation", vectorValue(s.rotationDeg)), r.set("across", across);
    return r;
}

std::vector<SceneOp> presetOps() {
    return {
        {"assembly_presets", "The ready links in the pie menu's order (clockwise from the top): word, name", "{}",
         [](const SceneGraph&, const Value&, Value& r, std::string&) {
             Value out = Value::list();
             for (int k = 0; k < kLinkPresets; ++k) {
                 Value row = Value::map();
                 row.set("word", presetWord(LinkPreset(k))), row.set("name", presetName(LinkPreset(k)));
                 out.push(row);
             }
             return r.set("presets", out), true;
         }, {}},
        {"assembly_suggest", "The ready link a click most likely means at a place (a face, or point + normal of the polygon hit)",
         "{target: int, face?: '+x'..'-z', point?: [x,y,z], normal?: [x,y,z]}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
             const Entity* t = entityOf(g, a);
             const FaceSnap at = t ? placeFrom(g, a, t->size) : FaceSnap();
             if (!at.found) return e = "no shape, or no place on it", false;
             const LinkPreset p = suggestPreset(g, t->id, at);
             return r.set("preset", presetWord(p)), r.set("name", presetName(p)), r.set("face", kFaces[int(at.face)]), true;
         }, {}},
        {"assembly_preset", "Put a ready link (assembly_presets) at a place: wheels, a hinge about the normal, a slider...",
         "{target: int, preset: str, face?: '+x'..'-z', point?: [x,y,z], normal?: [x,y,z], axis?: 0|1|2, flip?: bool, mirror?: bool}", {},
         [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
             LinkPreset p;
             const Entity* t = entityOf(g, a);
             if (!t || !presetFromWord(a["preset"].text(""), p)) return e = "needs a shape's id and a known preset", false;
             const FaceSnap at = placeFrom(g, a, t->size);
             Value links = Value::list();
             for (const Attached& l : applyPreset(g, t->id, at, p, presetOptions(a))) {
                 Value link = Value::map();
                 link.set("entity", l.entity), link.set("joint", l.joint);
                 links.push(link);
             }
             return r.set("links", links), links.size() > 0 || (e = "no place on the shape", false);
         }},
    };
}

// A wheel module at a side of a body (Assembly.h, addWheelModule): its parts by name; the wheel selected.
SceneOp wheelModuleOp() {
    return {"assembly_wheel", "A wheel module at a side of a body (forward +x, left -z of its own frame): a shock absorber, a motor wheel "
                   "whose positive duty drives forward, an encoder counting forward as positive, a driver at duty 0 on the battery",
                   "{target: int, side: front_left|front_right|rear_left|rear_right|left|right, model?: bodies|point, suspension?: bool, "
                   "encoder?: lines (0: none), power?: bool, battery?: int, radius?: m, rolling?: C_rr of a point wheel}", {},
                   [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Side side;
        if (!entityOf(g, a) || !sideFromWord(a["side"].text(""), side)) return e = "needs a shape's id and a side: front_left, front_right, rear_left, rear_right, left, right", false;
        WheelModuleOptions o;
        o.suspension = a["suspension"].truth(true), o.encoderLines = a["encoder"].integer(500), o.powered = a["power"].truth(true);
        o.battery = uint32_t(a["battery"].integer(0)), o.radius = Real(a["radius"].number(0.0));
        const std::string model = a["model"].text("bodies");
        if (model != "bodies" && model != "point") return e = "model is bodies (links and joints) or point (a point wheel: fast)", false;
        o.point = model == "point", o.rolling = Real(a["rolling"].number(double(o.rolling)));
        if (o.radius < 0 || o.encoderLines < 0) return e = "radius and encoder lines are not negative", false;
        const WheelModule m = addWheelModule(g, uint32_t(a["target"].integer()), side, o);
        r.set("wheel", m.wheel), r.set("axle", m.axle), r.set("shock", m.shock), r.set("strut", m.strut);
        r.set("encoder", m.encoder), r.set("driver", m.driver), r.set("battery", m.battery);
        r.set("ids", idsValue({m.wheel})), r.set("id", m.wheel);
        return m.wheel != 0 || (e = "no wheel could be put there", false);
    }};
}

} // namespace

// assembly_drive: the freedom's drive, the fields given over the ones it has (a target changed keeps the
// stiffness), each checked: finite, not negative where a negative means nothing, a DC motor's constant above 0.
bool driveOne(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    JointObject* j = nullptr;
    for (JointObject& x : g.joints)
        if (x.id == uint32_t(a["joint"].number())) j = &x;
    if (!j) return e = "no joint " + std::to_string(uint32_t(a["joint"].number())), false;
    static const char* dofs[] = {"tx", "ty", "tz", "rx", "ry", "rz"};
    int dof = mainFreedom(*j);
    if (a.has("dof")) {
        dof = -1;
        for (int k = 0; k < 6; ++k)
            if (a["dof"].text("") == dofs[k]) dof = k;
    }
    if (dof < 0) return e = "dof is tx, ty, tz, rx, ry or rz (the joint has no free one to take by default)", false;
    JointDrive d = j->dof[dof].drive;
    const std::string kind = a["kind"].text(d.constant > 0 ? "dcmotor" : d.stiffness > 0 || !d.enabled ? "servo" : "velocity");
    if (kind != "servo" && kind != "velocity" && kind != "dcmotor") return e = "kind is servo, velocity or dcmotor", false;
    const struct { const char* name; Real* to; bool positive; } fields[] = {
        {"stiffness", &d.stiffness, true}, {"damping", &d.damping, true}, {"target", &d.target, false}, {"velocity", &d.targetVelocity, false},
        {"force", &d.maxForce, true}, {"voltage", &d.voltage, false}, {"resistance", &d.resistance, true}, {"constant", &d.constant, true}};
    for (const auto& f : fields) {
        if (!a.has(f.name)) continue;
        const double v = a[f.name].number(std::nan(""));
        if (!std::isfinite(v) || (f.positive && v < 0)) return e = std::string(f.name) + (f.positive ? " is a number, 0 or more" : " is a number"), false;
        *f.to = Real(v);
    }
    if (kind == "velocity") d.stiffness = 0;
    if (kind != "dcmotor") d.constant = 0;
    if (kind == "dcmotor" && !(d.constant > 0 && d.resistance > 0)) return e = "a DC motor needs constant > 0 and resistance > 0", false;
    d.enabled = a.has("on") ? a["on"].truth(true) : true;
    j->dof[dof].drive = d;
    if (j->dof[dof].motion == JointMotion::Locked) j->dof[dof].motion = JointMotion::Free; // a drive on a locked freedom would move nothing
    r.set("joint", j->id), r.set("dof", dofs[dof]), r.set("kind", kind);
    return true;
}

// The same fields on several joints at once (joints: [...]): one edit, so a running scene takes them in the same step -
// a gripper's two fingers let go together (a call a finger, a window's frame apart, and the finger still closing
// pushed the cube a centimetre along before the other's call came).
bool driveOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    if (!a.has("joints")) return driveOne(g, a, r, e);
    Value each = Value::list();
    for (const Value& id : a["joints"].items()) {
        Value one = a, answer = Value::map();
        one.set("joint", id.number());
        if (!driveOne(g, one, answer, e)) return false;
        each.push(answer);
    }
    r.set("joints", each);
    return true;
}

SceneOp driveSceneOp() {
    return {"assembly_drive", "A drive on a joint's freedom (none: its main one - a hinge's turn, a slider's travel), its output a "
                   "generalised force the robot's own code puts on the joint: servo (stiffness, damping, target, velocity, force), velocity motor (damping, "
                   "velocity, force) or DC motor (voltage, resistance, constant; a driver feeds it: assembly_power). Fields not given keep "
                   "their values; joints: several at once, in the same step of a running scene (a gripper's fingers)",
                   "{joint: int | joints: [int], dof?: tx|ty|tz|rx|ry|rz, kind?: servo|velocity|dcmotor, stiffness?, damping?, target?, "
                   "velocity?, force?, voltage?, resistance?, constant?, on?: bool}", {}, driveOp};
}

// assembly_grip: a gripper closed or opened, as Robotiq's grippers are driven - a target and a force limit: closing,
// every finger slides toward its far end with its drive capped at `force`, so it stops on what it meets and presses
// with that force (a jelly is not crushed, a cube held by mu N); opening, back to its near end with the open force;
// travel: to that far from open, a step at a time (the window's wheel: an analog trigger, as a VR controller's).
// The fingers are the sliders on the effector of the goal given (an IK goal, or the effector itself), all in one edit,
// so they move in the same step (driveOp: why).
bool gripOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const uint32_t id = uint32_t(a["goal"].number());
    const Entity* goal = nullptr;
    for (const Entity& x : g.entities)
        if (x.id == id) goal = &x;
    if (!goal) return e = "no object " + std::to_string(id), false;
    const uint32_t effector = goal->goalOf ? goal->goalOf : goal->id;
    const bool byTravel = a.has("travel"); // a step of the wheel: the fingers to this travel from open, capped by force
    const bool close = byTravel || a["close"].truth(true);
    const double force = close ? a["force"].number(2.0) : a["open_force"].number(40.0), travel = a["travel"].number(0.0);
    if (byTravel && !(std::isfinite(travel) && travel >= 0)) return e = "travel is metres from open, 0 or more", false;
    if (!std::isfinite(force) || force <= 0) return e = "force is a number above 0", false;
    Value joints = Value::list();
    for (JointObject& j : g.joints) {
        const int dof = mainFreedom(j);
        if (dof < 0 || dof > 2 || (j.a != effector && j.b != effector) || j.dof[dof].motion == JointMotion::Locked) continue;
        Value one = Value::map(), answer = Value::map();
        one.set("joint", Real(j.id)), one.set("kind", "servo"), one.set("force", force);
        const Real lo = j.dof[dof].lower, hi = j.dof[dof].upper;
        one.set("target", byTravel ? std::min(hi, lo + Real(travel)) : close ? hi : lo);
        if (!driveOne(g, one, answer, e)) return false;
        joints.push(Real(j.id));
    }
    if (joints.items().empty()) return e = "no gripper fingers (sliders) on the effector of " + goal->name, false;
    r.set("joints", joints), r.set("close", close), r.set("force", force);
    if (byTravel) r.set("travel", travel);
    return true;
}

SceneOp gripSceneOp() {
    return {"assembly_grip", "A gripper closed (close: true, the default) or opened, as a Robotiq gripper is driven: each finger - a "
                   "slider on the effector of the IK goal given (or of the effector itself) - slides to its far end with its drive capped "
                   "at force N (default 2), so it stops on what it meets and presses with that force; opening, back to its near end with "
                   "open_force (default 40); travel: to that many metres from open, capped at force (a wheel's step). All the fingers in "
                   "one step",
                   "{goal: int, close?: bool, force?: number, open_force?: number, travel?: number}", {}, gripOp};
}

// assembly_sensor: a face's kinds on a shape, a joint's kinds on a joint.
static bool sensorOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const std::string kind = a["kind"].text("");
    if (kind == "wrench" || kind == "effort") {
        const char* names[] = {"tx", "ty", "tz", "rx", "ry", "rz"}; // JointDof
        int dof = -1;
        const std::string name = a["dof"].text("");
        for (int k = 0; k < 6; ++k)
            if (name == names[k]) dof = k;
        if (!name.empty() && dof < 0) return e = "dof is one of tx, ty, tz, rx, ry, rz", false;
        const uint32_t id = attachJointSensor(g, uint32_t(a["joint"].integer()), kind == "wrench" ? SensorKind::Wrench : SensorKind::Effort, dof);
        return r.set("id", id), id != 0 || (e = "no joint " + std::to_string(a["joint"].integer()), false);
    }
    const Entity* t = entityOf(g, a);
    if (!t || (kind != "imu" && kind != "sonar" && kind != "touch" && kind != "stereo"))
        return e = "needs a shape's id and kind imu, sonar, touch or stereo - or a joint's id and kind wrench or effort", false;
    const SensorKind k = kind == "imu" ? SensorKind::Imu : kind == "sonar" ? SensorKind::Sonar : kind == "touch" ? SensorKind::Touch : SensorKind::Stereo;
    const uint32_t id = attachSensor(g, t->id, placeFrom(g, a, Vector3(0.0_r)), k);
    return r.set("id", id), id != 0 || (e = "no place on the shape", false);
}

// assembly_camera: a stereo camera with its housing (CameraHousing.h), on a face of a shape or free in the world.
static bool cameraOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    CameraHousing h;
    h.model = a["model"].text("");
    h.name = a["name"].text(h.name);
    h.baseline = Real(a["baseline"].number(double(h.baseline)));
    h.fovDeg = Real(a["fov"].number(double(h.fovDeg)));
    h.width = int(a["width"].integer(h.width)), h.height = int(a["height"].integer(h.height));
    if (!(h.baseline > 0 && h.baseline < 2 && h.fovDeg > 1 && h.fovDeg < 170 && h.width >= 16 && h.height >= 16 && h.width <= 4096 && h.height <= 4096))
        return e = "baseline 0..2 m, fov 1..170 degrees, width and height 16..4096 pixels", false;
    Vector3 position, lookAt;
    PlacedCamera c;
    if (a.has("target")) {
        const Entity* t = entityOf(g, a);
        if (!t) return e = "no shape " + std::to_string(a["target"].integer()), false;
        c = mountStereoCamera(g, h, t->id, placeFrom(g, a, Vector3(0.0_r)));
    } else if (vectorArg(a, "position", position) && vectorArg(a, "look_at", lookAt)) {
        c = addStereoCamera(g, h, position, lookAt);
    } else {
        return e = "needs a target shape (and its face, or point and normal) or a position and look_at", false;
    }
    if (!c.problem.empty()) return e = c.problem, false;
    return r.set("housing", c.housing), r.set("sensor", c.sensor), r.set("joint", c.joint), true;
}

static SceneOp encoderSceneOp() {
    return {"assembly_encoder", "An incremental encoder on the joint's main freedom. A line gives four quadrature counts. "
                                "reference explicitly supplies the calibrated joint coordinate at count zero, in rad or m; "
                                "omitting it leaves absolute position unreferenced. No automatic homing or true-pose lookup.",
            "{joint: int, lines?: int, reference?: rad|m}", {},
            [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        if (a.has("reference") && (a["reference"].type() != Value::Type::Number || !std::isfinite(a["reference"].number()) ||
                                  std::fabs(a["reference"].number()) > 1e6)) return e = "reference is a finite calibrated radian or metre coordinate", false;
        const uint32_t id = attachEncoder(g, uint32_t(a["joint"].integer()), a["lines"].integer(500));
        if (id && a.has("reference"))
            for (SensorObject& sensor : g.sensors)
                if (sensor.id == id) sensor.hasEncoderReference = true, sensor.encoderReference = Real(a["reference"].number());
        return r.set("id", id), id != 0 || (e = "no joint " + std::to_string(a["joint"].integer()), false);
    }};
}

std::vector<SceneOp> assemblySceneOps() {
    std::vector<SceneOp> ops = presetOps();
    ops.push_back({"assembly_snap", "Where a link of `size` goes against a face of the target (a face, or the one nearest a world point)",
                   "{target: int, face?: '+x'..'-z', point?: [x,y,z], normal?: [x,y,z], size?: [x,y,z]}", [](const SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Vector3 size(0.2_r);
        vectorArg(a, "size", size);
        const FaceSnap s = placeFrom(g, a, size);
        if (!s.found) return e = "no shape " + std::to_string(a["target"].integer()) + ", or no face or point given", false;
        const Value v = snapValue(s);
        for (size_t k = 0; k < v.keys().size(); ++k) r.set(v.keys()[k], v.at(k));
        return true;
    }, {}});
    ops.push_back({"assembly_attach", "A new link against a face of the target, joined to it at the face's centre (the magnet's click). "
                   "axis: 0 and 1 across the face, 2 along its normal (a wheel)",
                   "{target: int, face: '+x'..'-z', joint?: 'hinge'|'weld'|'slider'|'ball'|'cylinder'|'universal'|'planar', axis?: 0|1|2, size?: [x,y,z], name?: str}",
                   {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        BoxFace f;
        const Entity* t = entityOf(g, a);
        if (!t || !face(a["face"], f) || !kindOk(a["joint"])) return e = "needs a shape's id, a face and a known joint kind", false;
        const Attached done = attachToFace(g, t->id, f, linkLike(g, *t, a), JointType::Hinge, a["axis"].integer(0));
        setKind(g, done.joint, a["joint"]);
        return r.set("entity", done.entity), r.set("joint", done.joint), true;
    }});
    ops.push_back({"assembly_joint", "A joint between two shapes (b 0: the world) at a world point along a world direction",
                   "{a: int, b: int, type?: 'hinge'|'weld'|'slider'|'ball'|'cylinder'|'universal'|'planar', anchor: [x,y,z], axis?: [x,y,z]}",
                   {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Vector3 anchor, axis(0, 1, 0);
        const uint32_t ea = uint32_t(a["a"].integer()), eb = uint32_t(a["b"].integer());
        if (!kindOk(a["type"]) || !vectorArg(a, "anchor", anchor) || !findObject(g, ea) || (eb != 0 && !findObject(g, eb)))
            return e = "needs two shapes (b 0: the world), a known kind and an anchor", false;
        vectorArg(a, "axis", axis);
        const uint32_t joint = addJoint(g, JointType::Hinge, ea, eb, anchor, axis);
        setKind(g, joint, a["type"]);
        return r.set("joint", joint), true;
    }});
    ops.push_back({"assembly_sensor", "A sensor put on a thing, no shape of its own: on a face of a shape a sonar looks out of it (x along the "
                                      "normal), an IMU lies flat on it (z out), a touch pad reads the contacts' normal force through it, a stereo "
                                      "camera looks out of it (its two lenses across the face; sensor_capture takes its frame); on a joint "
                                      "a wrench reads the force and torque it passes to its link, an effort its drive's force along a freedom",
                   "{target: int, kind: 'imu'|'sonar'|'touch'|'stereo', face?: '+x'..'-z', point?: [x,y,z], normal?: [x,y,z]} | "
                   "{joint: int, kind: 'wrench'|'effort', dof?: 'tx'..'rz'}", {}, sensorOp});
    ops.push_back({"assembly_camera", "A stereo camera with a body of its own, as one is bought: the housing (model: an OBJ or STL made in "
                                      "the mount's frame - +x out of the lenses, +y along the baseline, lenses on its mid-line, metres; none: a "
                                      "box about the lenses) and the stereo sensor its child on the lenses' axis, 1 mm before its front. On a "
                                      "face of a shape: its back on the face, looking out, welded to the shape when that moves. Free: fixed at "
                                      "position, looking at look_at, its baseline level. Answers the housing's, the sensor's and the weld's ids",
                   "{target?: int, face?: '+x'..'-z', point?: [x,y,z], normal?: [x,y,z], position?: [x,y,z], look_at?: [x,y,z], model?: str, "
                   "name?: str, baseline?: m (0.10), fov?: degrees (65), width?: px (320), height?: px (240)}", {}, cameraOp});
    ops.push_back(encoderSceneOp());
    ops.push_back(wheelModuleOp());
    ops.push_back(driveSceneOp());
    ops.push_back(gripSceneOp());
    ops.push_back({"assembly_power", "A joint's DC motor fed from a battery: a driver at full duty (battery 0: the scene's first, or a new 3S pack)",
                   "{joint: int, battery?: int}", {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        const uint32_t id = powerMotor(g, uint32_t(a["joint"].integer()), uint32_t(a["battery"].integer(0)));
        return r.set("id", id), id != 0 || (e = "joint " + std::to_string(a["joint"].integer()) + " has no DC motor", false);
    }});
    return ops;
}

} // namespace ke
