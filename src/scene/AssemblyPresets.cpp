// Ready links (Assembly.h, LinkPreset): what each preset puts against a face, and which one a click
// most likely means there.
//
// A wheel is a cylinder whose axis (its own y) is turned onto the face's normal, its radius 1.2 times
// the face's smaller half extent (a chassis 15 cm high gets wheels of 9 cm), its width half the
// radius. A motor wheel is driven by an electric DC motor (GenericJoint.h: torque K I, back EMF
// K omega): 12 V, the constant K that gives a no-load speed U / K of 4 rad/s, the winding resistance
// that gives a stall torque K U / R lifting half the target's weight on the wheel's lever, 0.5 m g r.
//
// A shock absorber is a slider with a spring and a damper along the snap direction nearest to "up"
// (against gravity): stiffness k = m_s (2 pi f)^2 for the share m_s of the target's mass it carries,
// f = 4 Hz (a sag g / (2 pi f)^2 = 1.6 cm), damping ratio 0.5 - c = 2 0.5 sqrt(k m_s) - and a travel
// of 0.6 r either way. A car is four of them along the face's longer direction, a radius in from the
// ends and low enough to keep twice the sag between the chassis and the floor, two on the face and
// two on the opposite one, each carrying a motor wheel on its outer face;
// the mirrored pair turns the other way about its flipped axis, so all four drive the same way. A car
// carries its power: a 3S lithium pack and a driver per wheel, so the wheels run on what the pack gives.
#include "scene/Assembly.h"
#include "scene/SceneOps.h"


#include <algorithm>
#include <cmath>

