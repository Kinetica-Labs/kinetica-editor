#pragma once
// An HC-SR04 ultrasonic range finder (the ElecFreaks / Cytron datasheet): a 10 us pulse on TRIG starts a
// measurement; the module sends 8 cycles of 40 kHz and then holds ECHO high for the sound's round trip
// to the nearest echo, 2 D / c - so a host's distance is the pulse's width times c / 2. Range 2 cm to
// 4 m, beam 15 degrees, accuracy 3 mm (taken as 3 sigma of a normal jitter of the trip when `noise`),
// a cycle of at least 60 ms between triggers. With no echo in range ECHO falls after 38 ms (the timeout
// common modules show; the datasheet gives none).
//
// The echo (Kuc and Siegel, IEEE PAMI 9, 1987; Leonard and Durrant-Whyte, IEEE Trans. Robotics and
// Automation 7, 1991): sound reflects specularly off a surface smooth at its 8.6 mm wavelength, so a
// surface sends an echo back only where its normal lies inside the beam, and the echo comes from the
// foot of the perpendicular. For a hit at distance t along a ray d with the normal n the range is the
// distance to the hit's tangent plane, t (-d . n), when the angle between the beam's axis and -n is at
// most the beam's half angle - exact for a plane, the local plane for a curved surface; the nearest
// such over 17 rays filling the cone (the axis, and 8 each at the half angle and half of it). A wall
// turned more than 7.5 degrees from the beam returns nothing: the sonar's mirror blind spot. Diffuse
// scattering off rough surfaces, diffraction at edges and multiple echoes are not modelled.
//
// The speed of sound in air, c = 331.3 sqrt(1 + T / 273.15) m/s (343.2 at 20 deg C).
#include "core/Random.h"
#include "devices/PinEdge.h"
#include "math/Quaternion.h"

#include <functional>
#include <vector>

namespace ke {

class Hcsr04 {
public:
    // A ray from `origin` along the unit `dir` up to maxT: the nearest hit's distance t and its normal.
    using Cast = std::function<bool(const Vector3& origin, const Vector3& dir, Real maxT, Real& t, Vector3& normal)>;

    static constexpr int64_t kCycleNs = 60000000;   // the datasheet's measurement cycle
    static constexpr int64_t kTriggerNs = 10000;    // TRIG high
    static constexpr int64_t kBurstNs = 200000;     // 8 cycles of 40 kHz, then ECHO rises
    static constexpr int64_t kTimeoutNs = 38000000; // ECHO high with no echo
    static constexpr double kMin = 0.02, kMax = 4.0; // m
    static constexpr double kHalfAngle = 7.5 * 3.14159265358979323846 / 180;
    static constexpr double kSigma = 0.001;         // m, of the range

    explicit Hcsr04(uint32_t seed = 1, bool noise = false) : random_(seed), noise_(noise) {}

    // The range of the echo a beam from `origin` along the x axis of `turn` gets back (-1: none in range).
    static double echoRange(const Vector3& origin, const Quaternion& turn, const Cast& cast);
    // One measurement, the trigger rising at t, of an echo from `range` (< 0: none): the TRIG and ECHO
    // edges into `out`; returns the distance a host reads from the pulse at the speed of sound in air
    // at `celsius` (-1: the echo timed out).
    double measure(int64_t t, double range, double celsius, std::vector<PinEdge>& out);
    static double soundSpeed(double celsius);

private:
    Random random_;
    bool noise_;
};

} // namespace ke
