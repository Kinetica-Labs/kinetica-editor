// The sonar (Sonar.h): one measurement's TRIG and ECHO pulses with the round trip in whole nanoseconds.
#include "devices/Sonar.h"

#include <cmath>

namespace ke {

double Sonar::soundSpeed(double celsius) { return 331.3 * std::sqrt(1 + celsius / 273.15); }

double Sonar::measure(int64_t t, double range, double celsius, std::vector<PinEdge>& out) {
    const int64_t rise = t + kTriggerNs + kBurstNs;
    out.push_back({t, 'T', true});
    out.push_back({t + kTriggerNs, 'T', false});
    out.push_back({rise, 'E', true});
    if (range < 0) {
        out.push_back({rise + int64_t(spec_.timeoutMs * 1e6), 'E', false});
        return -1;
    }
    const double c = soundSpeed(celsius);
    double trip = 2 * range / c;
    if (noise_) trip += random_.normal() * spec_.accuracyMm * 1e-3 / 3 * 2 / c;
    const int64_t width = std::llround(trip * 1e9);
    out.push_back({rise + width, 'E', false});
    return double(width) * 1e-9 * c / 2;
}

} // namespace ke
