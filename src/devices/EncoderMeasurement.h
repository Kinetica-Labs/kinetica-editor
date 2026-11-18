#pragma once
// Decoded incremental encoder observations, never the solver's continuous coordinate. A count
// becomes an absolute joint coordinate only after an explicit count-zero reference is configured.
// Resolution is one quadrature count in SI units; it is not an accuracy specification.
#include "math/Real.h"

#include <cstdint>
#include <string>

namespace ke {

struct EncoderMeasurement {
    uint32_t sensor = 0, joint = 0;
    int dof = 0;
    int64_t count = 0, timestampNs = 0;
    Real position = 0, velocity = 0, resolution = 0;
    bool valid = false, referenced = false, velocityValid = false;
    std::string problem;
};

} // namespace ke
