#include "xxrf/stream/rx_stream.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <hackrf.h>
#include <mutex>
#include <memory>
#include <new>
#include <optional>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace xxrf::stream {

static void atomic_max(std::atomic<std::uint64_t>& dst, std::uint64_t v) noexcept {
    std::uint64_t cur = dst.load(std::memory_order_relaxed);
    while (v > cur && !dst.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

static inline xxrf::core::Error make_local_error(std::string msg) {
    return xxrf::core::Error{.code = -1, .message = std::move(msg)};
}

struct RxStream::Impl final {
    std::optional<xxrf::core::Device> dev;
    RxHandler handler;

    RxStreamOptions opt{};

    
    std::vector<std::int8_t> storage;
    std::vector<std::uint32_t> lens;         
    std::vector<std::uint64_t> first_sample; 

    std::atomic<std::uint64_t> write_idx{0};
    std::atomic<std::uint64_t> read_idx{0};

    
    
    std::atomic<std::uint64_t> items{0};

    std::atomic<bool> stop_requested{false};
    std::atomic<bool> running{false};

    std::atomic<std::uint64_t> blocks_received{0};
    std::atomic<std::uint64_t> blocks_dropped{0};
    std::atomic<std::uint64_t> bytes_received{0};
    std::atomic<std::uint64_t> blocks_truncated{0};
    std::atomic<std::uint64_t> ring_max_depth{0};

    
    std::atomic<std::uint64_t> sample_counter{0};

    
    std::binary_semaphore sem{0};

    std::thread consumer;
    mutable std::mutex fatal_m;
    std::optional<xxrf::core::Error> fatal;

    [[nodiscard]] std::size_t capacity() const noexcept { return opt.ring_blocks; }

    std::int8_t* slot_ptr(std::size_t slot) noexcept { return storage.data() + (slot * opt.block_bytes); }

    void set_fatal(xxrf::core::Error e) noexcept {
        std::lock_guard lk(fatal_m);
        if (!fatal.has_value()) {
            fatal = std::move(e);
        }
    }

    std::optional<xxrf::core::Error> take_fatal() noexcept {
        std::lock_guard lk(fatal_m);
        auto tmp = std::move(fatal);
        fatal.reset();
        return tmp;
    }

    static int rx_callback(hackrf_transfer* transfer) noexcept {
        
        auto* self = static_cast<Impl*>(transfer->rx_ctx);
        if (self == nullptr) {
            return 1;
        }

        if (self->stop_requested.load(std::memory_order_relaxed)) {
            return 1; 
        }

        const std::size_t cap = self->capacity();
        const std::uint64_t w = self->write_idx.load(std::memory_order_relaxed);
        const std::uint64_t r = self->read_idx.load(std::memory_order_acquire);

        if ((w - r) >= cap) {
            
            self->blocks_dropped.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        const std::size_t slot = static_cast<std::size_t>(w % cap);

        std::size_t bytes = static_cast<std::size_t>(std::max(0, transfer->valid_length));

        bool truncated = false;
        if (bytes > self->opt.block_bytes) {
            bytes = self->opt.block_bytes;
            truncated = true;
        }

        bytes &= ~std::size_t{1}; 

        if (truncated) {
            self->blocks_truncated.fetch_add(1, std::memory_order_relaxed);
        }

        if (bytes != 0) {
            std::memcpy(self->slot_ptr(slot), transfer->buffer, bytes);
        }

        self->lens[slot] = static_cast<std::uint32_t>(bytes);

        const std::uint64_t first = self->sample_counter.fetch_add(bytes / 2, std::memory_order_relaxed);
        self->first_sample[slot] = first;

        self->bytes_received.fetch_add(bytes, std::memory_order_relaxed);
        self->blocks_received.fetch_add(1, std::memory_order_relaxed);

        self->write_idx.store(w + 1, std::memory_order_release);

        const std::uint64_t depth_after = (w + 1) - r;
        atomic_max(self->ring_max_depth, depth_after);

        const std::uint64_t prev_items = self->items.fetch_add(1, std::memory_order_release);
        if (prev_items == 0) {
            self->sem.release();
        }

        return 0;
    }

    void consumer_loop() {
        for (;;) {
            sem.acquire();

            while (items.load(std::memory_order_acquire) != 0) {
                const std::uint64_t r = read_idx.load(std::memory_order_relaxed);
                const std::uint64_t w = write_idx.load(std::memory_order_acquire);
                if (r == w) {
                    break;
                }

                const std::size_t slot = static_cast<std::size_t>(r % capacity());
                const std::uint32_t len = lens[slot];
                const std::uint64_t first = first_sample[slot];

                RxBlock blk{
                    .first_sample_index = first,
                    .iq_interleaved = std::span<const std::int8_t>(slot_ptr(slot), len),
                };

                if (handler) {
                    try {
                        handler(blk);
                    } catch (const std::exception& ex) {
                        set_fatal(make_local_error(std::string("RxStream: handler threw: ") + ex.what()));
                        stop_requested.store(true, std::memory_order_relaxed);
                        sem.release();
                        return;
                    } catch (...) {
                        set_fatal(make_local_error("RxStream: handler threw (unknown exception)"));
                        stop_requested.store(true, std::memory_order_relaxed);
                        sem.release();
                        return;
                    }
                }

                read_idx.store(r + 1, std::memory_order_release);
                items.fetch_sub(1, std::memory_order_acq_rel);
            }

            if (stop_requested.load(std::memory_order_relaxed) && items.load(std::memory_order_acquire) == 0) {
                break;
            }
        }
    }
};

xxrf::core::Result<RxStream> RxStream::start(xxrf::core::Device& dev, RxHandler handler, RxStreamOptions opt) {
    if (!dev.is_open()) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "RxStream::start: device handle is null"});
    }
    if (opt.ring_blocks == 0) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "RxStreamOptions: ring_blocks == 0"});
    }
    if (opt.block_bytes == 0 || (opt.block_bytes % 2) != 0) {
        return std::unexpected(
            xxrf::core::Error{.code = -1, .message = "RxStreamOptions: block_bytes must be > 0 and even"});
    }

    auto impl = std::make_unique<Impl>();
    impl->dev.emplace(dev.clone_for_internal_use());
    impl->handler = std::move(handler);
    impl->opt = opt;

    impl->storage.resize(opt.ring_blocks * opt.block_bytes);
    impl->lens.resize(opt.ring_blocks);
    impl->first_sample.resize(opt.ring_blocks);

    impl->stop_requested.store(false, std::memory_order_relaxed);
    impl->running.store(false, std::memory_order_relaxed);

    impl->consumer = std::thread([p = impl.get()] { p->consumer_loop(); });

    const int rc = hackrf_start_rx(impl->dev->native_handle(), &Impl::rx_callback, impl.get());
    if (rc != HACKRF_SUCCESS) {
        impl->stop_requested.store(true, std::memory_order_relaxed);
        impl->sem.release(); 
        if (impl->consumer.joinable()) {
            impl->consumer.join();
        }
        return std::unexpected(xxrf::core::make_error(rc, "hackrf_start_rx"));
    }

    impl->running.store(true, std::memory_order_relaxed);
    return RxStream{impl.release()};
}

