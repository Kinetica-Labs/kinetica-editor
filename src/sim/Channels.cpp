// The channels (Channels.h): the name cards of the scene channels and the sums that measure the scene,
// a body, a group, a joint and an IK goal - from the physics engine's states (PhysicsWorld::state, its
// contacts) and the robot's joints (RobotWorld). Sums of energy and momentum are kept in double: a
// change of one part in 10^5 would drown in float rounding of a sum of many terms.
#include "sim/Channels.h"
#include "core/UiLabel.h"

#include "math/ElementaryFunctions.h"
#include "scene/Assembly.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

struct Double3 {
    double x = 0, y = 0, z = 0;
    Double3() = default;
    Double3(double a, double b, double c) : x(a), y(b), z(c) {}
    explicit Double3(const Vector3& v) : x(v.x), y(v.y), z(v.z) {}
    Double3& operator+=(const Double3& o) { x += o.x, y += o.y, z += o.z; return *this; }
    Double3 operator-(const Double3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Double3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
};
double dot(const Double3& a, const Double3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Double3 cross(const Double3& a, const Double3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

Measurement measured(const std::string& id, const char* unit, const std::string& name, ChannelGroup group, double value) {
    Measurement m;
    m.info = {id, unit, name, group};
    m.value = value;
    return m;
}

void put(std::vector<Measurement>& out, const char* id, double value) {
    Measurement m;
    m.info = *describeChannel(id);
    m.value = value;
    out.push_back(std::move(m));
}

// What is summed over the members of anything: mass, first moment, momentum, energy, angular momentum about
// the origin (Σ x × m v + I ω: moved to the centre of mass afterwards, L_X = L_O - X × P). Members without a mass - a
// fixed body, one a sequence leads (kinematic: its mass is unbounded) - store no energy, but they stand somewhere and
// may move: with no mass in the sum, the centre and the velocity are their plain means.
struct Totals {
    double mass = 0, translational = 0, rotational = 0, potential = 0;
    Double3 firstMoment, momentum, angularAboutOrigin;
    int massless = 0;
    Double3 masslessCentres, masslessVelocities;
    Totals& operator+=(const Totals& o) {
        mass += o.mass, translational += o.translational, rotational += o.rotational, potential += o.potential;
        firstMoment += o.firstMoment, momentum += o.momentum, angularAboutOrigin += o.angularAboutOrigin;
        massless += o.massless, masslessCentres += o.masslessCentres, masslessVelocities += o.masslessVelocities;
        return *this;
    }
    Double3 centre() const { return mass > 0 ? firstMoment * (1.0 / mass) : massless ? masslessCentres * (1.0 / massless) : Double3(); }
    Double3 velocity() const { return mass > 0 ? momentum * (1.0 / mass) : massless ? masslessVelocities * (1.0 / massless) : Double3(); }
};

// The height reference of the potential energy: the floor, y = 0 (U = -m g . x).
Totals bodyTotals(const PhysicsWorld& physics, int body, const Vector3& gravity) {
    Totals t;
    if (body < 0 || !physics.alive(body)) return t;
    const BodyState s = physics.state(body);
    if (s.mass <= 0) { // fixed or led: no energy, a place and a velocity
        t.massless = 1, t.masslessCentres = Double3(s.centre), t.masslessVelocities = Double3(s.velocity);
        return t;
    }
    const double m = s.mass;
    const Double3 x(s.centre), v(s.velocity), w(s.angularVelocity), Iw(s.inertia * s.angularVelocity);
    t.mass = m;
    t.translational = 0.5 * m * dot(v, v);
    t.rotational = 0.5 * dot(w, Iw);
    t.potential = -m * dot(Double3(gravity), x);
    t.firstMoment = x * m;
    t.momentum = v * m;
    t.angularAboutOrigin = cross(x, v * m);
    t.angularAboutOrigin += Iw;
    return t;
}

Vector3 gravityOf(const Simulation& sim) { return sim.robot()->graph().world.gravity; }

int bodyOfEntity(const Simulation& sim, uint32_t entity) { return sim.robot() ? sim.robot()->bodyOf(entity) : -1; }

// The measurements every body and group share.
void addTotals(std::vector<Measurement>& out, const std::string& label, ChannelGroup group, const Totals& t) {
    auto add = [&](const char* quantity, const char* unit, const char* name, double value) {
        out.push_back(measured(label + "/" + quantity, unit, name, group, value));
    };
    const Double3 c = t.centre(), v = t.velocity();
    add("mass", KE_UI_LABEL("Kinetica", "кг"), KE_UI_LABEL("Kinetica", "Масса"), t.mass);
    add("centre x", KE_UI_LABEL("Kinetica", "м"), KE_UI_LABEL("Kinetica", "Центр масс, x"), c.x);
    add("centre y", KE_UI_LABEL("Kinetica", "м"), KE_UI_LABEL("Kinetica", "Центр масс, y"), c.y);
    add("centre z", KE_UI_LABEL("Kinetica", "м"), KE_UI_LABEL("Kinetica", "Центр масс, z"), c.z);
    add("height", KE_UI_LABEL("Kinetica", "м"), KE_UI_LABEL("Kinetica", "Высота центра масс над полом"), c.y);
    add("speed", KE_UI_LABEL("Kinetica", "м/с"), KE_UI_LABEL("Kinetica", "Скорость центра масс"), v.length());
    add("velocity x", KE_UI_LABEL("Kinetica", "м/с"), KE_UI_LABEL("Kinetica", "Скорость, x"), v.x);
    add("velocity y", KE_UI_LABEL("Kinetica", "м/с"), KE_UI_LABEL("Kinetica", "Скорость, y"), v.y);
    add("velocity z", KE_UI_LABEL("Kinetica", "м/с"), KE_UI_LABEL("Kinetica", "Скорость, z"), v.z);
    add("momentum", KE_UI_LABEL("Kinetica", "Н·с"), KE_UI_LABEL("Kinetica", "Импульс |P|"), t.momentum.length());
    add("kinetic energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Энергия движения (с вращением)"), t.translational + t.rotational);
    add("potential energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Энергия высоты над полом"), t.potential);
    add("energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Механическая энергия"), t.translational + t.rotational + t.potential);
}

