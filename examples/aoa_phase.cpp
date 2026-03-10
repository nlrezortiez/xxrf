#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <string_view>
#include <thread>
#include <xxrf/xxrf.hpp>

using namespace std::chrono_literals;

static bool parse_u64(std::string_view s, std::uint64_t& out) noexcept {
    // Accept decimal or "100e6"-like? For now: decimal only.
    std::uint64_t v = 0;
    const auto* p = s.data();
    const auto* e = s.data() + s.size();
    if (p == e) {
        return false;
    }
    for (; p != e; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        const std::uint64_t d = static_cast<std::uint64_t>(*p - '0');
        v = (v * 10U) + d;
    }
    out = v;
    return true;
}

static bool parse_double(std::string_view s, double& out) noexcept {
    // Minimal robust parse without exceptions.
    // Uses strtod; accepts "0.15", "1e-3", etc.
    std::string tmp{s};
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0') {
        return false;
    }
    out = v;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::print(
            "usage:\n"
            "  {} <trigger_out_serial> <trigger_in_serial> [baseline_m] [center_freq_hz] [sample_rate_hz] [seconds]\n"
            "\n"
            "defaults:\n"
            "  baseline_m     = 0.15\n"
            "  center_freq_hz = 100000000\n"
            "  sample_rate_hz = 10000000\n"
            "  seconds        = 5\n",
            (argc > 0 ? argv[0] : "xxrf_example_aoa_phase"));
        return EXIT_FAILURE;
    }

    const std::string_view serial_out = argv[1];
    const std::string_view serial_in = argv[2];

    double baseline_m = 0.15;
    std::uint64_t f0_hz = 100'000'000ULL;
    double fs_hz = 10'000'000.0;
    int seconds = 5;

    if (argc >= 4) {
        if (!parse_double(argv[3], baseline_m) || !(baseline_m > 0.0)) {
            std::print("bad baseline_m: {}\n", argv[3]);
            return EXIT_FAILURE;
        }
    }
    if (argc >= 5) {
        if (!parse_u64(argv[4], f0_hz) || f0_hz == 0) {
            std::print("bad center_freq_hz: {}\n", argv[4]);
            return EXIT_FAILURE;
        }
    }
    if (argc >= 6) {
        if (!parse_double(argv[5], fs_hz) || !(fs_hz > 0.0)) {
            std::print("bad sample_rate_hz: {}\n", argv[5]);
            return EXIT_FAILURE;
        }
    }
    if (argc >= 7) {
        seconds = std::atoi(argv[6]);
        if (seconds <= 0) {
            std::print("bad seconds: {}\n", argv[6]);
            return EXIT_FAILURE;
        }
    }

    auto ctxr = xxrf::core::Context::create();
    if (!ctxr) {
        std::print("Context::create failed: {}\n", ctxr.error().message);
        return EXIT_FAILURE;
    }
    auto& ctx = *ctxr;

    // AoA config (MVP: phase interferometry)
    xxrf::aoa::Config cfg;
    cfg.method = xxrf::aoa::Method::PhaseInterferometry;
    cfg.sample_rate_hz = fs_hz;
    cfg.center_freq_hz = f0_hz;

    cfg.geom.baseline_m = baseline_m;
    cfg.geom.baseline_azimuth_rad = 0.0; // keep 0 for smoke test

    // CPU control: stride reduces per-sample work.
    cfg.win.sample_stride = 32;
    cfg.win.window_samples = 8192;
    cfg.win.hop_samples = 2048;

    // For smoke test you typically want to see output even in poor RF conditions.
    // Raise this later (e.g. 0.6..0.9) when you have a clean narrowband source.
    cfg.min_coherence = 0.10;

    cfg.clamp_sin = true;
    cfg.require_contiguous = true;
    cfg.apply_calibration = true;

    xxrf::aoa::Calibration cal{};
    cal.ch1_gain = {1.0F, 0.0F}; // TODO: set from calibration tool later.

    auto procr = xxrf::aoa::Processor::create(cfg, cal);
    if (!procr) {
        std::print("AoA Processor::create failed: {}\n", procr.error().message);
        return EXIT_FAILURE;
    }
    auto proc = std::move(*procr);

    // DualRx device identities
    xxrf::sync::DualRxDeviceId did_out;
    did_out.serial = std::string(serial_out);
    did_out.role = xxrf::sync::TriggerRole::TriggerOut;

    xxrf::sync::DualRxDeviceId did_in;
    did_in.serial = std::string(serial_in);
    did_in.role = xxrf::sync::TriggerRole::TriggerInWait;

    // AoA Stream options (DualRx inside)
    xxrf::aoa::rt::StreamOptions sopt{};
    sopt.require_zero_skew = true;

    // DualRx pairing and queueing (safe defaults for smoke)
    sopt.dual.pairing = xxrf::sync::PairingMode::BySampleIndex;
    sopt.dual.max_skew_samples = 4096;
    sopt.dual.staging_queue_blocks = 32;

    // RxStream options
    sopt.dual.stream.ring_blocks = 64;
    sopt.dual.stream.block_bytes = 262144;

    // Sync policy (hardware trigger + clkout/clkin)
    sopt.dual.sync.enable_hardware_trigger = true;
    sopt.dual.sync.enable_clkout_on_trigger_out = true;
    sopt.dual.sync.require_clkin_on_trigger_in = true;
    sopt.dual.sync.arm_delay = 50ms;

    // Aggregates updated from handler (no printing in handler)
    std::atomic<std::uint64_t> est_count{0};
    std::atomic<double> last_theta_rad{0.0};
    std::atomic<double> last_az_rad{0.0};
    std::atomic<double> last_coh{0.0};
    std::atomic<std::uint64_t> last_si{0};

    auto streamr = xxrf::aoa::rt::Stream::start(
        ctx, did_out, did_in, std::move(proc),
        [&](const xxrf::aoa::Estimate& e) {
            // This is called in DualRx coordinator thread.
            // Do not block. Just atomics.
            est_count.fetch_add(1, std::memory_order_relaxed);
            last_theta_rad.store(e.theta_rad, std::memory_order_relaxed);
            last_az_rad.store(e.azimuth_rad, std::memory_order_relaxed);
            last_coh.store(e.quality.coherence, std::memory_order_relaxed);
            last_si.store(e.sample_index, std::memory_order_relaxed);
        },
        sopt);

    if (!streamr) {
        std::print("AoA Stream::start failed: {}\n", streamr.error().message);
        return EXIT_FAILURE;
    }
    auto stream = std::move(*streamr);

    std::print("AoA stream started\n");
    std::print("  OUT serial: {}\n", serial_out);
    std::print("   IN serial: {}\n", serial_in);
    std::print("  fs = {:.3f} Msps, f0 = {:.3f} MHz\n", fs_hz / 1e6, double(f0_hz) / 1e6);
    std::print("  baseline = {:.3f} m\n", baseline_m);
    std::print("  window_samples = {}, hop_samples = {}, stride = {}\n", cfg.win.window_samples, cfg.win.hop_samples,
               cfg.win.sample_stride);
    std::print("  min_coherence = {:.3f}\n\n", cfg.min_coherence);

    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        std::this_thread::sleep_for(100ms);

        const auto now = std::chrono::steady_clock::now();
        const auto dt = std::chrono::duration_cast<std::chrono::seconds>(now - t0).count();
        const auto n = est_count.load(std::memory_order_relaxed);

        const double th = last_theta_rad.load(std::memory_order_relaxed);
        const double az = last_az_rad.load(std::memory_order_relaxed);
        const double coh = last_coh.load(std::memory_order_relaxed);
        const auto si = last_si.load(std::memory_order_relaxed);

        std::print("[t={}s] estimates={}  last: theta={:+.3f} deg  az={:+.3f} deg  coh={:.3f}  sample_index={}\n", dt,
                   n, th * (180.0 / M_PI), az * (180.0 / M_PI), coh, si);

        if (dt >= seconds) {
            break;
        }
    }

    const auto st = stream.stats();
    std::print("\nStream.stats:\n");
    std::print("  AoA: frames_in={} samples_used={} estimates_emitted={} discontinuities={} invalid_geometry={} "
               "below_quality={}\n",
               st.aoa.frames_in, st.aoa.samples_used, st.aoa.estimates_emitted, st.aoa.discontinuities,
               st.aoa.invalid_geometry, st.aoa.below_quality);

    std::print("  DualRx: pairs_emitted={} max_abs_skew_samples={} drops_out={} drops_in={}\n", st.dual.pairs_emitted,
               st.dual.max_abs_skew_samples, st.dual.drops_pairing_trigger_out, st.dual.drops_pairing_trigger_in);

    std::print("  OUT: blocks_received={} dropped={} truncated={} bytes={} ring_max_depth={}\n",
               st.dual.trigger_out.blocks_received, st.dual.trigger_out.blocks_dropped,
               st.dual.trigger_out.blocks_truncated, st.dual.trigger_out.bytes_received,
               st.dual.trigger_out.ring_max_depth);

    std::print("   IN: blocks_received={} dropped={} truncated={} bytes={} ring_max_depth={}\n",
               st.dual.trigger_in.blocks_received, st.dual.trigger_in.blocks_dropped,
               st.dual.trigger_in.blocks_truncated, st.dual.trigger_in.bytes_received,
               st.dual.trigger_in.ring_max_depth);

    if (auto r = stream.stop(); !r) {
        std::print("stop failed: {}\n", r.error().message);
        return EXIT_FAILURE;
    }

    std::print("\nAoA stream stopped\n");
    return EXIT_SUCCESS;
}