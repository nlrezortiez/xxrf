#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <print>
#include <vector>
#include <xxrf/aoa/processor.hpp>

namespace {

std::vector<std::int8_t> to_iq_i8q8(const std::vector<std::complex<float>>& samples) {
    std::vector<std::int8_t> out;
    out.reserve(samples.size() * 2);
    for (const auto& s : samples) {
        const auto clamp_i8 = [](float v) {
            const int x = static_cast<int>(std::lround(std::clamp(v, -127.0f, 127.0f)));
            return static_cast<std::int8_t>(x);
        };
        out.push_back(clamp_i8(s.real()));
        out.push_back(clamp_i8(s.imag()));
    }
    return out;
}

xxrf::aoa::Config make_config() {
    xxrf::aoa::Config cfg;
    cfg.sample_rate_hz = 1'000'000.0;
    cfg.center_freq_hz = 80'000'000ULL;
    cfg.geom.baseline_m = 1.0;
    cfg.win.window_samples = 8;
    cfg.win.hop_samples = 1;
    cfg.win.sample_stride = 1;
    cfg.min_coherence = 0.0;
    cfg.signal_threshold_dbfs = -20.0;
    cfg.min_active_fraction = 0.5;
    cfg.phase_stability_subwindows = 4;
    cfg.max_phase_std_deg = 5.0;
    cfg.emit_below_quality = false;
    return cfg;
}

std::complex<float> polar_sample(float amp, float deg) {
    const float rad = deg * (std::numbers::pi_v<float> / 180.0f);
    return {amp * std::cos(rad), amp * std::sin(rad)};
}

} // namespace

int main() {
    auto cfg = make_config();
    auto proc_res = xxrf::aoa::Processor::create(cfg);
    if (!proc_res) {
        std::println(stderr, "Processor::create failed: {}", proc_res.error().message);
        return EXIT_FAILURE;
    }
    auto proc = std::move(*proc_res);

    std::vector<std::complex<float>> x0(8, polar_sample(64.0f, 0.0f));
    std::vector<std::complex<float>> x1(8, polar_sample(64.0f, 8.0f));
    auto iq0 = to_iq_i8q8(x0);
    auto iq1 = to_iq_i8q8(x1);

    std::size_t emitted = 0;
    proc.push({.first_sample_index = 0, .iq0_i8q8 = iq0, .iq1_i8q8 = iq1, .skew_samples = 0},
              xxrf::aoa::FunctionRef<void(const xxrf::aoa::Estimate&)>([&](const xxrf::aoa::Estimate&) { ++emitted; }));
    if (emitted != 1) {
        std::println(stderr, "stable window did not emit exactly one estimate: {}", emitted);
        return EXIT_FAILURE;
    }

    proc.reset();
    emitted = 0;
    x1.assign(8, polar_sample(64.0f, 0.0f));
    for (std::size_t i = 4; i < x1.size(); ++i) {
        x1[i] = polar_sample(64.0f, 14.0f);
    }
    iq1 = to_iq_i8q8(x1);
    proc.push({.first_sample_index = 100, .iq0_i8q8 = iq0, .iq1_i8q8 = iq1, .skew_samples = 0},
              xxrf::aoa::FunctionRef<void(const xxrf::aoa::Estimate&)>([&](const xxrf::aoa::Estimate&) { ++emitted; }));
    if (emitted != 0) {
        std::println(stderr, "unstable-phase window unexpectedly emitted {}", emitted);
        return EXIT_FAILURE;
    }

    const auto stats_after_phase = proc.stats();
    if (stats_after_phase.unstable_phase == 0 || stats_after_phase.below_quality == 0) {
        std::println(stderr, "phase instability counters were not incremented");
        return EXIT_FAILURE;
    }

    proc.reset();
    emitted = 0;
    x0.assign(8, {0.0f, 0.0f});
    x1.assign(8, {0.0f, 0.0f});
    for (std::size_t i = 0; i < 2; ++i) {
        x0[i] = polar_sample(64.0f, 0.0f);
        x1[i] = polar_sample(64.0f, 8.0f);
    }
    iq0 = to_iq_i8q8(x0);
    iq1 = to_iq_i8q8(x1);
    proc.push({.first_sample_index = 200, .iq0_i8q8 = iq0, .iq1_i8q8 = iq1, .skew_samples = 0},
              xxrf::aoa::FunctionRef<void(const xxrf::aoa::Estimate&)>([&](const xxrf::aoa::Estimate&) { ++emitted; }));
    if (emitted != 0) {
        std::println(stderr, "low-activity window unexpectedly emitted {}", emitted);
        return EXIT_FAILURE;
    }

    const auto stats_after_activity = proc.stats();
    if (stats_after_activity.below_activity == 0) {
        std::println(stderr, "activity rejection counter was not incremented");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
