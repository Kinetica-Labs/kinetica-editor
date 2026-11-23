// The assembly operations of the core (SceneOps.h, the work in Assembly.h): where a link goes against a
// face, the ready links (presets) and the one a click most likely means, putting one; a link attached to
// a face with a joint (the magnet's click), a joint between two shapes, a sensor on a face, an encoder on
// a joint, a joint's motor powered from a battery. Faces are "+x" ... "-z" of the target's own frame,
// or their sides front, rear, top, bottom, right, left; joint kinds the scene file's words.
#include "scene/SceneOps.h"

#include "scene/Assembly.h"
#include "scene/CameraHousing.h"
#include "scene/SceneActions.h"
#include "scene/SensorSpec.h"
#include "core/Format.h"
#include "core/Units.h"

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

const Entity* entityOf(const SceneGraph& g, const Value& a, const char* key = "target") { return findEntity(g, uint32_t(a[key].integer())); }

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
    return v.isNull() || v.text("") == "shock" || setJointKind(probe, v.text(""));
}

// A shock absorber: a slider whose spring carries a weight as a car's does (AssemblyPresets.cpp, shock) - 4 Hz, a sag of
// 1.6 cm, damping ratio 0.5 - limited to three sags or a quarter of a's size either way, whichever is more. The weight it
// carries: the one that moves when the other holds still (fixed, or the world) - a part hanging on it -, else the heavier
// of the two - a chassis on its wheels' hubs -, shared by all that body's shocks. Each shock of that body whose spring is
// still the share it was given (no hand changed it) is set to the new share, so four made one by one carry a quarter each.
constexpr Real kShockOmega = 2 * kPi * 4;

bool isShock(const JointObject& j) {
    const int d = mainFreedom(j);
    return d >= 0 && d < DofRx && j.dof[d].drive.enabled && j.dof[d].drive.stiffness > 0 && j.name.rfind("Амортизатор", 0) == 0;
}

void springFor(JointFreedom& f, Real carried) {
    f.drive.enabled = true, f.drive.stiffness = carried * kShockOmega * kShockOmega;
    f.drive.damping = 2 * 0.5_r * std::sqrt(f.drive.stiffness * carried);
}

