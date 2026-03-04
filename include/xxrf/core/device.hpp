#pragma once

#include "xxrf/core/error.hpp"

#include <cstdint>
#include <hackrf.h>
#include <mutex>
#include <string_view>

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

    hackrf_device* native_handle() noexcept { return dev_; }
    const hackrf_device* native_handle() const noexcept { return dev_; }

    Status stop_rx() noexcept;

private:
    explicit Device(hackrf_device* dev) noexcept : dev_(dev) {}
    Result<void> close();
    mutable std::mutex api_mtx_;
    hackrf_device* dev_{nullptr};
};

} // namespace xxrf::core
