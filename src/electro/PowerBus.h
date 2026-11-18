#pragma once
// A DC bus: a battery feeding H-bridge motor drivers and a constant load (a board). Each driver
// switches the bus onto its motor with a duty d in [-1, 1] - the sign is the direction - and the
// switching is averaged over its PWM period (the electrical time constant L / R of a small motor,
// ~1 ms, is far below the mechanical one): the motor sees d U, and by the power balance of ideal
// switches the bus gives d I_m for the motor's current I_m. The motor (GenericJoint's DC drive):
// I_m = (d U - K omega) / R, R the winding's and the switches' resistance together. The battery:
// U = E - R0 I_bus with E = OCV - V1 (Battery.h). Together, exactly:
//
//   U = (E + R0 (sum_m d_m K_m omega_m / R_m - I_c)) / (1 + R0 sum_m d_m^2 / R_m)
//
// A driver limits its current (a chopper): a motor whose |I_m| would pass its limit gets the duty
// that gives the limit, d = (K omega + R I_lim sign) / U, and the bus is solved again - a few passes,
// in a fixed order, so the same bits every time.
#include "electro/Battery.h"

#include <limits>
#include <vector>

namespace ke {

struct MotorLoad {
    Real duty = 0;              // [-1, 1] asked by the controller
    Real resistance = 1;        // winding + switches [Ohm]
    Real constant = 0.05_r;     // K [N m / A = V s / rad]
    Real speed = 0;             // the shaft's speed now [rad/s] (the joint's, relative)
    Real currentLimit = std::numeric_limits<Real>::infinity(); // [A]
};

struct BusState {
    Real voltage = 0;           // U [V]
    Real current = 0;           // out of the battery [A]
    std::vector<Real> duty;     // as applied (after the current limits)
    std::vector<Real> motorVoltage, motorCurrent; // d U and I_m of each
};

BusState solveBus(const Battery& battery, const std::vector<MotorLoad>& motors, Real constantCurrent);

} // namespace ke
