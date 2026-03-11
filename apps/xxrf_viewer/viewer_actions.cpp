#include "viewer_actions.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>

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
    cfg.clamp_sin = true;
    cfg.require_contiguous = false;
    cfg.apply_calibration = true;
    cfg.emit_below_quality = true;

    xxrf::aoa::Calibration cal{};
    cal.ch1_gain = {1.0F, 0.0F};

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
    vs.has_fix = false;
    vs.smooth_init = false;
    vs.emitted_estimates = 0;
    vs.last_error.clear();
    vs.theta_head = 0;
    vs.theta_count = 0;
    vs.coherence_head = 0;
    vs.coherence_count = 0;

    auto sr = xxrf::aoa::rt::Stream::start(
        did_out, did_in, std::move(proc),
        [&](const xxrf::aoa::Estimate& e) {
            xxrf_viewer::AoASample s;
            s.t_ns = now_monotonic_ns();
            s.sample_index = e.sample_index;
            s.theta_rad = static_cast<float>(e.theta_rad);
            s.coherence = static_cast<float>(e.quality.coherence);
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
        return;
    }
    if (auto st = vs.stream->stop(); !st) {
        vs.last_error = st.error().message;
    } else {
        vs.last_error.clear();
    }
    vs.stream.reset();
}
