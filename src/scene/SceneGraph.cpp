// Saving and loading a SceneGraph as text: one line per fact, so a scene file can be read, edited
// by hand and compared with diff. A sample:
//
//   # Kinetica scene (Kinetica 0.2.0)
//   world gravity 0 -9.81 0 size 4 3 4 [duration 10] [record_from 2]
//   physics substeps 16 contact_hertz 120 contact_damping 10 joint_hertz 240 joint_damping 2 sleeping 1 continuous 1 linear_damping 0 angular_damping 0
//   entity "Base"
//     object id 3 visible 1 locked 0
//     shape box size 0.3 0.1 0.3 position 0 0.05 0 rotation 0 0 0 color 0.6 0.6 0.6
//     rigid density 1000 friction 0.5 restitution 0.2 fixed 1 velocity 0 0 0 angular_velocity 0 0 0
//     collider kind auto fit 1 size 0.2 0.2 0.2 offset 0 0 0 rotation 0 0 0
//   end
//   entity "Gripper"
//     shape mesh file "models/gripper.obj" size 0.1 0.1 0.1 position 0 0.6 0 rotation 0 90 0 color 0.6 0.4 0.3
//     rigid density 600 friction 0.5 restitution 0.2 fixed 0 velocity 0 0 0 angular_velocity 0 0 0
//     collider kind capsule fit 1 size 0.2 0.2 0.2 offset 0 0 0 rotation 0 0 90
//   end
//   joint "Elbow"
//     object id 9
//     link a 5 b 3 anchor_a 0 -0.1 0 frame_a 0 0 0 anchor_b 0 0.1 0 frame_b 0 0 0
//     limit rx lower -1.5 upper 1.5
//     drive rx stiffness 0 damping 0 target 0 velocity 1 force 10
//   end
//   joint "Shock absorber"
//     object id 10
//     link a 6 b 3 anchor_a 0 0 0 frame_a 0 0 90 anchor_b 0.25 -0.05 0.2 frame_b 0 0 90
//     limit tx lower -0.05 upper 0.05
//     drive tx stiffness 3000 damping 80 target 0 velocity 0
//   end
//
//   battery "Pack"
//     object id 20
//     cells series 3 parallel 1 capacity 2.5 r0 0.03 r1 0.015 c1 2000 cutoff 3 soc 1
//   end
//   driver "Left wheel"
//     object id 21
//     bridge joint 9 dof rx battery 20 duty 0.8 switches 0.1 limit 5
//   end
//   controller "Arm panel"
//     object id 22
//     action move vector2 drive plane target 30 speed 0.15
//     action grip axis drive grip target 30 speed 2
//     bind move keys W S A D
//     bind move gamepad left_stick
//     bind grip keys Space toggle
//     bind grip gamepad right_trigger
//   end
//
// A joint's freedoms (tx ty tz: travel along frame b's axes; rx ry rz: turns about them) are locked
// unless a line says otherwise: "free <dof>", "limit <dof> lower .. upper ..", and a driven one also
// "drive <dof> ..." (a spring-damper or a velocity motor) or "dcmotor <dof> voltage .. resistance ..
// constant .." (an electric motor).
//
// A role is written only when it is on - the collider too, a role of its own (kinds: auto, box,
// sphere, capsule, hull, cylinder); a rigid entity without a collider line loads with the Auto collider.
// The physics line is written only when it differs from the defaults (PhysicsSettings). Numbers are written as the shortest decimal that reads
// back as the same float, so save -> load -> save gives the same text.
#include <algorithm>
#include <stdexcept>
#include "scene/SceneGraph.h"

#include "core/Format.h"
#include "scene/ArgsSchema.h"
#include "scene/SensorSpec.h"

#include <array>
#include <cstdlib>
#include <sstream>
#include <type_traits>
#include <utility>

namespace ke {

namespace {

const char* kShapeNames[] = {"box", "sphere", "cylinder", "cone", "plane", "mesh"};
const int kShapeCount = 6;
const char* kColliderNames[] = {"auto", "box", "sphere", "capsule", "hull", "cylinder", "mesh", "convex_parts"};
const int kColliderCount = 6;
// A joint's six freedoms, by JointDof.
const char* kDofNames[] = {"tx", "ty", "tz", "rx", "ry", "rz"};

std::string num(Real v) { return numberText(v); }
std::string vec(const Vector3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }

// A key under the name it had before the format settled on one style (words joined by '_'): files and journal
// snapshots written then still load, and are saved under the new name. The format only grows; a rename goes here.
std::string renamed(const std::string& key) {
    static const std::pair<const char*, const char*> kRenamed[] = {
        {"anchorA", "anchor_a"}, {"anchorB", "anchor_b"}, {"frameA", "frame_a"}, {"frameB", "frame_b"},
        {"contactHertz", "contact_hertz"}, {"contactDamping", "contact_damping"}, {"jointHertz", "joint_hertz"},
        {"jointDamping", "joint_damping"}, {"linearDamping", "linear_damping"}, {"angularDamping", "angular_damping"},
        {"heightFile", "height_file"}, {"rotationStep", "rotation_step"},
        {"spin", "angular_velocity"}}; // the word of every 3D standard (docs/contracts/names.md)
    for (const auto& [was, now] : kRenamed)
        if (key == was) return now;
    return key;
}

// Reads the words of one line as "key value(s)" pairs. `need(k)` says how many numbers follow key k.
class LineReader {
public:
    LineReader(const std::vector<std::string>& w, size_t start) : w_(w), i_(start) {}
    bool done() const { return i_ >= w_.size(); }
    // The next key; one met before on this line comes back as "<key> twice", which no reader takes: a line
    // like "drive rx target 1 damping 5 target 2" is refused, not read with its stiffness lost.
    std::string key() {
        const std::string k = renamed(w_[i_++]);
        if (std::find(seen_.begin(), seen_.end(), k) != seen_.end()) return k + " twice";
        seen_.push_back(k);
        return k;
    }
    bool number(Real& out) {
        if (i_ >= w_.size()) return false;
        if (!parseNumber(w_[i_], out)) return false;
        ++i_;
        return true;
    }
    bool vector(Vector3& v) { return number(v.x) && number(v.y) && number(v.z); }
    // A whole number: ids and bit masks stay exact (a float holds integers only up to 2^24).
    bool integer(uint32_t& out) {
        if (i_ >= w_.size()) return false;
        char* end = nullptr;
        const unsigned long v = std::strtoul(w_[i_].c_str(), &end, 10);
        if (end == w_[i_].c_str() || *end) return false;
        out = uint32_t(v);
        ++i_;
        return true;
    }
    bool integer(int& out) {
        uint32_t u;
        if (!integer(u)) return false;
        out = int(u);
        return true;
    }
    // Counts must be bounded before conversion (no fractional values or uint wrap).
    bool count(int& out, int low, int high) {
        if (i_ >= w_.size()) return false;
        char* end = nullptr;
        const long v = std::strtol(w_[i_].c_str(), &end, 10);
        if (end == w_[i_].c_str() || *end || v < low || v > high) return false;
        out = int(v); ++i_;
        return true;
    }
    // A word as it is (a quoted file path arrives without its quotes).
    bool text(std::string& out) {
        if (i_ >= w_.size()) return false;
        out = w_[i_++];
        return true;
    }
    bool flag(bool& b) {
        Real f;
        if (!number(f)) return false;
        b = f != 0;
        return true;
    }

private:
    const std::vector<std::string>& w_;
    size_t i_;
    std::vector<std::string> seen_; // the keys read from this line
};

// Remembers what could not be read and says "failed".
bool fail(std::string& bad, const std::string& what) {
    bad = what;
    return false;
}

} // namespace

std::vector<LineWord> lineWords(const std::string& line) {
    std::vector<LineWord> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        const size_t start = i;
        if (line[i] == '"') {
            const size_t close = line.find('"', i + 1);
            i = close == std::string::npos ? line.size() : close + 1;
            out.push_back({line.substr(start + 1, (close == std::string::npos ? line.size() : close) - start - 1), start, i, true});
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
            out.push_back({line.substr(start, i - start), start, i, false});
        }
    }
    return out;
}

std::string presentKey(const std::string& key) { return renamed(key); }

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------
static std::string quoted(const std::string& name) {
    std::string n = name;
    for (char& c : n)
        if (c == '"') c = '\'';
    return "\"" + n + "\"";
}

// A word that names something - a tag, an action of a controller - as the file writes it: bare, or in quotes when it
// would read as a number ("7"), so that what reads the line back word by word (scene_get) has the name, not 7.0.
static std::string named(const std::string& word) {
    double number = 0;
    return parseNumber(word, number) ? quoted(word) : word;
}

// The collider role, a line of its own with every field (written only when the role is on).
static void saveCollider(std::ostringstream& o, const ColliderRole& c) {
    o << "  collider kind " << kColliderNames[int(c.kind)] << " fit " << (c.fitToGeometry ? 1 : 0) << " size " << vec(c.size)
      << " offset " << vec(c.offset) << " rotation " << vec(c.rotationDeg) << (c.trigger ? " trigger 1" : "");
    const ColliderRole plain; // the convex parts' numbers written when set: a file without them reads everywhere
    if (c.hulls != plain.hulls) o << " hulls " << c.hulls;
    if (c.concavity != plain.concavity) o << " concavity " << num(c.concavity);
    o << "\n";
    if (c.ignores.empty()) return; // both written when set: a file without them reads everywhere
    o << "  ignores";
    for (const std::string& t : c.ignores) o << " " << named(t);
    o << "\n";
}

// An object's tags, a line of their own - written when it has any: a file without them reads everywhere.
static void saveTags(std::ostringstream& o, const std::vector<std::string>& tags) {
    if (tags.empty()) return;
    o << "  tags";
    for (const std::string& t : tags) o << " " << named(t);
    o << "\n";
}

// The plugins' components of an object, a line each - the last lines of its block, whatever it is.
static void saveComponents(std::ostringstream& o, const std::vector<PluginComponent>& components) {
    for (const PluginComponent& c : components) {
        o << "  component " << c.type;
        for (const auto& [key, word] : c.values) o << " " << key << " " << (word.find(' ') != std::string::npos || word.empty() ? quoted(word) : word);
        o << "\n";
    }
}

// The part every object has, for groups and arrays: a quoted name, its object line and its pose.
static void saveObjectHead(std::ostringstream& o, const char* kind, const SceneObject& s) {
    std::string name = s.name;
    for (char& c : name)
        if (c == '"') c = '\'';
    o << kind << " \"" << name << "\"\n";
    o << "  object id " << s.id << " visible " << (s.visible ? 1 : 0) << " locked " << (s.locked ? 1 : 0);
    if (s.parent != 0) o << " parent " << s.parent;
    o << "\n  pose position " << vec(s.position) << " rotation " << vec(s.rotationDeg) << " color " << vec(s.color) << "\n";
    saveTags(o, s.tags);
    saveComponents(o, s.components);
}