void makeShock(SceneGraph& g, uint32_t joint, uint32_t a, uint32_t b) {
    const Entity* ea = findEntity(g, a);
    const Entity* eb = b ? findEntity(g, b) : nullptr;
    if (!ea) return;
    const Entity& heavy = !eb || !eb->rigid.dynamic() ? *ea : !ea->rigid.dynamic() ? *eb : bodyMass(*ea) >= bodyMass(*eb) ? *ea : *eb;
    int shocks = 1; // this one, and the carried body's others
    for (const JointObject& o : g.joints)
        if (o.id != joint && isShock(o) && (o.a == heavy.id || o.b == heavy.id)) ++shocks;
    const Real m = bodyMass(heavy), share = m / Real(shocks), before = m / Real(std::max(shocks - 1, 1));
    for (JointObject& j : g.joints) {
        if (j.id == joint) {
            setJointKind(j, std::string("slider"));
            const int d = mainFreedom(j);
            if (d < 0) return;
            const Real travel = std::max(3 * 9.81_r / (kShockOmega * kShockOmega), 0.25_r * maxComp(ea->size));
            JointFreedom& f = j.dof[d];
            f.motion = JointMotion::Limited, f.lower = -travel, f.upper = travel;
            springFor(f, share);
            j.name = "Амортизатор " + j.name.substr(j.name.find_last_of(' ') + 1);
        } else if (isShock(j) && (j.a == heavy.id || j.b == heavy.id)) {
            JointFreedom& f = j.dof[mainFreedom(j)];
            if (std::fabs(f.drive.stiffness - before * kShockOmega * kShockOmega) < 0.01_r * f.drive.stiffness) springFor(f, share);
        }
    }
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
    if (vectorArg(a, "size", e.size)) e.rigid.mass = 0, e.rigid.centerGiven = e.rigid.inertiaGiven = false; // another size, another body: the target's kilograms are not its
    e.rigid.fixed = e.rigid.kinematic = false; // whatever kind of body the target is, a link is moved by its joint
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
             LinkPreset p = LinkPreset::Hinge;
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
                   "encoder?: lines (0: none), power?: bool, battery?: int, radius?: m, rolling?: number (C_rr of a point wheel)}", {},
                   [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Side side = Side::FrontLeft;
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
void tuneServo(const SceneGraph& g, const JointObject& j, int dof, JointDrive& drive); // below, beside assembly_actuator

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
    // The kind: as said; else as the fields given say (a stiffness or a target: a servo; a voltage, a resistance or a
    // constant: a DC motor); else the drive's own - never guessed from what the call did not give.
    const std::string own = d.constant > 0 ? "dcmotor" : !d.enabled || d.stiffness > 0 || d.damping > 0 ? "servo" : "velocity";
    const std::string kind = a["kind"].text(a.has("stiffness") || a.has("target") ? "servo"
                                            : a.has("voltage") || a.has("resistance") || a.has("constant") ? "dcmotor" : own);
    if (kind != "servo" && kind != "velocity" && kind != "dcmotor") return e = "kind is servo, velocity or dcmotor", false;
    if (kind == "velocity" && (a.has("stiffness") || a.has("target")))
        return e = "a velocity motor has no stiffness or target: it holds a speed (velocity, force). A position: kind servo", false;
    const struct { const char* name; Real* to; bool positive; } fields[] = {
        {"stiffness", &d.stiffness, true}, {"damping", &d.damping, true}, {"target", &d.target, false}, {"velocity", &d.targetVelocity, false},
        {"force", &d.maxForce, true}, {"voltage", &d.voltage, false}, {"resistance", &d.resistance, true}, {"constant", &d.constant, true}};
    for (const auto& f : fields) {
        if (!a.has(f.name)) continue;
        const double v = a[f.name].number(std::nan(""));
        if (!std::isfinite(v) || (f.positive && v < 0)) return e = std::string(f.name) + (f.positive ? " is a number, 0 or more" : " is a number"), false;
        *f.to = Real(v);
    }
    if (kind == "velocity") d.stiffness = d.damping = 0;
    if (kind != "dcmotor") d.constant = 0;
    if (kind == "dcmotor" && !(d.constant > 0 && d.resistance > 0)) return e = "a DC motor needs constant > 0 and resistance > 0", false;
    const bool turn = dof >= DofRx;
    if (kind == "servo" && !(d.stiffness > 0) && a.has("stiffness"))
        return e = std::string("a servo needs stiffness > 0 (") + (turn ? "N m/rad" : "N/m") + ") to hold its target; none given: tuned to its "
                   "link; a speed alone: kind velocity", false;
    std::string tuned;
    if (kind == "servo" && !(d.stiffness > 0)) { // none given: tuned to what the joint carries, said in the answer
        const Real damping = d.damping;
        tuneServo(g, *j, dof, d);
        if (a.has("damping")) d.damping = damping;
        tuned = "жёсткость подобрана по звену: " + std::to_string(int(std::lround(d.stiffness))) + (turn ? " Н·м/рад" : " Н/м") +
                ", провис под тяжестью не больше градуса";
    }
    d.enabled = a.has("on") ? a["on"].truth(true) : true;
    j->dof[dof].drive = d;
    if (j->dof[dof].motion == JointMotion::Locked) j->dof[dof].motion = JointMotion::Free; // a drive on a locked freedom would move nothing
    r.set("joint", j->id), r.set("dof", dofs[dof]), r.set("kind", kind);
    Value drive = Value::map(); // the drive as it now stands: no second call to read it back
    drive.set("on", d.enabled), drive.set("unit", turn ? "rad" : "m");
    if (kind == "servo") drive.set("stiffness", d.stiffness), drive.set("damping", d.damping), drive.set("target", d.target);
    if (kind != "dcmotor") drive.set("velocity", d.targetVelocity);
    if (kind == "dcmotor") drive.set("voltage", d.voltage), drive.set("resistance", d.resistance), drive.set("constant", d.constant);
    if (std::isfinite(d.maxForce)) drive.set("force", d.maxForce);
    r.set("drive", drive);
    if (!tuned.empty()) r.set("note", tuned);
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

// A joint holds bodies: a shape that is geometry only becomes one - a, the moving link, rigid; b, what it hangs on,
// rigid and fixed. The words for the answer's note (empty: both were bodies already).
std::string bodiesForJoint(SceneGraph& g, uint32_t a, uint32_t b) {
    std::string made;
    for (Entity& x : g.entities) {
        if ((x.id != a && x.id != b) || x.id == 0 || x.rigid.enabled) continue;
        x.rigid.enabled = x.collider.enabled = true;
        x.rigid.fixed = x.id == b;
        made += std::string(made.empty() ? "" : ", ") + "«" + x.name + "» " + (x.rigid.fixed ? "твёрдое и закреплено" : "твёрдое");
    }
    return made.empty() ? made : "Сочленению нужны тела: " + made;
}

// A servo tuned to what its joint carries: it holds the pose it is told and droops under gravity a degree at most (a turn:
// k = m g d / 0.0175 rad, d the link's centre of mass off the axis; a travel: 2 mm, k = m g / 0.002), critically damped
// (c = 2 sqrt(k I), I the link's inertia about the axis; m along a travel). The link is the joint's a. Its mass, centre
// of mass and inertia are the ones given to it (the rigid line), else those of its shape: a rod of its longest side.
void tuneServo(const SceneGraph& g, const JointObject& j, int dof, JointDrive& drive) {
    const Entity* link = findEntity(g, j.a);
    const Entity body = link ? resolveInstance(g, *link) : Entity();
    const Real mass = link ? std::max(bodyMass(body), 0.01_r) : 1.0_r;
    Vector3 originA, anchor;
    Quaternion frameA, frameB;
    jointWorldFrames(g, j, originA, frameA, anchor, frameB);
    Vector3 e(0.0_r);
    e[dof % 3] = 1;
    const Vector3 axis = frameB.rotate(e);
    const AABB box = objectBounds(g, j.a);
    Vector3 centre = box.valid() ? box.center() : anchor, at;
    Quaternion turn;
    if (link) worldPose(g, *link, at, turn);
    if (body.rigid.centerGiven) centre = at + turn.rotate(body.rigid.centerOfMass);
    const Vector3 off = centre - anchor;
    const Real d = std::max(length(off - axis * dot(off, axis)), 0.05_r), reach = link ? maxComp(body.size) : 0.1_r;
    Real about = mass * reach * reach / 12; // about the axis through the centre of mass
    if (body.rigid.inertiaGiven) {
        const Vector3 a = turn.conjugate().rotate(axis), &i = body.rigid.inertiaDiagonal, &p = body.rigid.inertiaProducts;
        about = i.x * a.x * a.x + i.y * a.y * a.y + i.z * a.z * a.z + 2 * (p.x * a.x * a.y + p.y * a.x * a.z + p.z * a.y * a.z);
    }
    const Real gravity = std::max(length(g.world.gravity), 1.0_r);
    if (dof >= DofRx) {
        drive.stiffness = std::max(mass * gravity * d / 0.0175_r, 10.0_r);
        drive.damping = 2 * std::sqrt(drive.stiffness * (mass * d * d + about));
    } else {
        drive.stiffness = std::max(mass * gravity / 0.002_r, 100.0_r);
        drive.damping = 2 * std::sqrt(drive.stiffness * mass);
    }
}

// assembly_actuator: a hinge or a slider with a servo tuned to what it carries, put where the two meet - what a person
// means by "join these two with a motor". The anchor: given, else between the two boxes' nearest points (the link's
// centre on the world). The servo (tuneServo) holds the pose it is made in, target 0.
bool actuatorOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    const std::string kind = a["kind"].text("rotary");
    if (kind != "rotary" && kind != "linear") return e = "kind is rotary (a turn) or linear (a travel)", false;
    const uint32_t ea = uint32_t(a["a"].integer()), eb = uint32_t(a["b"].integer(0));
    const Entity* link = findEntity(g, ea);
    if (!link || (eb != 0 && !findObject(g, eb))) return e = "a is the moving shape, b what it hangs on (0 or none: the world)", false;
    Vector3 axis(1, 0, 0), anchor;
    if (a.has("axis") && !vectorArg(a, "axis", axis)) return e = "axis is a direction [x, y, z]", false;
    if (!(length(axis) > 0)) return e = "axis is a direction [x, y, z], not zero", false;
    axis = normalize(axis);
    const AABB boxA = objectBounds(g, ea);
    if (a.has("anchor")) {
        if (!vectorArg(a, "anchor", anchor)) return e = "anchor is a world point [x, y, z]", false;
    } else if (eb != 0 && boxA.valid() && objectBounds(g, eb).valid()) {
        const AABB boxB = objectBounds(g, eb);
        auto nearest = [](const AABB& box, const Vector3& p) {
            return Vector3(std::clamp(p.x, box.lo.x, box.hi.x), std::clamp(p.y, box.lo.y, box.hi.y), std::clamp(p.z, box.lo.z, box.hi.z));
        };
        anchor = (nearest(boxA, boxB.center()) + nearest(boxB, boxA.center())) * 0.5_r;
    } else {
        anchor = boxA.valid() ? boxA.center() : link->position;
    }
    const std::string note = bodiesForJoint(g, ea, eb);
    const bool rotary = kind == "rotary";
    const uint32_t id = addJoint(g, rotary ? JointType::Hinge : JointType::Slider, ea, eb, anchor, axis);
    JointObject* j = nullptr;
    for (JointObject& x : g.joints)
        if (x.id == id) j = &x;
    const int dof = j ? mainFreedom(*j) : -1;
    if (dof < 0) return e = "the joint has no free motion to drive", false;
    JointDrive& drive = j->dof[dof].drive;
    drive.enabled = true, drive.target = 0, drive.targetVelocity = 0, drive.constant = 0;
    tuneServo(g, *j, dof, drive);
    r.set("id", id), r.set("joint", id), r.set("name", j->name);
    Value at = Value::list();
    for (int k = 0; k < 3; ++k) at.push(anchor[k]);
    r.set("anchor", at), r.set("stiffness", drive.stiffness), r.set("damping", drive.damping);
    if (!note.empty()) r.set("note", note);
    return true;
}

SceneOp driveSceneOp() {
    return {"assembly_drive", "A drive on a joint's freedom (none: its main one - a hinge's turn, a slider's travel), its output a "
                   "generalised force the robot's own code puts on the joint: servo (stiffness > 0, damping, target, velocity, force), velocity motor "
                   "(velocity, force) or DC motor (voltage, resistance, constant; a driver feeds it: assembly_power); a turn in rad and N m, a "
                   "travel in m and N. The kind: as given, else as the fields given say (stiffness or target: servo; voltage, resistance or "
                   "constant: DC motor), else the drive's own. Fields not given keep their values; the answer gives the drive as it now stands; joints: several at once, in the same step of a running scene (a gripper's fingers)",
                   "{joint: int | joints: [int], dof?: tx|ty|tz|rx|ry|rz, kind?: servo|velocity|dcmotor, stiffness?: number (N m/rad or N/m), "
                   "damping?: number (N m s/rad or N s/m), target?: number (rad or m), velocity?: number (rad/s or m/s), force?: number (N m or N), "
                   "voltage?: number (V), resistance?: number (ohm), constant?: number (N m/A), on?: bool}", {}, driveOp};
}

// assembly_grip: a gripper closed or opened - a target and a force limit: closing,
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
    return {"assembly_grip", "A gripper closed (close: true, the default) or opened: each finger - a "
                   "slider on the effector of the IK goal given (or of the effector itself) - slides to its far end with its drive capped "
                   "at force N (default 2), so it stops on what it meets and presses with that force; opening, back to its near end with "
                   "open_force (default 40); travel: to that many metres from open, capped at force (a wheel's step). All the fingers in "
                   "one step",
                   "{goal: int, close?: bool, force?: number, open_force?: number, travel?: number}", {}, gripOp};
}

