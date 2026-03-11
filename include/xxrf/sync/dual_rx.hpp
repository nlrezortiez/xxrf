#pragma once

#include "xxrf/core/device.hpp"
#include "xxrf/core/error.hpp"
#include "xxrf/stream/rx_stream.hpp"

#include <chrono>
#include <functional>
#include <span>
#include <string>

namespace xxrf::sync {

enum class TriggerRole : std::uint8_t { TriggerOut, TriggerInWait };

enum class PairingMode : std::uint8_t { BySampleIndex, ByArrivalOrder };

struct DualRxDeviceId final {
    std::string serial;
    TriggerRole role = TriggerRole::TriggerInWait;
};

struct DualRxCommonSettings final {
    bool apply_common_settings = true;

    double sample_rate_hz = 10'000'000.0;

    std::uint64_t center_freq_hz = 100'000'000ULL;
    std::uint32_t lna_gain_db = 16;
    std::uint32_t vga_gain_db = 16;

    bool amp_enable = false;
    bool bias_tee_enable = false;
};

struct DualRxSyncOptions final {
    bool require_clkin_on_trigger_in = true;
    bool enable_clkout_on_trigger_out = true;

    bool enable_hardware_trigger = true;
    std::chrono::milliseconds arm_delay{50};
};

struct DualRxOptions final {
    DualRxCommonSettings settings{};
    DualRxSyncOptions sync{};

    xxrf::stream::RxStreamOptions stream{.ring_blocks = 64, .block_bytes = 262144};

    std::size_t staging_queue_blocks = 32;

    PairingMode pairing = PairingMode::BySampleIndex;

    std::uint64_t max_skew_samples = 0;
};

struct DualRxStats final {
    xxrf::stream::RxStats trigger_out{};
    xxrf::stream::RxStats trigger_in{};

    std::uint64_t pairs_emitted = 0;

    std::uint64_t drops_pairing_trigger_out = 0;
    std::uint64_t drops_pairing_trigger_in = 0;

    std::uint64_t max_abs_skew_samples = 0;

    std::uint64_t staging_max_depth_trigger_out = 0;
    std::uint64_t staging_max_depth_trigger_in = 0;
};

struct DualRxBlockView final {
    std::uint64_t first_sample_index = 0;

    std::span<const std::int8_t> iq_trigger_out;
    std::span<const std::int8_t> iq_trigger_in;

    std::uint64_t skew_samples = 0;
};

using DualRxHandler = std::function<void(const DualRxBlockView&)>;

class DualRx final {
public:
    static xxrf::core::Result<DualRx> start(const DualRxDeviceId& trigger_out, const DualRxDeviceId& trigger_in,
                                            DualRxHandler handler, DualRxOptions opt = {});

    static xxrf::core::Result<DualRx> start(xxrf::core::Device trigger_out_dev, xxrf::core::Device trigger_in_dev,
                                            DualRxHandler handler, DualRxOptions opt = {});

    DualRx() = delete;
    DualRx(const DualRx&) = delete;
    DualRx& operator=(const DualRx&) = delete;

    DualRx(DualRx&&) noexcept;
    DualRx& operator=(DualRx&&);

    ~DualRx() noexcept;

    xxrf::core::Status stop(bool drop_queued = true);

    void request_stop(bool drop_queued = true) noexcept;

    DualRxStats stats() const noexcept;

    xxrf::core::Device& device_trigger_out() noexcept;
    xxrf::core::Device& device_trigger_in() noexcept;

private:
    struct Impl;
    explicit DualRx(Impl* impl) noexcept : impl_(impl) {}

    Impl* impl_{nullptr};
};

} 
