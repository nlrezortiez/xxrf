#include <chrono>
#include <cmath>
#include <print>
#include <thread>
#include <xxrf/stream/rx_stream.hpp>
#include <xxrf/xxrf.hpp>

static inline double mean_abs_iq(std::span<const std::int8_t> iq) {
    if (iq.size() < 2) {
        return 0.0;
    }

    const std::size_t n = iq.size() / 2;
    double acc = 0.0;

    const std::size_t max_n = (n > 4096) ? 4096 : n;

    for (std::size_t k = 0; k < max_n; ++k) {
        const int i = iq[(2 * k) + 0];
        const int q = iq[(2 * k) + 1];
        acc += std::sqrt(double((i * i) + (q * q)));
    }
    return (max_n != 0) ? (acc / double(max_n)) : 0.0;
}

int main() {
    auto devr = xxrf::core::Device::open_first();
    if (!devr) {
        std::println(stderr, "Device::open_first failed: {}", devr.error().message);
        return EXIT_FAILURE;
    }
    auto dev = std::move(*devr);

    const double sample_rate = 10'000'000.0;
    const std::uint64_t freq = 100'000'000ULL;

    if (auto r = dev.set_sample_rate(sample_rate); !r) {
        std::println(stderr, "set_sample_rate failed: {}", r.error().message);
        return EXIT_FAILURE;
    }
    if (auto r = dev.set_center_freq(freq); !r) {
        std::println(stderr, "set_center_freq failed: {}", r.error().message);
        return EXIT_FAILURE;
    }

    dev.set_lna_gain(16);
    dev.set_vga_gain(16);
    dev.set_amp_enable(false);

    std::atomic<std::uint64_t> blocks_seen{0};
    std::atomic<std::uint64_t> samples_seen{0};
    std::atomic<double> last_mean_abs{0.0};

    const xxrf::stream::RxStreamOptions opt{.ring_blocks = 64, .block_bytes = 262144};

    auto streamr = xxrf::stream::RxStream::start(
        dev,
        [&](const xxrf::stream::RxBlock& blk) {
            blocks_seen.fetch_add(1, std::memory_order_relaxed);
            samples_seen.fetch_add(blk.iq_interleaved.size() / 2, std::memory_order_relaxed);

            last_mean_abs.store(mean_abs_iq(blk.iq_interleaved), std::memory_order_relaxed);
        },
        opt);

    if (!streamr) {
        std::println(stderr, "RxStream::start failed: {}", streamr.error().message);
        return EXIT_FAILURE;
    }

    auto stream = std::move(*streamr);

    std::println(stderr, "RX started: fs={:.3f} Msps, f0={:.3f} MHz, ring_blocks={}, block_bytes={}", sample_rate / 1e6,
                 double(freq) / 1e6, opt.ring_blocks, opt.block_bytes);

    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const auto t1 = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t1 - t0).count();

    const auto st = stream.stats();

    const double bytes_per_s = (dt > 0) ? (double(st.bytes_received) / dt) : 0.0;
    const double mb_per_s = bytes_per_s / 1e6;
    const double complex_samples_per_s = (dt > 0) ? (double(st.bytes_received) / 2.0 / dt) : 0.0;
    const double msps = complex_samples_per_s / 1e6;

    const auto b = blocks_seen.load(std::memory_order_relaxed);
    const auto s = samples_seen.load(std::memory_order_relaxed);
    const auto m = last_mean_abs.load(std::memory_order_relaxed);

    std::println("stream.stats:");
    std::println("  blocks_received   = {}", st.blocks_received);
    std::println("  blocks_dropped    = {}", st.blocks_dropped);
    std::println("  blocks_truncated  = {}", st.blocks_truncated);
    std::println("  bytes_received    = {}", st.bytes_received);
    std::println("  ring_max_depth    = {} (of {})", st.ring_max_depth, opt.ring_blocks);

    std::println("derived:");
    std::println("  dt                = {:.3f} s", dt);
    std::println("  throughput        = {:.3f} MB/s", mb_per_s);
    std::println("  effective rate    = {:.3f} Msps (complex)", msps);

    std::println("handler:");
    std::println("  blocks_seen      = {}", b);
    std::println("  samples_seen     = {}", s);
    std::println("  last_mean_abs    = {:.4f}", m);

    if (auto r = stream.stop(); !r) {
        std::println(stderr, "stream.stop failed: {}", r.error().message);
        return EXIT_FAILURE;
    }

    std::println(stderr, "RX stopped");
    return EXIT_SUCCESS;
}
