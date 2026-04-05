#include "viewer_actions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <numbers>

std::uint64_t now_monotonic_ns() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (std::uint64_t(ts.tv_sec) * 1'000'000'000ULL) + std::uint64_t(ts.tv_nsec);
}

std::vector<std::string> enumerate_hackrf_serials() {
    std::vector<std::string> out;
    auto list = xxrf::core::DeviceList::enumerate();
    if (!list) {
        return out;
    }

    for (const auto& dev : list->devices()) {
        if (!dev.serial.empty()) {
            out.push_back(dev.serial);
        }
    }
    return out;
}

static void clear_measurement_state(ViewerState& vs) noexcept {
    vs.has_fix = false;
    vs.last = {};
    vs.has_valid_fix = false;
    vs.last_valid = {};
    vs.smooth_init = false;
    vs.sx = 0.0f;
    vs.sy = 1.0f;
    vs.last_t_ns = 0;
    vs.emitted_estimates = 0;
    vs.signal_gate_open = false;
    vs.rolling_samples.clear();
    vs.theta_head = 0;
    vs.theta_count = 0;
    vs.coherence_head = 0;
    vs.coherence_count = 0;
    vs.power_head = 0;
    vs.power_count = 0;
    vs.calibration_last = {};
    if (vs.calibration_running) {
        vs.calibration_running = false;
        vs.calibration_acc.reset();
    }
}

static float signal_power_to_dbfs(double mean_p0, double mean_p1) noexcept {
    const double limiting_channel_power = std::max(1e-12, std::min(mean_p0, mean_p1));
    return static_cast<float>(10.0 * std::log10(limiting_channel_power));
}

double theta_bias_to_phase_deg(double theta_deg, double center_freq_mhz, double baseline_m) noexcept {
    if (!(center_freq_mhz > 0.0) || !(baseline_m > 0.0)) {
        return 0.0;
    }
    constexpr double c = 299'792'458.0;
    const double lambda = c / (center_freq_mhz * 1e6);
    if (!(lambda > 0.0)) {
        return 0.0;
    }
    const double phase_rad =
        (2.0 * std::numbers::pi * baseline_m / lambda) * std::sin(theta_deg * (std::numbers::pi / 180.0));
    return phase_rad * (180.0 / std::numbers::pi);
}

static xxrf::core::Result<xxrf::aoa::Processor> make_processor(const ViewerState& s) noexcept {
    xxrf::aoa::Config cfg;
    cfg.method = xxrf::aoa::Method::PhaseInterferometry;

    cfg.center_freq_hz = static_cast<std::uint64_t>((s.center_freq_mhz * 1e6) + 0.5);
    cfg.sample_rate_hz = s.sample_rate_msps * 1e6;

    cfg.geom.baseline_m = s.baseline_m;
    cfg.geom.baseline_azimuth_rad = 0.0;

    cfg.win.window_samples = static_cast<std::size_t>(std::max(1, s.window_samples));
    cfg.win.hop_samples = static_cast<std::size_t>(std::max(1, s.hop_samples));
    cfg.win.sample_stride = static_cast<std::size_t>(std::max(1, s.sample_stride));

    cfg.min_coherence = s.min_coherence;
    cfg.signal_threshold_dbfs = s.signal_threshold_dbfs;
    cfg.min_active_fraction = s.min_active_fraction;
    cfg.phase_stability_subwindows = static_cast<std::size_t>(std::max(1, s.phase_stability_subwindows));
    cfg.max_phase_std_deg = s.max_phase_std_deg;
    cfg.clamp_sin = false;
    cfg.require_contiguous = false;
    cfg.apply_calibration = true;
    cfg.emit_below_quality = true;

    xxrf::aoa::Calibration cal{};
    const float cal_phase_rad = static_cast<float>(s.cal_phase_deg * (std::numbers::pi / 180.0));
    cal.ch1_gain = {std::cos(cal_phase_rad), std::sin(cal_phase_rad)};

    return xxrf::aoa::Processor::create(cfg, cal);
}

