// The lidar (Lidar.h): the pattern of its rays and what a ray reads.
#include "devices/Lidar.h"

#include "math/ElementaryFunctions.h"
#include "math/Real.h"

#include <algorithm>
#include <cmath>

namespace ke {

namespace {

// The k-th of n angles over a field of `fov` degrees, in radians: a full turn with no ray twice, else edge to edge.
double angleOf(int k, int n, double fov) {
    if (n <= 1) return 0;
    const double step = fov >= 360 ? fov / n : fov / (n - 1);
    return (-fov / 2 + step * k) * double(kPi) / 180;
}

} // namespace

std::vector<Vector3> Lidar::directions(const LidarSpec& spec) {
    std::vector<Vector3> out;
    const int h = std::max(spec.raysH, 1), v = std::max(spec.raysV, 1);
    out.reserve(size_t(h) * size_t(v));
    for (int j = 0; j < v; ++j) {
        const double el = angleOf(j, v, std::min(spec.fovVDeg, 180.0));
        for (int i = 0; i < h; ++i) {
            const double az = angleOf(i, h, std::min(spec.fovHDeg, 360.0));
            out.push_back(Vector3(Real(ke::cos(el) * ke::cos(az)), Real(ke::cos(el) * ke::sin(az)), Real(ke::sin(el))));
        }
    }
    return out;
}

double Lidar::read(double range) {
    if (range < spec_.minM || range > spec_.maxM) return -1; // no surface (< 0) is below any minimum
    double d = range;
    if (noise_) d += random_.normal() * spec_.accuracyMm * 1e-3 / 3;
    return std::max(std::round(d * 1000) / 1000, 0.0);
}

int64_t Lidar::periodNs() const { return std::max<int64_t>(std::llround(1e9 / std::max(spec_.rateHz, 1e-3)), 1); }

} // namespace ke
