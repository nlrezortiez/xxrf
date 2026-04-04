#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <print>
#include <span>
#include <string>
#include <thread>
#include <xxrf/sync/dual_rx.hpp>
#include <xxrf/xxrf.hpp>

static inline void atomic_min_u64(std::atomic<std::uint64_t>& dst, std::uint64_t v) noexcept {
    std::uint64_t cur = dst.load(std::memory_order_relaxed);
    while (v < cur && !dst.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

static inline void atomic_max_u64(std::atomic<std::uint64_t>& dst, std::uint64_t v) noexcept {
    std::uint64_t cur = dst.load(std::memory_order_relaxed);
    while (v > cur && !dst.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

static inline double mean_abs_iq_quick(std::span<const std::int8_t> iq) noexcept {

    if (iq.size() < 2) {
        return 0.0;
    }

    const std::size_t n = iq.size() / 2;
    const std::size_t take = (n > 4096) ? 4096 : n;

    double acc = 0.0;
    for (std::size_t k = 0; k < take; ++k) {
        const int i = iq[(2 * k) + 0];
        const int q = iq[(2 * k) + 1];

        acc += double(std::abs(i) + std::abs(q));
    }
    return acc / double(take);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::print("usage:\n"
                   "  {} <serial_trigger_out> <serial_trigger_in> [seconds]\n\n"
                   "notes:\n"
                   "  - serial_trigger_out: устройство, которое стартует и выдаёт trigger-out\n"
                   "  - serial_trigger_in : устройство, которое ждёт trigger-in\n",
                   argv[0]);
        return EXIT_FAILURE;
    }

    const std::string serial_out = argv[1];
    const std::string serial_in = argv[2];

    int seconds = 3;
    if (argc >= 4) {
        seconds = std::max(1, std::atoi(argv[3]));
    }

    std::atomic<std::uint64_t> pairs_seen{0};
    std::atomic<std::uint64_t> first_sample_min{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> first_sample_max{0};

    std::atomic<std::uint64_t> skew_min{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> skew_max{0};
    std::atomic<std::uint64_t> skew_sum{0};

    std::atomic<double> last_mean_out{0.0};
    std::atomic<double> last_mean_in{0.0};

    xxrf::sync::DualRxOptions opt{};
    opt.settings.apply_common_settings = true;
    opt.settings.sample_rate_hz = 10'000'000.0;
    opt.settings.center_freq_hz = 433'000'000ULL;
    opt.settings.lna_gain_db = 16;
    opt.settings.vga_gain_db = 16;
    opt.settings.amp_enable = false;
    opt.settings.bias_tee_enable = false;

    opt.sync.enable_hardware_trigger = true;
    opt.sync.arm_delay = std::chrono::milliseconds(50);
    opt.sync.enable_clkout_on_trigger_out = true;
    opt.sync.require_clkin_on_trigger_in = true;

    opt.stream.ring_blocks = 64;
    opt.stream.block_bytes = 262144;

    opt.staging_queue_blocks = 32;

    opt.pairing = xxrf::sync::PairingMode::BySampleIndex;
    opt.max_skew_samples = 0;

    auto dualr = xxrf::sync::DualRx::start(
        xxrf::sync::DualRxDeviceId{.serial = serial_out, .role = xxrf::sync::TriggerRole::TriggerOut},
        xxrf::sync::DualRxDeviceId{.serial = serial_in, .role = xxrf::sync::TriggerRole::TriggerInWait},
        [&](const xxrf::sync::DualRxBlockView& blk) {
            pairs_seen.fetch_add(1, std::memory_order_relaxed);

            atomic_min_u64(first_sample_min, blk.first_sample_index);
            atomic_max_u64(first_sample_max, blk.first_sample_index);

            atomic_min_u64(skew_min, blk.skew_samples);
            atomic_max_u64(skew_max, blk.skew_samples);
            skew_sum.fetch_add(blk.skew_samples, std::memory_order_relaxed);

            last_mean_out.store(mean_abs_iq_quick(blk.iq_trigger_out), std::memory_order_relaxed);
            last_mean_in.store(mean_abs_iq_quick(blk.iq_trigger_in), std::memory_order_relaxed);
        },
        opt);

    if (!dualr) {
        std::print("DualRx::start failed: {}\n", dualr.error().message);
        return EXIT_FAILURE;
    }

    auto dual = std::move(*dualr);

    std::print("DualRX started\n"
               "  trigger_out serial = {}\n"
               "  trigger_in  serial = {}\n"
               "  fs = {:.3f} Msps, f0 = {:.3f} MHz\n"
               "  pairing=BySampleIndex, max_skew_samples={}\n",
               serial_out, serial_in, opt.settings.sample_rate_hz / 1e6, double(opt.settings.center_freq_hz) / 1e6,
               opt.max_skew_samples);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    if (auto r = dual.stop(); !r) {
        std::print("DualRx::stop failed: {}\n", r.error().message);
        return EXIT_FAILURE;
    }

    const auto st = dual.stats();
    const auto n = pairs_seen.load(std::memory_order_relaxed);

    const auto skew_min_v = (n == 0) ? 0 : skew_min.load(std::memory_order_relaxed);
    const auto skew_max_v = (n == 0) ? 0 : skew_max.load(std::memory_order_relaxed);
    const auto skew_sum_v = skew_sum.load(std::memory_order_relaxed);
    const double skew_avg = (n == 0) ? 0.0 : (double(skew_sum_v) / double(n));

    std::print("\nDualRx.stats:\n");
    std::print("  pairs_emitted               = {}\n", st.pairs_emitted);
    std::print("  max_abs_skew_samples        = {}\n", st.max_abs_skew_samples);
    std::print("  drops_pairing_trigger_out   = {}\n", st.drops_pairing_trigger_out);
    std::print("  drops_pairing_trigger_in    = {}\n", st.drops_pairing_trigger_in);
    std::print("  staging_max_depth_out       = {}\n", st.staging_max_depth_trigger_out);
    std::print("  staging_max_depth_in        = {}\n", st.staging_max_depth_trigger_in);

    std::print("\nPer-stream RxStats:\n");
    std::print("  OUT: blocks_received={} dropped={} truncated={} bytes={} ring_max_depth={}\n",
               st.trigger_out.blocks_received, st.trigger_out.blocks_dropped, st.trigger_out.blocks_truncated,
               st.trigger_out.bytes_received, st.trigger_out.ring_max_depth);
    std::print("   IN: blocks_received={} dropped={} truncated={} bytes={} ring_max_depth={}\n",
               st.trigger_in.blocks_received, st.trigger_in.blocks_dropped, st.trigger_in.blocks_truncated,
               st.trigger_in.bytes_received, st.trigger_in.ring_max_depth);

    std::print("\nHandler aggregates:\n");
    std::print("  pairs_seen          = {}\n", n);
    if (n != 0) {
        std::print("  first_sample range  = [{} .. {}]\n", first_sample_min.load(std::memory_order_relaxed),
                   first_sample_max.load(std::memory_order_relaxed));
        std::print("  skew_samples  min/max/avg = {} / {} / {:.3f}\n", skew_min_v, skew_max_v, skew_avg);
        std::print("  last_mean_abs (OUT/IN)    = {:.3f} / {:.3f}\n", last_mean_out.load(std::memory_order_relaxed),
                   last_mean_in.load(std::memory_order_relaxed));
    }

    std::print("\nDualRX stopped\n");
    return EXIT_SUCCESS;
}
