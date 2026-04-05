#pragma once

#include "xxrf/aoa/processor.hpp"
#include "xxrf/aoa/types.hpp"
#include "xxrf/core/error.hpp"
#include "xxrf/sync/dual_rx.hpp"

#include <functional>

namespace xxrf::aoa::rt {

using ResultHandler = std::function<void(const xxrf::aoa::Estimate&)>;

struct StreamOptions final {
    xxrf::sync::DualRxOptions dual{};

    bool require_zero_skew = false;
    bool drop_on_stop = true;
};

struct StreamStats final {
    xxrf::sync::DualRxStats dual{};
    xxrf::aoa::Stats aoa{};
};

class Stream final {
public:
    static xxrf::core::Result<Stream> start(const xxrf::sync::DualRxDeviceId& trigger_out,
                                            const xxrf::sync::DualRxDeviceId& trigger_in, xxrf::aoa::Processor proc,
                                            ResultHandler handler, StreamOptions opt = {});

    Stream() = delete;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    Stream(Stream&&) noexcept;
    Stream& operator=(Stream&&);

    ~Stream() noexcept;

    xxrf::core::Status stop();
    void request_stop() noexcept;
    StreamStats stats() const noexcept;

private:
    struct Impl;
    explicit Stream(Impl* p) noexcept : impl_(p) {}
    Impl* impl_{nullptr};
};

} // namespace xxrf::aoa::rt
