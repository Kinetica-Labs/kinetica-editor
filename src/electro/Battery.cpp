// The battery's equivalent circuit and its energy books (Battery.h). Over a step the current is held:
// the state of charge falls linearly, the polarisation relaxes exactly as an exponential, and the
// integrals of the books are taken exactly over that motion (the chemistry's by the trapezoid of OCV,
// which is linear in the state of charge within a segment of the table).
#include "electro/Battery.h"

#include "math/ElementaryFunctions.h"

#include <algorithm>

namespace ke {

// A typical NMC lithium-ion cell's open-circuit voltage at rest (manufacturers' discharge curves at
// C/20, e.g. Panasonic NCR18650): flat in the middle, steep at both ends.
BatterySpec BatterySpec::liIon18650() {
    BatterySpec s;
    s.ocv = {{0.00_r, 3.00_r}, {0.05_r, 3.45_r}, {0.10_r, 3.55_r}, {0.20_r, 3.63_r}, {0.30_r, 3.68_r}, {0.40_r, 3.72_r},
             {0.50_r, 3.76_r}, {0.60_r, 3.82_r}, {0.70_r, 3.89_r}, {0.80_r, 3.97_r}, {0.90_r, 4.06_r}, {1.00_r, 4.20_r}};
    return s;
}

Battery::Battery(BatterySpec spec) : spec_(std::move(spec)) {
    if (spec_.ocv.empty()) spec_.ocv = BatterySpec::liIon18650().ocv;
    soc_ = double(std::clamp(spec_.initialSoc, 0.0_r, 1.0_r));
}

Real Battery::cellOcv(Real soc) const {
    const auto& t = spec_.ocv;
    if (soc <= t.front().first) return t.front().second;
    if (soc >= t.back().first) return t.back().second;
    size_t k = 1;
    while (t[k].first < soc) ++k;
    const Real f = (soc - t[k - 1].first) / (t[k].first - t[k - 1].first);
    return t[k - 1].second + f * (t[k].second - t[k - 1].second);
}

Real Battery::openCircuitVoltage() const { return Real(spec_.series) * cellOcv(Real(soc_)); }
Real Battery::seriesResistance() const { return spec_.r0 * Real(spec_.series) / Real(spec_.parallel); }
Real Battery::packR1() const { return spec_.r1 * Real(spec_.series) / Real(spec_.parallel); }
Real Battery::packC1() const { return spec_.c1 * Real(spec_.parallel) / Real(spec_.series); }
Real Battery::terminalVoltage(Real current) const { return openCircuitVoltage() - v1_ - seriesResistance() * current; }
double Battery::storedInC1() const { return 0.5 * double(packC1()) * double(v1_) * double(v1_); }

// One step of h seconds at the current I. V1(t) = V1inf + D e^{-t/tau}, V1inf = R1 I, D = V1(0) - V1inf:
//   int V1 dt     = V1inf h + D tau (1 - e^{-h/tau})
//   int V1^2 dt   = V1inf^2 h + 2 V1inf D tau (1 - e^{-h/tau}) + D^2 tau / 2 (1 - e^{-2h/tau})
// and V1 I = V1^2 / R1 + C1 V1 dV1/dt, so int V1 I dt = heat in R1 + the change of C1 V1^2 / 2 exactly.
void Battery::step(Real current, Real h) {
    if (!(h > 0)) return;
    const double I = double(current), dt = double(h), Q = double(spec_.capacityAh) * 3600.0 * double(spec_.parallel);
    const double ocv0 = double(openCircuitVoltage());
    soc_ = std::clamp(soc_ - I * dt / Q, 0.0, 1.0);
    const double ocv1 = double(openCircuitVoltage());
    const double r1 = double(packR1()), tau = r1 * double(packC1()), inf = r1 * I, d = double(v1_) - inf;
    const double decay = ke::exp(-dt / tau), decay2 = decay * decay;
    const double v1Integral = inf * dt + d * tau * (1 - decay);
    const double v1SquaredIntegral = inf * inf * dt + 2 * inf * d * tau * (1 - decay) + d * d * 0.5 * tau * (1 - decay2);
    const double chemistry = 0.5 * (ocv0 + ocv1) * I * dt, ohmic = I * I * double(seriesResistance()) * dt;
    chemical_ += chemistry;
    heatR0_ += ohmic;
    heatR1_ += v1SquaredIntegral / r1;
    delivered_ += chemistry - ohmic - I * v1Integral;
    v1_ = Real(inf + d * decay);
    // The cut-off latches, as a battery's protection does: a pack once spent stays off.
    spent_ = spent_ || soc_ <= 0 || (current > 0 && terminalVoltage(current) < spec_.cutoff * Real(spec_.series));
}

} // namespace ke
