// The scene in words (Describe.h): a header line with the counts and the moving mass, the bodies as a
// tree of joints - a link under what it hangs on - then the sensors and the electrics. A shape's mass
// is its density times the volume of its shape (a box x y z, a sphere pi/6, a cylinder pi/4, a cone
// pi/12 of it), a reshaped one's the volume of its mesh; a model's is left out.
#include "scene/Describe.h"

#include "core/Format.h"
#include "scene/Assembly.h"
#include "scene/SensorSpec.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

// A number for reading: rounded to `places` decimals, the shortest text that reads back as it.
std::string num(Real v, int places = 3) {
    double scale = 1.0;
    for (int k = 0; k < places; ++k) scale *= 10.0;
    return numberText(float(std::round(double(v) * scale) / scale));
}

std::string vec(const Vector3& v) { return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ")"; }
std::string quoted(const std::string& name) { return "«" + name + "»"; }

// A direction of the carrier's frame in words (the modules' convention: forward +x, up +y, left -z).
std::string direction(const Vector3& d) {
    const char* names[] = {"вперёд", "назад", "вверх", "вниз", "вправо", "влево"};
    int best = 0;
    for (int k = 1; k < 6; ++k)
        if ((k % 2 ? -1.0_r : 1.0_r) * d[k / 2] > (best % 2 ? -1.0_r : 1.0_r) * d[best / 2]) best = k;
    return names[best];
}

Real massOf(const Entity& e) {
    static const Real share[] = {1.0_r, kPi / 6, kPi / 4, kPi / 12, 1.0_r, 0.0_r};
    if (!e.rigid.enabled || e.rigid.fixed) return 0;
    return e.rigid.density * share[int(e.shape)] * e.size.x * e.size.y * e.size.z;
}

std::string shapeWords(const Entity& e) {
    static const char* shapes[] = {"куб", "сфера", "цилиндр", "конус", "плоскость", "модель"};
    std::string s = std::string(shapes[int(e.shape)]) + " " + num(e.size.x) + " × " + num(e.size.y) + " × " + num(e.size.z) + " м";
    if (massOf(e) > 0) s += ", " + num(massOf(e), 2) + " кг";
    std::string roles = e.rigid.enabled ? "твёрдое" : "";
    if (e.rigid.enabled && e.rigid.fixed) roles += ", закреплено";
    if (roles.empty()) roles = e.collider.enabled ? "только коллайдер (неподвижное препятствие)" : "только геометрия";
    return s + ", " + roles;
}

const DriverObject* driverOf(const SceneGraph& g, uint32_t joint) {
    for (const DriverObject& d : g.drivers)
        if (d.joint == joint) return &d;
    return nullptr;
}

// The joint's axis in its carrier's frame, in words ("ось вверх"): the main freedom's axis of frame b,
// which the joint keeps in b's own frame.
// The axis in words and as the world's axis it lies along ("ось вправо (+z мира)"; off every axis: its vector).
std::string axisWords(const SceneGraph& g, const JointObject& j, int main) {
    Vector3 originA, originB;
    Quaternion frameA, frameB;
    jointWorldFrames(g, j, originA, frameA, originB, frameB);
    Vector3 e(0.0_r);
    e[main % 3] = 1;
    const Vector3 v = frameB.rotate(e);
    int k = 0;
    for (int i = 1; i < 3; ++i)
        if (std::fabs(v[i]) > std::fabs(v[k])) k = i;
    const std::string world = std::fabs(v[k]) > 0.999_r ? std::string(v[k] > 0 ? "+" : "-") + char('x' + k) : "[" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + "]";
    return std::string("ось ") + direction(v) + " (" + world + " мира)";
}

std::string jointWords(const SceneGraph& g, const JointObject& j) {
    const std::string kind = jointKindWord(j);
    const char* words[][2] = {{"weld", "сварка"}, {"hinge", "шарнир"}, {"slider", "ползун"}, {"ball", "шаровое"}, {"cylinder", "цилиндрическое"},
                              {"universal", "карданное"}, {"planar", "плоское"}};
    std::string s = "своё сочленение";
    for (const auto& w : words)
        if (kind == w[0]) s = w[1];
    const int main = mainFreedom(j);
    const std::string id = " (сочленение #" + std::to_string(j.id) + ")";
    if (main < 0) return s + id;
    const JointFreedom& f = j.dof[main];
    s += ", " + axisWords(g, j, main);
    if (f.motion == JointMotion::Limited) s += ", ход " + num(f.lower) + " … " + num(f.upper) + (main >= DofRx ? " рад" : " м");
    if (f.drive.enabled && f.drive.constant > 0) s += ", мотор постоянного тока " + num(f.drive.voltage, 1) + " В";
    else if (f.drive.enabled && f.drive.stiffness > 0)
        s += ", серво к " + num(f.drive.target) + (main >= DofRx ? " рад" : " м") + ", жёсткость " + num(f.drive.stiffness, 0) + (main >= DofRx ? " Н·м/рад" : " Н/м");
    else if (f.drive.enabled) s += ", мотор скорости " + num(f.drive.targetVelocity) + (main >= DofRx ? " рад/с" : " м/с");
    if (const DriverObject* d = driverOf(g, j.id)) s += ", драйвер " + quoted(d->name) + " ШИМ " + num(d->duty, 2);
    return s + id;
}

const JointObject* holder(const SceneGraph& g, uint32_t entity) {
    for (const JointObject& j : g.joints)
        if (j.a == entity) return &j;
    return nullptr;
}

