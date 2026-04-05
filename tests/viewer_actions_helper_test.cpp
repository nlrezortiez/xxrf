#include "viewer_actions.hpp"

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <print>
#include <xxrf/aoa/geometry.hpp>

int main() {
    if (theta_bias_to_phase_deg(0.0, 80.0, 0.15) != 0.0) {
        std::println(stderr, "zero theta bias should map to zero phase bias");
        return EXIT_FAILURE;
    }

    const double theta_deg = -22.5;
    const double phase_deg = theta_bias_to_phase_deg(theta_deg, 80.0, 0.15);
    const double phase_rad = phase_deg * (std::numbers::pi / 180.0);

    xxrf::aoa::ArrayGeometry geom;
    geom.baseline_m = 0.15;
    const auto mapped = xxrf::aoa::phase_to_angle(phase_rad, geom, 80'000'000ULL, false);
    if (!mapped.ok) {
        std::println(stderr, "phase bias did not map back to a valid angle");
        return EXIT_FAILURE;
    }

    const double mapped_theta_deg = mapped.theta_rad * (180.0 / std::numbers::pi);
    if (std::abs(mapped_theta_deg - theta_deg) > 1e-9) {
        std::println(stderr, "phase/angle mapping mismatch: got {}, expected {}", mapped_theta_deg, theta_deg);
        return EXIT_FAILURE;
    }

    ViewerState vs;
    vs.has_fix = true;
    vs.has_valid_fix = true;
    vs.last.sample_index = 123;
    vs.last_valid.sample_index = 123;
    vs.signal_gate_open = true;
    vs.emitted_estimates = 7;
    vs.calibration_running = true;
    vs.calibration_last.valid = true;
    push_history_sample(vs.theta_history, vs.theta_head, vs.theta_count, 1.0f);
    push_history_sample(vs.coherence_history, vs.coherence_head, vs.coherence_count, 0.9f);
    push_history_sample(vs.power_history, vs.power_head, vs.power_count, -40.0f);

    stop_stream(vs);

    if (vs.has_fix || vs.has_valid_fix || vs.signal_gate_open) {
        std::println(stderr, "stop_stream did not clear live flags");
        return EXIT_FAILURE;
    }
    if (vs.emitted_estimates != 0) {
        std::println(stderr, "stop_stream did not reset emitted estimates");
        return EXIT_FAILURE;
    }
    if (vs.theta_count != 0 || vs.coherence_count != 0 || vs.power_count != 0) {
        std::println(stderr, "stop_stream did not reset histories");
        return EXIT_FAILURE;
    }
    if (vs.calibration_running || vs.calibration_last.valid) {
        std::println(stderr, "stop_stream did not clear calibration state");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
