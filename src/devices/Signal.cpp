// The stages of a datasheet's signal chain (Signal.h).
#include "devices/Signal.h"

#include "math/ElementaryFunctions.h"

#include <algorithm>
#include <cmath>

namespace ke {

SampleClock::SampleClock(double rateHz) : periodNs_(std::max<int64_t>(1, std::llround(1e9 / std::max(rateHz, 1e-3)))) {}

void Channel3::drawOffset(Random& r) {
    for (int k = 0; k < 3; ++k) offset[k] = Real(r.normal() * spec_.offsetSigma);
}

void Channel3::drawScale(Random& r) {
    for (int k = 0; k < 3; ++k) scale[k] = Real(1 + r.normal() * spec_.scaleSigma);
}

// The draws go x, y, z.
Vector3 Channel3::sample(const Vector3& truth, Random* noise) {
    const double f = spec_.rateHz, sigma = spec_.noiseSigma + spec_.noiseDensity * std::sqrt(f / 2);
    const double a = spec_.bandwidthHz > 0 ? 1 - ke::exp(-2 * 3.14159265358979323846 * spec_.bandwidthHz / f) : 1;
    saturated_ = false;
    Vector3 out;
    for (int k = 0; k < 3; ++k) {
        const double x = double(scale[k] * truth[k] + offset[k]) + (noise ? noise->normal() * sigma : 0);
        state_[k] += a * (x - state_[k]);
        double y = state_[k];
        if (spec_.step > 0) {
            const double top = spec_.range > 0 ? std::floor(spec_.range / spec_.step + 0.5) : 1e300, n = std::floor(y / spec_.step + 0.5);
            saturated_ = saturated_ || n >= top - 1 || n <= -top;
            y = std::clamp(n, -top, top - 1) * spec_.step;
        } else if (spec_.range > 0) {
            saturated_ = saturated_ || std::fabs(y) > spec_.range;
            y = std::clamp(y, -spec_.range, spec_.range);
        }
        out[k] = Real(y);
    }
    return out;
}

} // namespace ke