// A sequence: the object head, its clip line, then each track - its object, then its keys, by time.
static void saveSequence(std::ostringstream& o, const SequenceObject& q) {
    saveObjectHead(o, "sequence", q);
    o << "  clip time " << num(q.time) << " loop " << (q.loop ? 1 : 0) << " active " << (q.active ? 1 : 0) << " play " << (q.play ? 1 : 0) << "\n";
    for (const SequenceTrack& t : q.tracks) {
        o << "  track object " << t.object << "\n";
        for (const AnimKey& k : t.keys) {
            o << "  key time " << num(k.time) << " position " << vec(k.position) << " rotation " << vec(k.rotationDeg);
            if (k.hasVelocity) o << " velocity " << vec(k.velocity);
            if (k.interp != AnimInterp::Linear) o << " interp " << interpName(k.interp);
            o << "\n";
        }
    }
    for (const FieldTrack& t : q.fields) { // a keyed field: its object, line and key, then its numbers by time
        o << "  field object " << t.object << " line " << quoted(t.line) << " key " << quoted(t.key) << "\n";
        for (const FieldKey& k : t.keys) {
            o << "  value time " << num(k.time) << " values";
            for (Real v : k.values) o << " " << num(v);
            if (k.interp != AnimInterp::Linear) o << " interp " << interpName(k.interp);
            o << "\n";
        }
    }
    o << "end\n";
}

static void saveGroup(std::ostringstream& o, const Group& g) {
    saveObjectHead(o, "group", g);
    if (g.glued) o << "  glued\n";
    o << "end\n";
}

static const char* kPatternNames[] = {"line", "grid", "circle"};

static void saveArray(std::ostringstream& o, const ArrayObject& a) {
    saveObjectHead(o, "array", a);
    o << "  pattern " << kPatternNames[int(a.pattern)] << " template " << a.templateId << " count " << a.count[0] << " "
      << a.count[1] << " " << a.count[2] << " step " << vec(a.step) << " radius " << num(a.radius) << " rotation_step "
      << vec(a.rotationStepDeg) << " jitter " << num(a.jitter) << " seed " << a.seed << "\n";
    o << "end\n";
}

static const char* kLightNames[] = {"sun", "point", "spot"};

// Lights and cameras: only their own settings follow the object head (the physics ignores them).
static void saveLight(std::ostringstream& o, const Light& l) {
    saveObjectHead(o, "light", l);
    o << "  kind " << kLightNames[int(l.kind)] << " intensity " << num(l.intensity) << " range " << num(l.range) << " cone "
      << num(l.coneDeg) << " softness " << num(l.softnessDeg) << " shadows " << (l.shadows ? 1 : 0) << "\n";
    o << "end\n";
}

const char* kForceShapeNames[3] = {"directional", "radial", "torque"};
const char* kForceModeNames[2] = {"impulse", "constant"};
static const char* kForceFalloffNames[] = {"none", "linear", "inverse_square"};

const char* forceFault(const Force& f) {
    if (!std::isfinite(f.strength) || std::fabs(f.strength) > kForceMaxStrength) return "a force's strength is a finite number, at most 100000 in size";
    if (!std::isfinite(f.radius) || f.radius < 0 || f.radius > kForceMaxRadius) return "a force's radius is from 0 to 1000 m";
    if (!std::isfinite(f.start) || f.start < 0 || f.start > kForceMaxStart) return "a force's start is from 0 to 3600 s";
    if (!f.tag.empty() && !tagWord(f.tag)) return "a force's tag is one word, without spaces or quotes";
    if (f.shape == ForceShape::Radial && f.radius <= 0) return "a radial force needs a radius above 0";
    return nullptr;
}

// The effect line: the radius comes before the shape, so that a one-key-at-a-time edit of a directional force into a
// radial one can set the radius first. falloff, start and tag are written only when set.
static void saveForce(std::ostringstream& o, const Force& f) {
    saveObjectHead(o, "force", f);
    o << "  effect radius " << num(f.radius) << " shape " << kForceShapeNames[int(f.shape)] << " mode " << kForceModeNames[int(f.mode)]
      << " strength " << num(f.strength);
    if (f.falloff != ForceFalloff::None) o << " falloff " << kForceFalloffNames[int(f.falloff)];
    if (f.start != 0) o << " start " << num(f.start);
    if (!f.tag.empty()) o << " tag " << named(f.tag);
    o << "\nend\n";
}

static void saveCamera(std::ostringstream& o, const Camera& c) {
    saveObjectHead(o, "camera", c);
    o << "  lens fov " << num(c.fovDeg) << " near " << num(c.nearClip) << " far " << num(c.farClip) << " active "
      << (c.active ? 1 : 0) << "\n";
    if (c.kind == CameraKind::Target) o << "  target object " << c.target << "\n"; // a free camera has none: older files read the same
    o << "end\n";
}

static void saveTexture(std::ostringstream& o, const SurfaceTexture& t) {
    if (t.kind == TextureKind::None && t.bumpHeight == 0 && t.normalFile.empty()) return;
    if (int(t.kind) < 0 || int(t.kind) > 4 || !std::isfinite(t.repeats) || t.repeats <= 0 || t.repeats > 10000 ||
        t.file.find_first_of("\"\r\n") != std::string::npos ||
        t.heightFile.find_first_of("\"\r\n") != std::string::npos || t.normalFile.find_first_of("\"\r\n") != std::string::npos ||
        !std::isfinite(t.bumpHeight) || t.bumpHeight < 0 || t.bumpHeight > 0.05_r)
        throw std::invalid_argument("invalid surface texture settings");
    const char* kinds[] = {"none", "checker", "image", "speckle", "stone"}; // TextureKind
    o << "  texture kind " << kinds[int(t.kind)]
      << " repeats " << num(t.repeats) << " file \"" << t.file << "\"";
    if (t.bumpHeight > 0) o << " bump " << num(t.bumpHeight) << " height_file \"" << t.heightFile << "\"";
    if (!t.normalFile.empty()) o << " normal_file \"" << t.normalFile << "\""; // written when set: a file without it reads everywhere
    o << "\n";
}

static void saveEntity(std::ostringstream& o, const Entity& e) {
    std::string name = e.name;
    for (char& c : name)
        if (c == '"') c = '\''; // the name is written in double quotes
    o << "entity \"" << name << "\"\n";
    o << "  object id " << e.id << " visible " << (e.visible ? 1 : 0) << " locked " << (e.locked ? 1 : 0);
    if (e.parent != 0) o << " parent " << e.parent;
    if (e.instanceOf != 0) o << " instance " << e.instanceOf;
    o << "\n";
    saveTags(o, e.tags);
    o << "  shape " << kShapeNames[int(e.shape)];
    if (e.shape == ShapeKind::Mesh) o << " file \"" << e.meshFile << "\"";
    o << " size " << vec(e.size) << " position " << vec(e.position)
      << " rotation " << vec(e.rotationDeg) << " color " << vec(e.color);
    if (e.opacity < 1) o << " opacity " << num(e.opacity); // written when set, as the words that came after the first sample
    o << "\n";
    if (e.goalOf != 0) o << "  goal effector " << e.goalOf << "\n";
    if (e.targetOf != 0) o << "  target camera " << e.targetOf << "\n";
    if (!e.path.empty()) {
        o << "  path speed " << num(e.pathSpeed) << " loop " << (e.pathLoop ? 1 : 0) << " smooth " << (e.pathSmooth ? 1 : 0);
        if (e.pathTurnLength != 0) o << " turn " << num(e.pathTurnLength); // written only when set: older files stay as they were
        o << "\n";
    }
    for (size_t k = 0; k < e.path.size(); ++k) {
        const Vector3& p = e.path[k];
        o << "  waypoint " << num(p.x) << " " << num(p.y) << " " << num(p.z);
        if (k < e.pathTurns.size()) o << " turn " << num(e.pathTurns[k].x) << " " << num(e.pathTurns[k].y) << " " << num(e.pathTurns[k].z);
        o << "\n";
    }
    saveTexture(o, e.texture);
    if (e.rigid.enabled) {
        o << "  rigid density " << num(e.rigid.density) << " friction " << num(e.rigid.friction) << " restitution "
          << num(e.rigid.restitution) << " fixed " << (e.rigid.fixed ? 1 : 0) << " velocity " << vec(e.rigid.velocity)
          << " angular_velocity " << vec(e.rigid.angularVelocity) << (e.rigid.kinematic ? " kinematic 1" : ""); // written when set: a file
                                                                                  // without one reads everywhere - as the next three
        if (e.rigid.mass > 0) o << " mass " << num(e.rigid.mass);
        if (e.rigid.centerGiven) o << " center_of_mass " << vec(e.rigid.centerOfMass);
        if (e.rigid.inertiaGiven) o << " inertia " << vec(e.rigid.inertiaDiagonal) << " " << vec(e.rigid.inertiaProducts);
        if (e.rigid.gravityScale != 1) o << " gravity_scale " << num(e.rigid.gravityScale);
        if (e.rigid.linearDamping >= 0) o << " linear_damping " << num(e.rigid.linearDamping);
        if (e.rigid.angularDamping >= 0) o << " angular_damping " << num(e.rigid.angularDamping);
        o << "\n";
    }
    if (e.collider.enabled) saveCollider(o, e.collider);
    saveComponents(o, e.components);
    o << "end\n";
}

// A freedom's lines: its motion when not locked, then its drive when on.
static void saveFreedom(std::ostringstream& o, int k, const JointFreedom& f) {
    if (f.motion == JointMotion::Free) o << "  free " << kDofNames[k] << "\n";
    if (f.motion == JointMotion::Limited)
        o << "  limit " << kDofNames[k] << " lower " << num(f.lower) << " upper " << num(f.upper) << "\n";
    const JointDrive& d = f.drive;
    if (!d.enabled) return;
    if (d.constant > 0) {
        o << "  dcmotor " << kDofNames[k] << " voltage " << num(d.voltage) << " resistance " << num(d.resistance) << " constant "
          << num(d.constant) << "\n";
        return;
    }
    o << "  drive " << kDofNames[k] << " stiffness " << num(d.stiffness) << " damping " << num(d.damping) << " target "
      << num(d.target) << " velocity " << num(d.targetVelocity);
    if (std::isfinite(d.maxForce)) o << " force " << num(d.maxForce);
    o << "\n";
}

