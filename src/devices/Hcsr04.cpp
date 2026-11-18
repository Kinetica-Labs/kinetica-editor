// The HC-SR04 (Hcsr04.h): the specular echo of a cone of rays, and one measurement's TRIG and ECHO
// pulses with the round trip in whole nanoseconds.
#include "devices/Hcsr04.h"

#include "math/ElementaryFunctions.h"

#include <cmath>

namespace ke {

double Hcsr04::soundSpeed(double celsius) { return 331.3 * std::sqrt(1 + celsius / 273.15); }

// The axis, then rings of 8 rays at half the half angle and at the half angle, each ray d in the
// sensor's frame (cos a, sin a cos b, sin a sin b) turned into the world.
double Hcsr04::echoRange(const Vector3& origin, const Quaternion& turn, const Cast& cast) {
    const Vector3 axis = turn.rotate(Vector3(1, 0, 0));
    const double cosBeam = ke::cos(kHalfAngle);
    double best = -1;
    for (int ray = 0; ray < 17; ++ray) {
        const double a = ray == 0 ? 0 : (ray <= 8 ? 0.5 : 1.0) * kHalfAngle, b = double((ray - 1) % 8) * 3.14159265358979323846 / 4;
        const Vector3 d = turn.rotate(Vector3(Real(ke::cos(a)), Real(ke::sin(a) * ke::cos(b)), Real(ke::sin(a) * ke::sin(b))));
        Real t = 0;
        Vector3 n(0.0_r);
        if (!cast(origin, d, Real(kMax / cosBeam), t, n) || -double(dot(axis, n)) < cosBeam) continue; // no hit, or a mirror turned away
        const double range = double(t) * -double(dot(d, n));
        if (range >= kMin && range <= kMax && (best < 0 || range < best)) best = range;
    }
    return best;
}

double Hcsr04::measure(int64_t t, double range, double celsius, std::vector<PinEdge>& out) {
    const int64_t rise = t + kTriggerNs + kBurstNs;
    out.push_back({t, 'T', true});
    out.push_back({t + kTriggerNs, 'T', false});
    out.push_back({rise, 'E', true});
    if (range < 0) {
        out.push_back({rise + kTimeoutNs, 'E', false});
        return -1;
    }
    const double c = soundSpeed(celsius);
    double trip = 2 * range / c;
    if (noise_) trip += random_.normal() * kSigma * 2 / c;
    const int64_t width = std::llround(trip * 1e9);
    out.push_back({rise + width, 'E', false});
    return double(width) * 1e-9 * c / 2;
}

} // namespace ke
