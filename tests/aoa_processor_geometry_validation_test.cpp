#include <cstdlib>
#include <print>
#include <string>
#include <xxrf/aoa/geometry.hpp>
#include <xxrf/aoa/processor.hpp>

static xxrf::aoa::Config make_valid_config() {
    xxrf::aoa::Config cfg;
    cfg.sample_rate_hz = 1'000'000.0;
    cfg.center_freq_hz = 100'000'000ULL;
    cfg.geom.baseline_m = 0.1;
    cfg.win.window_samples = 4;
    cfg.win.hop_samples = 1;
    cfg.win.sample_stride = 1;
    cfg.min_coherence = 0.0;
    return cfg;
}

int main() {
    auto cfg = make_valid_config();

    cfg.geom.baseline_m = xxrf::aoa::max_unambiguous_baseline_m(cfg.center_freq_hz) + 1e-6;
    auto too_wide = xxrf::aoa::Processor::create(cfg);
    if (too_wide) {
        std::println(stderr, "Processor::create unexpectedly accepted ambiguous baseline");
        return EXIT_FAILURE;
    }

    if (too_wide.error().message.find("lambda/2") == std::string::npos) {
        std::println(stderr, "Processor::create error message does not explain lambda/2 constraint: {}",
                     too_wide.error().message);
        return EXIT_FAILURE;
    }

    cfg = make_valid_config();
    cfg.geom.baseline_m = xxrf::aoa::max_unambiguous_baseline_m(cfg.center_freq_hz);
    auto exact_limit = xxrf::aoa::Processor::create(cfg);
    if (!exact_limit) {
        std::println(stderr, "Processor::create rejected baseline at lambda/2 limit: {}", exact_limit.error().message);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
