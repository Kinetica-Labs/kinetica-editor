// The robot's power (RobotWorld.h, "The scene's power"): batteries and motor drivers of the scene wired to the
// joints' DC motors. A driver is an averaged H-bridge: duty d puts d V on its motor, V the bus voltage, and draws d I
// from the bus, I the motor's current. With a pack's open-circuit voltage less its polarisation, E = OCV - V1, and its
// series resistance R0, the bus and all its motors at once (motor i: constant K, winding plus switches R, speed w):
//
//   V = E - R0 sum d I,   I = (d V - K w) / R   ->   V = (E + R0 sum d K w / R) / (1 + R0 sum d^2 / R)
//
// exact for any number of motors on one pack. A current limit holds a motor's current at the limit, and V is solved
// again with that current fixed. At duty 0 the bridge coasts (the winding open, no current) or brakes (the winding
// shorted, I = -K w / R), drawing nothing from the bus either way. The pack is then stepped at the bus current
// (Battery::step), its books kept. The motors' torque is K I, put on the links by RobotWorld::drive.
#include "sim/RobotWorld.h"

#include <algorithm>

namespace ke {

void RobotWorld::buildPower() {
    packs_.clear();
    drivers_.clear();
    for (const BatteryObject& b : graph_.batteries) {
        BatterySpec spec = BatterySpec::liIon18650();
        spec.series = b.series, spec.parallel = b.parallel, spec.capacityAh = b.capacityAh;
        spec.r0 = b.r0, spec.r1 = b.r1, spec.c1 = b.c1, spec.cutoff = b.cutoff, spec.initialSoc = b.soc;
        packs_.push_back({b.id, Battery(spec)});
    }
    for (size_t i = 0; i < graph_.drivers.size(); ++i) {
        const DriverObject& d = graph_.drivers[i];
        const auto joint = std::find_if(joints_.begin(), joints_.end(), [&](const LiveJoint& j) { return j.spec.id == d.joint; });
        if (joint == joints_.end()) {
            notes_.push_back("«" + d.name + "»: нет сочленения с мотором, драйвер не подключён");
            continue;
        }
        if (joint->dof != d.dof || joint->spec.dof[d.dof].drive.constant <= 0) {
            notes_.push_back("«" + d.name + "»: у «" + joint->spec.name + "» на этой оси нет мотора постоянного тока");
            continue;
        }
        const auto pack = std::find_if(packs_.begin(), packs_.end(), [&](const LivePack& p) { return p.id == d.battery; });
        joint->driver = int(drivers_.size());
        drivers_.push_back({i, size_t(joint - joints_.begin()), pack == packs_.end() ? -1 : int(pack - packs_.begin())});
    }
}

void RobotWorld::power(const PhysicsWorld& physics, Real h) {
    struct Motor {
        LiveDriver* live;
        double duty, K, R, w, limit;
        bool brake, fixed;
    };
    std::vector<Motor> motors;
    for (int p = -1; p < int(packs_.size()); ++p) {
        motors.clear();
        for (LiveDriver& d : drivers_) {
            if (d.pack != p) continue;
            const LiveJoint& j = joints_[d.joint];
            const JointDrive& m = j.spec.dof[j.dof].drive;
            const DriverObject& spec = graph_.drivers[d.spec];
            if (m.constant <= 0 || !physics.jointAlive(j.joint)) {
                d.voltage = d.current = 0;
                continue;
            }
            const bool open = p >= 0 && packs_[size_t(p)].battery.spent(); // the pack's protection opened the bus
            motors.push_back({&d, open ? 0.0 : std::clamp(double(spec.duty), -1.0, 1.0), double(m.constant),
                              std::max(double(m.resistance) + double(spec.switches), 1e-6), double(axisOf(physics, j).speed),
                              double(spec.currentLimit), spec.brake, false});
        }
        auto held = [](const Motor& m, double i) { return m.limit > 0 ? std::clamp(i, -m.limit, m.limit) : i; };
        if (p < 0) { // an ideal bus: each motor at its own line's voltage
            for (Motor& m : motors) {
                const double V = double(joints_[m.live->joint].spec.dof[joints_[m.live->joint].dof].drive.voltage);
                m.live->current = Real(m.duty != 0 ? held(m, (m.duty * V - m.K * m.w) / m.R) : m.brake ? held(m, -m.K * m.w / m.R) : 0.0);
                m.live->voltage = Real(m.duty != 0 ? m.duty * V : m.brake ? 0.0 : m.K * m.w);
            }
            continue;
        }
        LivePack& pack = packs_[size_t(p)];
        const double E = double(pack.battery.openCircuitVoltage()) - double(pack.battery.polarisation());
        const double R0 = double(pack.battery.seriesResistance());
        auto busVoltage = [&] {
            double num = E, den = 1;
            for (const Motor& m : motors) {
                if (m.duty == 0) continue;
                if (m.fixed) num -= R0 * m.duty * double(m.live->current);
                else num += R0 * m.duty * m.K * m.w / m.R, den += R0 * m.duty * m.duty / m.R;
            }
            return num / den;
        };
        double V = busVoltage();
        bool clamped = false;
        for (Motor& m : motors) {
            if (m.duty == 0) continue;
            const double I = (m.duty * V - m.K * m.w) / m.R;
            m.live->current = Real(held(m, I));
            if (held(m, I) != I) m.fixed = clamped = true;
        }
        if (clamped) {
            V = busVoltage();
            for (Motor& m : motors)
                if (m.duty != 0 && !m.fixed) m.live->current = Real((m.duty * V - m.K * m.w) / m.R);
        }
        double bus = 0;
        for (Motor& m : motors) {
            if (m.duty != 0) {
                bus += m.duty * double(m.live->current);
                m.live->voltage = Real(m.duty * V);
            } else {
                m.live->current = Real(m.brake ? held(m, -m.K * m.w / m.R) : 0.0);
                m.live->voltage = Real(m.brake ? 0.0 : m.K * m.w);
            }
        }
        pack.voltage = Real(V);
        pack.current = Real(bus);
        pack.battery.step(Real(bus), h);
    }
}

bool RobotWorld::powerReading(uint32_t id, PowerReading& out) const {
    for (const LivePack& p : packs_)
        if (p.id == id) {
            out = {p.voltage, p.current, p.battery.soc(), p.battery.spent()};
            return true;
        }
    for (const LiveDriver& d : drivers_)
        if (graph_.drivers[d.spec].id == id) {
            out = {d.voltage, d.current, 0, d.pack >= 0 && packs_[size_t(d.pack)].battery.spent()};
            return true;
        }
    return false;
}

void RobotWorld::setDuties(const std::vector<DriverObject>& drivers) {
    for (const DriverObject& given : drivers)
        for (SceneGraph* g : {&graph_, &live_})
            for (DriverObject& d : g->drivers)
                if (d.id == given.id) d.duty = std::clamp(given.duty, -1.0_r, 1.0_r);
}

} // namespace ke