// A body's point wheels (WheelObject): the fast model of a wheel, no body of its own.
void pointWheels(const SceneGraph& g, uint32_t body, int depth, std::string& out) {
    for (const WheelObject& w : g.wheels) {
        if (w.parent != body) continue;
        out += std::string(2 * depth + 2, ' ') + "- " + quoted(w.name) + " #" + std::to_string(w.id) + ": точечное колесо r " + num(w.radius) + " м, пружина " + num(w.stiffness, 0) +
               " Н/м, ход " + num(w.rest) + " м, качение C_rr " + num(w.rolling);
        if (w.constant > 0) out += ", мотор постоянного тока " + num(w.voltage, 1) + " В";
        if (const DriverObject* d = driverOf(g, w.id)) out += ", драйвер " + quoted(d->name) + " ШИМ " + num(d->duty, 2);
        out += "\n";
    }
}

void tree(const SceneGraph& g, uint32_t id, int depth, std::string& out) {
    for (const Entity& e : g.entities) {
        const JointObject* j = holder(g, e.id);
        if ((j ? j->b : 0) != id || (depth == 0 && j && j->b != 0)) continue;
        out += std::string(2 * depth + 2, ' ') + "- " + quoted(e.name) + " #" + std::to_string(e.id) + ": " + shapeWords(e);
        if (j) out += "; " + jointWords(g, *j) + (j->b ? " — на " + quoted(findObject(g, j->b)->name) : " — к миру");
        else out += "; в мире " + vec(e.position);
        out += "\n";
        pointWheels(g, e.id, depth + 1, out);
        tree(g, e.id, depth + 1, out);
    }
}

std::string sensors(const SceneGraph& g) {
    std::string out;
    for (const SensorObject& s : g.sensors) {
        out += "  - " + quoted(s.name) + " #" + std::to_string(s.id) + (s.model.empty() ? std::string() : " (" + s.model + ")") + ": ";
        if (s.kind == SensorKind::Encoder) {
            const JointObject* j = findJoint(g, s.parent);
            const SceneObject* on = findObject(g, s.parent);
            out += "энкодер " + std::to_string(s.lines) + " линий " + (j ? "на сочленении " + quoted(j->name) : on ? "на " + quoted(on->name) : "на теле") + "\n";
            continue;
        }
        if (s.kind == SensorKind::Wrench || s.kind == SensorKind::Effort) {
            const JointObject* j = findJoint(g, s.parent);
            const std::string where = j ? " на сочленении " + quoted(j->name) : " без сочленения";
            const char* names[] = {"tx", "ty", "tz", "rx", "ry", "rz"}; // JointDof
            out += (s.kind == SensorKind::Wrench ? "сила и момент" : std::string("усилие привода по ") + names[std::clamp(s.dof, 0, 5)]) + where + "\n";
            continue;
        }
        if (s.kind == SensorKind::Stereo) {
            const SceneObject* on = findObject(g, s.parent);
            out += "стереокамера " + std::to_string(s.width) + "×" + std::to_string(s.height) + ", база " + format("%.0f мм", double(s.baseline) * 1000) +
                   (s.projector ? ", с проектором" : "") + (on ? " на " + quoted(on->name) : std::string(" в мире")) + ", смотрит " +
                   direction(objectRotation(s).rotate(Vector3(1, 0, 0))) + "\n";
            continue;
        }
        if (s.kind == SensorKind::Touch) {
            const SceneObject* on = findObject(g, s.parent);
            out += "касание" + (on ? " на " + quoted(on->name) : std::string(" в мире")) + ", грань смотрит " +
                   direction(objectRotation(s).rotate(Vector3(1, 0, 0))) + "\n";
            continue;
        }
        const SceneObject* on = findObject(g, s.parent);
        out += std::string(s.kind == SensorKind::Imu ? "IMU" : "сонар") + (on ? " на " + quoted(on->name) : " в мире");
        if (s.kind == SensorKind::Sonar) out += ", смотрит " + direction(objectRotation(s).rotate(Vector3(1, 0, 0)));
        out += "\n";
    }
    return out;
}

} // namespace

std::string describeScene(const SceneGraph& g) {
    Real mass = 0;
    for (const Entity& e : g.entities) mass += massOf(e);
    for (const WheelObject& w : g.wheels) mass += w.mass + w.unsprung; // a point wheel's body carries them
    for (const SensorObject& s : g.sensors) mass += sensorMass(s);   // and a body its sensors
    std::string out = "Сцена. Тел: " + std::to_string(g.entities.size()) + ", сочленений: " + std::to_string(g.joints.size()) + ", точечных колёс: " +
                      std::to_string(g.wheels.size()) + ", датчиков: " +
                      std::to_string(g.sensors.size()) + ", батарей: " + std::to_string(g.batteries.size()) + ", драйверов: " +
                      std::to_string(g.drivers.size()) + "; подвижная масса " + num(mass, 2) + " кг.\n";
    out += "Тела (под телом — то, что держится на нём сочленением):\n";
    tree(g, 0, 0, out);
    if (!g.sensors.empty()) out += "Датчики:\n" + sensors(g);
    for (const BatteryObject& b : g.batteries)
        out += (&b == &g.batteries.front() ? "Электрика:\n" : "") + std::string("  - ") + quoted(b.name) + " #" + std::to_string(b.id) + ": " + std::to_string(b.series) + "S" +
               std::to_string(b.parallel) + "P, " + num(b.capacityAh * b.parallel, 2) + " А·ч, заряд " + num(100 * b.soc, 0) + " %\n";
    return out;
}

} // namespace ke
