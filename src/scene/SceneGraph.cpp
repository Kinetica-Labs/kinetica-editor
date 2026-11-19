// Saving and loading a SceneGraph as text: one line per fact, so a scene file can be read, edited
// by hand and compared with diff. A sample:
//
//   # Kinetica scene
//   world gravity 0 -9.81 0 size 4 3 4
//   physics substeps 16 contactHertz 120 contactDamping 10 jointHertz 240 jointDamping 2 sleeping 1 continuous 1 linearDamping 0 angularDamping 0
//   entity "Base"
//     object id 3 visible 1 locked 0
//     shape box size 0.3 0.1 0.3 position 0 0.05 0 rotation 0 0 0 color 0.6 0.6 0.6
//     rigid density 1000 friction 0.5 restitution 0.2 fixed 1 velocity 0 0 0 spin 0 0 0
//     collider kind auto fit 1 size 0.2 0.2 0.2 offset 0 0 0 rotation 0 0 0
//   end
//   entity "Gripper"
//     shape mesh file "models/gripper.obj" size 0.1 0.1 0.1 position 0 0.6 0 rotation 0 90 0 color 0.6 0.4 0.3
//     rigid density 600 friction 0.5 restitution 0.2 fixed 0 velocity 0 0 0 spin 0 0 0
//     collider kind capsule fit 1 size 0.2 0.2 0.2 offset 0 0 0 rotation 0 0 90
//   end
//   joint "Elbow"
//     object id 9
//     link a 5 b 3 anchorA 0 -0.1 0 frameA 0 0 0 anchorB 0 0.1 0 frameB 0 0 0
//     limit rx lower -1.5 upper 1.5
//     drive rx stiffness 0 damping 0 target 0 velocity 1 force 10
//   end
//   joint "Shock absorber"
//     object id 10
//     link a 6 b 3 anchorA 0 0 0 frameA 0 0 90 anchorB 0.25 -0.05 0.2 frameB 0 0 90
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
#include "scene/SensorSpec.h"

#include <cstdlib>
#include <sstream>

