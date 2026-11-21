// The sensors of the running world (LiveSensors.h): the probes of the physics, the devices of the datasheets, and the
// recipes of the kinds.
#include "sim/LiveSensors.h"

#include "devices/Encoder.h"
#include "devices/ForceTorque.h"
#include "devices/Imu.h"
#include "devices/Sonar.h"
#include "math/ElementaryFunctions.h"
#include "scene/SensorSpec.h"
#include "sim/RobotWorld.h"

#include <cmath>

namespace ke {

void LiveSensor::step(const PhysicsWorld& physics, const RobotWorld& robot, Real h, int64_t untilNs) {
    std::string problem;
    if (!probe_->sense(physics, robot, h, truth_, problem)) {
        reading_.problem = problem;
        return;
    }
    reading_.problem.clear();
    device_->run(untilNs, truth_, reading_);
}

namespace {

// ---------------------------------------------------------------------------
// The probes: what a sensor touches in the world
// ---------------------------------------------------------------------------

// Where a sensor rides: a body of the engine and its pose in the body's frame, or fixed in the world (body -1).
struct Mount {
    int body = -1;
    Vector3 position{0.0_r};
    Quaternion rotation;
    void pose(const PhysicsWorld& physics, Vector3& p, Quaternion& q) const {
        p = position, q = rotation;
        if (body < 0) return;
        const BodyState s = physics.state(body);
        p = s.position + s.rotation.rotate(position), q = (s.rotation * rotation).normalized();
    }
};

// A coordinate along a freedom, a turn unwrapped across +-pi: a shaft counts on past a full turn.
struct Unwrap {
    Real last = 0;
    double turns = 0;
    bool had = false;
    Real operator()(Real x, int dof) {
        if (dof < DofRx) return x;
        if (had) turns += std::round((double(last) - double(x)) / (2 * kPi));
        last = x, had = true;
        return Real(double(x) + turns * 2 * kPi);
    }
};

// An accelerometer and a gyroscope at a point: the specific force - the point's acceleration less gravity, so at rest
// it reads 1 g up - and the angular rate, in the sensor's axes. The acceleration is the change of the point's velocity
// v + w x (p - c) over the substep: at rest from the start, the first is taken as none.
class InertialProbe : public SensorProbe {
public:
    InertialProbe(const Mount& m, const Vector3& gravity) : mount_(m), gravity_(gravity) {}
    bool sense(const PhysicsWorld& physics, const RobotWorld&, Real h, SensorValues& truth, std::string&) override {
        Vector3 p;
        Quaternion q;
        mount_.pose(physics, p, q);
        Vector3 v(0.0_r), w(0.0_r);
        if (mount_.body >= 0) {
            const BodyState s = physics.state(mount_.body);
            w = s.angularVelocity, v = s.velocity + cross(w, p - s.centre);
        }
        const Vector3 a = had_ && h > 0 ? (v - last_) / h : Vector3(0.0_r);
        last_ = v, had_ = true;
        truth.a = q.conjugate().rotate(a - gravity_);
        truth.b = q.conjugate().rotate(w);
        return true;
    }

private:
    Mount mount_;
    Vector3 gravity_, last_{0.0_r};
    bool had_ = false;
};

// A sonar's echo: sound reflects specularly off a surface smooth at its wavelength (8.6 mm at 40 kHz), so a surface sends an
// echo back only where its normal lies inside the beam, and the echo comes from the foot of the perpendicular. For a
// hit at distance t along a ray d with the normal n the range is the distance to the hit's tangent plane, t (-d . n),
// when the angle between the beam's axis and -n is at most the beam's half angle - exact for a plane, the local plane
// for a curved surface; the nearest such over 17 rays filling the cone (the axis, and 8 each at the half angle and
// half of it), its carrier left out. A wall turned more than the half angle from the beam returns nothing: the
// sonar's mirror blind spot. Diffuse scattering off rough surfaces, diffraction at edges and multiple echoes are not
// modelled. No echo in range: -1.
class EchoProbe : public SensorProbe {
public:
    EchoProbe(const Mount& m, const SonarSpec& spec) : mount_(m), spec_(spec) {}
    bool sense(const PhysicsWorld& physics, const RobotWorld&, Real, SensorValues& truth, std::string&) override {
        Vector3 origin;
        Quaternion turn;
        mount_.pose(physics, origin, turn);
        const double half = spec_.beamDeg / 2 * kPi / 180, cosBeam = ke::cos(half);
        const Vector3 axis = turn.rotate(Vector3(1, 0, 0));
        double best = -1;
        for (int ray = 0; ray < 17; ++ray) {
            const double a = ray == 0 ? 0 : (ray <= 8 ? 0.5 : 1.0) * half, b = double((ray - 1) % 8) * kPi / 4;
            const Vector3 d = turn.rotate(Vector3(Real(ke::cos(a)), Real(ke::sin(a) * ke::cos(b)), Real(ke::sin(a) * ke::sin(b))));
            BodyHit hit;
            if (!physics.castRay(origin, origin + d * Real(spec_.maxM / cosBeam), hit, mount_.body) || -double(dot(axis, hit.normal)) < cosBeam) continue;
            const double range = double(hit.distance) * -double(dot(d, hit.normal));
            if (range >= spec_.minM && range <= spec_.maxM && (best < 0 || range < best)) best = range;
        }
        truth.value = Real(best);
        return true;
    }

private:
    Mount mount_;
    SonarSpec spec_;
};

// A joint's quantity: its coordinate along a freedom (unwrapped), the load it passes to its link, or what its drive put out.
class JointProbe : public SensorProbe {
public:
    enum class What { Coordinate, Wrench, Effort };
    JointProbe(uint32_t joint, int dof, What what) : joint_(joint), dof_(dof), what_(what) {}
    bool sense(const PhysicsWorld& physics, const RobotWorld& robot, Real, SensorValues& truth, std::string& problem) override {
        if (what_ == What::Wrench) return robot.jointWrench(physics, joint_, truth.a, truth.b) || (problem = "its joint is not built", false);
        if (what_ == What::Effort) return truth.value = robot.jointEffort(physics, joint_), true;
        Real x[6];
        if (!robot.jointCoordinates(physics, joint_, x)) return problem = "its joint is not built", false;
        truth.value = unwrap_(x[dof_], dof_);
        return true;
    }

private:
    uint32_t joint_;
    int dof_;
    What what_;
    Unwrap unwrap_;
};

// A frame against another with no joint between (a frame sensor with a reference frame): the sensor's frame on
// its body against `against` (a body, or the world) fixed where the two were when it began to read - a measuring
// wheel, a turntable, a tilt.
class FrameProbe : public SensorProbe {
public:
    FrameProbe(const Mount& m, const Mount& against, int dof) : mount_(m), against_(against), dof_(dof) {}
    bool sense(const PhysicsWorld& physics, const RobotWorld&, Real, SensorValues& truth, std::string& problem) override {
        if (against_.body >= 0 && !physics.alive(against_.body)) return problem = "what it is measured against is gone", false;
        Vector3 p, o;
        Quaternion q, r;
        mount_.pose(physics, p, q);
        against_.pose(physics, o, r);
        if (!started_) { // where the two are now: the zero, fixed to `against`
            zero_ = {r.conjugate().rotate(p - o), (r.conjugate() * q).normalized()};
            started_ = true;
        }
        Real x[6];
        frameCoordinates(p, q, o + r.rotate(zero_.first), (r * zero_.second).normalized(), x);
        truth.value = unwrap_(x[dof_], dof_);
        return true;
    }

private:
    Mount mount_, against_;
    int dof_;
    Unwrap unwrap_;
    std::pair<Vector3, Quaternion> zero_;
    bool started_ = false;
};

// A touch pad on a face: the normal force of the contacts through it - the points whose outward normal lies within 45
// degrees of the pad's x - and how many, from the engine's contacts of the last substep (impulse over its length).
class TouchProbe : public SensorProbe {
public:
    explicit TouchProbe(const Mount& m) : mount_(m) {}
    bool sense(const PhysicsWorld& physics, const RobotWorld&, Real h, SensorValues& truth, std::string& problem) override {
        if (mount_.body < 0) return problem = "a touch pad needs a body to lie on", false;
        Vector3 p;
        Quaternion q;
        mount_.pose(physics, p, q);
        const Vector3 x = q.rotate(Vector3(1, 0, 0));
        truth.value = 0, truth.count = 0;
        for (const ContactPoint& c : physics.contactsOf(mount_.body)) { // its body's alone: a pile of a thousand touches elsewhere costs nothing
            const Vector3 out = c.a == mount_.body ? c.normal : -c.normal;
            if (dot(out, x) < ke::cos(kPi / 4)) continue;
            truth.value += h > 0 ? c.normalImpulse / h : 0, ++truth.count;
        }
        return true;
    }

private:
    Mount mount_;
};

// ---------------------------------------------------------------------------
// The devices: what the datasheet makes of the truth
// ---------------------------------------------------------------------------

class ImuDevice : public SensorDevice {
public:
    ImuDevice(const ImuSpec& spec, uint32_t seed, bool noise) : imu_(spec, seed, noise) {}
    void run(int64_t untilNs, const SensorValues& truth, SensorReading& out) override {
        imu_.run(untilNs, truth.a, truth.b);
        out.a = imu_.accel(), out.b = imu_.gyro(), out.sampleNs = imu_.sampleNs(), out.saturated = imu_.saturated();
    }

private:
    Imu imu_;
};

class WrenchDevice : public SensorDevice {
public:
    WrenchDevice(const ForceTorqueSpec& spec, uint32_t seed, bool noise) : ft_(spec, seed, noise) {}
    void run(int64_t untilNs, const SensorValues& truth, SensorReading& out) override {
        ft_.run(untilNs, truth.a, truth.b);
        out.a = ft_.force(), out.b = ft_.torque(), out.sampleNs = ft_.sampleNs(), out.saturated = ft_.saturated();
    }

private:
    ForceTorque ft_;
};

// A measurement every cycle from time 0: the trigger, then the echo's pulse; the host's distance from its width.
class SonarDevice : public SensorDevice {
public:
    SonarDevice(const SonarSpec& spec, uint32_t seed, bool noise) : sonar_(spec, seed, noise) {}
    void run(int64_t untilNs, const SensorValues& truth, SensorReading& out) override {
        const int64_t cycle = std::max<int64_t>(sonar_.cycleNs(), 1);
        for (; next_ <= untilNs; next_ += cycle) {
            edges_.clear();
            out.value = Real(sonar_.measure(next_, double(truth.value), 20, edges_));
            out.sampleNs = edges_.back().timeNs; // the echo's fall: when the host has the distance
        }
    }

private:
    Sonar sonar_;
    int64_t next_ = 0;
    std::vector<PinEdge> edges_;
};

// The quadrature encoder: its edges as the coordinate moves over the substep, the count a decoder keeps, and the
// coordinate it means - with a known zero only.
class EncoderDevice : public SensorDevice {
public:
    EncoderDevice(const SensorObject& s) : encoder_(s.dof >= DofRx ? std::max(s.lines, 1) : 0, s.dof >= DofRx ? 0 : 4.0 * std::max(s.lines, 1)),
                                           referenced_(s.hasEncoderReference), zero_(s.encoderReference) {}
    void run(int64_t untilNs, const SensorValues& truth, SensorReading& out) override {
        if (!started_) encoder_.start(double(truth.value)), started_ = true;
        else encoder_.advance(last_, double(truth.value), lastNs_, untilNs, edges_);
        edges_.clear();
        last_ = double(truth.value), lastNs_ = untilNs;
        out.count = encoder_.count();
        out.value = Real(double(encoder_.count()) / encoder_.stepsPerUnit() + (referenced_ ? double(zero_) : 0));
        out.referenced = referenced_;
        out.sampleNs = untilNs;
    }

private:
    Encoder encoder_;
    bool referenced_;
    Real zero_;
    bool started_ = false;
    double last_ = 0;
    int64_t lastNs_ = 0;
    std::vector<PinEdge> edges_;
};

// No datasheet yet: the truth as it is, every substep (a touch pad, a drive's effort).
class IdealDevice : public SensorDevice {
public:
    void run(int64_t untilNs, const SensorValues& truth, SensorReading& out) override {
        static_cast<SensorValues&>(out) = truth;
        out.sampleNs = untilNs;
    }
};

// The mount of a sensor on an entity: its body and its pose on the entity's frame (the body's); on nothing, its pose in
// the world.
Mount mountOf(const SensorObject& s, const SceneGraph& g, const RobotWorld& robot) {
    Mount m;
    m.body = s.parent ? robot.bodyOf(s.parent) : -1;
    if (s.parent && m.body >= 0) return m.position = s.position, m.rotation = objectRotation(s), m;
    worldPose(g, s, m.position, m.rotation);
    return m;
}

} // namespace

std::unique_ptr<LiveSensor> makeSensor(const SensorObject& s, const SceneGraph& g, const RobotWorld& robot, std::string& why) {
    const uint32_t seed = s.seed ? s.seed : s.id;
    const bool onJoint = robot.jointSpec(s.parent) != nullptr;
    std::unique_ptr<SensorProbe> probe;
    std::unique_ptr<SensorDevice> device;
    switch (s.kind) {
    case SensorKind::Imu:
        probe = std::make_unique<InertialProbe>(mountOf(s, g, robot), g.world.gravity);
        device = std::make_unique<ImuDevice>(imuSpec(s), seed, s.noise);
        break;
    case SensorKind::Sonar:
        probe = std::make_unique<EchoProbe>(mountOf(s, g, robot), sonarSpec(s));
        device = std::make_unique<SonarDevice>(sonarSpec(s), seed, s.noise);
        break;
    case SensorKind::Touch:
        probe = std::make_unique<TouchProbe>(mountOf(s, g, robot));
        device = std::make_unique<IdealDevice>();
        break;
    case SensorKind::Wrench:
    case SensorKind::Effort:
        if (!onJoint) return why = "«" + s.name + "»: not on a joint", nullptr;
        probe = std::make_unique<JointProbe>(s.parent, s.dof, s.kind == SensorKind::Wrench ? JointProbe::What::Wrench : JointProbe::What::Effort);
        if (s.kind == SensorKind::Wrench) device = std::make_unique<WrenchDevice>(wrenchSpec(s), seed, s.noise);
        else device = std::make_unique<IdealDevice>();
        break;
    case SensorKind::Encoder:
        if (onJoint) probe = std::make_unique<JointProbe>(s.parent, s.dof, JointProbe::What::Coordinate);
        else if (s.parent && robot.bodyOf(s.parent) >= 0) {
            Mount against;
            if (s.against) against.body = robot.bodyOf(s.against);
            if (s.against && against.body < 0) return why = "«" + s.name + "»: what it is measured against has no body", nullptr;
            probe = std::make_unique<FrameProbe>(mountOf(s, g, robot), against, s.dof);
        } else {
            return why = "«" + s.name + "»: an encoder needs a joint or a body", nullptr;
        }
        device = std::make_unique<EncoderDevice>(s);
        break;
    case SensorKind::Stereo:
        return why.clear(), nullptr; // its frame is taken on request (sensor_capture), not every substep
    }
    return std::make_unique<LiveSensor>(s, std::move(probe), std::move(device));
}

} // namespace ke
