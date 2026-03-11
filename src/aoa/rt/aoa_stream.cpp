#include "xxrf/aoa/rt/aoa_stream.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace xxrf::aoa::rt {

struct Stream::Impl final {
    std::optional<xxrf::sync::DualRx> dual;   
    std::optional<xxrf::aoa::Processor> proc; 
    ResultHandler handler;
    StreamOptions opt{};

    std::atomic<bool> stop_requested{false};

    [[nodiscard]] StreamStats stats() const noexcept {
        StreamStats s;
        s.dual = dual ? dual->stats() : xxrf::sync::DualRxStats{};
        s.aoa = proc ? proc->stats() : xxrf::aoa::Stats{};
        return s;
    }
};

xxrf::core::Result<Stream> Stream::start(const xxrf::sync::DualRxDeviceId& trigger_out,
                                         const xxrf::sync::DualRxDeviceId& trigger_in, xxrf::aoa::Processor proc,
                                         ResultHandler handler, StreamOptions opt) {
    if (!handler) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "AoAStream::start: handler is empty"});
    }

    
    const auto cfg = proc.config();
    opt.dual.settings.apply_common_settings = true;
    opt.dual.settings.sample_rate_hz = cfg.sample_rate_hz;
    opt.dual.settings.center_freq_hz = cfg.center_freq_hz;

    
    auto impl = std::make_unique<Impl>();
    impl->opt = opt;
    impl->handler = std::move(handler);
    impl->proc.emplace(std::move(proc));

    
    auto dualr = xxrf::sync::DualRx::start(
        trigger_out, trigger_in,
        [p = impl.get()](const xxrf::sync::DualRxBlockView& blk) {
            
            if (p == nullptr || !p->proc.has_value()) {
                return;
            }

            auto& proc_ref = *p->proc;

            if (p->opt.require_zero_skew && blk.skew_samples != 0) {
                return;
            }

            xxrf::aoa::InputFrameView f;
            f.first_sample_index = blk.first_sample_index;
            f.iq0_i8q8 = blk.iq_trigger_out; 
            f.iq1_i8q8 = blk.iq_trigger_in;  
            f.skew_samples = blk.skew_samples;

            proc_ref.push(f,
                          xxrf::aoa::FunctionRef<void(const xxrf::aoa::Estimate&)>([&](const xxrf::aoa::Estimate& est) {
                              
                              if (p->handler) {
                                  p->handler(est);
                              }
                          }));
        },
        impl->opt.dual);

    if (!dualr) {
        return std::unexpected(dualr.error());
    }

    
    impl->dual.emplace(std::move(*dualr));

    return Stream{impl.release()};
}

Stream::Stream(Stream&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

Stream& Stream::operator=(Stream&& other) {
    if (this == &other) {
        return *this;
    }
    (void)stop();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

Stream::~Stream() noexcept {
    try {
        (void)stop();
    } catch (...) {
    }
    delete impl_;
    impl_ = nullptr;
}

void Stream::request_stop() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    impl_->stop_requested.store(true, std::memory_order_relaxed);
    if (impl_->dual) {
        impl_->dual->request_stop(impl_->opt.drop_on_stop);
    }
}

xxrf::core::Status Stream::stop() {
    if (impl_ == nullptr) {
        return xxrf::core::ok();
    }
    impl_->stop_requested.store(true, std::memory_order_relaxed);

    if (impl_->dual) {
        return impl_->dual->stop(impl_->opt.drop_on_stop);
    }
    return xxrf::core::ok();
}

StreamStats Stream::stats() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->stats();
}

} 