RxStream::RxStream(RxStream&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

RxStream& RxStream::operator=(RxStream&& other) {
    if (this == &other) {
        return *this;
    }
    (void)stop();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

RxStream::~RxStream() noexcept {
    try {
        (void)stop();
    } catch (...) {
    }
    delete impl_;
    impl_ = nullptr;
}

void RxStream::request_stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    impl_->stop_requested.store(true, std::memory_order_relaxed);
    impl_->sem.release(); 
}

xxrf::core::Status RxStream::stop() {
    if (impl_ == nullptr) {
        return xxrf::core::ok();
    }

    if (impl_->consumer.joinable() && std::this_thread::get_id() == impl_->consumer.get_id()) {
        request_stop();
        return std::unexpected(make_local_error("[RxStream::stop] must not be called from RxHandler; use request_stop()"));
    }

    const bool was_running = impl_->running.exchange(false, std::memory_order_relaxed);

    impl_->stop_requested.store(true, std::memory_order_relaxed);

    
    impl_->sem.release();

    xxrf::core::Status st = xxrf::core::ok();
    if (was_running) {
        
        st = impl_->dev->stop_rx();
    }
    impl_->sem.release();
    if (impl_->consumer.joinable()) {
        impl_->consumer.join();
    }

    if (auto f = impl_->take_fatal(); f) {
        return std::unexpected(*f);
    }

    if (was_running && !st) {
        return std::unexpected(st.error());
    }

    return xxrf::core::ok();
}

RxStats RxStream::stats() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    RxStats s;
    s.blocks_received = impl_->blocks_received.load(std::memory_order_relaxed);
    s.blocks_dropped = impl_->blocks_dropped.load(std::memory_order_relaxed);
    s.blocks_truncated = impl_->blocks_truncated.load(std::memory_order_relaxed);
    s.bytes_received = impl_->bytes_received.load(std::memory_order_relaxed);
    s.ring_max_depth = impl_->ring_max_depth.load(std::memory_order_relaxed);
    return s;
}

} 