namespace ke {

namespace {

const char* kShapeNames[] = {"box", "sphere", "cylinder", "cone", "plane", "mesh"};
const int kShapeCount = 6;
const char* kColliderNames[] = {"auto", "box", "sphere", "capsule", "hull", "cylinder"};
const int kColliderCount = 6;
// A joint's six freedoms, by JointDof.
const char* kDofNames[] = {"tx", "ty", "tz", "rx", "ry", "rz"};

std::string num(Real v) { return numberText(v); }
std::string vec(const Vector3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }

// Splits a line into words; a word in double quotes may contain spaces.
std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            out.push_back(line.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1));
            i = end == std::string::npos ? line.size() : end + 1;
        } else {
            const size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

// Reads the words of one line as "key value(s)" pairs. `need(k)` says how many numbers follow key k.
class LineReader {
public:
    LineReader(const std::vector<std::string>& w, size_t start) : w_(w), i_(start) {}
    bool done() const { return i_ >= w_.size(); }
    // The next key; one met before on this line comes back as "<key> twice", which no reader takes: a line
    // like "drive rx target 1 damping 5 target 2" is refused, not read with its stiffness lost.
    std::string key() {
        const std::string& k = w_[i_++];
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

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------
// The collider role, a line of its own with every field (written only when the role is on).
static void saveCollider(std::ostringstream& o, const ColliderRole& c) {
    o << "  collider kind " << kColliderNames[int(c.kind)] << " fit " << (c.fitToGeometry ? 1 : 0) << " size " << vec(c.size)
      << " offset " << vec(c.offset) << " rotation " << vec(c.rotationDeg) << "\n";
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
      << a.count[1] << " " << a.count[2] << " step " << vec(a.step) << " radius " << num(a.radius) << " rotationStep "
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

static void saveCamera(std::ostringstream& o, const Camera& c) {
    saveObjectHead(o, "camera", c);
    o << "  lens fov " << num(c.fovDeg) << " near " << num(c.nearClip) << " far " << num(c.farClip) << " active "
      << (c.active ? 1 : 0) << "\n";
    if (c.kind == CameraKind::Target) o << "  target object " << c.target << "\n"; // a free camera has none: older files read the same
    o << "end\n";
}

static void saveTexture(std::ostringstream& o, const SurfaceTexture& t) {
    if (t.kind == TextureKind::None && t.bumpHeight == 0) return;
    if (int(t.kind) < 0 || int(t.kind) > 4 || !std::isfinite(t.repeats) || t.repeats <= 0 || t.repeats > 10000 ||
        t.file.find_first_of("\"\r\n") != std::string::npos ||
        t.heightFile.find_first_of("\"\r\n") != std::string::npos ||
        !std::isfinite(t.bumpHeight) || t.bumpHeight < 0 || t.bumpHeight > 0.05_r)
        throw std::invalid_argument("invalid surface texture settings");
    const char* kinds[] = {"none", "checker", "image", "speckle", "stone"}; // TextureKind
    o << "  texture kind " << kinds[int(t.kind)]
      << " repeats " << num(t.repeats) << " file \"" << t.file << "\"";
    if (t.bumpHeight > 0) o << " bump " << num(t.bumpHeight) << " heightFile \"" << t.heightFile << "\"";
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
    o << "  shape " << kShapeNames[int(e.shape)];
    if (e.shape == ShapeKind::Mesh) o << " file \"" << e.meshFile << "\"";
    o << " size " << vec(e.size) << " position " << vec(e.position)
      << " rotation " << vec(e.rotationDeg) << " color " << vec(e.color) << "\n";
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
          << " spin " << vec(e.rigid.angularVelocity) << "\n";
    }
    if (e.collider.enabled) saveCollider(o, e.collider);
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
    o << "  link a " << j.a << " b " << j.b << " anchorA " << vec(j.anchorA) << " frameA " << vec(j.frameA) << " anchorB "
      << vec(j.anchorB) << " frameB " << vec(j.frameB) << "\n";
    for (int k = 0; k < 6; ++k) saveFreedom(o, k, j.dof[k]);
    if (j.collide) o << "  collide\n";
    o << "end\n";
}

static std::string quoted(const std::string& name) {
    std::string n = name;
    for (char& c : n)
        if (c == '"') c = '\'';
    return "\"" + n + "\"";
}

static void saveBattery(std::ostringstream& o, const BatteryObject& b) {
    o << "battery " << quoted(b.name) << "\n  object id " << b.id << "\n";
    o << "  cells series " << b.series << " parallel " << b.parallel << " capacity " << num(b.capacityAh) << " r0 " << num(b.r0) << " r1 "
      << num(b.r1) << " c1 " << num(b.c1) << " cutoff " << num(b.cutoff) << " soc " << num(b.soc) << "\nend\n";
}

static void saveDriver(std::ostringstream& o, const DriverObject& d) {
    o << "driver " << quoted(d.name) << "\n  object id " << d.id << "\n";
    o << "  bridge joint " << d.joint << " dof " << kDofNames[std::clamp(d.dof, 0, 5)] << " battery " << d.battery << " duty " << num(d.duty)
      << " switches " << num(d.switches) << " limit " << num(d.currentLimit) << " stop " << (d.brake ? "brake" : "coast") << "\nend\n";
}

// A controller: one line an action (what value it has, what it drives, how fast), one line a binding of it.
static const char* const kControlValues[] = {"button", "axis", "vector2"};
static const char* const kControlDrives[] = {"plane", "lift", "turn", "grip", "jog", "duty"};

static void saveController(std::ostringstream& o, const ControllerObject& c) {
    o << "controller " << quoted(c.name) << "\n  object id " << c.id << "\n";
    for (const ControllerAction& a : c.actions) {
        o << "  action " << a.name << " " << kControlValues[int(a.value)] << " drive " << kControlDrives[int(a.drive)] << " target " << a.target;
        if (a.drive == ControlDrive::Jog) o << " dof " << kDofNames[std::clamp(a.dof, 0, 5)];
        o << " speed " << num(a.speed) << "\n";
    }
    for (const ControllerAction& a : c.actions)
        for (const ControlBinding& b : a.bindings) {
            o << "  bind " << a.name << " " << b.device;
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
    if (s.kind == SensorKind::Imu || s.kind == SensorKind::Sonar || s.kind == SensorKind::Wrench) {
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
    o << "physics substeps " << p.substeps << " contactHertz " << num(p.contactHertz) << " contactDamping " << num(p.contactDamping)
      << " jointHertz " << num(p.jointHertz) << " jointDamping " << num(p.jointDamping) << " sleeping " << (p.sleeping ? 1 : 0)
      << " continuous " << (p.continuous ? 1 : 0) << " linearDamping " << num(p.linearDamping) << " angularDamping "
      << num(p.angularDamping) << "\n";
    return o.str();
}

static std::string worldLine(const WorldSettings& w) {
    return "world gravity " + vec(w.gravity) + " size " + vec(w.size) + "\n";
}

std::string worldText(const WorldSettings& w) { return worldLine(w) + physicsText(w.physics); }

std::string SceneGraph::save() const {
    std::ostringstream o;
    o << "# Kinetica scene\n";
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
    for (const WheelObject& w : wheels) saveWheel(o, w);
    for (const ControllerObject& c : controllers) saveController(o, c);
    for (const SequenceObject& q : sequences) saveSequence(o, q);
    return o.str();
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
static bool readWorld(LineReader& r, WorldSettings& w, std::string& bad) {
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = k == "gravity" ? r.vector(w.gravity) : k == "size" ? r.vector(w.size) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readPhysics(LineReader& r, PhysicsSettings& p, std::string& bad) {
    auto rate = [&r](Real& v) { return r.number(v) && std::isfinite(v) && v > 0 && v <= 10000; };
    auto damping = [&r](Real& v) { return r.number(v) && std::isfinite(v) && v >= 0 && v <= 100; };
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = k == "substeps" ? r.count(p.substeps, 1, 64)
            : k == "contactHertz" ? rate(p.contactHertz) : k == "contactDamping" ? damping(p.contactDamping)
            : k == "jointHertz" ? rate(p.jointHertz) : k == "jointDamping" ? damping(p.jointDamping)
            : k == "sleeping" ? r.flag(p.sleeping) : k == "continuous" ? r.flag(p.continuous)
            : k == "linearDamping" ? damping(p.linearDamping) : k == "angularDamping" ? damping(p.angularDamping) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readShape(LineReader& r, Entity& e, const std::string& shapeName, std::string& bad) {
    bool known = false;
    for (int s = 0; s < kShapeCount; ++s)
        if (shapeName == kShapeNames[s]) e.shape = ShapeKind(s), known = true;
    if (!known) return fail(bad, shapeName);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = k == "size" ? r.vector(e.size) : k == "position" ? r.vector(e.position)
                      : k == "rotation" ? r.vector(e.rotationDeg) : k == "color" ? r.vector(e.color)
                      : k == "file" ? r.text(e.meshFile) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

// One "key value(s)" of a role line; false if the key is not one of that role's.
static bool readTextureKey(const std::string& k, LineReader& r, SurfaceTexture& t) {
    if (k == "file") return r.text(t.file);
    if (k == "heightFile") return r.text(t.heightFile);
    if (k == "bump") return r.number(t.bumpHeight) && std::isfinite(t.bumpHeight) && t.bumpHeight >= 0 && t.bumpHeight <= 0.05_r;
    if (k == "repeats") return r.number(t.repeats) && std::isfinite(t.repeats) && t.repeats > 0 && t.repeats <= 10000;
    if (k != "kind") return false;
    std::string kind;
    if (!r.text(kind)) return false;
    if (kind == "none") t.kind = TextureKind::None;
    else if (kind == "checker") t.kind = TextureKind::Checker;
    else if (kind == "image") t.kind = TextureKind::Image;
    else if (kind == "speckle") t.kind = TextureKind::Speckle;
    else if (kind == "stone") t.kind = TextureKind::Stone;
    else return false;
    return true;
}

static bool readRoleKey(const std::string& role, const std::string& k, LineReader& r, Entity& e) {
    if (role == "texture") return readTextureKey(k, r, e.texture);
    if (role == "object") {
        if (k == "instance") return r.integer(e.instanceOf);
        return k == "id" ? r.integer(e.id) : k == "visible" ? r.flag(e.visible) : k == "locked" ? r.flag(e.locked)
             : k == "parent" ? r.integer(e.parent) : false;
    }
    if (role == "rigid") {
        return k == "density" ? r.number(e.rigid.density) : k == "friction" ? r.number(e.rigid.friction)
             : k == "restitution" ? r.number(e.rigid.restitution) : k == "fixed" ? r.flag(e.rigid.fixed)
             : k == "velocity" ? r.vector(e.rigid.velocity) : k == "spin" ? r.vector(e.rigid.angularVelocity) : false;
    }
    if (role == "collider") {
        ColliderRole& c = e.collider;
        if (k != "kind")
            return k == "fit" ? r.flag(c.fitToGeometry) : k == "size" ? r.vector(c.size) : k == "offset" ? r.vector(c.offset)
                 : k == "rotation" ? r.vector(c.rotationDeg) : false;
        std::string kind;
        if (!r.text(kind)) return false;
        for (int i = 0; i < kColliderCount; ++i)
            if (kind == kColliderNames[i]) { c.kind = ColliderKind(i); return true; }
        return false;
    }
    return false;
}

static bool readRole(const std::string& role, LineReader& r, Entity& e, std::string& bad) {
    while (!r.done()) {
        const std::string k = r.key();
        if (!readRoleKey(role, k, r, e)) return fail(bad, k);
    }
    return true;
}

// One line inside "entity ... end". Returns false with `bad` naming what was not understood.
static bool readEntityLine(const std::vector<std::string>& w, Entity& e, std::string& bad) {
    const std::string& what = w[0];
    if (what == "shape") {
        if (w.size() < 2) return fail(bad, what);
        LineReader r(w, 2); // "shape <kind> key values ..."
        return readShape(r, e, w[1], bad);
    }
    if (what == "target") { // "target camera <id>": a camera's own target
        LineReader r(w, 1);
        return (!r.done() && r.key() == "camera" && r.integer(e.targetOf) && r.done() && e.targetOf != 0) || fail(bad, what);
    }
    if (what == "goal") { // "goal effector <id>"
        LineReader r(w, 1);
        return (!r.done() && r.key() == "effector" && r.integer(e.goalOf) && r.done() && e.goalOf != 0) || fail(bad, what);
    }
    if (what == "path") { // "path speed <m/s> loop <0|1> smooth <0|1> [turn <m a rad>]"
        LineReader r(w, 1);
        while (!r.done()) {
            const std::string k = r.key();
            if (!(k == "speed" ? r.number(e.pathSpeed) && e.pathSpeed > 0 : k == "loop" ? r.flag(e.pathLoop) : k == "smooth" ? r.flag(e.pathSmooth)
                  : k == "turn" ? r.number(e.pathTurnLength) && e.pathTurnLength >= 0 : false))
                return fail(bad, k);
        }
        return true;
    }
    if (what == "waypoint") { // "waypoint x y z [turn rx ry rz]": every waypoint turned, or none
        Vector3 p, t;
        const bool point = (w.size() == 4 || w.size() == 8) && parseNumber(w[1], p.x) && parseNumber(w[2], p.y) && parseNumber(w[3], p.z);
        const bool turned = w.size() == 8 && w[4] == "turn" && parseNumber(w[5], t.x) && parseNumber(w[6], t.y) && parseNumber(w[7], t.z);
        if (!point || (w.size() == 8 && !turned) || (turned ? e.pathTurns.size() != e.path.size() : !e.pathTurns.empty())) return fail(bad, what);
        e.path.push_back(p);
        if (turned) e.pathTurns.push_back(t);
        return true;
    }
    LineReader r(w, 1);
    if (what == "object" || what == "texture") {} // the object's id and flags, not a role
    else if (what == "collider") e.collider.enabled = true;
    else if (what == "rigid") e.rigid.enabled = true;
    else return fail(bad, what);
    return readRole(what, r, e, bad);
}

// The object line and the pose line of a group or an array.
static bool readObjectLine(const std::vector<std::string>& w, SceneObject& s, std::string& bad) {
    LineReader r(w, 1);

    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = w[0] == "object" ? (k == "id" ? r.integer(s.id) : k == "visible" ? r.flag(s.visible)
                                            : k == "locked" ? r.flag(s.locked) : k == "parent" ? r.integer(s.parent) : false)
                                         : (k == "position" ? r.vector(s.position) : k == "rotation" ? r.vector(s.rotationDeg)
                                            : k == "color" ? r.vector(s.color) : false);
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readGroupLine(const std::vector<std::string>& w, Group& g, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, g, bad);
    if (w[0] == "glued" && w.size() == 1) return g.glued = true;
    return fail(bad, w[0]);
}

static bool readArrayLine(const std::vector<std::string>& w, ArrayObject& a, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, a, bad);
    if (w[0] != "pattern" || w.size() < 2) return fail(bad, w[0]);
    bool known = false;
    for (int p = 0; p < 3; ++p)
        if (w[1] == kPatternNames[p]) a.pattern = ArrayPattern(p), known = true;
    if (!known) return fail(bad, w[1]);
    LineReader r(w, 2);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = k == "template" ? r.integer(a.templateId)
                      : k == "count" ? r.integer(a.count[0]) && r.integer(a.count[1]) && r.integer(a.count[2])
                      : k == "step" ? r.vector(a.step) : k == "radius" ? r.number(a.radius)
                      : k == "rotationStep" ? r.vector(a.rotationStepDeg) : k == "jitter" ? r.number(a.jitter)
                      : k == "seed" ? r.integer(a.seed) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readLightLine(const std::vector<std::string>& w, Light& l, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, l, bad);
    if (w[0] != "kind" || w.size() < 2) return fail(bad, w[0]);
    bool known = false;
    for (int k = 0; k < 3; ++k)
        if (w[1] == kLightNames[k]) l.kind = LightKind(k), known = true;
    if (!known) return fail(bad, w[1]);
    LineReader r(w, 2);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = k == "intensity" ? r.number(l.intensity) : k == "range" ? r.number(l.range)
                      : k == "cone" ? r.number(l.coneDeg) : k == "softness" ? r.number(l.softnessDeg)
                      : k == "shadows" ? r.flag(l.shadows) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readCameraLine(const std::vector<std::string>& w, Camera& c, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, c, bad);
    if (w[0] == "target") { // "target object <id>" | "target point x y z": a target camera
        LineReader r(w, 1);
        c.kind = CameraKind::Target;
        const std::string k = r.done() ? std::string() : r.key();
        const bool ok = k == "object" && r.integer(c.target);
        return ok && r.done() ? true : fail(bad, k.empty() ? "target" : k);
    }
    if (w[0] != "lens") return fail(bad, w[0]);
    LineReader r(w, 1);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = k == "fov" ? r.number(c.fovDeg) : k == "near" ? r.number(c.nearClip)
                      : k == "far" ? r.number(c.farClip) : k == "active" ? r.flag(c.active) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

// "limit|free|drive|dcmotor <dof> key value...": the freedom named by the second word.
static bool readFreedomLine(const std::vector<std::string>& w, JointObject& j, std::string& bad) {
    int k = -1;
    for (int d = 0; d < 6 && w.size() > 1; ++d)
        if (w[1] == kDofNames[d]) k = d;
    if (k < 0) return fail(bad, w.size() > 1 ? w[1] : w[0]);
    JointFreedom& f = j.dof[k];
    if (w[0] == "free") return f.motion = JointMotion::Free, w.size() == 2 || fail(bad, w[2]);
    if (w[0] == "limit") f.motion = JointMotion::Limited;
    else f.drive.enabled = true;
    LineReader r(w, 2);
    while (!r.done()) {
        const std::string key = r.key();
        JointDrive& d = f.drive;
        const bool ok = w[0] == "limit" ? (key == "lower" ? r.number(f.lower) : key == "upper" ? r.number(f.upper) : false)
                      : w[0] == "dcmotor" ? (key == "voltage" ? r.number(d.voltage) : key == "resistance" ? r.number(d.resistance)
                                            : key == "constant" ? r.number(d.constant) : false)
                      : key == "stiffness" ? r.number(d.stiffness) : key == "damping" ? r.number(d.damping)
                      : key == "target" ? r.number(d.target) : key == "velocity" ? r.number(d.targetVelocity)
                      : key == "force" ? r.number(d.maxForce) : false;
        if (!ok) return fail(bad, key);
    }
    return true;
}

static bool readJointLine(const std::vector<std::string>& w, JointObject& j, std::string& bad) {
    if (w[0] == "collide" && w.size() == 1) return j.collide = true;
    if (w[0] == "free" || w[0] == "limit" || w[0] == "drive" || w[0] == "dcmotor") return readFreedomLine(w, j, bad);
    if (w[0] != "object" && w[0] != "link") return fail(bad, w[0]);
    LineReader r(w, 1);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = w[0] == "object" ? k == "id" && r.integer(j.id)
                      : k == "a" ? r.integer(j.a) : k == "b" ? r.integer(j.b) : k == "anchorA" ? r.vector(j.anchorA)
                      : k == "anchorB" ? r.vector(j.anchorB) : k == "frameA" ? r.vector(j.frameA) : k == "frameB" ? r.vector(j.frameB)
                      : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readBatteryLine(const std::vector<std::string>& w, BatteryObject& b, std::string& bad) {
    if (w[0] != "object" && w[0] != "cells") return fail(bad, w[0]);
    LineReader r(w, 1);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = w[0] == "object" ? k == "id" && r.integer(b.id)
                      : k == "series" ? r.count(b.series, 1, 1000) : k == "parallel" ? r.count(b.parallel, 1, 1000)
                      : k == "capacity" ? r.number(b.capacityAh) : k == "r0" ? r.number(b.r0) : k == "r1" ? r.number(b.r1)
                      : k == "c1" ? r.number(b.c1) : k == "cutoff" ? r.number(b.cutoff) : k == "soc" ? r.number(b.soc) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

// A freedom by its name (tx ... rz).
static bool readDof(LineReader& r, int& dof) {
    std::string name;
    if (!r.text(name)) return false;
    for (int n = 0; n < 6; ++n)
        if (name == kDofNames[n]) return dof = n, true;
    return false;
}

static bool readSensorLine(const std::vector<std::string>& w, SensorObject& s, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, s, bad);
    if (w[0] == "model") return w.size() == 2 ? (s.model = w[1], true) : fail(bad, "model");
    if (w[0] == "spec") { // after the kind's line: its fields' keys
        LineReader r(w, 1);
        while (!r.done()) {
            const std::string k = r.key();
            const bool known = std::any_of(specFields(s.kind).begin(), specFields(s.kind).end(), [&](const Field& f) { return k == f.key; });
            if (!known || !r.number(s.spec[k])) return fail(bad, k);
        }
        return true;
    }
    const char* words[] = {"encoder", "imu", "sonar", "touch", "wrench", "effort", "stereo"}; // in the order of SensorKind
    int kind = -1;
    for (int n = 0; n < 7; ++n)
        if (w[0] == words[n]) kind = n;
    if (kind < 0) return fail(bad, w[0]);
    s.kind = SensorKind(kind);
    LineReader r(w, 1);
    while (!r.done()) {
        const std::string k = r.key();
        const bool noisy = s.kind == SensorKind::Imu || s.kind == SensorKind::Sonar || s.kind == SensorKind::Wrench;
        const bool stereo = s.kind == SensorKind::Stereo;
        const bool ok = stereo ? (k == "width" ? r.count(s.width, 16, 4096) : k == "height" ? r.count(s.height, 16, 4096)
                                  : k == "fov" ? r.number(s.fovDeg) && s.fovDeg > 1 && s.fovDeg < 170 : k == "baseline" ? r.number(s.baseline) && s.baseline > 0
                                  : k == "projector" ? r.flag(s.projector) : k == "noise" ? r.flag(s.noise) : k == "seed" ? r.integer(s.seed)
                                  : false)
                      : noisy ? (k == "noise" ? r.flag(s.noise) : k == "seed" ? r.integer(s.seed) : false)
                      : s.kind == SensorKind::Effort ? (k == "dof" && readDof(r, s.dof))
                      : s.kind != SensorKind::Encoder ? false
                      : k == "dof" ? readDof(r, s.dof) : k == "lines" ? r.count(s.lines, 1, 1 << 30)
                      : k == "against" ? r.integer(s.against)
                      : k == "reference" ? (s.hasEncoderReference = r.number(s.encoderReference)) : false;
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readWheelLine(const std::vector<std::string>& w, WheelObject& x, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, x, bad);
    if (w[0] != "tyre" && w[0] != "spring" && w[0] != "dcmotor") return fail(bad, w[0]);
    LineReader r(w, 1);
    while (!r.done()) {
        const std::string k = r.key();
        const bool ok = w[0] == "tyre"   ? (k == "radius" ? r.number(x.radius) : k == "width" ? r.number(x.width) : k == "mass" ? r.number(x.mass)
                                            : k == "friction" ? r.number(x.friction) : k == "unsprung" ? r.number(x.unsprung) : k == "rolling" ? r.number(x.rolling) : false)
                      : w[0] == "spring" ? (k == "rest" ? r.number(x.rest) : k == "stiffness" ? r.number(x.stiffness) : k == "damping" ? r.number(x.damping) : false)
                                         : (k == "voltage" ? r.number(x.voltage) : k == "resistance" ? r.number(x.resistance)
                                            : k == "constant" ? r.number(x.constant) : k == "flip" ? r.flag(x.flip) : false);
        if (!ok) return fail(bad, k);
    }
    return true;
}

static bool readDriverLine(const std::vector<std::string>& w, DriverObject& d, std::string& bad) {
    if (w[0] != "object" && w[0] != "bridge") return fail(bad, w[0]);
    LineReader r(w, 1);
    while (!r.done()) {
        const std::string k = r.key();
        bool ok = false;
        if (w[0] == "object") ok = k == "id" && r.integer(d.id);
        else if (k == "dof") ok = readDof(r, d.dof);
        else if (k == "stop") {
            std::string how;
            ok = r.text(how) && (how == "coast" || how == "brake");
            d.brake = how == "brake";
        } else {
            ok = k == "joint" ? r.integer(d.joint) : k == "battery" ? r.integer(d.battery) : k == "duty" ? r.number(d.duty)
               : k == "switches" ? r.number(d.switches) : k == "limit" ? r.number(d.currentLimit) : false;
        }
        if (!ok) return fail(bad, k);
    }
    return true;
}

// "action <name> <value> drive <drive> target <id> [dof <dof>] speed <x>"
static bool readControlAction(const std::vector<std::string>& w, ControllerObject& c, std::string& bad) {
    if (w.size() < 3 || std::any_of(c.actions.begin(), c.actions.end(), [&](const ControllerAction& a) { return a.name == w[1]; }))
        return fail(bad, w.size() > 1 ? "action " + w[1] : "action");
    ControllerAction a;
    a.name = w[1];
    int value = -1;
    for (int k = 0; k < 3; ++k)
        if (w[2] == kControlValues[k]) value = k;
    if (value < 0) return fail(bad, w[2]);
    a.value = ControlValue(value);
    LineReader r(w, 3);
    while (!r.done()) {
        const std::string k = r.key();
        std::string drive;
        int d = -1;
        bool ok = k == "target" ? r.integer(a.target) : k == "speed" ? r.number(a.speed) : k == "dof" ? readDof(r, a.dof) : false;
        if (k == "drive" && r.text(drive))
            for (int n = 0; n < 6; ++n)
                if (drive == kControlDrives[n]) d = n, ok = true;
        if (k == "drive" && d >= 0) a.drive = ControlDrive(d);
        if (k == "drive" && d < 0) // the words a reader may write, named in the error: a guess costs a call otherwise
            return fail(bad, "drive " + drive + "' - a drive is one of plane, lift, turn, grip (an IK goal), jog (a joint, with dof), duty (a driver)");
        if (!ok) return fail(bad, k);
    }
    c.actions.push_back(a);
    return true;
}

// "bind <action> <device> <control>... [invert] [toggle]" - after the action's own line.
static bool readControlBinding(const std::vector<std::string>& w, ControllerObject& c, std::string& bad) {
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
}

static bool readControllerLine(const std::vector<std::string>& w, ControllerObject& c, std::string& bad) {
    if (w[0] == "action") return readControlAction(w, c, bad);
    if (w[0] == "bind") return readControlBinding(w, c, bad);
    if (w[0] != "object" || w.size() != 3 || w[1] != "id") return fail(bad, w[0]);
    LineReader r(w, 2);
    return r.integer(c.id) || fail(bad, w[0]);
}


// "clip time <s> loop <0|1> active <0|1> play <0|1>", "track object <id>", "key time <s> position x y z rotation x y z" (to the
// track above, by time).
static bool readSequenceLine(const std::vector<std::string>& w, SequenceObject& q, std::string& bad) {
    if (w[0] == "object" || w[0] == "pose") return readObjectLine(w, q, bad);
    LineReader r(w, 1);
    if (w[0] == "clip") {
        while (!r.done()) {
            const std::string k = r.key();
            if (!(k == "time" ? r.number(q.time) : k == "loop" ? r.flag(q.loop) : k == "active" ? r.flag(q.active) : k == "play" ? r.flag(q.play) : false))
                return fail(bad, k);
        }
        return true;
    }
    if (w[0] == "track") {
        q.tracks.emplace_back();
        return (!r.done() && r.key() == "object" && r.integer(q.tracks.back().object) && r.done()) || fail(bad, "track");
    }
    if (w[0] != "key" || q.tracks.empty()) return fail(bad, w[0]);
    AnimKey key;
    while (!r.done()) {
        const std::string k = r.key();
        if (!(k == "time" ? r.number(key.time) : k == "position" ? r.vector(key.position) : k == "rotation" ? r.vector(key.rotationDeg)
              : k == "velocity" ? (key.hasVelocity = true, key.interp = AnimInterp::Smooth, r.vector(key.velocity))
              : k == "interp" ? !r.done() && interpOf(r.key(), key.interp) : false))
            return fail(bad, k);
    }
    std::vector<AnimKey>& keys = q.tracks.back().keys;
    if (!keys.empty() && !(key.time > keys.back().time)) return fail(bad, "time");
    keys.push_back(key);
    return true;
}

namespace {
enum class Block { None, Entity, Group, Array, Light, Camera, Joint, Battery, Driver, Sensor, Wheel, Controller, Sequence };
}

// One line of a scene file in the block it belongs to (or starting / ending one).
static bool readLine(const std::vector<std::string>& w, SceneGraph& g, Block& block, std::string& bad) {
    if (block == Block::None) {
        const std::string name = w.size() > 1 ? w[1] : std::string();
        if (w[0] == "physics") {
            LineReader r(w, 1);
            return readPhysics(r, g.world.physics, bad);
        }
        if (w[0] == "world") {
            LineReader r(w, 1);
            return readWorld(r, g.world, bad);
        }
        if (w[0] == "entity") g.entities.emplace_back(), g.entities.back().name = name, block = Block::Entity;
        else if (w[0] == "group") g.groups.emplace_back(), g.groups.back().name = name, block = Block::Group;
        else if (w[0] == "array") g.arrays.emplace_back(), g.arrays.back().name = name, block = Block::Array;
        else if (w[0] == "light") g.lights.emplace_back(), g.lights.back().name = name, block = Block::Light;
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
    if (w[0] == "end") {
        // A file from before colliders were a role of their own: its rigid bodies collide as their
        // geometry - the Auto collider, now written out.
        if (block == Block::Entity && g.entities.back().rigid.enabled) g.entities.back().collider.enabled = true;
        block = Block::None;
        return true;
    }
    if (block == Block::Entity) return readEntityLine(w, g.entities.back(), bad);
    if (block == Block::Group) return readGroupLine(w, g.groups.back(), bad);
    if (block == Block::Light) return readLightLine(w, g.lights.back(), bad);
    if (block == Block::Camera) return readCameraLine(w, g.cameras.back(), bad);
    if (block == Block::Joint) return readJointLine(w, g.joints.back(), bad);
    if (block == Block::Battery) return readBatteryLine(w, g.batteries.back(), bad);
    if (block == Block::Driver) return readDriverLine(w, g.drivers.back(), bad);
    if (block == Block::Sensor) return readSensorLine(w, g.sensors.back(), bad);
    if (block == Block::Wheel) return readWheelLine(w, g.wheels.back(), bad);
    if (block == Block::Controller) return readControllerLine(w, g.controllers.back(), bad);
    if (block == Block::Sequence) return readSequenceLine(w, g.sequences.back(), bad);
    return readArrayLine(w, g.arrays.back(), bad);
}

bool SceneGraph::load(const std::string& text, std::string& error) {
    SceneGraph g;
    std::istringstream in(text);
    std::string line, bad;
    Block block = Block::None;
    for (int number = 1; std::getline(in, line); ++number) {
        const std::vector<std::string> w = words(line);
        if (w.empty() || w[0][0] == '#') continue;
        if (!readLine(w, g, block, bad)) {
            error = "line " + std::to_string(number) + ": cannot read '" + bad + "'";
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