const char* kDofs[] = {"tx", "ty", "tz", "rx", "ry", "rz"}; // JointDof

// The angle between two turns [rad]: 2 acos |q1 . q2|, by atan2 for its accuracy near zero.
double angleBetween(const Quaternion& a, const Quaternion& b) {
    const Quaternion d = (a * b.conjugate()).normalized();
    const double s = std::sqrt(double(d.x * d.x + d.y * d.y + d.z * d.z)), c = std::fabs(double(d.w));
    return 2 * double(ke::atan2(Real(s), Real(c)));
}

} // namespace

const std::vector<ChannelInfo>& sceneChannelCatalog() {
    static const std::vector<ChannelInfo> cards = {
        {"rigid/kinetic energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Твёрдые тела: энергия движения, Σ m v²/2"), ChannelGroup::Scene},
        {"rigid/rotational energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Твёрдые тела: энергия вращения, Σ ω·Iω/2"), ChannelGroup::Scene},
        {"rigid/potential energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Твёрдые тела: энергия высоты над полом"), ChannelGroup::Scene},
        {"rigid/mechanical energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Твёрдые тела: полная механическая энергия"), ChannelGroup::Scene},
        {"rigid/momentum", KE_UI_LABEL("Kinetica", "Н·с"), KE_UI_LABEL("Kinetica", "Твёрдые тела: импульс |P|"), ChannelGroup::Scene},
        {"rigid/momentum x", KE_UI_LABEL("Kinetica", "Н·с"), KE_UI_LABEL("Kinetica", "Твёрдые тела: импульс, x"), ChannelGroup::Scene},
        {"rigid/momentum y", KE_UI_LABEL("Kinetica", "Н·с"), KE_UI_LABEL("Kinetica", "Твёрдые тела: импульс, y"), ChannelGroup::Scene},
        {"rigid/momentum z", KE_UI_LABEL("Kinetica", "Н·с"), KE_UI_LABEL("Kinetica", "Твёрдые тела: импульс, z"), ChannelGroup::Scene},
        {"rigid/angular momentum", KE_UI_LABEL("Kinetica", "Н·м·с"), KE_UI_LABEL("Kinetica", "Твёрдые тела: момент импульса |L| около центра масс"), ChannelGroup::Scene},
        {"rigid/deepest penetration", KE_UI_LABEL("Kinetica", "м"), KE_UI_LABEL("Kinetica", "Твёрдые тела: самое глубокое проникновение"), ChannelGroup::Scene},
        {"scene/mechanical energy", KE_UI_LABEL("Kinetica", "Дж"), KE_UI_LABEL("Kinetica", "Сцена: механическая энергия"), ChannelGroup::Scene},
    };
    return cards;
}

const ChannelInfo* describeChannel(const std::string& id) {
    for (const ChannelInfo& c : sceneChannelCatalog())
        if (c.id == id) return &c;
    return nullptr;
}

