#pragma once
// A lidar as its datasheet describes it (LidarSpec; a model's numbers come from its row of data/sensors/lidar.csv,
// scene/SensorSpec.h): a pattern of rays sent out of the sensor at once - one beam, a ring, a sphere, a narrow grid -
// set by how many rays and how wide a field across each axis, and a scan of them `rate_hz` times a second. Each ray
// reads the distance to the first surface it meets, at any angle to it (light scatters off matte surfaces), from
// min to max; the stated accuracy is 3 sigma of a normal error, the distance then rounded to the millimetre. A ray
// with no surface in range reads -1. Mirrors, glass, a weak return at a grazing angle and the time a rotating head
// takes over one scan are not modelled: the whole scan is taken at one instant.
// Where the rays meet surfaces is the physics' - the probe of the running world (sim/LiveSensors.cpp) casts them.
//
// The sensor's axes: x the middle of the field, a ray's azimuth turned about z toward +y, its elevation toward +z;
// the rays in layers from the lowest, each from the most negative azimuth. A full turn (360 degrees) spreads its rays
// over the turn with no ray twice; a narrower field has its first and last ray on its edges.
#include "core/Random.h"
#include "math/Vector3.h"

#include <vector>

namespace ke {

struct LidarSpec {
    int raysH = 1, raysV = 1;
    double fovHDeg = 0, fovVDeg = 0;
    double minM = 0.05, maxM = 12;
    double accuracyMm = 30; // 3 sigma
    double rateHz = 10;     // scans a second
};

class Lidar {
public:
    explicit Lidar(const LidarSpec& spec = LidarSpec(), uint32_t seed = 1, bool noise = false) : spec_(spec), random_(seed), noise_(noise) {}

    static std::vector<Vector3> directions(const LidarSpec& spec); // unit vectors in the sensor's axes, in the rays' order
    double read(double range);                                      // a ray's true distance (< 0: no surface) as it reads
    int64_t periodNs() const;                                       // between two scans
    const LidarSpec& spec() const { return spec_; }

private:
    LidarSpec spec_;
    Random random_;
    bool noise_;
};

} // namespace ke
