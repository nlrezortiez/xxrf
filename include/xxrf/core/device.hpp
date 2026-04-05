#pragma once

#include "xxrf/core/context.hpp"
#include "xxrf/core/error.hpp"

#include <cstdint>
#include <hackrf.h>
#include <memory>
#include <string_view>

namespace xxrf::stream {
class RxStream;
}

namespace xxrf::core {

class Device final {
public:
    static Result<Device> open_first();
    static Result<Device> open_by_serial(std::string_view serial);

    Device() = delete;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;

    ~Device();

    Status set_sample_rate(const double hz);
    Status set_center_freq(const std::uint64_t hz);

    Status set_lna_gain(uint32_t db);
    Status set_vga_gain(uint32_t db);
    Status set_amp_enable(bool on);
    Status set_bias_tee_enable(bool on);
    Status set_hw_sync_mode(bool on);
    Status set_clkout_enable(bool on);
    Result<uint8_t> clkin_detected();

    [[nodiscard]] bool is_open() const noexcept;

    Status stop_rx();

private:
    struct State;

    explicit Device(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

    Device clone_for_internal_use() const noexcept { return Device{state_}; }
    hackrf_device* native_handle() noexcept;
    const hackrf_device* native_handle() const noexcept;

    std::shared_ptr<State> state_{};

    friend class xxrf::stream::RxStream;
};

} // namespace xxrf::core
