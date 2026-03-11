#pragma once

#include "xxrf/aoa/types.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace xxrf::aoa {

inline constexpr double kSpeedOfLight = 299'792'458.0;

[[nodiscard]] inline double wavelength_m(std::uint64_t center_freq_hz) noexcept {
    if (center_freq_hz == 0) {
        return 0.0;
    }
    return kSpeedOfLight / static_cast<double>(center_freq_hz);
}

[[nodiscard]] inline double max_unambiguous_baseline_m(std::uint64_t center_freq_hz) noexcept {
    return wavelength_m(center_freq_hz) * 0.5;
}

struct PhaseToAngleResult final {
    double theta_rad = 0.0;   
    double azimuth_rad = 0.0; 
    bool ok = false;
};

[[nodiscard]] inline PhaseToAngleResult phase_to_angle(double phase_rad, const ArrayGeometry& g,
                                                       std::uint64_t center_freq_hz, bool clamp_sin) noexcept {
    PhaseToAngleResult out{};

    const double d = g.baseline_m;
    const double lambda = wavelength_m(center_freq_hz);

    if (!(d > 0.0) || !(lambda > 0.0)) {
        return out; 
    }

    
    const double denom = (2.0 * std::numbers::pi * d);
    const double sin_theta = (phase_rad * lambda) / denom;

    double s = sin_theta;
    if (std::abs(s) > 1.0) {
        if (!clamp_sin) {
            return out; 
        }
        s = std::copysign(1.0, s);
    }

    out.theta_rad = std::asin(s);
    out.azimuth_rad = g.baseline_azimuth_rad + out.theta_rad;
    out.ok = true;
    return out;
}

} 
