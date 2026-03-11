#include <cstdlib>
#include <exception>
#include <print>
#include <stdexcept>
#include <vector>
#include <xxrf/aoa/processor.hpp>

int main() {
    xxrf::aoa::Config cfg;
    cfg.sample_rate_hz = 1'000'000.0;
    cfg.center_freq_hz = 100'000'000ULL;
    cfg.geom.baseline_m = 0.1;
    cfg.win.window_samples = 4;
    cfg.win.hop_samples = 1;
    cfg.win.sample_stride = 1;
    cfg.min_coherence = 0.0;
    cfg.emit_below_quality = true;

    auto procr = xxrf::aoa::Processor::create(cfg);
    if (!procr) {
        std::println(stderr, "Processor::create failed: {}", procr.error().message);
        return EXIT_FAILURE;
    }

    auto proc = std::move(*procr);

    const std::vector<std::int8_t> iq = {
        64, 0,
        64, 0,
        64, 0,
        64, 0,
    };

    bool threw = false;
    try {
        proc.push(xxrf::aoa::InputFrameView{
                      .first_sample_index = 0,
                      .iq0_i8q8 = iq,
                      .iq1_i8q8 = iq,
                      .skew_samples = 0,
                  },
                  [&](const xxrf::aoa::Estimate&) {
                      throw std::runtime_error("expected regression exception");
                  });
    } catch (const std::runtime_error&) {
        threw = true;
    }

    if (!threw) {
        std::println(stderr, "Processor::push did not propagate callback exception");
        return EXIT_FAILURE;
    }

    const auto st = proc.stats();
    if (st.estimates_emitted != 0) {
        std::println(stderr, "Processor::stats overcounted emitted estimates after throwing callback: {}",
                     st.estimates_emitted);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
