#pragma once

#include "xxrf/core/device.hpp"
#include "xxrf/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace xxrf::stream {

struct RxStreamOptions final {
    std::size_t ring_blocks = 256;
    std::size_t block_bytes = 262144;
};

struct RxStats final {
    std::uint64_t blocks_received = 0;
    std::uint64_t blocks_dropped = 0;
    std::uint64_t blocks_truncated = 0;
    std::uint64_t bytes_received = 0;
    std::uint64_t ring_max_depth = 0;
};

struct RxBlock final {
    std::uint64_t first_sample_index = 0;
    std::span<const std::int8_t> iq_interleaved;
};

using RxHandler = std::function<void(const RxBlock&)>;

class RxStream final {
public:
    static xxrf::core::Result<RxStream> start(xxrf::core::Device& dev, RxHandler handler,
                                              RxStreamOptions opt = {}) noexcept;

    RxStream() = delete;
    RxStream(const RxStream&) = delete;
    RxStream& operator=(const RxStream&) = delete;

    RxStream(RxStream&&) noexcept;
    RxStream& operator=(RxStream&&) noexcept;

    ~RxStream() noexcept;

    xxrf::core::Status stop() noexcept;
    void request_stop() noexcept;
    RxStats stats() const noexcept;

private:
    struct Impl;
    explicit RxStream(Impl* impl) noexcept : impl_(impl) {}
    Impl* impl_{nullptr};
};

} // namespace xxrf::stream