xxrf::core::Result<xxrf::aoa::rt::Stream> start_stream(ViewerState& vs) noexcept {
    if (vs.serials.size() < 2) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "Нужно как минимум 2 устройства HackRF"});
    }
    if (vs.idx_out < 0 || vs.idx_out >= static_cast<int>(vs.serials.size()) || vs.idx_in < 0 ||
        vs.idx_in >= static_cast<int>(vs.serials.size())) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "Некорректный выбор устройств"});
    }
    if (vs.idx_out == vs.idx_in) {
        return std::unexpected(
            xxrf::core::Error{.code = -1, .message = "Ведущий и ведомый должны быть разными устройствами"});
    }

    auto procr = make_processor(vs);
    if (!procr) {
        return std::unexpected(procr.error());
    }
    auto proc = std::move(*procr);

    xxrf::sync::DualRxDeviceId did_out;
    did_out.serial = vs.serials[static_cast<std::size_t>(vs.idx_out)];
    did_out.role = xxrf::sync::TriggerRole::TriggerOut;

    xxrf::sync::DualRxDeviceId did_in;
    did_in.serial = vs.serials[static_cast<std::size_t>(vs.idx_in)];
    did_in.role = xxrf::sync::TriggerRole::TriggerInWait;

    xxrf::aoa::rt::StreamOptions opt{};
    opt.require_zero_skew = false;
    opt.dual.sync.enable_hardware_trigger = vs.hw_trigger;
    opt.dual.sync.enable_clkout_on_trigger_out = vs.clkout_on_master;
    opt.dual.sync.require_clkin_on_trigger_in = vs.require_clkin_on_slave;
    opt.dual.sync.arm_delay = std::chrono::milliseconds(std::max(0, vs.arm_delay_ms));
    opt.dual.pairing = xxrf::sync::PairingMode::BySampleIndex;
    opt.dual.max_skew_samples = 4096;
    opt.dual.staging_queue_blocks = 32;
    opt.dual.stream.ring_blocks = 64;
    opt.dual.stream.block_bytes = 262144;
    opt.dual.settings.apply_common_settings = true;
    opt.dual.settings.sample_rate_hz = proc.config().sample_rate_hz;
    opt.dual.settings.center_freq_hz = proc.config().center_freq_hz;
    opt.dual.settings.lna_gain_db = static_cast<std::uint32_t>(std::max(0, vs.lna_gain));
    opt.dual.settings.vga_gain_db = static_cast<std::uint32_t>(std::max(0, vs.vga_gain));
    opt.dual.settings.amp_enable = vs.amp_enable;

    vs.q.reset();
    clear_measurement_state(vs);
    vs.last_error.clear();

    auto sr = xxrf::aoa::rt::Stream::start(
        did_out, did_in, std::move(proc),
        [&](const xxrf::aoa::Estimate& e) {
            xxrf_viewer::AoASample s;
            s.t_ns = now_monotonic_ns();
            s.sample_index = e.sample_index;
            s.theta_rad = static_cast<float>(e.theta_rad);
            s.coherence = static_cast<float>(e.quality.coherence);
            s.signal_power_dbfs = signal_power_to_dbfs(e.quality.mean_p0, e.quality.mean_p1);
            s.active_fraction = static_cast<float>(e.quality.active_fraction);
            s.phase_std_deg = static_cast<float>(e.quality.phase_std_deg);
            s.quality_ok = e.quality.ok;
            vs.q.push_drop_oldest(s);
        },
        opt);

    if (!sr) {
        vs.last_error = sr.error().message;
        return std::unexpected(sr.error());
    }
    return std::move(*sr);
}

void stop_stream(ViewerState& vs) noexcept {
    if (!vs.stream) {
        clear_measurement_state(vs);
        return;
    }
    if (auto st = vs.stream->stop(); !st) {
        vs.last_error = st.error().message;
    } else {
        vs.last_error.clear();
    }
    vs.stream.reset();
    vs.q.reset();
    clear_measurement_state(vs);
}