std::vector<Measurement> measureScene(const Simulation& sim) {
    std::vector<Measurement> out;
    const PhysicsWorld* physics = sim.physics();
    if (!physics || !sim.robot()) return out;
    Totals t;
    const Vector3 g = gravityOf(sim);
    for (const Entity& e : sim.robot()->built()) t += bodyTotals(*physics, bodyOfEntity(sim, e.id), g); // not flattened a frame again
    const Double3 L = t.angularAboutOrigin - cross(t.centre(), t.momentum);
    const double mechanical = t.translational + t.rotational + t.potential;
    double deepest = 0;
    for (const ContactPoint& c : sim.robot()->contactsNow(*physics)) deepest = std::max(deepest, double(c.depth));
    put(out, "rigid/kinetic energy", t.translational);
    put(out, "rigid/rotational energy", t.rotational);
    put(out, "rigid/potential energy", t.potential);
    put(out, "rigid/mechanical energy", mechanical);
    put(out, "rigid/momentum", t.momentum.length());
    put(out, "rigid/momentum x", t.momentum.x);
    put(out, "rigid/momentum y", t.momentum.y);
    put(out, "rigid/momentum z", t.momentum.z);
    put(out, "rigid/angular momentum", L.length());
    put(out, "rigid/deepest penetration", deepest);
    put(out, "scene/mechanical energy", mechanical);
    for (const BatteryObject& b : sim.robot()->graph().batteries) { // the packs: what their motors draw (RobotPower.cpp)
        RobotWorld::PowerReading r;
        if (!sim.robot()->powerReading(b.id, r)) continue;
        out.push_back(measured("power/" + b.name + " voltage", KE_UI_LABEL("Kinetica", "В"), b.name + KE_UI_LABEL("Kinetica", ": напряжение"), ChannelGroup::Scene, double(r.voltage)));
        out.push_back(measured("power/" + b.name + " current", KE_UI_LABEL("Kinetica", "А"), b.name + KE_UI_LABEL("Kinetica", ": ток"), ChannelGroup::Scene, double(r.current)));
        out.push_back(measured("power/" + b.name + " charge", "%", b.name + KE_UI_LABEL("Kinetica", ": заряд"), ChannelGroup::Scene, 100.0 * double(r.soc)));
    }
    return out;
}

std::vector<Measurement> measureObject(const Simulation& sim, const ObjectRef& o) {
    std::vector<Measurement> out;
    const PhysicsWorld* physics = sim.physics();
    const int body = bodyOfEntity(sim, o.entity);
    if (!physics || body < 0 || !physics->alive(body)) return out;
    const BodyState s = physics->state(body);
    addTotals(out, o.label, ChannelGroup::Object, bodyTotals(*physics, body, gravityOf(sim)));
    // The normal force of its contacts: each point's impulse over the engine's last step - the frame's last
    // substep (RobotWorld::step drives the joints between substeps) - from a to b. A sleeping body is not
    // solved: sleep means the forces on it balance, so its contacts hold exactly its weight, -m g.
    const double h = double(sim.frameDt) / sim.robot()->engineSteps(sim.robot()->graph().world.physics.substeps);
    Double3 F;
    int points = 0;
    for (const ContactPoint& c : sim.robot()->contactsNow(*physics)) {
        if (c.a != body && c.b != body) continue;
        ++points;
        F += Double3(c.normal) * (double(c.normalImpulse) / h * (c.b == body ? 1.0 : -1.0));
    }
    if (!s.awake && s.mass > 0) F = Double3(gravityOf(sim)) * -double(s.mass);
    auto add = [&](const char* quantity, const char* unit, const char* name, double value) {
        out.push_back(measured(o.label + "/" + quantity, unit, name, ChannelGroup::Object, value));
    };
    add("angular speed", KE_UI_LABEL("Kinetica", "рад/с"), KE_UI_LABEL("Kinetica", "Угловая скорость |ω|"), double(length(s.angularVelocity)));
    add("asleep", KE_UI_LABEL("Kinetica", "да/нет"), KE_UI_LABEL("Kinetica", "Спит (1) или движется (0)"), s.awake ? 0.0 : 1.0);
    add("contact force", KE_UI_LABEL("Kinetica", "Н"), KE_UI_LABEL("Kinetica", "Сила контактов |F| (нормальная)"), F.length());
    add("contact force x", KE_UI_LABEL("Kinetica", "Н"), KE_UI_LABEL("Kinetica", "Сила контактов, x"), F.x);
    add("contact force y", KE_UI_LABEL("Kinetica", "Н"), KE_UI_LABEL("Kinetica", "Сила контактов, y"), F.y);
    add("contact force z", KE_UI_LABEL("Kinetica", "Н"), KE_UI_LABEL("Kinetica", "Сила контактов, z"), F.z);
    add("contacts", KE_UI_LABEL("Kinetica", "шт"), KE_UI_LABEL("Kinetica", "Точек контакта"), points);
    return out;
}

