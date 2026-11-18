// The DC bus solved exactly for its voltage (PowerBus.h), with the drivers' current limits.
#include "electro/PowerBus.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

// The bus voltage for the duties as they are: U = (E + R0 (sum d K w / R - Ic)) / (1 + R0 sum d^2 / R).
Real busVoltage(const Battery& b, const std::vector<MotorLoad>& m, const std::vector<Real>& duty, Real constantCurrent) {
    const double E = double(b.openCircuitVoltage()) - double(b.polarisation()), R0 = double(b.seriesResistance());
    double num = E - R0 * double(constantCurrent), den = 1;
    for (size_t k = 0; k < m.size(); ++k) {
        num += R0 * double(duty[k]) * double(m[k].constant) * double(m[k].speed) / double(m[k].resistance);
        den += R0 * double(duty[k]) * double(duty[k]) / double(m[k].resistance);
    }
    return Real(num / den);
}

} // namespace

BusState solveBus(const Battery& battery, const std::vector<MotorLoad>& motors, Real constantCurrent) {
    BusState s;
    s.duty.resize(motors.size());
    for (size_t k = 0; k < motors.size(); ++k) s.duty[k] = std::clamp(motors[k].duty, -1.0_r, 1.0_r);
    s.motorVoltage.assign(motors.size(), 0);
    s.motorCurrent.assign(motors.size(), 0);
    for (int pass = 0; pass < 4; ++pass) {
        s.voltage = std::max(busVoltage(battery, motors, s.duty, constantCurrent), 0.0_r);
        bool limited = false;
        for (size_t k = 0; k < motors.size(); ++k) {
            const MotorLoad& m = motors[k];
            const Real back = m.constant * m.speed, current = (s.duty[k] * s.voltage - back) / m.resistance;
            if (!(std::fabs(current) > m.currentLimit) || !(s.voltage > 0)) continue;
            const Real lim = current > 0 ? m.currentLimit : -m.currentLimit;
            s.duty[k] = std::clamp((back + m.resistance * lim) / s.voltage, -1.0_r, 1.0_r);
            limited = true;
        }
        if (!limited) break;
    }
    s.current = constantCurrent;
    for (size_t k = 0; k < motors.size(); ++k) {
        s.motorVoltage[k] = s.duty[k] * s.voltage;
        s.motorCurrent[k] = (s.motorVoltage[k] - motors[k].constant * motors[k].speed) / motors[k].resistance;
        s.current += s.duty[k] * s.motorCurrent[k];
    }
    return s;
}

} // namespace ke
