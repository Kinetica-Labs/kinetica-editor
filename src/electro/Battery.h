#pragma once
// A battery as an equivalent circuit (the Thevenin model of a cell: Chen and Rincon-Mora, IEEE Trans.
// Energy Conversion 21, 2006; Plett, "Battery Management Systems", vol. 1, 2015): the open-circuit
// voltage of the chemistry, which depends on the state of charge, OCV(SoC); a series resistance R0
// (the ohmic drop, at once); and one RC pair R1 || C1 (the polarisation that builds up over tens of
// seconds and relaxes after the load goes). With the current I (> 0 discharging):
//
//   terminal voltage   V = OCV(SoC) - V1 - R0 I
//   charge             dSoC/dt = -I / Q               (Coulomb counting, Q in coulombs)
//   polarisation       dV1/dt = (I - V1 / R1) / C1    -> over a step with I held: the exact
//                      V1' = V1 e^{-h/tau} + R1 I (1 - e^{-h/tau}),  tau = R1 C1
//
// A pack of s cells in series and p in parallel: voltages times s, resistances times s / p, the
// capacity times p, the RC pair's capacitance times p / s (its time constant unchanged).
//
// The battery keeps the books of its energy, every step: what the chemistry gave, the integral of
// OCV I; what the load got, the integral of V I; the heat in R0, I^2 R0, and in R1, V1^2 / R1; and
// what C1 holds, C1 V1^2 / 2 - so that chemistry = load + heat + stored, which a test checks. A
// negative current charges it (regeneration): the same books, the other way.
#include "math/Real.h"

#include <utility>
#include <vector>

namespace ke {

struct BatterySpec {
    // The open-circuit voltage of one cell against its state of charge (0..1), ascending SoC,
    // interpolated linearly.
    std::vector<std::pair<Real, Real>> ocv;
    int series = 1, parallel = 1;
    Real capacityAh = 2.5_r;    // of one cell [A h]
    Real r0 = 0.03_r;           // of one cell [Ohm]
    Real r1 = 0.015_r;          // [Ohm]
    Real c1 = 2000.0_r;         // [F]: tau = r1 c1 = 30 s
    Real cutoff = 3.0_r;        // per cell [V]: below it under load the pack is spent
    Real initialSoc = 1.0_r;

    // A lithium-ion cell of the 18650 kind (NMC): 2.5 A h, 3.0 .. 4.2 V - a typical discharge curve.
    static BatterySpec liIon18650();
};

class Battery {
public:
    explicit Battery(BatterySpec spec);

    Real soc() const { return Real(soc_); }
    Real polarisation() const { return v1_; }                // V1 of the pack [V]
    Real openCircuitVoltage() const;                         // OCV(SoC) of the pack [V]
    Real seriesResistance() const;                           // R0 of the pack [Ohm]
    Real terminalVoltage(Real current) const;                // V at this current, now [V]
    // Empty: no charge left, or the voltage at the current it last gave fell under the cut-off - and
    // then it stays so, as a battery's protection latches.
    bool spent() const { return spent_; }

    // The current I [A] (> 0 out of the battery) held for h seconds.
    void step(Real current, Real h);

    // The books, since construction [J], summed in double.
    double chemicalEnergy() const { return chemical_; }      // integral of OCV I
    double deliveredEnergy() const { return delivered_; }    // integral of V I: what the load got
    double heatR0() const { return heatR0_; }
    double heatR1() const { return heatR1_; }
    double storedInC1() const;                               // C1 V1^2 / 2 now

    const BatterySpec& spec() const { return spec_; }

private:
    Real cellOcv(Real soc) const;
    Real packR1() const;
    Real packC1() const;

    BatterySpec spec_;
    double soc_ = 1;  // in double: hundreds of thousands of small steps (a float drifts 0.3 % over an hour at 1 C)
    Real v1_ = 0;
    bool spent_ = false;
    double chemical_ = 0, delivered_ = 0, heatR0_ = 0, heatR1_ = 0;
};

} // namespace ke
