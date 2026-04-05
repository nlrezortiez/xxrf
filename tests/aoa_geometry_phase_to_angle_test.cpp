#include <cmath>
#include <cstdlib>
#include <numbers>
#include <print>
#include <xxrf/aoa/geometry.hpp>

namespace {

bool approx(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) <= eps;
}

}

int main() {
    using namespace xxrf::aoa;

    constexpr std::uint64_t freq_hz = 100'000'000ULL;
    ArrayGeometry geom;
    geom.baseline_m = 0.1;
    geom.baseline_azimuth_rad = 0.25;

    const double theta_rad = 30.0 * (std::numbers::pi / 180.0);
    const double lambda = wavelength_m(freq_hz);
    const double phase_rad = (2.0 * std::numbers::pi * geom.baseline_m / lambda) * std::sin(theta_rad);

    const auto ok = phase_to_angle(phase_rad, geom, freq_hz, false);
    if (!ok.ok) {
        std::println(stderr, "phase_to_angle unexpectedly rejected valid phase");
        return EXIT_FAILURE;
    }
    if (!approx(ok.theta_rad, theta_rad, 1e-9)) {
        std::println(stderr, "theta mismatch: got {}, expected {}", ok.theta_rad, theta_rad);
        return EXIT_FAILURE;
    }
    if (!approx(ok.azimuth_rad, geom.baseline_azimuth_rad + theta_rad, 1e-9)) {
        std::println(stderr, "azimuth mismatch: got {}, expected {}", ok.azimuth_rad,
                     geom.baseline_azimuth_rad + theta_rad);
        return EXIT_FAILURE;
    }

    geom.baseline_m = 0.0;
    if (phase_to_angle(phase_rad, geom, freq_hz, false).ok) {
        std::println(stderr, "phase_to_angle unexpectedly accepted zero baseline");
        return EXIT_FAILURE;
    }

    geom.baseline_m = 0.1;
    const double impossible_phase = phase_rad * 4.0;
    if (phase_to_angle(impossible_phase, geom, freq_hz, false).ok) {
        std::println(stderr, "phase_to_angle unexpectedly accepted out-of-range phase without clamp");
        return EXIT_FAILURE;
    }

    const auto clamped = phase_to_angle(impossible_phase, geom, freq_hz, true);
    if (!clamped.ok) {
        std::println(stderr, "phase_to_angle unexpectedly rejected out-of-range phase with clamp");
        return EXIT_FAILURE;
    }
    if (!approx(clamped.theta_rad, std::numbers::pi / 2.0, 1e-9)) {
        std::println(stderr, "phase_to_angle clamp did not saturate to +90 degrees: {}", clamped.theta_rad);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