namespace ke {

namespace {

const char* kNames[kLinkPresets] = {"Шарнир", "Колено", "Колесо", "Мотор-колесо", "Амортизатор", "Машина", "Сварка", "Шаровое", "Поворот", "Ход"};
const char* kWords[kLinkPresets] = {"hinge", "elbow", "wheel", "motor_wheel", "shock", "car", "weld", "ball", "rotary", "linear"};


std::string numbered(const SceneGraph& g, const std::string& word) {
    int n = 0;
    for (const Entity& e : g.entities) n += e.name.rfind(word, 0) == 0;
    return word + " " + std::to_string(n + 1);
}

Real massOf(const Entity& e) { return e.rigid.density * e.size.x * e.size.y * e.size.z; }

// A copy of the target: its geometry and components, a link of its own.
LinkSpec copyLink(const SceneGraph& g, const Entity& target, JointType joint, int axis) {
    LinkSpec s;
    s.prototype = resolveInstance(g, target);
    s.prototype.name = numbered(g, "Звено");
    s.joint = joint;
    s.axis = axis;
    return s;
}

Real wheelRadius(const FaceSnap& at) { return 1.2_r * std::min(at.halfAcross[0], at.halfAcross[1]); }

LinkSpec wheel(const SceneGraph& g, Real r, int faceAxis, Real carried, bool motor) {
    LinkSpec s;
    Entity& w = s.prototype;
    w.name = numbered(g, "Колесо");
    w.shape = ShapeKind::Cylinder;
    w.size = Vector3(2 * r, 0.5_r * r, 2 * r);
    w.color = Vector3(0.13_r, 0.13_r, 0.15_r);
    w.rigid.enabled = w.collider.enabled = true;
    w.collider.kind = ColliderKind::Cylinder; // round: the hull of the drawn 24-gon would knock on the floor
    w.rigid.density = 900;
    w.rigid.friction = 1;
    Vector3 ea(0.0_r);
    ea[faceAxis] = 1;
    s.turn = Quaternion::fromTwoVectors(Vector3(0, 1, 0), ea);
    s.joint = JointType::Hinge;
    s.axis = 2;
    if (!motor) return s;
    JointDrive& d = s.drive;
    d.enabled = true;
    d.voltage = 12;
    d.constant = d.voltage / 4;                                           // no-load speed U / K = 4 rad/s
    d.resistance = d.constant * d.voltage / std::max(0.5_r, 0.5_r * carried * 9.81_r * r); // stall torque K U / R
    return s;
}

// The snap direction (0, 1: across, 2: the normal) nearest to "up" - a spring's line.
int upward(const SceneGraph& g, const FaceSnap& at) {
    const Vector3 up = length(g.world.gravity) > 0 ? normalize(g.world.gravity) * -1.0_r : Vector3(0, 1, 0);
    int best = 2;
    for (int k = 0; k < 2; ++k)
        if (std::fabs(dot(snapAxis(at, k), up)) > std::fabs(dot(snapAxis(at, best), up))) best = k;
    return best;
}

LinkSpec shock(const SceneGraph& g, const FaceSnap& at, const Vector3& size, Real carried, Real travel) {
    LinkSpec s;
    Entity& e = s.prototype;
    e.name = numbered(g, "Амортизатор");
    e.size = size;
    e.color = Vector3(0.75_r, 0.72_r, 0.2_r);
    e.rigid.enabled = e.collider.enabled = true;
    e.rigid.density = 2000;
    s.joint = JointType::Slider;
    s.axis = upward(g, at);
    const Real omega = 2 * kPi * 4; // 4 Hz
    s.drive.enabled = true;
    s.drive.stiffness = carried * omega * omega;
    s.drive.damping = 2 * 0.5_r * std::sqrt(s.drive.stiffness * carried);
    s.limited = true;
    s.lower = -travel, s.upper = travel;
    return s;
}

void powerWheels(SceneGraph& g, const std::vector<Attached>& links);

// The ground clearance: twice the springs' sag g / omega^2 (k = m_s omega^2) - the strut is put that
// much lower than the face's middle, so the floor never meets the body (along `along` it is not moved).
void keepClearance(const SceneGraph& g, const FaceSnap& f, int along, Real carried, Real r, LinkSpec& strut) {
    const int up = upward(g, f);
    if (up == 2 || up == along) return;
    const Vector3 rise = length(g.world.gravity) > 0 ? normalize(g.world.gravity) * -1.0_r : Vector3(0, 1, 0);
    const Real sag = carried * 9.81_r / std::max(strut.drive.stiffness, 1e-6_r);
    const Real lower = std::min(std::max(0.0_r, 2 * sag - (r - f.halfAcross[up])), f.halfAcross[up]);
    (up == 0 ? strut.u : strut.v) = dot(snapAxis(f, up), rise) > 0 ? -lower : lower;
}

// Four shock absorbers along the face's longer direction - two on it, two mirrored on the opposite
// face - each with a motor wheel on its outer face.
std::vector<Attached> car(SceneGraph& g, uint32_t target, const FaceSnap& at) {
    const Entity t = *findEntity(g, target);
    const int along = at.halfAcross[0] >= at.halfAcross[1] ? 0 : 1, faceAxis = int(at.face) / 2;
    const Real r = wheelRadius(at), carried = 0.25_r * massOf(t);
    std::vector<Attached> out;
    for (int side = 0; side < 2; ++side) {
        const FaceSnap f = side == 0 ? at : snapToFace(g, target, BoxFace(int(at.face) ^ 1), Vector3(0.0_r));
        Vector3 strutSize(0.8_r * r);
        strutSize[faceAxis] = 0.4_r * r;
        LinkSpec strut = shock(g, f, strutSize, carried, 0.6_r * r);
        const Real reach = std::max(0.0_r, f.halfAcross[along] - 1.2_r * r);
        keepClearance(g, f, along, carried, r, strut);
        for (Real sign : {-1.0_r, 1.0_r}) {
            (along == 0 ? strut.u : strut.v) = sign * reach;
            strut.prototype.name = numbered(g, "Амортизатор");
            const Attached s = attachLink(g, target, f, strut);
            out.push_back(s);
            LinkSpec w = wheel(g, r, faceAxis, carried, true);
            if (side == 1) w.drive.voltage = -w.drive.voltage; // its axis is flipped
            out.push_back(attachLink(g, s.entity, snapToFace(g, s.entity, f.face, Vector3(0.0_r)), w));
        }
    }
    powerWheels(g, out);
    return out;
}

// The car's power: a 3S pack of 18650 cells (12.6 V full) and a driver per motor wheel at full duty,
// the sign the direction its motor's voltage had - the wheels then run on what the pack gives.
void powerWheels(SceneGraph& g, const std::vector<Attached>& links) {
    BatteryObject pack;
    pack.id = nextId(g);
    pack.name = numbered(g, "Батарея");
    pack.series = 3;
    g.batteries.push_back(pack);
    int n = 0;
    for (const Attached& l : links)
        for (const JointObject& j : g.joints)
            if (j.id == l.joint && j.dof[DofRx].drive.enabled && j.dof[DofRx].drive.constant > 0) {
                DriverObject d;
                d.id = nextId(g);
                d.name = "Драйвер " + std::to_string(++n);
                d.joint = j.id;
                d.battery = pack.id;
                d.duty = j.dof[DofRx].drive.voltage < 0 ? -1.0_r : 1.0_r;
                g.drivers.push_back(d);
            }
}

// The mirrored half of a mirrored preset turns the mirrored way: its drive's direction reversed.
void reverseDrives(SceneGraph& g, const std::vector<Attached>& links) {
    for (const Attached& l : links)
        for (JointObject& j : g.joints)
            if (j.id == l.joint)
                for (JointFreedom& f : j.dof) f.drive.targetVelocity = -f.drive.targetVelocity, f.drive.voltage = -f.drive.voltage;
}

} // namespace

uint32_t powerMotor(SceneGraph& g, uint32_t joint, uint32_t battery) {
    const JointObject* j = findJoint(g, joint);
    const auto wheel = std::find_if(g.wheels.begin(), g.wheels.end(), [&](const WheelObject& w) { return w.id == joint && w.constant > 0; });
    int dof = wheel != g.wheels.end() ? int(DofRx) : -1; // a point wheel's motor turns it about its axle
    for (int k = 0; j && k < 6 && dof < 0; ++k)
        if (j->dof[k].drive.enabled && j->dof[k].drive.constant > 0) dof = k;
    if (dof < 0) return 0;
    const Real sign = (j ? j->dof[dof].drive.voltage : wheel->voltage) < 0 ? -1.0_r : 1.0_r;
    if (battery == 0 && !g.batteries.empty()) battery = g.batteries.front().id;
    if (battery == 0) {
        BatteryObject pack;
        pack.id = battery = nextId(g);
        pack.name = numbered(g, "Батарея");
        pack.series = 3;
        g.batteries.push_back(pack);
    }
    DriverObject d;
    d.id = nextId(g);
    d.name = "Драйвер " + std::to_string(g.drivers.size() + 1);
    d.joint = joint, d.dof = dof, d.battery = battery, d.duty = sign;
    g.drivers.push_back(d);
    return d.id;
}

namespace {

const char* kSideWords[kSides] = {"front_left", "front_right", "rear_left", "rear_right", "left", "right"};
const char* kSideNames[kSides] = {"спереди слева", "спереди справа", "сзади слева", "сзади справа", "слева", "справа"};

// `base`, or `base N` when an object already has that name.
std::string unique(const SceneGraph& g, const std::string& base) {
    auto taken = [&](const std::string& name) {
        auto in = [&](const auto& v) { return std::any_of(v.begin(), v.end(), [&](const auto& o) { return o.name == name; }); };
        return in(g.entities) || in(g.sensors) || in(g.drivers) || in(g.joints);
    };
    if (!taken(base)) return base;
    for (int n = 2;; ++n)
        if (!taken(base + " " + std::to_string(n))) return base + " " + std::to_string(n);
}

template <class T>
void rename(std::vector<T>& objects, uint32_t id, const std::string& name) {
    for (T& o : objects)
        if (o.id == id) o.name = name;
}

} // namespace

const char* sideName(Side s) { return kSideNames[int(s)]; }

bool sideFromWord(const std::string& word, Side& s) {
    for (int k = 0; k < kSides; ++k)
        if (word == kSideWords[k]) return s = Side(k), true;
    return false;
}

namespace {

// Where a wheel module goes: the side's face of the body (-z left, +z right), the face's axis along the
// body's forward, the place along it a wheel's radius in from the end (the middle for left and right),
// the wheel's radius, the quarter of the body's weight it carries, its side in words.
struct Corner {
    FaceSnap f;
    int along = 0;
    Real r = 0, carried = 0, offset = 0;
    std::string where;
    Vector3 p, up, forward;
    Quaternion q;
};

bool cornerOf(const SceneGraph& g, uint32_t body, Side side, Real radius, Corner& c) {
    const Entity* t = findEntity(g, body);
    if (!t) return false;
    worldPose(g, *t, c.p, c.q);
    c.forward = c.q.rotate(Vector3(1, 0, 0));
    c.up = length(g.world.gravity) > 0 ? normalize(g.world.gravity) * -1.0_r : Vector3(0, 1, 0);
    const bool left = side == Side::FrontLeft || side == Side::RearLeft || side == Side::Left;
    c.f = snapToFace(g, body, left ? BoxFace::MinusZ : BoxFace::PlusZ, Vector3(0.0_r));
    c.along = std::fabs(dot(snapAxis(c.f, 0), c.forward)) >= std::fabs(dot(snapAxis(c.f, 1), c.forward)) ? 0 : 1;
    const Real end = side == Side::FrontLeft || side == Side::FrontRight ? 1.0_r : side == Side::Left || side == Side::Right ? 0.0_r : -1.0_r;
    c.r = radius > 0 ? radius : wheelRadius(c.f), c.carried = 0.25_r * massOf(*t);
    c.offset = end * std::max(0.0_r, c.f.halfAcross[c.along] - 1.2_r * c.r) * (dot(snapAxis(c.f, c.along), c.forward) >= 0 ? 1.0_r : -1.0_r);
    c.where = sideName(side);
    return true;
}

// The strut a car's corner has (a shock absorber lowered to keep the clearance), at the corner's place.
LinkSpec cornerStrut(const SceneGraph& g, const Corner& c) {
    Vector3 size(0.8_r * c.r);
    size[2] = 0.4_r * c.r;
    LinkSpec strut = shock(g, c.f, size, c.carried, 0.6_r * c.r);
    keepClearance(g, c.f, c.along, c.carried, c.r, strut);
    (c.along == 0 ? strut.u : strut.v) = c.offset;
    return strut;
}

// The wheel of bodies: the strut on the face, the wheel on its outer face. The axle turns forward when
// its axis n points along up x forward: a wheel turning +omega about n rolls the body along
// (up x forward) x up = forward - so the axle is flipped where n points the other way.
void bodyWheel(SceneGraph& g, uint32_t body, const Corner& c, bool suspension, WheelModule& m) {
    uint32_t mount = body;
    FaceSnap mountFace = c.f;
    LinkSpec w = wheel(g, c.r, 2, c.carried, true);
    if (suspension) {
        LinkSpec strut = cornerStrut(g, c);
        strut.prototype.name = unique(g, "Амортизатор " + c.where);
        const Attached s = attachLink(g, body, c.f, strut);
        m.shock = s.entity, m.strut = s.joint, mount = s.entity;
        rename(g.joints, m.strut, unique(g, "Подвеска " + c.where));
        mountFace = snapToFace(g, s.entity, c.f.face, Vector3(0.0_r));
    } else {
        (c.along == 0 ? w.u : w.v) = c.offset;
    }
    w.prototype.name = unique(g, "Колесо " + c.where);
    w.flip = dot(snapAxis(mountFace, 2), cross(c.up, c.forward)) < 0;
    const Attached wheelLink = attachLink(g, mount, mountFace, w);
    m.wheel = wheelLink.entity, m.axle = wheelLink.joint;
    rename(g.joints, m.axle, unique(g, "Ось " + c.where));
}

// The point wheel: the same strut and wheel, not built - their spring, damper, motor and masses, the
// hub where the wheel of bodies would stand (on the strut's outer face), the mount `rest` above it in
// the body's frame, the wheel's frame the body's own (it rolls forward on positive spin unflipped).
void pointWheel(SceneGraph& g, uint32_t body, const Corner& c, Real rolling, WheelModule& m) {
    const LinkSpec strut = cornerStrut(g, c), w = wheel(g, c.r, 2, c.carried, true);
    const Vector3& ss = strut.prototype.size;
    const Vector3& ws = w.prototype.size;
    const Vector3 hub = c.f.anchor + snapAxis(c.f, 0) * strut.u + snapAxis(c.f, 1) * strut.v + snapAxis(c.f, 2) * (ss[2] + 0.5_r * ws.y);
    WheelObject x;
    x.id = nextId(g), x.name = unique(g, "Колесо " + c.where), x.parent = body, x.color = w.prototype.color;
    x.radius = c.r, x.width = ws.y, x.friction = w.prototype.rigid.friction, x.rolling = rolling;
    x.mass = w.prototype.rigid.density * kPi / 4 * ws.x * ws.y * ws.z;            // as the wheel's cylinder weighs
    x.unsprung = strut.prototype.rigid.density * ss.x * ss.y * ss.z;            // the strut moves with it
    x.rest = 0.6_r * c.r, x.stiffness = strut.drive.stiffness, x.damping = strut.drive.damping;
    x.voltage = w.drive.voltage, x.resistance = w.drive.resistance, x.constant = w.drive.constant;
    x.position = c.q.conjugate().rotate(hub - c.p) + Vector3(0.0_r, x.rest, 0.0_r);
    g.wheels.push_back(x);
    m.wheel = x.id;
}

} // namespace

WheelModule addWheelModule(SceneGraph& g, uint32_t body, Side side, const WheelModuleOptions& o) {
    Corner c;
    if (!cornerOf(g, body, side, o.radius, c)) return {};
    WheelModule m;
    if (o.point) pointWheel(g, body, c, o.rolling, m);
    else bodyWheel(g, body, c, o.suspension, m);
    const uint32_t shaft = o.point ? m.wheel : m.axle; // what turns: the axle's joint, or the point wheel
    if (o.encoderLines > 0 && (m.encoder = attachEncoder(g, shaft, o.encoderLines))) rename(g.sensors, m.encoder, unique(g, "Энкодер " + c.where));
    if (o.powered && (m.driver = powerMotor(g, shaft, o.battery))) {
        for (DriverObject& d : g.drivers)
            if (d.id == m.driver) d.duty = 0, d.name = unique(g, "Драйвер " + c.where), m.battery = d.battery;
    }
    return m;
}

const char* presetName(LinkPreset p) { return kNames[int(p)]; }
const char* presetWord(LinkPreset p) { return kWords[int(p)]; }

bool presetFromWord(const std::string& word, LinkPreset& p) {
    for (int k = 0; k < kLinkPresets; ++k)
        if (word == kWords[k]) return p = LinkPreset(k), true;
    return false;
}

// A box: its opposite face, the same place on it. Anything else: the point and the normal reflected
// across the middle plane, n' = n - 2 (n.f) f, x' = x - 2 ((x - c).f) f, f the face's axis.
FaceSnap mirroredPlace(const SceneGraph& g, uint32_t target, const FaceSnap& at) {
    const Entity* e = findEntity(g, target);
    if (!e || !at.found) return FaceSnap();
    const FaceSnap opposite = snapToFace(g, target, BoxFace(int(at.face) ^ 1), Vector3(0.0_r));
    if (e->shape == ShapeKind::Box || e->shape == ShapeKind::Plane) return opposite;
    const FaceSnap facing = snapToFace(g, target, at.face, Vector3(0.0_r));
    const Vector3 f = facing.normal, c = (facing.anchor + opposite.anchor) * 0.5_r;
    const Vector3 point = at.anchor - f * (2 * dot(at.anchor - c, f)), normal = at.normal - f * (2 * dot(at.normal, f));
    return snapToSurface(g, target, point, normal, e->size);
}

std::vector<Attached> applyPreset(SceneGraph& g, uint32_t target, const FaceSnap& at, LinkPreset p, const PresetOptions& o) {
    const Entity* found = findEntity(g, target);
    if (!found || !at.found) return {};
    if (o.mirror && p != LinkPreset::Car) {
        PresetOptions one = o;
        one.mirror = false;
        const FaceSnap other = mirroredPlace(g, target, at);
        std::vector<Attached> out = applyPreset(g, target, at, p, one), twin = applyPreset(g, target, other, p, one);
        reverseDrives(g, twin);
        out.insert(out.end(), twin.begin(), twin.end());
        return out;
    }
    const Entity t = *found;
    LinkSpec s;
    switch (p) {
    case LinkPreset::Hinge: s = copyLink(g, t, JointType::Hinge, 2); break;
    case LinkPreset::Elbow: s = copyLink(g, t, JointType::Hinge, 0); break;
    case LinkPreset::Wheel: s = wheel(g, wheelRadius(at), int(at.face) / 2, massOf(t), false); break;
    case LinkPreset::MotorWheel: s = wheel(g, wheelRadius(at), int(at.face) / 2, massOf(t), true); break;
    case LinkPreset::Shock: s = shock(g, at, resolveInstance(g, t).size * 0.5_r, massOf(t), 0.5_r * minComp(t.size)); break;
    case LinkPreset::Car: return car(g, target, at);
    case LinkPreset::Weld: s = copyLink(g, t, JointType::Fixed, 2); break;
    case LinkPreset::Ball: s = copyLink(g, t, JointType::Ball, 2); break;
    case LinkPreset::Rotary: s = copyLink(g, t, JointType::Hinge, 2); break;
    case LinkPreset::Linear: s = copyLink(g, t, JointType::Slider, 2); break;
    }
    if (o.axis >= 0 && s.prototype.shape != ShapeKind::Cylinder) s.axis = std::clamp(o.axis, 0, 2);
    s.flip = o.flip;
    return {attachLink(g, target, at, s)};
}

LinkPreset magnetPreset(LinkPreset p) {
    switch (p) {
    case LinkPreset::Hinge: case LinkPreset::Elbow: return LinkPreset::Rotary;
    case LinkPreset::Shock: return LinkPreset::Linear;
    case LinkPreset::MotorWheel: case LinkPreset::Car: return LinkPreset::Wheel; // a car's suggestion: its wheels
    default: return p;
    }
}

// Flat and wide: its height along "up" (against gravity) under half its largest horizontal extent;
// the face a side: its normal within 45 degrees of horizontal.
LinkPreset suggestPreset(const SceneGraph& g, uint32_t target, const FaceSnap& at) {
    const Entity* e = findEntity(g, target);
    if (!e || !at.found) return LinkPreset::Hinge;
    const Vector3 up = length(g.world.gravity) > 0 ? normalize(g.world.gravity) * -1.0_r : Vector3(0, 1, 0);
    Vector3 p;
    Quaternion q;
    worldPose(g, *e, p, q);
    Vector3 extent(0.0_r); // the world box's half extents, |R| h
    for (int k = 0; k < 3; ++k) {
        Vector3 axis(0.0_r);
        axis[k] = 1;
        const Vector3 edge = q.rotate(axis) * (0.5_r * e->size[k]);
        for (int i = 0; i < 3; ++i) extent[i] += std::fabs(edge[i]);
    }
    const Real height = std::fabs(dot(extent, up)), width = maxComp(extent - up * dot(extent, up));
    const bool side = std::fabs(dot(at.normal, up)) < 0.7072_r;
    if (!side || height >= 0.5_r * width) return LinkPreset::Hinge;
    for (const JointObject& j : g.joints)
        if (j.b == target && findEntity(g, j.a)) return LinkPreset::MotorWheel; // it carries links already
    return LinkPreset::Car;
}

} // namespace ke