// A joint: its link line (the two entities, the frame on each: origin and turn in its own entity's
// frame), then the lines of its freedoms that are not plainly locked, and the collision when on.
static void saveJoint(std::ostringstream& o, const JointObject& j) {
    std::string name = j.name;
    for (char& c : name)
        if (c == '"') c = '\'';
    o << "joint \"" << name << "\"\n  object id " << j.id << "\n";
    saveTags(o, j.tags);
    saveComponents(o, j.components);
    o << "  link a " << j.a << " b " << j.b << " anchor_a " << vec(j.anchorA) << " frame_a " << vec(j.frameA) << " anchor_b "
      << vec(j.anchorB) << " frame_b " << vec(j.frameB) << "\n";
    for (int k = 0; k < 6; ++k) saveFreedom(o, k, j.dof[k]);
    if (j.collide) o << "  collide\n";
    o << "end\n";
}

static void saveBattery(std::ostringstream& o, const BatteryObject& b) {
    o << "battery " << quoted(b.name) << "\n  object id " << b.id << "\n";
    saveTags(o, b.tags);
    saveComponents(o, b.components);
    o << "  cells series " << b.series << " parallel " << b.parallel << " capacity " << num(b.capacityAh) << " r0 " << num(b.r0) << " r1 "
      << num(b.r1) << " c1 " << num(b.c1) << " cutoff " << num(b.cutoff) << " soc " << num(b.soc) << "\nend\n";
}

static void saveDriver(std::ostringstream& o, const DriverObject& d) {
    o << "driver " << quoted(d.name) << "\n  object id " << d.id << "\n";
    saveTags(o, d.tags);
    saveComponents(o, d.components);
    o << "  bridge joint " << d.joint << " dof " << kDofNames[std::clamp(d.dof, 0, 5)] << " battery " << d.battery << " duty " << num(d.duty)
      << " switches " << num(d.switches) << " limit " << num(d.currentLimit) << " stop " << (d.brake ? "brake" : "coast") << "\nend\n";
}

// A controller: one line an action (what value it has, what it drives, how fast), one line a binding of it.
static const char* const kControlValues[] = {"button", "axis", "vector2"};
static const char* const kControlDrives[] = {"plane", "lift", "turn", "grip", "jog", "duty", "value"};

static void saveController(std::ostringstream& o, const ControllerObject& c) {
    o << "controller " << quoted(c.name) << "\n  object id " << c.id << "\n";
    saveTags(o, c.tags);
    saveComponents(o, c.components);
    for (const ControllerAction& a : c.actions) {
        o << "  action " << named(a.name) << " " << kControlValues[int(a.value)] << " drive " << kControlDrives[int(a.drive)] << " target " << a.target;
        if (a.drive == ControlDrive::Jog) o << " dof " << kDofNames[std::clamp(a.dof, 0, 5)];
        o << " speed " << num(a.speed) << "\n";
    }
    for (const ControllerAction& a : c.actions)
        for (const ControlBinding& b : a.bindings) {
            o << "  bind " << named(a.name) << " " << b.device;
            for (const std::string& k : b.controls) o << " " << k;
            o << (b.invert ? " invert" : "") << (b.toggle ? " toggle" : "") << "\n";
        }
    o << "end\n";
}

// A kind's datasheet, every field written: the model it came from, and each number by its key.
static void saveSpec(std::ostringstream& o, const SensorObject& s) {
    if (specFields(s.kind).empty()) return;
    if (!s.model.empty()) o << "  model " << quoted(s.model) << "\n";
    o << "  spec";
    for (const Field& f : specFields(s.kind)) o << " " << f.key << " " << num(specValue(s, f.key));
    o << "\n";
}

// A sensor: the object head (its parent and its pose on it), then what it is and reads, then its datasheet.
static void saveSensor(std::ostringstream& o, const SensorObject& s) {
    saveObjectHead(o, "sensor", s);
    if (s.kind == SensorKind::Imu || s.kind == SensorKind::Sonar || s.kind == SensorKind::Wrench || s.kind == SensorKind::Lidar) {
        o << "  " << sensorKindKey(s.kind) << " noise " << (s.noise ? 1 : 0) << " seed " << s.seed << "\n";
        saveSpec(o, s);
        o << "end\n";
        return;
    }
    if (s.kind == SensorKind::Touch) {
        o << "  touch\nend\n";
        return;
    }
    if (s.kind == SensorKind::Effort) {
        o << "  effort dof " << kDofNames[std::clamp(s.dof, 0, 5)] << "\nend\n";
        return;
    }
    if (s.kind == SensorKind::Stereo) {
        o << "  stereo width " << s.width << " height " << s.height << " fov " << num(s.fovDeg) << " baseline " << num(s.baseline) << " projector "
          << (s.projector ? 1 : 0) << " noise " << (s.noise ? 1 : 0) << " seed " << s.seed;
        o << "\nend\n";
        return;
    }
    o << "  encoder lines " << s.lines << " dof " << kDofNames[std::clamp(s.dof, 0, 5)];
    if (s.against != 0) o << " against " << s.against;
    if (s.hasEncoderReference) o << " reference " << num(s.encoderReference);
    o << "\nend\n";
}

// A point wheel: the object head (its parent and the mount on it), its tyre, its suspension, its motor.
static void saveWheel(std::ostringstream& o, const WheelObject& w) {
    saveObjectHead(o, "wheel", w);
    o << "  tyre radius " << num(w.radius) << " width " << num(w.width) << " mass " << num(w.mass) << " friction " << num(w.friction)
      << " unsprung " << num(w.unsprung) << " rolling " << num(w.rolling) << "\n";
    o << "  spring rest " << num(w.rest) << " stiffness " << num(w.stiffness) << " damping " << num(w.damping) << "\n";
    o << "  dcmotor voltage " << num(w.voltage) << " resistance " << num(w.resistance) << " constant " << num(w.constant) << " flip "
      << (w.flip ? 1 : 0) << "\nend\n";
}

static std::string physicsText(const PhysicsSettings& p) {
    std::ostringstream o;
    o << "physics substeps " << p.substeps << " contact_hertz " << num(p.contactHertz) << " contact_damping " << num(p.contactDamping)
      << " joint_hertz " << num(p.jointHertz) << " joint_damping " << num(p.jointDamping) << " sleeping " << (p.sleeping ? 1 : 0)
      << " continuous " << (p.continuous ? 1 : 0) << " linear_damping " << num(p.linearDamping) << " angular_damping "
      << num(p.angularDamping) << "\n";
    return o.str();
}

static std::string worldLine(const WorldSettings& w) {
    return "world gravity " + vec(w.gravity) + " size " + vec(w.size) + (w.duration > 0 ? " duration " + num(w.duration) : "") +
           (w.recordFrom > 0 ? " record_from " + num(w.recordFrom) : "") + "\n";
}

std::string worldText(const WorldSettings& w) { return worldLine(w) + physicsText(w.physics); }

// The program that writes or reads: its version as the first line of a file names it. Words only grow
// (docs/contracts/names.md), so a file of an older program always reads; a newer one's may hold a word this program
// does not know, and the first line then says whose it is - a comment, which every program skips.
using Version = std::array<int, 3>;
constexpr Version kProgram{KE_VERSION_MAJOR, KE_VERSION_MINOR, KE_VERSION_PATCH};

