#include "viewer_state.hpp"

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <print>

int main() {
    CalibrationAccumulator acc;
    acc.reset();

    xxrf_viewer::AoASample a;
    a.sample_index = 100;
    a.theta_rad = 179.0f * (std::numbers::pi_v<float> / 180.0f);
    a.coherence = 0.9f;
    a.signal_power_dbfs = -40.0f;

    xxrf_viewer::AoASample b;
    b.sample_index = 140;
    b.theta_rad = -179.0f * (std::numbers::pi_v<float> / 180.0f);
    b.coherence = 0.7f;
    b.signal_power_dbfs = -38.0f;

    acc.add(a);
    acc.add(b);

    const MeasurementSummary summary = acc.summary();
    if (!summary.valid || summary.count != 2) {
        std::println(stderr, "unexpected invalid summary");
        return EXIT_FAILURE;
    }

    const double wrapped_abs = std::abs(std::abs(summary.theta_circular_avg_deg) - 180.0);
    if (wrapped_abs > 1.5) {
        std::println(stderr, "circular average unexpectedly moved away from wrap boundary: {}",
                     summary.theta_circular_avg_deg);
        return EXIT_FAILURE;
    }

    if (std::abs(summary.theta_avg_deg - summary.theta_circular_avg_deg) > 1e-9) {
        std::println(stderr, "theta_avg_deg no longer mirrors circular average");
        return EXIT_FAILURE;
    }

    if (summary.sample_index_first != 100 || summary.sample_index_last != 140) {
        std::println(stderr, "sample range mismatch: {}..{}", summary.sample_index_first, summary.sample_index_last);
        return EXIT_FAILURE;
    }

    if (std::abs(summary.coherence_avg - 0.8) > 1e-6) {
        std::println(stderr, "coherence average mismatch: {}", summary.coherence_avg);
        return EXIT_FAILURE;
    }

    if (std::abs(summary.power_avg_dbfs - (-39.0)) > 1e-6) {
        std::println(stderr, "power average mismatch: {}", summary.power_avg_dbfs);
        return EXIT_FAILURE;
    }

    acc.reset();
    if (acc.summary().valid) {
        std::println(stderr, "summary unexpectedly valid after reset");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
