#pragma once

#include "telemetry.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>
#include <xxrf/xxrf.hpp>

struct LiveWindowSample final {
    std::uint64_t t_ns = 0;
    std::uint64_t sample_index = 0;
    float theta_deg = 0.0f;
    float coherence = 0.0f;
    float power_dbfs = -120.0f;
};

struct MeasurementSummary final {
    bool valid = false;
    std::uint64_t count = 0;

    double theta_avg_deg = 0.0;
    double theta_circular_avg_deg = 0.0;
    double theta_min_deg = 0.0;
    double theta_max_deg = 0.0;

    double coherence_avg = 0.0;
    double coherence_min = 0.0;
    double coherence_max = 0.0;

    double power_avg_dbfs = 0.0;
    double power_min_dbfs = 0.0;
    double power_max_dbfs = 0.0;

    std::uint64_t sample_index_first = 0;
    std::uint64_t sample_index_last = 0;
};

struct CalibrationAccumulator final {
    std::uint64_t count = 0;
    double sum_sin = 0.0;
    double sum_cos = 0.0;
    double theta_min_deg = std::numeric_limits<double>::infinity();
    double theta_max_deg = -std::numeric_limits<double>::infinity();
    double sum_coherence = 0.0;
    double coherence_min = std::numeric_limits<double>::infinity();
    double coherence_max = -std::numeric_limits<double>::infinity();
    double sum_power_dbfs = 0.0;
    double power_min_dbfs = std::numeric_limits<double>::infinity();
    double power_max_dbfs = -std::numeric_limits<double>::infinity();
    std::uint64_t sample_index_first = 0;
    std::uint64_t sample_index_last = 0;
    bool have_sample_range = false;

    void reset() noexcept {
        *this = {};
        theta_min_deg = std::numeric_limits<double>::infinity();
        theta_max_deg = -std::numeric_limits<double>::infinity();
        coherence_min = std::numeric_limits<double>::infinity();
        coherence_max = -std::numeric_limits<double>::infinity();
        power_min_dbfs = std::numeric_limits<double>::infinity();
        power_max_dbfs = -std::numeric_limits<double>::infinity();
    }

    void add(const xxrf_viewer::AoASample& sample) noexcept {
        const double theta_deg = static_cast<double>(sample.theta_rad) * (180.0 / std::numbers::pi);
        if (count == 0) {
            sample_index_first = sample.sample_index;
            have_sample_range = true;
        }
        sample_index_last = sample.sample_index;
        ++count;
        sum_sin += std::sin(sample.theta_rad);
        sum_cos += std::cos(sample.theta_rad);
        theta_min_deg = std::min(theta_min_deg, theta_deg);
        theta_max_deg = std::max(theta_max_deg, theta_deg);
        sum_coherence += sample.coherence;
        coherence_min = std::min(coherence_min, static_cast<double>(sample.coherence));
        coherence_max = std::max(coherence_max, static_cast<double>(sample.coherence));
        sum_power_dbfs += sample.signal_power_dbfs;
        power_min_dbfs = std::min(power_min_dbfs, static_cast<double>(sample.signal_power_dbfs));
        power_max_dbfs = std::max(power_max_dbfs, static_cast<double>(sample.signal_power_dbfs));
    }

    MeasurementSummary summary() const noexcept {
        MeasurementSummary out;
        if (count == 0) {
            return out;
        }

        out.valid = true;
        out.count = count;
        out.theta_circular_avg_deg = std::atan2(sum_sin, sum_cos) * (180.0 / std::numbers::pi);
        out.theta_avg_deg = out.theta_circular_avg_deg;
        out.theta_min_deg = theta_min_deg;
        out.theta_max_deg = theta_max_deg;
        out.coherence_avg = sum_coherence / static_cast<double>(count);
        out.coherence_min = coherence_min;
        out.coherence_max = coherence_max;
        out.power_avg_dbfs = sum_power_dbfs / static_cast<double>(count);
        out.power_min_dbfs = power_min_dbfs;
        out.power_max_dbfs = power_max_dbfs;
        out.sample_index_first = sample_index_first;
        out.sample_index_last = sample_index_last;
        return out;
    }
};

enum class AllowedAoASide : std::uint8_t {
    Both = 0,
    Positive = 1,
    Negative = 2,
};

inline bool is_theta_side_allowed(float theta_rad, AllowedAoASide side) noexcept {
    switch (side) {
    case AllowedAoASide::Both:
        return true;
    case AllowedAoASide::Positive:
        return theta_rad >= 0.0f;
    case AllowedAoASide::Negative:
        return theta_rad <= 0.0f;
    }
    return true;
}

struct ViewerState final {
    static constexpr std::size_t history_size = 180;
    static constexpr std::uint64_t stale_fix_timeout_ns = 750'000'000ULL;

    std::optional<xxrf::aoa::rt::Stream> stream;
    xxrf_viewer::SpscRing<4096> q;

    bool has_fix = false;
    xxrf_viewer::AoASample last{};
    bool has_valid_fix = false;
    xxrf_viewer::AoASample last_valid{};

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
    double min_active_fraction = 0.15;
    int phase_stability_subwindows = 4;
    double max_phase_std_deg = 20.0;
    AllowedAoASide allowed_side = AllowedAoASide::Both;
    double smooth_tau_s = 0.20;
    float signal_threshold_dbfs = -55.0f;
    float signal_threshold_hysteresis_db = 2.0f;
    bool signal_gate_open = false;
    double cal_phase_deg = 5.514;

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
    double rolling_window_s = 5.0;
    std::deque<LiveWindowSample> rolling_samples;
    std::array<float, history_size> theta_history{};
    std::array<float, history_size> coherence_history{};
    std::array<float, history_size> power_history{};
    std::size_t theta_head = 0;
    std::size_t theta_count = 0;
    std::size_t coherence_head = 0;
    std::size_t coherence_count = 0;
    std::size_t power_head = 0;
    std::size_t power_count = 0;

    bool calibration_running = false;
    std::uint64_t calibration_started_ns = 0;
    double calibration_duration_s = 15.0;
    CalibrationAccumulator calibration_acc{};
    MeasurementSummary calibration_last{};
};

template <std::size_t N>
inline void push_history_sample(std::array<float, N>& values, std::size_t& head, std::size_t& count, float sample) {
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
