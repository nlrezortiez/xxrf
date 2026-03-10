#include "xxrf/sync/dual_rx.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace xxrf::sync {

static inline void atomic_max(std::atomic<std::uint64_t>& dst, std::uint64_t v) noexcept {
    std::uint64_t cur = dst.load(std::memory_order_relaxed);
    while (v > cur && !dst.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
        // cur updated by CAS
    }
}

static inline xxrf::core::Error make_local_error(std::string msg) {
    return xxrf::core::Error{.code = -1, .message = std::move(msg)};
}

struct BlockCopy final {
    std::uint64_t first_sample = 0;
    std::vector<std::int8_t> iq; // interleaved I/Q
};

struct DualRx::Impl final {
    // Keep libhackrf initialized while DualRx exists (ref-counted).
    xxrf::core::Context ctx_guard;

    DualRxOptions opt{};

    // Own devices to avoid lifetime hazards (streams refer to these).
    xxrf::core::Device dev_out; // TriggerOut
    xxrf::core::Device dev_in;  // TriggerInWait

    // Two independent RxStreams. Their handlers push copies into staging queues.
    std::optional<xxrf::stream::RxStream> rx_out;
    std::optional<xxrf::stream::RxStream> rx_in;

    // Coordinator thread: pairs staging blocks and calls user handler.
    std::thread coordinator;
    DualRxHandler handler;

    // Staging queues: protected by one mutex for consistent pairing decisions.
    std::mutex m;
    std::condition_variable cv;

    std::deque<BlockCopy> q_out;
    std::deque<BlockCopy> q_in;

    std::atomic<bool> stop_requested{false};
    bool out_done = false;
    bool in_done = false;

    // stats
    std::atomic<std::uint64_t> pairs_emitted{0};
    std::atomic<std::uint64_t> drops_pairing_out{0};
    std::atomic<std::uint64_t> drops_pairing_in{0};
    std::atomic<std::uint64_t> max_abs_skew_samples{0};

    std::atomic<std::uint64_t> staging_max_depth_out{0};
    std::atomic<std::uint64_t> staging_max_depth_in{0};

    // fatal error reported from any worker thread (handler throw/bad_alloc, etc.)
    mutable std::mutex fatal_m;
    std::optional<xxrf::core::Error> fatal;

    static void set_fatal(Impl* self, xxrf::core::Error e) noexcept {
        if (self == nullptr) {
            return;
        }
        std::lock_guard lk(self->fatal_m);
        if (!self->fatal.has_value()) {
            self->fatal = std::move(e);
        }
    }

    std::optional<xxrf::core::Error> take_fatal() noexcept {
        std::lock_guard lk(fatal_m);
        auto tmp = std::move(fatal);
        fatal.reset();
        return tmp;
    }

    void request_stop() noexcept {
        stop_requested.store(true, std::memory_order_relaxed);

        if (rx_out) {
            rx_out->request_stop();
        }
        if (rx_in) {
            rx_in->request_stop();
        }

        {
            std::lock_guard lk(m);
            out_done = true;
            in_done = true;

            q_out.clear();
            q_in.clear();
        }
        cv.notify_all();
    }