// The datasheet of a sensor as an answer: the model it came from, whether its numbers are still that model's, the
// numbers by key.
static Value specAnswer(const SensorObject& s) {
    Value out = Value::map(), spec = Value::map();
    for (const Field& f : specFields(s.kind)) spec.set(f.key, double(specValue(s, f.key)));
    const SensorModel* m = sensorCatalog().find(s.kind, s.model);
    out.set("model", s.model);
    out.set("as_model", m != nullptr && specMatches(s, *m));
    out.set("spec", spec);
    return out;
}

// A model's numbers from the tables, then the numbers given over them, each checked against its field.
static bool setSpec(SensorObject& s, const Value& a, std::string& e) {
    const std::string kind = sensorKindKey(s.kind);
    if (specFields(s.kind).empty()) return e = "a " + kind + " sensor has no datasheet", false;
    if (a.has("model")) {
        const SensorModel* m = sensorCatalog().find(s.kind, a["model"].text(""));
        if (!m) {
            e = "no model «" + a["model"].text("") + "» in the " + kind + " table; it has:";
            for (const SensorModel* x : sensorCatalog().models(s.kind)) e += " «" + x->name + "»";
            return false;
        }
        applyModel(s, *m);
    }
    const Value& spec = a["spec"];
    for (size_t k = 0; k < spec.keys().size(); ++k) {
        const std::string& key = spec.keys()[k];
        const auto f = std::find_if(specFields(s.kind).begin(), specFields(s.kind).end(), [&](const Field& x) { return key == x.key; });
        if (f == specFields(s.kind).end()) {
            e = "a " + kind + "'s datasheet has no " + key + "; its fields:";
            for (const Field& x : specFields(s.kind)) e += std::string(" ") + x.key;
            return false;
        }
        if (!checkField(*f, spec.at(k), e)) return false;
        s.spec[key] = Real(spec.at(k).number());
    }
    return true;
}

