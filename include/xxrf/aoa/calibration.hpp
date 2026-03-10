#pragma once

#include <complex>

namespace xxrf::aoa {

// Minimal inter-channel calibration: multiply channel-1 by this complex gain before estimation.
// You can expand this later to frequency-dependent LUT.
struct Calibration final {
    std::complex<float> ch1_gain = {1.0f, 0.0f}; // apply: x1_corrected = x1 * ch1_gain
};

} // namespace xxrf::aoa