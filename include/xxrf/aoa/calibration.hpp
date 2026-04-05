#pragma once

#include <complex>

namespace xxrf::aoa {

struct Calibration final {
    std::complex<float> ch1_gain = {1.0f, 0.0f};
};

} // namespace xxrf::aoa