static std::string versionText(const Version& v) {
    return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

// From the first line "# Kinetica scene (Kinetica 0.3.1)"; {0, 0, 0} when it does not say.
static Version writtenBy(const std::string& text) {
    const std::string first = text.substr(0, text.find('\n'));
    const size_t at = first.find("(Kinetica ");
    if (first.rfind("# Kinetica scene", 0) != 0 || at == std::string::npos) return {0, 0, 0};
    Version v{};
    char dot = 0, dot2 = 0;
    std::istringstream in(first.substr(at + 10));
    in >> v[0] >> dot >> v[1] >> dot2 >> v[2];
    return in && dot == '.' && dot2 == '.' ? v : Version{0, 0, 0};
}

std::string SceneGraph::save() const {
    std::ostringstream o;
    o << "# Kinetica scene (Kinetica " << versionText(kProgram) << ")\n";
    o << worldLine(world);
    if (physicsText(world.physics) != physicsText(PhysicsSettings{})) o << physicsText(world.physics);
    for (const Entity& e : entities) saveEntity(o, e);
    for (const Group& g : groups) saveGroup(o, g);
    for (const ArrayObject& a : arrays) saveArray(o, a);
    for (const Light& l : lights) saveLight(o, l);
    for (const Camera& c : cameras) saveCamera(o, c);
    for (const JointObject& j : joints) saveJoint(o, j);
    for (const BatteryObject& b : batteries) saveBattery(o, b);
    for (const DriverObject& d : drivers) saveDriver(o, d);
    for (const SensorObject& x : sensors) saveSensor(o, x);
    for (const Force& f : forces) saveForce(o, f);
    for (const WheelObject& w : wheels) saveWheel(o, w);
    for (const ControllerObject& c : controllers) saveController(o, c);
    for (const SequenceObject& q : sequences) saveSequence(o, q);
    return o.str();
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
// An inertia tensor a body can have: positive about each axis, no axis heavier than the other two together (a tensor
// of a real distribution of mass is so), the products within what the diagonal allows.
static bool inertiaOk(const RigidRole& r) {
    const Vector3& d = r.inertiaDiagonal;
    const Vector3& p = r.inertiaProducts;
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(d[k]) || !std::isfinite(p[k]) || !(d[k] > 0)) return false;
    const Real slack = 1.000001_r;
    return d.x <= (d.y + d.z) * slack && d.y <= (d.x + d.z) * slack && d.z <= (d.x + d.y) * slack && p.x * p.x <= d.x * d.y && p.y * p.y <= d.x * d.z &&
           p.z * p.z <= d.y * d.z;
}

// The format as data: every kind of block, its lines and their keys, each key with the form of its values, their unit and
// how they are read and checked. The reader reads by these tables and schema_get answers them: a key written here is
// read and described at once - there is no other description to drift from it (docs/contracts/names.md, the abstract
// layer).
//
// A key reads its values into the object: false - not readable, `why` naming the word that was not (else the key is).
template <class T>
struct Key {
    const char* word;
    const char* form;   // what its values are, as schema_get says it
    const char* unit;   // "": none
    bool (*read)(LineReader& r, T& t, std::string& why);
};

// The plain forms, by the field they fill.
template <class T, Real T::*M> bool number(LineReader& r, T& t, std::string&) { return r.number(t.*M); }
template <class T, Vector3 T::*M> bool vector(LineReader& r, T& t, std::string&) { return r.vector(t.*M); }
template <class T, bool T::*M> bool flag(LineReader& r, T& t, std::string&) { return r.flag(t.*M); }
template <class T, uint32_t T::*M> bool whole(LineReader& r, T& t, std::string&) { return r.integer(t.*M); }
template <class T, std::string T::*M> bool text(LineReader& r, T& t, std::string&) { return r.text(t.*M); }
static bool within(LineReader& r, Real& v, Real low, Real high) { return r.number(v) && std::isfinite(v) && v >= low && v <= high; }
static bool above(LineReader& r, Real& v, Real low, Real high) { return r.number(v) && std::isfinite(v) && v > low && v <= high; }

// A word of a list: its index.
template <size_t N>
static bool oneOf(const std::string& word, const char* (&names)[N], int& out) {
    for (size_t i = 0; i < N; ++i)
        if (word == names[i]) return out = int(i), true;
    return false;
}
template <size_t N>
static bool oneOf(const std::string& word, const char* const (&names)[N], int& out) {
    for (size_t i = 0; i < N; ++i)
        if (word == names[i]) return out = int(i), true;
    return false;
}

// A freedom by its name (tx ... rz).
static bool readDof(LineReader& r, int& dof) {
    std::string name;
    return r.text(name) && oneOf(name, kDofNames, dof);
}

// What follows the word that could not be read, in `bad`: the hint load() says after it.
constexpr char kHint = '\x1f';

// The keys of a line from where the reader stands to its end, by the table. A word that is no key of the line is told
// with the line's keys and the one it most likely meant: a typo is mended in one look, not by a guess.
template <class T, size_t N>
static bool readKeys(LineReader& r, T& t, const Key<T> (&keys)[N], std::string& bad) {
    while (!r.done()) {
        const std::string k = r.key();
        const Key<T>* key = std::find_if(std::begin(keys), std::end(keys), [&](const Key<T>& x) { return k == x.word; });
        if (key == std::end(keys) && k.find(' ') == std::string::npos) { // not «<key> twice»: a key met again is no typo
            std::vector<std::string> words;
            std::string list;
            for (const Key<T>& x : keys) words.push_back(x.word), list += (list.empty() ? "" : ", ") + std::string(x.word);
            const std::string near = nearestWord(k, words);
            return fail(bad, k + kHint + (near.empty() ? "" : "did you mean '" + near + "'? ") + "the keys here: " + list);
        }
        std::string why;
        if (key == std::end(keys) || !key->read(r, t, why)) return fail(bad, why.empty() ? k : why);
    }
    return true;
}
template <class T, size_t N>
static std::vector<SchemaKey> described(const Key<T> (&keys)[N]) {
    std::vector<SchemaKey> out;
    for (const Key<T>& k : keys) out.push_back({k.word, k.form, k.unit});
    return out;
}

// --- the keys of each line ---------------------------------------------------------------------------------------------
static const Key<WorldSettings> kWorldKeys[] = {
    {"gravity", "vector", "m/s²", vector<WorldSettings, &WorldSettings::gravity>},
    {"size", "vector", "m", vector<WorldSettings, &WorldSettings::size>},
    {"duration", "number 0..86400; 0 - no end", "s", [](LineReader& r, WorldSettings& w, std::string&) { return within(r, w.duration, 0, 86400); }},
    {"record_from", "number 0..86400; before duration", "s", [](LineReader& r, WorldSettings& w, std::string&) { return within(r, w.recordFrom, 0, 86400); }},
};
static const Key<PhysicsSettings> kPhysicsKeys[] = {
    {"substeps", "count 1..64", "", [](LineReader& r, PhysicsSettings& p, std::string&) { return r.count(p.substeps, 1, 64); }},
    {"contact_hertz", "number above 0, to 10000", "Hz", [](LineReader& r, PhysicsSettings& p, std::string&) { return above(r, p.contactHertz, 0, 10000); }},
    {"contact_damping", "number 0..100", "", [](LineReader& r, PhysicsSettings& p, std::string&) { return within(r, p.contactDamping, 0, 100); }},
    {"joint_hertz", "number above 0, to 10000", "Hz", [](LineReader& r, PhysicsSettings& p, std::string&) { return above(r, p.jointHertz, 0, 10000); }},
    {"joint_damping", "number 0..100", "", [](LineReader& r, PhysicsSettings& p, std::string&) { return within(r, p.jointDamping, 0, 100); }},
    {"sleeping", "flag", "", flag<PhysicsSettings, &PhysicsSettings::sleeping>},
    {"continuous", "flag", "", flag<PhysicsSettings, &PhysicsSettings::continuous>},
    {"linear_damping", "number 0..100", "1/s", [](LineReader& r, PhysicsSettings& p, std::string&) { return within(r, p.linearDamping, 0, 100); }},
    {"angular_damping", "number 0..100", "1/s", [](LineReader& r, PhysicsSettings& p, std::string&) { return within(r, p.angularDamping, 0, 100); }},
};
static const Key<SceneObject> kObjectKeys[] = {
    {"id", "id", "", whole<SceneObject, &SceneObject::id>},
    {"visible", "flag", "", flag<SceneObject, &SceneObject::visible>},
    {"locked", "flag", "", flag<SceneObject, &SceneObject::locked>},
    {"parent", "id; 0 - none", "", whole<SceneObject, &SceneObject::parent>},
};
static const Key<SceneObject> kPoseKeys[] = {
    {"position", "vector", "m", vector<SceneObject, &SceneObject::position>},
    {"rotation", "vector of Euler angles, applied Ry Rx Rz", "°", vector<SceneObject, &SceneObject::rotationDeg>},
    {"color", "vector 0..1", "", vector<SceneObject, &SceneObject::color>},
};
static const Key<Entity> kEntityObjectKeys[] = { // an entity's object line: an instance says whose copy it is
    {"id", "id", "", [](LineReader& r, Entity& e, std::string&) { return r.integer(e.id); }},
    {"visible", "flag", "", [](LineReader& r, Entity& e, std::string&) { return r.flag(e.visible); }},
    {"locked", "flag", "", [](LineReader& r, Entity& e, std::string&) { return r.flag(e.locked); }},
    {"parent", "id; 0 - none", "", [](LineReader& r, Entity& e, std::string&) { return r.integer(e.parent); }},
    {"instance", "id of the template", "", whole<Entity, &Entity::instanceOf>},
};
static const Key<Entity> kShapeKeys[] = {
    {"size", "vector", "m", [](LineReader& r, Entity& e, std::string&) { return r.vector(e.size); }},
    {"position", "vector", "m", [](LineReader& r, Entity& e, std::string&) { return r.vector(e.position); }},
    {"rotation", "vector of Euler angles, applied Ry Rx Rz", "°", [](LineReader& r, Entity& e, std::string&) { return r.vector(e.rotationDeg); }},
    {"color", "vector 0..1", "", [](LineReader& r, Entity& e, std::string&) { return r.vector(e.color); }},
    {"file", "text: an OBJ or STL file, from the scene's folder", "", text<Entity, &Entity::meshFile>},
    {"opacity", "number 0..1", "", [](LineReader& r, Entity& e, std::string&) { return within(r, e.opacity, 0, 1); }},
};
static const char* const kTextureNames[] = {"none", "checker", "image", "speckle", "stone"}; // in the order of TextureKind
static const Key<SurfaceTexture> kTextureKeys[] = {
    {"kind", "one of: none checker image speckle stone", "", [](LineReader& r, SurfaceTexture& t, std::string&) {
         std::string word;
         int k = 0;
         return r.text(word) && oneOf(word, kTextureNames, k) && (t.kind = TextureKind(k), true);
     }},
    {"file", "text: a picture, from the scene's folder", "", text<SurfaceTexture, &SurfaceTexture::file>},
    {"height_file", "text: a height map", "", text<SurfaceTexture, &SurfaceTexture::heightFile>},
    {"normal_file", "text: a normal map, green up", "", text<SurfaceTexture, &SurfaceTexture::normalFile>},
    {"bump", "number 0..0.05", "m", [](LineReader& r, SurfaceTexture& t, std::string&) { return within(r, t.bumpHeight, 0, 0.05_r); }},
    {"repeats", "number above 0, to 10000", "1/m", [](LineReader& r, SurfaceTexture& t, std::string&) { return above(r, t.repeats, 0, 10000); }},
};
static const Key<RigidRole> kRigidKeys[] = {
    {"density", "number", "kg/m³", number<RigidRole, &RigidRole::density>},
    {"friction", "number", "", number<RigidRole, &RigidRole::friction>},
    {"restitution", "number 0..1", "", number<RigidRole, &RigidRole::restitution>},
    {"fixed", "flag", "", flag<RigidRole, &RigidRole::fixed>},
    {"velocity", "vector", "m/s", vector<RigidRole, &RigidRole::velocity>},
    {"angular_velocity", "vector", "rad/s", vector<RigidRole, &RigidRole::angularVelocity>},
    {"kinematic", "flag", "", flag<RigidRole, &RigidRole::kinematic>},
    {"mass", "number 0 or above; 0 - from the density", "kg", [](LineReader& r, RigidRole& g, std::string&) { return r.number(g.mass) && std::isfinite(g.mass) && g.mass >= 0; }},
    {"center_of_mass", "vector, in the body's axes", "m", [](LineReader& r, RigidRole& g, std::string&) { return g.centerGiven = r.vector(g.centerOfMass); }},
    {"inertia", "2 vectors: the diagonal and the products", "kg·m²", [](LineReader& r, RigidRole& g, std::string&) {
         return g.inertiaGiven = r.vector(g.inertiaDiagonal) && r.vector(g.inertiaProducts) && inertiaOk(g);
     }},
    {"gravity_scale", "number -100..100", "", [](LineReader& r, RigidRole& g, std::string&) { return within(r, g.gravityScale, -100, 100); }},
    {"linear_damping", "number 0..100", "1/s", [](LineReader& r, RigidRole& g, std::string&) { return within(r, g.linearDamping, 0, 100); }},
    {"angular_damping", "number 0..100", "1/s", [](LineReader& r, RigidRole& g, std::string&) { return within(r, g.angularDamping, 0, 100); }},
};
static const Key<ColliderRole> kColliderKeys[] = {
    {"kind", "one of: auto box sphere capsule hull cylinder mesh convex_parts", "", [](LineReader& r, ColliderRole& c, std::string&) {
         std::string word;
         int k = 0;
         return r.text(word) && oneOf(word, kColliderNames, k) && (c.kind = ColliderKind(k), true);
     }},
    {"fit", "flag: the collider fits the shape", "", flag<ColliderRole, &ColliderRole::fitToGeometry>},
    {"size", "vector", "m", vector<ColliderRole, &ColliderRole::size>},
    {"offset", "vector, in the body's axes", "m", vector<ColliderRole, &ColliderRole::offset>},
    {"rotation", "vector of Euler angles", "°", vector<ColliderRole, &ColliderRole::rotationDeg>},
    {"trigger", "flag: it notices contacts, pushes nothing", "", flag<ColliderRole, &ColliderRole::trigger>},
    {"hulls", "whole number 1..64: a moving body's mesh as at most so many convex parts", "",
     [](LineReader& r, ColliderRole& c, std::string&) { return r.integer(c.hulls) && c.hulls >= 1 && c.hulls <= 64; }},
    {"concavity", "number 0.005..0.5: how far a convex part may stand out into the air, a share of the body's diagonal", "",
     [](LineReader& r, ColliderRole& c, std::string&) { return within(r, c.concavity, 0.005_r, 0.5_r); }},
};
static const Key<Entity> kPathKeys[] = {
    {"speed", "number above 0", "m/s", [](LineReader& r, Entity& e, std::string&) { return r.number(e.pathSpeed) && e.pathSpeed > 0; }},
    {"loop", "flag", "", flag<Entity, &Entity::pathLoop>},
    {"smooth", "flag", "", flag<Entity, &Entity::pathSmooth>},
    {"turn", "number 0 or above: metres of the path a radian of turn costs", "m/rad",
     [](LineReader& r, Entity& e, std::string&) { return r.number(e.pathTurnLength) && e.pathTurnLength >= 0; }},
};
static const Key<ArrayObject> kPatternKeys[] = {
    {"template", "id", "", whole<ArrayObject, &ArrayObject::templateId>},
    {"count", "3 whole numbers: copies along x, y, z", "", [](LineReader& r, ArrayObject& a, std::string&) {
         return r.integer(a.count[0]) && r.integer(a.count[1]) && r.integer(a.count[2]);
     }},
    {"step", "vector", "m", vector<ArrayObject, &ArrayObject::step>},
    {"radius", "number", "m", number<ArrayObject, &ArrayObject::radius>},
    {"rotation_step", "vector of Euler angles", "°", vector<ArrayObject, &ArrayObject::rotationStepDeg>},
    {"jitter", "number", "m", number<ArrayObject, &ArrayObject::jitter>},
    {"seed", "whole number", "", whole<ArrayObject, &ArrayObject::seed>},
};
static const Key<Light> kLightKeys[] = {
    {"intensity", "number", "", number<Light, &Light::intensity>},
    {"range", "number", "m", number<Light, &Light::range>},
    {"cone", "number", "°", number<Light, &Light::coneDeg>},
    {"softness", "number", "°", number<Light, &Light::softnessDeg>},
    {"shadows", "flag", "", flag<Light, &Light::shadows>},
};
template <class E, size_t N>
static bool wordOf(LineReader& r, const char* (&names)[N], E& out, std::string& why) {
    std::string word;
    int v = 0;
    if (!r.text(word) || !oneOf(word, names, v)) return why = word, false; // the word is named, not the key
    return out = E(v), true;
}
static const Key<Force> kEffectKeys[] = {
    {"radius", "number", "m", number<Force, &Force::radius>},
    {"strength", "number: a field's m/s or m/s², a body's own N s or N, a torque's N m s or N m", "", number<Force, &Force::strength>},
    {"start", "number", "s", number<Force, &Force::start>},
    {"tag", "text: the bodies it acts on; none - every body", "", text<Force, &Force::tag>},
    {"shape", "one of: directional radial torque", "", [](LineReader& r, Force& f, std::string& why) { return wordOf(r, kForceShapeNames, f.shape, why); }},
    {"mode", "one of: impulse constant", "", [](LineReader& r, Force& f, std::string& why) { return wordOf(r, kForceModeNames, f.mode, why); }},
    {"falloff", "one of: none linear inverse_square", "", [](LineReader& r, Force& f, std::string& why) { return wordOf(r, kForceFalloffNames, f.falloff, why); }},
};
static const Key<Camera> kLensKeys[] = {
    {"fov", "number: the vertical field of view", "°", number<Camera, &Camera::fovDeg>},
    {"near", "number", "m", number<Camera, &Camera::nearClip>},
    {"far", "number", "m", number<Camera, &Camera::farClip>},
    {"active", "flag: the view is through it", "", flag<Camera, &Camera::active>},
};
static const Key<JointObject> kJointObjectKeys[] = {{"id", "id", "", whole<JointObject, &JointObject::id>}};
static const Key<JointObject> kLinkKeys[] = {
    {"a", "id: the moving body", "", whole<JointObject, &JointObject::a>},
    {"b", "id: what it hangs on; 0 - the world", "", whole<JointObject, &JointObject::b>},
    {"anchor_a", "vector, in a's axes", "m", vector<JointObject, &JointObject::anchorA>},
    {"anchor_b", "vector, in b's axes", "m", vector<JointObject, &JointObject::anchorB>},
    {"frame_a", "vector of Euler angles: the joint's axes in a", "°", vector<JointObject, &JointObject::frameA>},
    {"frame_b", "vector of Euler angles: the joint's axes in b", "°", vector<JointObject, &JointObject::frameB>},
};
static const Key<JointFreedom> kLimitKeys[] = {
    {"lower", "number", "rad or m", number<JointFreedom, &JointFreedom::lower>},
    {"upper", "number", "rad or m", number<JointFreedom, &JointFreedom::upper>},
};
static const Key<JointDrive> kDriveKeys[] = {
    {"stiffness", "number", "N·m/rad or N/m", number<JointDrive, &JointDrive::stiffness>},
    {"damping", "number", "N·m·s/rad or N·s/m", number<JointDrive, &JointDrive::damping>},
    {"target", "number", "rad or m", number<JointDrive, &JointDrive::target>},
    {"velocity", "number", "rad/s or m/s", number<JointDrive, &JointDrive::targetVelocity>},
    {"force", "number: the most it gives", "N·m or N", number<JointDrive, &JointDrive::maxForce>},
};
static const Key<JointDrive> kMotorKeys[] = {
    {"voltage", "number", "V", number<JointDrive, &JointDrive::voltage>},
    {"resistance", "number", "Ω", number<JointDrive, &JointDrive::resistance>},
    {"constant", "number", "N·m/A", number<JointDrive, &JointDrive::constant>},
};
static const Key<BatteryObject> kBatteryObjectKeys[] = {{"id", "id", "", whole<BatteryObject, &BatteryObject::id>}};
static const Key<BatteryObject> kCellsKeys[] = {
    {"series", "count 1..1000", "", [](LineReader& r, BatteryObject& b, std::string&) { return r.count(b.series, 1, 1000); }},
    {"parallel", "count 1..1000", "", [](LineReader& r, BatteryObject& b, std::string&) { return r.count(b.parallel, 1, 1000); }},
    {"capacity", "number: of one cell", "A·h", number<BatteryObject, &BatteryObject::capacityAh>},
    {"r0", "number: of one cell", "Ω", number<BatteryObject, &BatteryObject::r0>},
    {"r1", "number: of one cell", "Ω", number<BatteryObject, &BatteryObject::r1>},
    {"c1", "number: of one cell", "F", number<BatteryObject, &BatteryObject::c1>},
    {"cutoff", "number: of one cell", "V", number<BatteryObject, &BatteryObject::cutoff>},
    {"soc", "number 0..1: the charge it starts with", "", number<BatteryObject, &BatteryObject::soc>},
};
static const Key<SensorObject> kNoisyKeys[] = {
    {"noise", "flag: its datasheet's noise and offsets", "", flag<SensorObject, &SensorObject::noise>},
    {"seed", "whole number", "", whole<SensorObject, &SensorObject::seed>},
};
static const Key<SensorObject> kStereoKeys[] = {
    {"width", "count 16..4096", "px", [](LineReader& r, SensorObject& s, std::string&) { return r.count(s.width, 16, 4096); }},
    {"height", "count 16..4096", "px", [](LineReader& r, SensorObject& s, std::string&) { return r.count(s.height, 16, 4096); }},
    {"fov", "number above 1, below 170: the horizontal field of view", "°", [](LineReader& r, SensorObject& s, std::string&) { return r.number(s.fovDeg) && s.fovDeg > 1 && s.fovDeg < 170; }},
    {"baseline", "number above 0", "m", [](LineReader& r, SensorObject& s, std::string&) { return r.number(s.baseline) && s.baseline > 0; }},
    {"projector", "flag", "", flag<SensorObject, &SensorObject::projector>},
    {"noise", "flag", "", flag<SensorObject, &SensorObject::noise>},
    {"seed", "whole number", "", whole<SensorObject, &SensorObject::seed>},
};
static const Key<SensorObject> kEffortKeys[] = {
    {"dof", "one of: tx ty tz rx ry rz", "", [](LineReader& r, SensorObject& s, std::string&) { return readDof(r, s.dof); }},
};
static const Key<SensorObject> kEncoderKeys[] = {
    {"dof", "one of: tx ty tz rx ry rz", "", [](LineReader& r, SensorObject& s, std::string&) { return readDof(r, s.dof); }},
    {"lines", "count from 1: per turn, or per metre of a travel", "", [](LineReader& r, SensorObject& s, std::string&) { return r.count(s.lines, 1, 1 << 30); }},
    {"against", "id: what it counts against; 0 - the world", "", whole<SensorObject, &SensorObject::against>},
    {"reference", "number: the joint's coordinate at the count 0", "rad or m",
     [](LineReader& r, SensorObject& s, std::string&) { return s.hasEncoderReference = r.number(s.encoderReference); }},
};
static const Key<WheelObject> kTyreKeys[] = {
    {"radius", "number", "m", number<WheelObject, &WheelObject::radius>},
    {"width", "number", "m", number<WheelObject, &WheelObject::width>},
    {"mass", "number", "kg", number<WheelObject, &WheelObject::mass>},
    {"friction", "number", "", number<WheelObject, &WheelObject::friction>},
    {"unsprung", "number", "kg", number<WheelObject, &WheelObject::unsprung>},
    {"rolling", "number: the rolling resistance coefficient", "", number<WheelObject, &WheelObject::rolling>},
};
static const Key<WheelObject> kSpringKeys[] = {
    {"rest", "number: its length unloaded", "m", number<WheelObject, &WheelObject::rest>},
    {"stiffness", "number", "N/m", number<WheelObject, &WheelObject::stiffness>},
    {"damping", "number", "N·s/m", number<WheelObject, &WheelObject::damping>},
};
static const Key<WheelObject> kWheelMotorKeys[] = {
    {"voltage", "number", "V", number<WheelObject, &WheelObject::voltage>},
    {"resistance", "number", "Ω", number<WheelObject, &WheelObject::resistance>},
    {"constant", "number", "N·m/A", number<WheelObject, &WheelObject::constant>},
    {"flip", "flag: it turns the other way", "", flag<WheelObject, &WheelObject::flip>},
};
static const Key<DriverObject> kDriverObjectKeys[] = {{"id", "id", "", whole<DriverObject, &DriverObject::id>}};
static const Key<DriverObject> kBridgeKeys[] = {
    {"dof", "one of: tx ty tz rx ry rz", "", [](LineReader& r, DriverObject& d, std::string&) { return readDof(r, d.dof); }},
    {"stop", "one of: coast brake", "", [](LineReader& r, DriverObject& d, std::string&) {
         std::string how;
         const bool ok = r.text(how) && (how == "coast" || how == "brake");
         d.brake = how == "brake";
         return ok;
     }},
    {"joint", "id", "", whole<DriverObject, &DriverObject::joint>},
    {"battery", "id", "", whole<DriverObject, &DriverObject::battery>},
    {"duty", "number -1..1", "", number<DriverObject, &DriverObject::duty>},
    {"switches", "number: the bridge's switches", "Ω", number<DriverObject, &DriverObject::switches>},
    {"limit", "number: the current limit", "A", number<DriverObject, &DriverObject::currentLimit>},
};
static const Key<ControllerAction> kActionKeys[] = {
    {"drive", "one of: plane lift turn grip (an IK goal) jog (a joint, with dof) duty (a driver) value (nothing: a plugin reads it - a car's steer and throttle)", "", [](LineReader& r, ControllerAction& a, std::string& why) {
         std::string drive;
         int d = -1;
         if (r.text(drive) && oneOf(drive, kControlDrives, d)) return a.drive = ControlDrive(d), true;
         // the words a reader may write, named in the error: a guess costs a call otherwise
         return why = "drive " + drive + "' - a drive is one of plane, lift, turn, grip (an IK goal), jog (a joint, with dof), duty (a driver)", false;
     }},
    {"target", "id: what it drives", "", whole<ControllerAction, &ControllerAction::target>},
    {"speed", "number: at a value of 1", "m/s or rad/s", number<ControllerAction, &ControllerAction::speed>},
    {"dof", "one of: tx ty tz rx ry rz", "", [](LineReader& r, ControllerAction& a, std::string&) { return readDof(r, a.dof); }},
};
static const Key<SequenceObject> kClipKeys[] = {
    {"time", "number: the playhead", "s", number<SequenceObject, &SequenceObject::time>},
    {"loop", "flag", "", flag<SequenceObject, &SequenceObject::loop>},
    {"active", "flag: the Sequencer's", "", flag<SequenceObject, &SequenceObject::active>},
    {"play", "flag: ▶ plays it", "", flag<SequenceObject, &SequenceObject::play>},
};
static const Key<AnimKey> kKeyKeys[] = {
    {"time", "number, after the track's last key", "s", number<AnimKey, &AnimKey::time>},
    {"position", "vector", "m", vector<AnimKey, &AnimKey::position>},
    {"rotation", "vector of Euler angles", "°", vector<AnimKey, &AnimKey::rotationDeg>},
    {"velocity", "vector: the motion's velocity there", "m/s",
     [](LineReader& r, AnimKey& k, std::string&) { return k.hasVelocity = true, k.interp = AnimInterp::Smooth, r.vector(k.velocity); }},
    {"interp", "one of: linear smooth step", "", [](LineReader& r, AnimKey& k, std::string&) { return !r.done() && interpOf(r.key(), k.interp); }},
};

// --- the lines of each block ------------------------------------------------------------------------------------------
// A line of a block: its first word, what stands between it and its keys (schema_get's lead), its keys for the schema and
// how the whole line is read.
template <class T>
struct Part {
    const char* word;
    const char* lead;
    std::vector<SchemaKey> (*keys)();
    bool (*read)(const std::vector<std::string>& w, T& t, std::string& bad);
};
static std::vector<SchemaKey> none() { return {}; }
template <class T, class O, size_t N>
static bool keysFrom(const std::vector<std::string>& w, size_t start, O& o, const Key<T> (&keys)[N], std::string& bad) {
    LineReader r(w, start);
    return readKeys(r, static_cast<T&>(o), keys, bad);
}
// A line whose second word is one of a list (a shape, a pattern, a light's kind), then keys.
template <class E, size_t N>
static bool leadWord(const std::vector<std::string>& w, const char* (&names)[N], E& out, std::string& bad) {
    int v = 0;
    if (w.size() < 2) return fail(bad, w[0]);
    if (!oneOf(w[1], names, v)) return fail(bad, w[1]);
    return out = E(v), true;
}

static const Part<SceneObject> kObjectPart = {"object", "", [] { return described(kObjectKeys); },
                                              [](const std::vector<std::string>& w, SceneObject& s, std::string& bad) { return keysFrom(w, 1, s, kObjectKeys, bad); }};
static const Part<SceneObject> kPosePart = {"pose", "", [] { return described(kPoseKeys); },
                                            [](const std::vector<std::string>& w, SceneObject& s, std::string& bad) { return keysFrom(w, 1, s, kPoseKeys, bad); }};

static const Part<Entity> kEntityParts[] = {
    {"object", "", [] { return described(kEntityObjectKeys); },
     [](const std::vector<std::string>& w, Entity& e, std::string& bad) { return keysFrom(w, 1, e, kEntityObjectKeys, bad); }},
    {"shape", "one of: box sphere cylinder cone plane mesh", [] { return described(kShapeKeys); }, [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         if (w.size() < 2) return fail(bad, w[0]);
         int s = 0;
         if (!oneOf(w[1], kShapeNames, s)) return fail(bad, w[1]);
         e.shape = ShapeKind(s);
         return keysFrom(w, 2, e, kShapeKeys, bad);
     }},
    {"texture", "", [] { return described(kTextureKeys); },
     [](const std::vector<std::string>& w, Entity& e, std::string& bad) { return keysFrom(w, 1, e.texture, kTextureKeys, bad); }},
    {"rigid", "", [] { return described(kRigidKeys); }, [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         e.rigid.enabled = true;
         return keysFrom(w, 1, e.rigid, kRigidKeys, bad);
     }},
    {"collider", "", [] { return described(kColliderKeys); }, [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         e.collider.enabled = true;
         return keysFrom(w, 1, e.collider, kColliderKeys, bad);
     }},
    {"target", "camera <id>: this entity is that camera's own target", none, [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         LineReader r(w, 1);
         return (!r.done() && r.key() == "camera" && r.integer(e.targetOf) && r.done() && e.targetOf != 0) || fail(bad, w[0]);
     }},
    {"goal", "effector <id>: an inverse kinematics goal of that effector", none, [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         LineReader r(w, 1);
         return (!r.done() && r.key() == "effector" && r.integer(e.goalOf) && r.done() && e.goalOf != 0) || fail(bad, w[0]);
     }},
    {"ignores", "tags: what the collider has no contact with", none, [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         for (size_t i = 1; i < w.size(); ++i) {
             if (!tagWord(w[i])) return fail(bad, w[i]);
             if (std::find(e.collider.ignores.begin(), e.collider.ignores.end(), w[i]) == e.collider.ignores.end()) e.collider.ignores.push_back(w[i]);
         }
         return true;
     }},
    {"path", "", [] { return described(kPathKeys); }, [](const std::vector<std::string>& w, Entity& e, std::string& bad) { return keysFrom(w, 1, e, kPathKeys, bad); }},
    {"waypoint", "x y z [turn rx ry rz]: a point of the path, m, and its turn, ° - every waypoint turned, or none", none,
     [](const std::vector<std::string>& w, Entity& e, std::string& bad) {
         Vector3 p, t;
         const bool point = (w.size() == 4 || w.size() == 8) && parseNumber(w[1], p.x) && parseNumber(w[2], p.y) && parseNumber(w[3], p.z);
         const bool turned = w.size() == 8 && w[4] == "turn" && parseNumber(w[5], t.x) && parseNumber(w[6], t.y) && parseNumber(w[7], t.z);
         if (!point || (w.size() == 8 && !turned) || (turned ? e.pathTurns.size() != e.path.size() : !e.pathTurns.empty())) return fail(bad, w[0]);
         e.path.push_back(p);
         if (turned) e.pathTurns.push_back(t);
         return true;
     }},
};
static const Part<Group> kGroupParts[] = {
    {"glued", "nothing: the group moves as one", none, [](const std::vector<std::string>& w, Group& g, std::string& bad) { return w.size() == 1 ? (g.glued = true) : fail(bad, w[0]); }},
};
static const Part<ArrayObject> kArrayParts[] = {
    {"pattern", "one of: line grid circle", [] { return described(kPatternKeys); }, [](const std::vector<std::string>& w, ArrayObject& a, std::string& bad) {
         return leadWord(w, kPatternNames, a.pattern, bad) && keysFrom(w, 2, a, kPatternKeys, bad);
     }},
};
static const Part<Light> kLightParts[] = {
    {"kind", "one of: sun point spot", [] { return described(kLightKeys); }, [](const std::vector<std::string>& w, Light& l, std::string& bad) {
         return leadWord(w, kLightNames, l.kind, bad) && keysFrom(w, 2, l, kLightKeys, bad);
     }},
};
static const Part<Force> kForceParts[] = {
    {"effect", "", [] { return described(kEffectKeys); }, [](const std::vector<std::string>& w, Force& f, std::string& bad) {
         if (!keysFrom(w, 1, f, kEffectKeys, bad)) return false;
         const char* fault = forceFault(f);
         return !fault || fail(bad, fault);
     }},
};
static const Part<Camera> kCameraParts[] = {
    {"target", "object <id>: a target camera looks at that object", none, [](const std::vector<std::string>& w, Camera& c, std::string& bad) {
         LineReader r(w, 1);
         c.kind = CameraKind::Target;
         const std::string k = r.done() ? std::string() : r.key();
         const bool ok = k == "object" && r.integer(c.target);
         return ok && r.done() ? true : fail(bad, k.empty() ? "target" : k);
     }},
    {"lens", "", [] { return described(kLensKeys); }, [](const std::vector<std::string>& w, Camera& c, std::string& bad) { return keysFrom(w, 1, c, kLensKeys, bad); }},
};
// "free|limit|drive|dcmotor <dof> key value...": the freedom named by the second word.
static JointFreedom* freedom(const std::vector<std::string>& w, JointObject& j, std::string& bad) {
    int k = -1;
    if (w.size() < 2 || !oneOf(w[1], kDofNames, k)) return fail(bad, w.size() > 1 ? w[1] : w[0]), nullptr;
    return &j.dof[k];
}
static const Part<JointObject> kJointParts[] = {
    {"object", "", [] { return described(kJointObjectKeys); },
     [](const std::vector<std::string>& w, JointObject& j, std::string& bad) { return keysFrom(w, 1, j, kJointObjectKeys, bad); }},
    {"link", "", [] { return described(kLinkKeys); }, [](const std::vector<std::string>& w, JointObject& j, std::string& bad) { return keysFrom(w, 1, j, kLinkKeys, bad); }},
    {"collide", "nothing: its two bodies collide", none, [](const std::vector<std::string>& w, JointObject& j, std::string& bad) { return w.size() == 1 ? (j.collide = true) : fail(bad, w[0]); }},
    {"free", "one of: tx ty tz rx ry rz - a free freedom", none, [](const std::vector<std::string>& w, JointObject& j, std::string& bad) {
         JointFreedom* f = freedom(w, j, bad);
         return f && (f->motion = JointMotion::Free, w.size() == 2 || fail(bad, w[2]));
     }},
    {"limit", "one of: tx ty tz rx ry rz - a limited freedom", [] { return described(kLimitKeys); }, [](const std::vector<std::string>& w, JointObject& j, std::string& bad) {
         JointFreedom* f = freedom(w, j, bad);
         return f && (f->motion = JointMotion::Limited, keysFrom(w, 2, *f, kLimitKeys, bad));
     }},
    {"drive", "one of: tx ty tz rx ry rz - the freedom's drive", [] { return described(kDriveKeys); }, [](const std::vector<std::string>& w, JointObject& j, std::string& bad) {
         JointFreedom* f = freedom(w, j, bad);
         return f && (f->drive.enabled = true, keysFrom(w, 2, f->drive, kDriveKeys, bad));
     }},
    {"dcmotor", "one of: tx ty tz rx ry rz - the freedom's DC motor", [] { return described(kMotorKeys); }, [](const std::vector<std::string>& w, JointObject& j, std::string& bad) {
         JointFreedom* f = freedom(w, j, bad);
         return f && (f->drive.enabled = true, keysFrom(w, 2, f->drive, kMotorKeys, bad));
     }},
};
static const Part<BatteryObject> kBatteryParts[] = {
    {"object", "", [] { return described(kBatteryObjectKeys); },
     [](const std::vector<std::string>& w, BatteryObject& b, std::string& bad) { return keysFrom(w, 1, b, kBatteryObjectKeys, bad); }},
    {"cells", "", [] { return described(kCellsKeys); }, [](const std::vector<std::string>& w, BatteryObject& b, std::string& bad) { return keysFrom(w, 1, b, kCellsKeys, bad); }},
};
template <SensorKind K, size_t N>
static bool sensorLine(const std::vector<std::string>& w, SensorObject& s, const Key<SensorObject> (&keys)[N], std::string& bad) {
    s.kind = K;
    return keysFrom(w, 1, s, keys, bad);
}
static const Part<SensorObject> kSensorParts[] = { // a sensor's kind is the first word of its line, in the order of SensorKind
    {"encoder", "", [] { return described(kEncoderKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Encoder>(w, s, kEncoderKeys, bad); }},
    {"imu", "", [] { return described(kNoisyKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Imu>(w, s, kNoisyKeys, bad); }},
    {"sonar", "", [] { return described(kNoisyKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Sonar>(w, s, kNoisyKeys, bad); }},
    {"touch", "", none, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { s.kind = SensorKind::Touch; return w.size() == 1 || fail(bad, w[1]); }},
    {"wrench", "", [] { return described(kNoisyKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Wrench>(w, s, kNoisyKeys, bad); }},
    {"effort", "", [] { return described(kEffortKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Effort>(w, s, kEffortKeys, bad); }},
    {"stereo", "", [] { return described(kStereoKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Stereo>(w, s, kStereoKeys, bad); }},
    {"lidar", "", [] { return described(kNoisyKeys); }, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { return sensorLine<SensorKind::Lidar>(w, s, kNoisyKeys, bad); }},
    {"model", "text: a row of data/sensors/<kind>.csv", none, [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) {
         return w.size() == 2 ? (s.model = w[1], true) : fail(bad, "model");
     }},
    {"spec", "key number...: the datasheet's fields - the columns of data/sensors/<kind>.csv (sensor_models)", none,
     [](const std::vector<std::string>& w, SensorObject& s, std::string& bad) { // after the kind's line: its fields' keys
         LineReader r(w, 1);
         while (!r.done()) {
             const std::string k = r.key();
             const bool known = std::any_of(specFields(s.kind).begin(), specFields(s.kind).end(), [&](const Field& f) { return k == f.key; });
             if (!known || !r.number(s.spec[k])) return fail(bad, k);
         }
         return true;
     }},
};
static const Part<WheelObject> kWheelParts[] = {
    {"tyre", "", [] { return described(kTyreKeys); }, [](const std::vector<std::string>& w, WheelObject& x, std::string& bad) { return keysFrom(w, 1, x, kTyreKeys, bad); }},
    {"spring", "", [] { return described(kSpringKeys); }, [](const std::vector<std::string>& w, WheelObject& x, std::string& bad) { return keysFrom(w, 1, x, kSpringKeys, bad); }},
    {"dcmotor", "", [] { return described(kWheelMotorKeys); }, [](const std::vector<std::string>& w, WheelObject& x, std::string& bad) { return keysFrom(w, 1, x, kWheelMotorKeys, bad); }},
};
static const Part<DriverObject> kDriverParts[] = {
    {"object", "", [] { return described(kDriverObjectKeys); },
     [](const std::vector<std::string>& w, DriverObject& d, std::string& bad) { return keysFrom(w, 1, d, kDriverObjectKeys, bad); }},
    {"bridge", "", [] { return described(kBridgeKeys); }, [](const std::vector<std::string>& w, DriverObject& d, std::string& bad) { return keysFrom(w, 1, d, kBridgeKeys, bad); }},
};
static const Part<ControllerObject> kControllerParts[] = {
    {"object", "id <id>", none, [](const std::vector<std::string>& w, ControllerObject& c, std::string& bad) {
         if (w.size() != 3 || w[1] != "id") return fail(bad, w[0]);
         LineReader r(w, 2);
         return r.integer(c.id) || fail(bad, w[0]);
     }},
    // "action <name> <value> drive <drive> target <id> [dof <dof>] speed <x>"
    {"action", "<name> one of: button axis vector2", [] { return described(kActionKeys); }, [](const std::vector<std::string>& w, ControllerObject& c, std::string& bad) {
         if (w.size() < 3 || std::any_of(c.actions.begin(), c.actions.end(), [&](const ControllerAction& a) { return a.name == w[1]; }))
             return fail(bad, w.size() > 1 ? "action " + w[1] : "action");
         ControllerAction a;
         a.name = w[1];
         int value = -1;
         if (!oneOf(w[2], kControlValues, value)) return fail(bad, w[2]);
         a.value = ControlValue(value);
         if (!keysFrom(w, 3, a, kActionKeys, bad)) return false;
         c.actions.push_back(a);
         return true;
     }},
    // "bind <action> <device> <control>... [invert] [toggle]" - after the action's own line.
    {"bind", "<action> <device: keys mouse gamepad> <control>... [invert] [toggle] - keys by Qt's names (W, Space, Up, Down, Left, Right; a vector: up down left right, an axis: plus minus), gamepad: left_stick right_stick left_x left_y right_x right_y left_trigger right_trigger a b x y left_bumper right_bumper start back, mouse: left right middle", none, [](const std::vector<std::string>& w, ControllerObject& c, std::string& bad) {
         auto a = std::find_if(c.actions.begin(), c.actions.end(), [&](const ControllerAction& x) { return w.size() > 1 && x.name == w[1]; });
         if (w.size() < 4 || a == c.actions.end()) return fail(bad, w.size() > 1 ? "bind " + w[1] : "bind");
         ControlBinding b;
         b.device = w[2];
         for (size_t k = 3; k < w.size(); ++k) {
             if (w[k] == "invert") b.invert = true;
             else if (w[k] == "toggle") b.toggle = true;
             else b.controls.push_back(w[k]);
         }
         if (b.controls.empty()) return fail(bad, "bind " + w[1]);
         a->bindings.push_back(b);
         return true;
     }},
};
static const Part<SequenceObject> kSequenceParts[] = {
    {"clip", "", [] { return described(kClipKeys); }, [](const std::vector<std::string>& w, SequenceObject& q, std::string& bad) { return keysFrom(w, 1, q, kClipKeys, bad); }},
    {"track", "object <id>: the object the keys below lead", none, [](const std::vector<std::string>& w, SequenceObject& q, std::string& bad) {
         LineReader r(w, 1);
         q.tracks.emplace_back();
         return (!r.done() && r.key() == "object" && r.integer(q.tracks.back().object) && r.done()) || fail(bad, "track");
     }},
    {"key", "", [] { return described(kKeyKeys); }, [](const std::vector<std::string>& w, SequenceObject& q, std::string& bad) { // to the track above, by time
         if (q.tracks.empty()) return fail(bad, w[0]);
         AnimKey key;
         if (!keysFrom(w, 1, key, kKeyKeys, bad)) return false;
         std::vector<AnimKey>& keys = q.tracks.back().keys;
         if (!keys.empty() && !(key.time > keys.back().time)) return fail(bad, "time");
         keys.push_back(key);
         return true;
     }},
    {"field", "object <id> line \"<line>\" key \"<key>\": a field of the object the values below set, as scene_set names it", none,
     [](const std::vector<std::string>& w, SequenceObject& q, std::string& bad) {
         FieldTrack t;
         LineReader r(w, 1);
         if (r.done() || r.key() != "object" || !r.integer(t.object)) return fail(bad, "field");
         for (size_t k = 3; k + 1 < w.size(); k += 2) {
             if (w[k] == "line") t.line = w[k + 1];
             else if (w[k] == "key") t.key = w[k + 1];
             else return fail(bad, w[k]);
         }
         if (t.line.empty() || w.size() % 2 == 0) return fail(bad, "field");
         q.fields.push_back(t);
         return true;
     }},
    {"value", "time <s> values <numbers...> [interp linear|smooth|step]: the field above at that time", none,
     [](const std::vector<std::string>& w, SequenceObject& q, std::string& bad) { // to the field above, by time
         if (q.fields.empty() || w.size() < 5 || w[1] != "time" || w[3] != "values") return fail(bad, w[0]);
         FieldKey k;
         if (!parseNumber(w[2], k.time)) return fail(bad, "time");
         size_t i = 4;
         for (; i < w.size() && w[i] != "interp"; ++i) {
             Real v;
             if (!parseNumber(w[i], v)) return fail(bad, w[i]);
             k.values.push_back(v);
         }
         if (i < w.size() && (i + 2 != w.size() || !interpOf(w[i + 1], k.interp))) return fail(bad, "interp");
         std::vector<FieldKey>& keys = q.fields.back().keys;
         if (k.values.empty() || (!keys.empty() && !(k.time > keys.back().time))) return fail(bad, k.values.empty() ? "values" : "time");
         keys.push_back(k);
         return true;
     }},
};

// One line of a block of T: a SceneObject's object and pose lines when T is one, then its own parts.
template <class T, size_t N>
static bool readPart(const std::vector<std::string>& w, T& t, const Part<T> (&parts)[N], std::string& bad) {
    if constexpr (std::is_base_of_v<SceneObject, T> && !std::is_same_v<T, Entity>)
        if (w[0] == "object" || w[0] == "pose") return (w[0] == "object" ? kObjectPart : kPosePart).read(w, t, bad);
    const Part<T>* part = std::find_if(std::begin(parts), std::end(parts), [&](const Part<T>& p) { return w[0] == p.word; });
    return part != std::end(parts) ? part->read(w, t, bad) : fail(bad, w[0]);
}
template <class T, size_t N>
static SchemaKind schemaOf(const char* kind, const Part<T> (&parts)[N]) {
    SchemaKind k{kind, {}};
    if constexpr (std::is_base_of_v<SceneObject, T> && !std::is_same_v<T, Entity>)
        for (const Part<SceneObject>* p : {&kObjectPart, &kPosePart}) k.parts.push_back({p->word, p->lead, p->keys()});
    for (const Part<T>& p : parts) k.parts.push_back({p.word, p.lead, p.keys()});
    k.parts.push_back({"tags", "words: what it is marked with", {}}); // every block's (readLine)
    k.parts.push_back({"component", "<plugin>.<name> key value...: a plugin's component and its fields", {}});
    return k;
}

const std::vector<SchemaKind>& sceneSchema() {
    static const std::vector<SchemaKind> schema = [] {
        std::vector<SchemaKind> s;
        s.push_back({"world", {{"world", "", described(kWorldKeys)}}});
        s.push_back({"physics", {{"physics", "", described(kPhysicsKeys)}}});
        s.push_back(schemaOf("entity", kEntityParts));
        s.push_back(schemaOf("group", kGroupParts));
        s.push_back(schemaOf("array", kArrayParts));
        s.push_back(schemaOf("light", kLightParts));
        s.push_back(schemaOf("force", kForceParts));
        s.push_back(schemaOf("camera", kCameraParts));
        s.push_back(schemaOf("joint", kJointParts));
        s.push_back(schemaOf("battery", kBatteryParts));
        s.push_back(schemaOf("driver", kDriverParts));
        s.push_back(schemaOf("sensor", kSensorParts));
        s.push_back(schemaOf("wheel", kWheelParts));
        s.push_back(schemaOf("controller", kControllerParts));
        s.push_back(schemaOf("sequence", kSequenceParts));
        return s;
    }();
    return schema;
}

namespace {
enum class Block { None, Entity, Group, Array, Light, Force, Camera, Joint, Battery, Driver, Sensor, Wheel, Controller, Sequence };
}

// One line of a scene file in the block it belongs to (or starting / ending one).
static bool readLine(const std::vector<std::string>& w, SceneGraph& g, Block& block, std::string& bad) {
    if (block == Block::None) {
        const std::string name = w.size() > 1 ? w[1] : std::string();
        if (w[0] == "physics") {
            LineReader r(w, 1);
            return readKeys(r, g.world.physics, kPhysicsKeys, bad);
        }
        if (w[0] == "world") {
            LineReader r(w, 1);
            if (!readKeys(r, g.world, kWorldKeys, bad)) return false;
            if (g.world.duration > 0 && g.world.recordFrom >= g.world.duration) // a take that would start after the run ends
                return bad = "record_from " + num(g.world.recordFrom) + " is not before duration " + num(g.world.duration), false;
            return true;
        }
        if (w[0] == "entity") g.entities.emplace_back(), g.entities.back().name = name, block = Block::Entity;
        else if (w[0] == "group") g.groups.emplace_back(), g.groups.back().name = name, block = Block::Group;
        else if (w[0] == "array") g.arrays.emplace_back(), g.arrays.back().name = name, block = Block::Array;
        else if (w[0] == "light") g.lights.emplace_back(), g.lights.back().name = name, block = Block::Light;
        else if (w[0] == "force") g.forces.emplace_back(), g.forces.back().name = name, block = Block::Force;
        else if (w[0] == "camera") g.cameras.emplace_back(), g.cameras.back().name = name, block = Block::Camera;
        else if (w[0] == "joint") g.joints.emplace_back(), g.joints.back().name = name, block = Block::Joint;
        else if (w[0] == "battery") g.batteries.emplace_back(), g.batteries.back().name = name, block = Block::Battery;
        else if (w[0] == "driver") g.drivers.emplace_back(), g.drivers.back().name = name, block = Block::Driver;
        else if (w[0] == "sensor") g.sensors.emplace_back(), g.sensors.back().name = name, block = Block::Sensor;
        else if (w[0] == "wheel") g.wheels.emplace_back(), g.wheels.back().name = name, block = Block::Wheel;
        else if (w[0] == "controller") g.controllers.emplace_back(), g.controllers.back().name = name, block = Block::Controller;
        else if (w[0] == "sequence") g.sequences.emplace_back(), g.sequences.back().name = name, block = Block::Sequence;
        else return fail(bad, w[0]);
        return true;
    }
    if (w[0] == "tags" || w[0] == "component") { // an object's, whatever it is
        std::vector<std::string>* tags = nullptr;
        std::vector<PluginComponent>* components = nullptr;
        const auto last = [&](auto& objects) { tags = &objects.back().tags, components = &objects.back().components; };
        switch (block) {
        case Block::Entity: last(g.entities); break;
        case Block::Group: last(g.groups); break;
        case Block::Array: last(g.arrays); break;
        case Block::Light: last(g.lights); break;
        case Block::Force: last(g.forces); break;
        case Block::Camera: last(g.cameras); break;
        case Block::Joint: last(g.joints); break;
        case Block::Battery: last(g.batteries); break;
        case Block::Driver: last(g.drivers); break;
        case Block::Sensor: last(g.sensors); break;
        case Block::Wheel: last(g.wheels); break;
        case Block::Controller: last(g.controllers); break;
        case Block::Sequence: last(g.sequences); break;
        case Block::None: return fail(bad, w[0]); // no default: a kind of block added later is missed here loudly, by the compiler
        }
        if (w[0] == "component") { // "component <plugin>.<name> key value key value…": a plugin's, kept as it is
            if (w.size() < 2 || w.size() % 2 != 0) return fail(bad, w[0]);
            PluginComponent c;
            c.type = w[1];
            for (size_t i = 2; i + 1 < w.size(); i += 2) c.values.push_back({w[i], w[i + 1]});
            components->push_back(std::move(c));
            return true;
        }
        for (size_t i = 1; i < w.size(); ++i) {
            if (!tagWord(w[i])) return fail(bad, w[i]);
            if (std::find(tags->begin(), tags->end(), w[i]) == tags->end()) tags->push_back(w[i]);
        }
        return true;
    }
    if (w[0] == "end") {
        // A file from before colliders were a role of their own: its rigid bodies collide as their
        // geometry - the Auto collider, now written out.
        if (block == Block::Entity && g.entities.back().rigid.enabled) g.entities.back().collider.enabled = true;
        block = Block::None;
        return true;
    }
    switch (block) {
    case Block::Entity: return readPart(w, g.entities.back(), kEntityParts, bad);
    case Block::Group: return readPart(w, g.groups.back(), kGroupParts, bad);
    case Block::Array: return readPart(w, g.arrays.back(), kArrayParts, bad);
    case Block::Light: return readPart(w, g.lights.back(), kLightParts, bad);
    case Block::Force: return readPart(w, g.forces.back(), kForceParts, bad);
    case Block::Camera: return readPart(w, g.cameras.back(), kCameraParts, bad);
    case Block::Joint: return readPart(w, g.joints.back(), kJointParts, bad);
    case Block::Battery: return readPart(w, g.batteries.back(), kBatteryParts, bad);
    case Block::Driver: return readPart(w, g.drivers.back(), kDriverParts, bad);
    case Block::Sensor: return readPart(w, g.sensors.back(), kSensorParts, bad);
    case Block::Wheel: return readPart(w, g.wheels.back(), kWheelParts, bad);
    case Block::Controller: return readPart(w, g.controllers.back(), kControllerParts, bad);
    case Block::Sequence: return readPart(w, g.sequences.back(), kSequenceParts, bad);
    case Block::None: break; // no default: a kind of block added later is missed here loudly, by the compiler
    }
    return fail(bad, w[0]);
}

bool tagWord(const std::string& word) {
    if (word.empty() || word.size() > 48) return false;
    for (const unsigned char c : word)
        if (c <= ' ' || c == '"' || c == '\'' || c == '#' || c == 127) return false;
    return true;
}

// What every kind of object has under one name - its tags, its components - of the object with this id.
template <class Graph, class Get>
static auto partOf(Graph& g, uint32_t id, Get get) -> decltype(get(g.entities.front())) {
    decltype(get(g.entities.front())) found = nullptr;
    const auto look = [&](auto& objects) {
        for (auto& o : objects)
            if (o.id == id) found = get(o);
    };
    eachObjectList(g, look);
    return id == 0 ? nullptr : found;
}
std::vector<std::string>* objectTags(SceneGraph& g, uint32_t id) { return partOf(g, id, [](auto& o) { return &o.tags; }); }
const std::vector<std::string>* objectTags(const SceneGraph& g, uint32_t id) { return partOf(g, id, [](auto& o) { return &o.tags; }); }
std::vector<PluginComponent>* objectComponents(SceneGraph& g, uint32_t id) { return partOf(g, id, [](auto& o) { return &o.components; }); }

bool SceneGraph::load(const std::string& text, std::string& error) {
    SceneGraph g;
    std::istringstream in(text);
    std::string line, bad;
    Block block = Block::None;
    for (int number = 1; std::getline(in, line); ++number) {
        std::vector<std::string> w;
        for (LineWord& word : lineWords(line)) w.push_back(std::move(word.text));
        if (w.empty() || w[0][0] == '#') continue;
        if (!readLine(w, g, block, bad)) {
            const size_t hint = bad.find(kHint);
            error = "line " + std::to_string(number) + ": cannot read '" + bad.substr(0, hint) + "'" +
                    (hint == std::string::npos ? std::string() : " - " + bad.substr(hint + 1));
            const Version by = writtenBy(text);
            if (by > kProgram)
                error += " - the file was written by Kinetica " + versionText(by) + ", newer than this program (" + versionText(kProgram) +
                         "): open it there, or update this one";
            return false;
        }
    }
    if (block != Block::None) {
        error = "the last block has no 'end'";
        return false;
    }
    g.baseDirectory = baseDirectory; // where this graph's model files are: not in the text
    g.idFloor = idFloor;
    *this = std::move(g);
    return true;
}

} // namespace ke