    void push_block(bool is_out, const xxrf::stream::RxBlock& blk) noexcept {
        // This runs in RxStream consumer thread, not in libusb callback.
        // Must be exception-safe: any throw would terminate the thread.
        if (stop_requested.load(std::memory_order_relaxed)) {
            return;
        }

        if (opt.staging_queue_blocks == 0) {
            if (is_out) {
                drops_pairing_out.fetch_add(1, std::memory_order_relaxed);
            } else {
                drops_pairing_in.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }

        try {
            BlockCopy c;
            c.first_sample = blk.first_sample_index;
            c.iq.resize(blk.iq_interleaved.size());
            if (!c.iq.empty()) {
                std::memcpy(c.iq.data(), blk.iq_interleaved.data(), c.iq.size());
            }

            std::unique_lock lk(m);

            auto& q = is_out ? q_out : q_in;
            const std::size_t cap = opt.staging_queue_blocks;

            if (q.size() >= cap) {
                // Real-time policy: drop oldest to keep latency bounded.
                q.pop_front();
                if (is_out) {
                    drops_pairing_out.fetch_add(1, std::memory_order_relaxed);
                } else {
                    drops_pairing_in.fetch_add(1, std::memory_order_relaxed);
                }
            }

            q.push_back(std::move(c));

            const std::uint64_t depth = static_cast<std::uint64_t>(q.size());
            if (is_out) {
                atomic_max(staging_max_depth_out, depth);
            } else {
                atomic_max(staging_max_depth_in, depth);
            }

            lk.unlock();
            cv.notify_one();
        } catch (const std::bad_alloc&) {
            set_fatal(this, make_local_error("DualRx: OOM while copying RX block into staging queue"));
            request_stop();
        } catch (const std::exception& ex) {
            set_fatal(this, make_local_error(std::string("DualRx: exception in staging push: ") + ex.what()));
            request_stop();
        } catch (...) {
            set_fatal(this, make_local_error("DualRx: unknown exception in staging push"));
            request_stop();
        }
    }

    static std::uint64_t abs_diff_u64(std::uint64_t a, std::uint64_t b) noexcept {
        return (a >= b) ? (a - b) : (b - a);
    }

    void coordinator_loop() noexcept {
        // Ensures handler is called from a single thread.
        // Never call handler while holding mutex m (avoid blocking producers).
        for (;;) {
            BlockCopy a;
            BlockCopy b;

            std::uint64_t first_common = 0;
            std::size_t skip_a_bytes = 0;
            std::size_t skip_b_bytes = 0;
            std::size_t use_bytes = 0;
            std::uint64_t skew_abs = 0;

            {
                std::unique_lock lk(m);
                cv.wait(lk, [&] {
                    if (stop_requested.load(std::memory_order_relaxed)) {
                        return true;
                    }
                    return !q_out.empty() && !q_in.empty();
                });

                // Exit condition: stop requested, both streams stopped, queues drained.
                if (stop_requested.load(std::memory_order_relaxed) && out_done && in_done && q_out.empty() &&
                    q_in.empty()) {
                    break;
                }

                if (q_out.empty() || q_in.empty()) {
                    // Could be stop wakeup or one side not ready yet.
                    continue;
                }

                if (opt.pairing == PairingMode::ByArrivalOrder) {
                    a = std::move(q_out.front());
                    q_out.pop_front();
                    b = std::move(q_in.front());
                    q_in.pop_front();

                    // common index for info only: choose max start
                    first_common = std::max(a.first_sample, b.first_sample);
                    skew_abs = abs_diff_u64(a.first_sample, b.first_sample);
                    atomic_max(max_abs_skew_samples, skew_abs);

                    // Align by trimming leading skew (best effort), even in arrival-order mode.
                    if (a.first_sample < first_common) {
                        skip_a_bytes = static_cast<std::size_t>((first_common - a.first_sample) * 2);
                    }
                    if (b.first_sample < first_common) {
                        skip_b_bytes = static_cast<std::size_t>((first_common - b.first_sample) * 2);
                    }

                    const std::size_t a_av = (a.iq.size() > skip_a_bytes) ? (a.iq.size() - skip_a_bytes) : 0;
                    const std::size_t b_av = (b.iq.size() > skip_b_bytes) ? (b.iq.size() - skip_b_bytes) : 0;
                    use_bytes = std::min(a_av, b_av);
                    use_bytes &= ~std::size_t{1};

                } else { // PairingMode::BySampleIndex
                    for (;;) {
                        if (q_out.empty() || q_in.empty()) {
                            break; // need both to decide
                        }

                        const auto& ao = q_out.front();
                        const auto& bi = q_in.front();

                        const std::uint64_t out_first = ao.first_sample;
                        const std::uint64_t in_first = bi.first_sample;

                        const std::uint64_t skew = abs_diff_u64(out_first, in_first);
                        atomic_max(max_abs_skew_samples, skew);

                        if (skew <= opt.max_skew_samples) {
                            // Pair these two, and align by trimming the earlier one (if skew>0).
                            a = std::move(q_out.front());
                            q_out.pop_front();
                            b = std::move(q_in.front());
                            q_in.pop_front();

                            first_common = std::max(out_first, in_first);
                            skew_abs = skew;

                            if (out_first < first_common) {
                                skip_a_bytes = static_cast<std::size_t>((first_common - out_first) * 2);
                            }
                            if (in_first < first_common) {
                                skip_b_bytes = static_cast<std::size_t>((first_common - in_first) * 2);
                            }

                            const std::size_t a_av = (a.iq.size() > skip_a_bytes) ? (a.iq.size() - skip_a_bytes) : 0;
                            const std::size_t b_av = (b.iq.size() > skip_b_bytes) ? (b.iq.size() - skip_b_bytes) : 0;
                            use_bytes = std::min(a_av, b_av);
                            use_bytes &= ~std::size_t{1};
                            break;
                        }

                        // If skew too large: drop the earlier block (move forward) to catch up.
                        if (out_first < in_first) {
                            q_out.pop_front();
                            drops_pairing_out.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            q_in.pop_front();
                            drops_pairing_in.fetch_add(1, std::memory_order_relaxed);
                        }

                        // Continue loop; we might be able to form a pair now.
                    }
                } // BySampleIndex
            } // unlock m

            if (stop_requested.load(std::memory_order_relaxed) && (a.iq.empty() || b.iq.empty())) {
                // If we were woken to stop, and no valid pair extracted, continue to drain/exit.
                continue;
            }

            if (a.iq.empty() || b.iq.empty() || use_bytes == 0) {
                // No usable aligned intersection.
                continue;
            }

            // Call user handler outside locks, and ensure exception safety.
            try {
                DualRxBlockView view;
                view.first_sample_index = first_common;
                view.iq_trigger_out = std::span<const std::int8_t>(a.iq.data() + skip_a_bytes, use_bytes);
                view.iq_trigger_in = std::span<const std::int8_t>(b.iq.data() + skip_b_bytes, use_bytes);
                view.skew_samples = skew_abs;

                if (handler) {
                    handler(view);
                }

                pairs_emitted.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& ex) {
                set_fatal(this, make_local_error(std::string("DualRx: handler threw: ") + ex.what()));
                request_stop();
            } catch (...) {
                set_fatal(this, make_local_error("DualRx: handler threw (unknown exception)"));
                request_stop();
            }
        }
    }

    Impl(xxrf::core::Context ctx, DualRxOptions o, xxrf::core::Device out_dev, xxrf::core::Device in_dev,
         DualRxHandler h)
        : ctx_guard(std::move(ctx)), opt(o), dev_out(std::move(out_dev)), dev_in(std::move(in_dev)),
          handler(std::move(h)) {}
};

static xxrf::core::Status apply_common_settings(xxrf::core::Device& d, const DualRxCommonSettings& s) noexcept {
    if (auto r = d.set_sample_rate(s.sample_rate_hz); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = d.set_center_freq(s.center_freq_hz); !r) {
        return std::unexpected(r.error());
    }

    if (auto r = d.set_lna_gain(s.lna_gain_db); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = d.set_vga_gain(s.vga_gain_db); !r) {
        return std::unexpected(r.error());
    }

    if (auto r = d.set_amp_enable(s.amp_enable); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = d.set_bias_tee_enable(s.bias_tee_enable); !r) {
        return std::unexpected(r.error());
    }

    return xxrf::core::ok();
}

static xxrf::core::Status configure_sync(xxrf::core::Device& out_dev, xxrf::core::Device& in_dev,
                                         const DualRxSyncOptions& so) noexcept {
    if (!so.enable_hardware_trigger) {
        // Ensure hw_sync is off on both.
        if (auto r = out_dev.set_hw_sync_mode(false); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = in_dev.set_hw_sync_mode(false); !r) {
            return std::unexpected(r.error());
        }
        return xxrf::core::ok();
    }

    // Trigger-out device: hw_sync off (starts immediately, emits trigger out on start).
    if (auto r = out_dev.set_hw_sync_mode(false); !r) {
        return std::unexpected(r.error());
    }

    // Trigger-in device: hw_sync on (wait for external trigger).
    if (auto r = in_dev.set_hw_sync_mode(true); !r) {
        return std::unexpected(r.error());
    }

    // External clock handling: enable clkout on trigger-out, then validate clkin on trigger-in if requested.
    if (so.enable_clkout_on_trigger_out) {
        if (auto r = out_dev.set_clkout_enable(true); !r) {
            return std::unexpected(r.error());
        }
    }

    if (so.require_clkin_on_trigger_in) {
        auto st = in_dev.clkin_detected();
        if (!st) {
            return std::unexpected(st.error());
        }
        if (*st == 0) {
            return std::unexpected(
                make_local_error("DualRx: CLKIN not detected on TriggerIn device (require_clkin_on_trigger_in=true)"));
        }
    }

    return xxrf::core::ok();
}

xxrf::core::Result<DualRx> DualRx::start(xxrf::core::Context& /*ctx*/, const DualRxDeviceId& trigger_out,
                                         const DualRxDeviceId& trigger_in, DualRxHandler handler,
                                         DualRxOptions opt) noexcept {
    // This overload exists for convenience; we still create an internal Context guard
    // so DualRx remains safe even if caller destroys its Context earlier.
    auto guard = xxrf::core::Context::create();
    if (!guard) {
        return std::unexpected(guard.error());
    }

    if (trigger_out.role != TriggerRole::TriggerOut || trigger_in.role != TriggerRole::TriggerInWait) {
        return std::unexpected(
            make_local_error("DualRx::start: roles must be TriggerOut and TriggerInWait respectively"));
    }

    auto d_out = xxrf::core::Device::open_by_serial(trigger_out.serial);
    if (!d_out) {
        return std::unexpected(d_out.error());
    }

    auto d_in = xxrf::core::Device::open_by_serial(trigger_in.serial);
    if (!d_in) {
        return std::unexpected(d_in.error());
    }

    // Delegate to the Device-owning overload.
    // Move guard into Impl there.
    // We temporarily ignore the ctx parameter; the guard ensures initialization lifetime.
    // (Context is ref-counted, so this doesn't duplicate hackrf_init unless needed.)
    // NOLINTNEXTLINE
    return DualRx::start(std::move(*d_out), std::move(*d_in), std::move(handler), std::move(opt));
}

xxrf::core::Result<DualRx> DualRx::start(xxrf::core::Device trigger_out_dev, xxrf::core::Device trigger_in_dev,
                                         DualRxHandler handler, DualRxOptions opt) noexcept {

    // Construct Impl in steps to avoid any undefined constructs.
    std::unique_ptr<Impl> impl;
    {
        auto guard = xxrf::core::Context::create();
        if (!guard) {
            return std::unexpected(guard.error());
        }

        impl = std::make_unique<Impl>(std::move(*guard), opt, std::move(trigger_out_dev), std::move(trigger_in_dev),
                                      std::move(handler));
    }

    // Apply common settings if requested.
    if (impl->opt.settings.apply_common_settings) {
        if (auto r = apply_common_settings(impl->dev_out, impl->opt.settings); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = apply_common_settings(impl->dev_in, impl->opt.settings); !r) {
            return std::unexpected(r.error());
        }
    }

    // Configure sync (hw trigger, clkin/clkout policy).
    if (auto r = configure_sync(impl->dev_out, impl->dev_in, impl->opt.sync); !r) {
        return std::unexpected(r.error());
    }

    // Start coordinator first (it will block on cv).
    impl->coordinator = std::thread([p = impl.get()] { p->coordinator_loop(); });

    // Start TriggerIn stream first (so it can enter wait-for-trigger state),
    // then wait arm_delay, then start TriggerOut stream (which emits trigger output on start).
    {
        auto rx_in_res = xxrf::stream::RxStream::start(
            impl->dev_in, [p = impl.get()](const xxrf::stream::RxBlock& blk) { p->push_block(false, blk); },
            impl->opt.stream);
        if (!rx_in_res) {
            impl->request_stop();
            {
                std::lock_guard lk(impl->m);
                impl->in_done = true;
                impl->out_done = true;
            }
            impl->cv.notify_all();
            if (impl->coordinator.joinable()) {
                impl->coordinator.join();
            }
            return std::unexpected(rx_in_res.error());
        }
        impl->rx_in.emplace(std::move(*rx_in_res));
    }

    if (impl->opt.sync.enable_hardware_trigger) {
        std::this_thread::sleep_for(impl->opt.sync.arm_delay);
    }

    {
        auto rx_out_res = xxrf::stream::RxStream::start(
            impl->dev_out, [p = impl.get()](const xxrf::stream::RxBlock& blk) { p->push_block(true, blk); },
            impl->opt.stream);
        if (!rx_out_res) {
            // stop in-stream and coordinator
            impl->request_stop();

            (void)impl->rx_in->stop();

            {
                std::lock_guard lk(impl->m);
                impl->in_done = true;
                impl->out_done = true;
            }
            impl->cv.notify_all();
            if (impl->coordinator.joinable()) {
                impl->coordinator.join();
            }
            return std::unexpected(rx_out_res.error());
        }
        impl->rx_out.emplace(std::move(*rx_out_res));
    }

    return DualRx{impl.release()};
}

DualRx::DualRx(DualRx&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

DualRx& DualRx::operator=(DualRx&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    (void)stop();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

DualRx::~DualRx() noexcept {
    (void)stop();
    delete impl_;
    impl_ = nullptr;
}

xxrf::core::Status DualRx::stop() noexcept {
    if (impl_ == nullptr) {
        return xxrf::core::ok();
    }

    if (impl_->coordinator.joinable() && std::this_thread::get_id() == impl_->coordinator.get_id()) {
        return std::unexpected(
            make_local_error("[DualRx::stop] must not be called from DualRxHandler; use request_stop()"));
    }

    impl_->request_stop(); // выставит stop_requested, дернёт stream request_stop, выставит done, notify_all

    xxrf::core::Status e1 = xxrf::core::ok();
    xxrf::core::Status e2 = xxrf::core::ok();

    if (impl_->rx_out) {
        e1 = impl_->rx_out->stop();
    }
    if (impl_->rx_in) {
        e2 = impl_->rx_in->stop();
    }

    if (impl_->coordinator.joinable()) {
        impl_->coordinator.join();
    }

    if (auto f = impl_->take_fatal(); f) {
        return std::unexpected(*f);
    }
    if (!e1) {
        return std::unexpected(e1.error());
    }
    if (!e2) {
        return std::unexpected(e2.error());
    }
    return xxrf::core::ok();
}

void DualRx::request_stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    impl_->request_stop();
}

DualRxStats DualRx::stats() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }

    DualRxStats s;
    s.trigger_out = impl_->rx_out ? impl_->rx_out->stats() : xxrf::stream::RxStats{};
    s.trigger_in = impl_->rx_in ? impl_->rx_in->stats() : xxrf::stream::RxStats{};

    s.pairs_emitted = impl_->pairs_emitted.load(std::memory_order_relaxed);
    s.drops_pairing_trigger_out = impl_->drops_pairing_out.load(std::memory_order_relaxed);
    s.drops_pairing_trigger_in = impl_->drops_pairing_in.load(std::memory_order_relaxed);
    s.max_abs_skew_samples = impl_->max_abs_skew_samples.load(std::memory_order_relaxed);

    s.staging_max_depth_trigger_out = impl_->staging_max_depth_out.load(std::memory_order_relaxed);
    s.staging_max_depth_trigger_in = impl_->staging_max_depth_in.load(std::memory_order_relaxed);

    return s;
}

xxrf::core::Device& DualRx::device_trigger_out() noexcept { return impl_->dev_out; }

xxrf::core::Device& DualRx::device_trigger_in() noexcept { return impl_->dev_in; }

} // namespace xxrf::sync