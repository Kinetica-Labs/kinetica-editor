// The electrical side of mechatronics (src/electro) against exact answers: a battery discharged at a
// constant current lasts Q / I and starts at OCV - R0 I; its polarisation follows R1 I (1 - e^{-t/tau})
// to the last bit whatever the step; its energy books balance (chemistry = load + heat + stored) for
// any current, charging too; a pack of cells scales as s in series, p in parallel.
#include "TestRunner.h"

#include "electro/Battery.h"

#include <cmath>
#include <cstdio>

// One 2.5 A h cell at 1 C (2.5 A): empty after Q / I = 3600 s by Coulomb counting; at the first
// instant the terminal voltage is OCV(1) - R0 I (the polarisation has not built up yet); spent when
// the voltage under load falls under the 3.0 V cut-off.
void testBatteryConstantDischarge() {
    Battery b(BatterySpec::liIon18650());
    const Real I = 2.5_r, first = b.terminalVoltage(I);
    double t = 0, spentAt = 0;
    const double h = 0.01;
    while (b.soc() > 0 && t < 4000) {
        b.step(I, Real(h));
        t += h;
        if (b.spent() && spentAt == 0) spentAt = t;
    }
    std::printf("  1 C discharge of a 2.5 A h cell: empty after %.2f s (Q / I = 3600 s), first voltage %.4f V (OCV - R0 I = %.4f), "
                "spent at %.1f s under load\n", t, double(first), 4.2 - 0.03 * 2.5, spentAt);
    CHECK(std::fabs(t - 3600) < 2 * h, "the cell lasted %f s, not Q / I = 3600 s", t);
    CHECK(std::fabs(double(first) - (4.2 - 0.075)) < 1e-5, "the first voltage %f is not OCV - R0 I", double(first));
    CHECK(spentAt > 3000 && spentAt <= t, "the cut-off came at %f s", spentAt);
}

// A current step from rest: V1(t) = R1 I (1 - e^{-t/tau}), tau = R1 C1 = 30 s - exact at any step,
// since a step holds the current and V1 is advanced by the exact exponential. Then the load goes and
// V1 relaxes as e^{-t/tau}.
void testBatteryPolarisation() {
    double worst = 0;
    for (double h : {0.001, 0.1, 5.0}) {
        Battery b(BatterySpec::liIon18650());
        const double I = 2, R1 = 0.015, tau = 30;
        double t = 0;
        while (t < 30 - 1e-9) b.step(Real(I), Real(h)), t += h;
        worst = std::max(worst, std::fabs(double(b.polarisation()) - R1 * I * (1 - std::exp(-t / tau))) / (R1 * I));
        const double charged = double(b.polarisation());
        for (double s = 0; s < 60 - 1e-9; s += h) b.step(0, Real(h));
        worst = std::max(worst, std::fabs(double(b.polarisation()) - charged * std::exp(-60 / tau)) / (R1 * I));
    }
    std::printf("  polarisation after a current step and its relaxation, steps 1 ms .. 5 s: worst error %.2g of R1 I\n", worst);
    CHECK(worst < 1e-5, "the polarisation is off the exponential by %g of R1 I", worst);
}

// The energy books for a current that swings both ways (discharging and charging) over ten minutes:
// what the chemistry gave = what the load got + the heat in R0 and R1 + what C1 holds.
void testBatteryEnergyBooks() {
    Battery b(BatterySpec::liIon18650());
    for (int k = 0; k < 60000; ++k) {
        const double t = k * 0.01, I = 3 * std::sin(0.05 * t) + 0.5 * std::sin(1.7 * t);
        b.step(Real(I), 0.01_r);
    }
    const double out = b.deliveredEnergy() + b.heatR0() + b.heatR1() + b.storedInC1(), in = b.chemicalEnergy();
    std::printf("  books after 10 min of a swinging current: chemistry %.6f J = load %.6f + R0 %.6f + R1 %.6f + C1 %.6f (mismatch %.2g J)\n",
                in, b.deliveredEnergy(), b.heatR0(), b.heatR1(), b.storedInC1(), out - in);
    CHECK(std::fabs(out - in) < 1e-6 * (std::fabs(in) + b.heatR0()), "the books do not balance: %g J", out - in);
}

// A pack of 3 cells in series and 2 in parallel: three times the voltage, R0 times 3 / 2, twice the
// charge - so 2.5 A from it lasts twice as long as from one cell, at three times the voltage.
void testBatteryPack() {
    BatterySpec spec = BatterySpec::liIon18650();
    spec.series = 3, spec.parallel = 2;
    Battery pack(spec);
    double t = 0;
    while (pack.soc() > 0 && t < 9000) pack.step(2.5_r, 0.5_r), t += 0.5;
    Battery fresh(spec);
    std::printf("  3S2P pack: OCV %.3f V (3 x 4.2), R0 %.4f Ohm (0.03 x 3 / 2), 2.5 A for %.0f s (2 x 3600)\n",
                double(fresh.openCircuitVoltage()), double(fresh.seriesResistance()), t);
    CHECK(std::fabs(double(fresh.openCircuitVoltage()) - 12.6) < 1e-5 && std::fabs(double(fresh.seriesResistance()) - 0.045) < 1e-7,
          "the pack's voltage or resistance does not scale");
    CHECK(std::fabs(t - 7200) < 1.0, "the pack lasted %f s, not 7200 s", t);
}
