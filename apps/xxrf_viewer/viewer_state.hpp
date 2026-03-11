#pragma once

#include "telemetry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <xxrf/xxrf.hpp>

struct ViewerState final {
    static constexpr std::size_t history_size = 180;
    static constexpr std::uint64_t stale_fix_timeout_ns = 750'000'000ULL;

    std::optional<xxrf::aoa::rt::Stream> stream;
    xxrf_viewer::SpscRing<4096> q;

    bool has_fix = false;
    xxrf_viewer::AoASample last{};

    bool smooth_init = false;
    float sx = 0.0f;
    float sy = 1.0f;
    std::uint64_t last_t_ns = 0;

    std::vector<std::string> serials;
    int idx_out = 0;
    int idx_in = 0;

    double center_freq_mhz = 433.92;
    double sample_rate_msps = 5.0;
    double baseline_m = 0.15;

    int window_samples = 8192;
    int hop_samples = 2048;
    int sample_stride = 32;

    double min_coherence = 0.20;
    double smooth_tau_s = 0.20;

    int lna_gain = 16;
    int vga_gain = 16;
    bool amp_enable = false;

    bool hw_trigger = true;
    bool clkout_on_master = true;
    bool require_clkin_on_slave = true;
    int arm_delay_ms = 50;

    std::uint64_t emitted_estimates = 0;
    std::string last_error;
    bool show_config = true;
    bool show_diagnostics = false;
    std::array<float, history_size> theta_history{};
    std::array<float, history_size> coherence_history{};
    std::size_t theta_head = 0;
    std::size_t theta_count = 0;
    std::size_t coherence_head = 0;
    std::size_t coherence_count = 0;
};

template <std::size_t N>
inline void push_history_sample(std::array<float, N>& values, std::size_t& head, std::size_t& count,
                                float sample) {
    values[head] = sample;
    head = (head + 1U) % N;
    if (count < N) {
        ++count;
    }
}

template <std::size_t N>
inline std::size_t build_history_plot(const std::array<float, N>& values, std::size_t head, std::size_t count,
                                      std::array<float, N>& plot) {
    if (count == 0) {
        plot[0] = 0.0f;
        return 1;
    }

    const std::size_t start = (count == N) ? head : 0U;
    for (std::size_t i = 0; i < count; ++i) {
        plot[i] = values[(start + i) % N];
    }
    return count;
}