std::vector<Measurement> measureGroup(const Simulation& sim, const GroupRef& group) {
    std::vector<Measurement> out;
    if (!sim.physics()) return out;
    Totals t;
    for (const ObjectRef& o : group.members) t += bodyTotals(*sim.physics(), bodyOfEntity(sim, o.entity), gravityOf(sim));
    addTotals(out, group.label, ChannelGroup::Group, t);
    out.push_back(measured(group.label + "/members", KE_UI_LABEL("Kinetica", "шт"), KE_UI_LABEL("Kinetica", "Объектов в группе"), ChannelGroup::Group, double(group.members.size())));
    return out;
}

std::vector<Measurement> measureJoint(const Simulation& sim, const JointChannelRef& ref) {
    std::vector<Measurement> out;
    const RobotWorld* robot = sim.robot();
    const JointObject* j = robot ? robot->jointSpec(ref.joint) : nullptr;
    Real x[6];
    if (!j || !robot->jointCoordinates(*sim.physics(), ref.joint, x)) return out;
    const int main = mainFreedom(*j);
    for (int k = 0; k < 6; ++k) {
        if (j->dof[k].motion == JointMotion::Locked) continue;
        const bool turn = k >= DofRx;
        const std::string id = ref.label + "/" + kDofs[k], who = kDofs[k];
        out.push_back(measured(id + " coordinate", turn ? KE_UI_LABEL("Kinetica", "рад") : KE_UI_LABEL("Kinetica", "м"), who + ": " + (turn ? KE_UI_LABEL("Kinetica", "угол") : KE_UI_LABEL("Kinetica", "ход")), ChannelGroup::Object, double(x[k])));
        const JointDrive& d = j->dof[k].drive;
        if (d.enabled && d.stiffness > 0 && d.constant == 0)
            out.push_back(measured(id + " target", turn ? KE_UI_LABEL("Kinetica", "рад") : KE_UI_LABEL("Kinetica", "м"), who + KE_UI_LABEL("Kinetica", ": цель сервы"), ChannelGroup::Object, double(d.target)));
        if (d.enabled && k == main)
            out.push_back(measured(id + " effort", turn ? KE_UI_LABEL("Kinetica", "Н·м") : KE_UI_LABEL("Kinetica", "Н"), who + (turn ? KE_UI_LABEL("Kinetica", ": момент мотора") : KE_UI_LABEL("Kinetica", ": сила мотора")), ChannelGroup::Object,
                                   double(robot->jointEffort(*sim.physics(), ref.joint))));
    }
    return out;
}

std::vector<Measurement> measureGoal(const Simulation& sim, const GoalChannelRef& ref) {
    std::vector<Measurement> out;
    if (!sim.robot()) return out;
    const SceneGraph& live = sim.robot()->live();
    const Entity *goal = nullptr, *hand = nullptr;
    for (const Entity& e : live.entities)
        if (e.id == ref.goal) goal = &e;
    for (const Entity& e : live.entities)
        if (goal && e.id == goal->goalOf) hand = &e;
    if (!goal || !hand) return out;
    Vector3 gp, hp;
    Quaternion gq, hq;
    worldPose(live, *goal, gp, gq);
    worldPose(live, *hand, hp, hq);
    out.push_back(measured(ref.label + "/miss", KE_UI_LABEL("Kinetica", "мм"), KE_UI_LABEL("Kinetica", "Промах схвата"), ChannelGroup::Object, 1000.0 * double(length(gp - hp))));
    out.push_back(measured(ref.label + "/turn miss", "°", KE_UI_LABEL("Kinetica", "Промах поворота"), ChannelGroup::Object, angleBetween(gq, hq) * 180.0 / 3.14159265358979323846));
    return out;
}

std::vector<Measurement> measureAll(const Simulation& sim, const ChannelRequests& requests) {
    std::vector<Measurement> out;
    auto append = [&](std::vector<Measurement>&& part) {
        for (Measurement& m : part) out.push_back(std::move(m));
    };
    if (requests.scene) append(measureScene(sim));
    for (const ObjectRef& o : requests.objects) append(measureObject(sim, o));
    for (const GroupRef& g : requests.groups) append(measureGroup(sim, g));
    for (const JointChannelRef& j : requests.joints) append(measureJoint(sim, j));
    for (const GoalChannelRef& g : requests.goals) append(measureGoal(sim, g));
    return out;
}

} // namespace ke