static SensorObject* sensorOf(SceneGraph& g, uint32_t id) {
    for (SensorObject& s : g.sensors)
        if (s.id == id && id != 0) return &s;
    return nullptr;
}

// assembly_sensor: a face's kinds on a shape, a joint's kinds on a joint; then its model's datasheet.
static bool placeSensor(SceneGraph& g, const Value& a, Value& r, std::string& e);
static bool sensorOp(SceneGraph& g, const Value& a, Value& r, std::string& e) {
    if (!placeSensor(g, a, r, e)) return false;
    if (!a.has("model") && !a.has("spec")) return true;
    SensorObject* s = sensorOf(g, uint32_t(r["id"].integer()));
    if (!s || !setSpec(*s, a, e)) return false;
    return r.set("datasheet", specAnswer(*s)), true;
}

static bool placeSensor(SceneGraph& g, const Value& a, Value& r, std::string& e) {
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
    if (!t || (kind != "imu" && kind != "sonar" && kind != "touch" && kind != "stereo" && kind != "lidar"))
        return e = "needs a shape's id and kind imu, sonar, touch, stereo or lidar - or a joint's id and kind wrench or effort", false;
    const SensorKind k = kind == "imu" ? SensorKind::Imu : kind == "sonar" ? SensorKind::Sonar : kind == "touch" ? SensorKind::Touch : kind == "lidar" ? SensorKind::Lidar : SensorKind::Stereo;
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
                   "axis: 0 and 1 across the face, 2 along its normal (a wheel); the answer gives the joint's axis in the world. Answers id (the new "
                   "link, as entity) and joint",
                   "{target: int, face: '+x'..'-z', joint?: 'hinge'|'weld'|'slider'|'ball'|'cylinder'|'universal'|'planar', axis?: 0|1|2, size?: [x,y,z], name?: str}",
                   {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        BoxFace f = BoxFace::PlusX;
        const Entity* t = entityOf(g, a);
        if (!t || !face(a["face"], f) || !kindOk(a["joint"])) return e = "needs a shape's id, a face and a known joint kind", false;
        const Attached done = attachToFace(g, t->id, f, linkLike(g, *t, a), JointType::Hinge, a["axis"].integer(0));
        setKind(g, done.joint, a["joint"]);
        Vector3 at, axis; // the joint's axis in the world: no second call to learn which way the link turns
        if (const JointObject* j = findJoint(g, done.joint)) jointWorldFrame(g, *j, at, axis);
        Value along = Value::list();
        for (int k = 0; k < 3; ++k) along.push(axis[k]);
        return r.set("id", done.entity), r.set("entity", done.entity), r.set("joint", done.joint), r.set("axis", along), true;
    }});
    ops.push_back({"assembly_joint", "A joint between two shapes (b 0: the world) at a world point along a world direction (a shock: a slider "
                                     "whose spring and damper carry a weight at 4 Hz, damping ratio 0.5 - a's when b is fixed or the world, "
                                     "else the heavier one's shared by its shocks, the earlier ones set to the new share unless changed by hand). A shape that "
                                     "is geometry only becomes a body - a, the moving link, rigid; b, what it hangs on, rigid and fixed - "
                                     "since a joint holds bodies; the answer's note says which",
                   "{a: int, b: int, type?: 'hinge'|'weld'|'slider'|'ball'|'cylinder'|'universal'|'planar'|'shock', anchor: [x,y,z], axis?: [x,y,z]}",
                   {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        Vector3 anchor, axis(0, 1, 0);
        const uint32_t ea = uint32_t(a["a"].integer()), eb = uint32_t(a["b"].integer());
        if (!kindOk(a["type"]) || !vectorArg(a, "anchor", anchor) || !findObject(g, ea) || (eb != 0 && !findObject(g, eb)))
            return e = "needs two shapes (b 0: the world), a known kind and an anchor", false;
        vectorArg(a, "axis", axis);
        const uint32_t joint = addJoint(g, JointType::Hinge, ea, eb, anchor, axis);
        const std::string note = bodiesForJoint(g, ea, eb); // geometry alone is never built - and a shock's spring weighs the body
        if (a["type"].text("") == "shock") makeShock(g, joint, ea, eb);
        else setKind(g, joint, a["type"]);
        if (!note.empty()) r.set("note", note);
        return r.set("id", joint), r.set("joint", joint), true;
    }});
    ops.push_back({"assembly_actuator", "Join two shapes with a motor in one step: a hinge (rotary) or a slider (linear) along a world "
                                        "axis, put where they meet (anchor: else between their nearest points), with a servo tuned to "
                                        "what it carries - it holds the pose it is made in, drooping a degree at most, critically damped. "
                                        "Then assembly_drive target turns or slides it. Geometry alone becomes a body (the note says)",
                   "{kind?: rotary|linear, a: int, b?: int, axis?: [x,y,z], anchor?: [x,y,z]}", {}, actuatorOp});
    ops.push_back({"assembly_sensor", "A sensor put on a thing, no shape of its own: on a face of a shape a sonar looks out of it (x along the "
                                      "normal), an IMU lies flat on it (z out), a touch pad reads the contacts' normal force through it, a stereo "
                                      "camera looks out of it (its two lenses across the face; sensor_capture takes its frame), a lidar "
                                      "sends its pattern of rays out of it - a beam, a ring, a sphere, a narrow grid, by its spec's rays_h, "
                                      "fov_h_deg, rays_v, fov_v_deg (its ring level on a side face); on a joint "
                                      "a wrench reads the force and torque it passes to its link, an effort its drive's force along a freedom",
                   "{target: int, kind: 'imu'|'sonar'|'touch'|'stereo'|'lidar', face?: '+x'..'-z', point?: [x,y,z], normal?: [x,y,z]} | "
                   "{joint: int, kind: 'wrench'|'effort', dof?: 'tx'..'rz'}, and for an imu, a sonar, a wrench or a lidar model?: str (a row of "
                   "its table, sensor_models; none: the table's first), spec?: {key: number} over it", {}, sensorOp});
    ops.push_back({"sensor_models", "The tables of sensor models (data/sensors/<kind>.csv, a row per model as its datasheet states it) "
                                    "and the fields of each kind's datasheet: key, words, unit, range, the value of a sensor no model gave one",
                   "{kind?: 'imu'|'sonar'|'wrench'|'lidar'}", [](const SceneGraph&, const Value& a, Value& r, std::string& e) {
        Value kinds = Value::list();
        for (SensorKind k : kTableKinds) {
            if (a.has("kind") && a["kind"].text("") != sensorKindKey(k)) continue;
            Value kind = Value::map(), fields = Value::list(), models = Value::list();
            for (const Field& f : specFields(k)) {
                Value x = Value::map();
                x.set("key", f.key), x.set("label", f.label), x.set("unit", f.unit), x.set("unit_text", unitText(f.unit)), x.set("min", f.min),
                    x.set("max", f.max), x.set("value", f.value);
                fields.push(x);
            }
            for (const SensorModel* m : sensorCatalog().models(k)) {
                Value x = Value::map(), values = Value::map();
                for (const auto& [key, v] : m->values) values.set(key, double(v));
                x.set("model", m->name), x.set("source", m->source), x.set("spec", values);
                models.push(x);
            }
            kind.set("kind", sensorKindKey(k)), kind.set("fields", fields), kind.set("models", models);
            kinds.push(kind);
        }
        if (kinds.size() == 0) return e = "kind is imu, sonar or wrench", false;
        Value files = Value::list();
        for (const std::string& f : sensorCatalog().files()) files.push(f);
        return r.set("kinds", kinds), r.set("files", files), true;
    }, {}});
    ops.push_back({"sensor_spec", "A sensor's datasheet: a model's numbers from its table, and numbers given over them (the model's name stays as "
                                  "where they came from; as_model says whether they still are its). Answers the datasheet",
                   "{sensor: int, model?: str, spec?: {key: number}}", {}, [](SceneGraph& g, const Value& a, Value& r, std::string& e) {
        SensorObject* s = sensorOf(g, uint32_t(a["sensor"].integer()));
        if (!s) return e = "no sensor " + std::to_string(a["sensor"].integer()), false;
        if (!setSpec(*s, a, e)) return false;
        r.set("id", s->id);
        return r.set("datasheet", specAnswer(*s)), true;
    }});
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
