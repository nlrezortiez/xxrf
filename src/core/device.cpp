#include <utility>
#include <xxrf/core/device.hpp>

namespace xxrf::core {

static inline Result<void> check(int rc, const char* where) {
    if (rc == HACKRF_SUCCESS) {
        return ok();
    }

    return std::unexpected(make_error(rc, where));
}

Result<Device> Device::open_first() {
    hackrf_device* dev = nullptr;
    const int rc = hackrf_open(&dev);
    check(rc, "[Device::open_first] hackrf_open()");
    return Device{dev};
}

Result<Device> Device::open_by_serial(std::string_view serial) {
    hackrf_device* dev = nullptr;
    const int rc = hackrf_open_by_serial(serial.data(), &dev);
    check(rc, "[Device::open_by_serial] hackrf_open_by_serial");
    return Device{dev};
}

Device::Device(Device&& other) noexcept : dev_(std::exchange(other.dev_, nullptr)) {}

Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        this->~Device();
        dev_ = std::exchange(other.dev_, nullptr);
    }
    return *this;
}

Device::~Device() { close(); }

Status Device::close() {
    if (dev_ == nullptr) {
        return ok();
    }

    std::scoped_lock lc(api_mtx_);

    const int streaming = hackrf_is_streaming(dev_);
    if (streaming == HACKRF_TRUE) {
        hackrf_stop_rx(dev_);
        hackrf_stop_tx(dev_);
    }

    const int rc = hackrf_close(dev_);
    dev_ = nullptr;
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::close] hackrf_close"));
    }
    return ok();
}

Status Device::stop_rx() noexcept {
    if (dev_ == nullptr) {
        return xxrf::core::ok();
    }

    std::scoped_lock lk(api_mtx_);
    const int streaming = hackrf_is_streaming(dev_);
    if (streaming == HACKRF_TRUE) {
        const int rc = hackrf_stop_rx(dev_);
        if (rc != HACKRF_SUCCESS) {
            return std::unexpected(make_error(rc, "[Device::stop_rx] hackrf_stop_rx"));
        }
    }
    return ok();
}

Status Device::set_sample_rate(const double hz) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_sample_rate] null handle"});
    }
    return check(hackrf_set_sample_rate(dev_, hz), "[Device::set_sample_rate] hackrf_set_sample_rate");
}

Status Device::set_center_freq(const std::uint64_t hz) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_center_freq] null handle"});
    }
    return check(hackrf_set_freq(dev_, hz), "[Device::set_center_freq] hackrf_set_freq");
}

Status Device::set_lna_gain(std::uint32_t db) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_lna_gain] null handle"});
    }
    return check(hackrf_set_lna_gain(dev_, db), "[Device::set_lna_gain] hackrf_set_lna_gain");
}

Status Device::set_vga_gain(std::uint32_t db) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_vga_gain] null handle"});
    }
    return check(hackrf_set_vga_gain(dev_, db), "[Device::set_vga_gain] hackrf_set_vga_gain");
}

Status Device::set_amp_enable(bool on) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_amp_enable] null handle"});
    }
    return check(hackrf_set_amp_enable(dev_, on ? 1U : 0U), "[Device::set_amp_enable] hackrf_set_amp_enable");
}

Status Device::set_bias_tee_enable(bool on) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_bias_tee_enable] null handle"});
    }
    return check(hackrf_set_antenna_enable(dev_, on ? 1U : 0U),
                 "[Device::set_bias_tee_enable] hackrf_set_antenna_enable");
}

Status Device::set_hw_sync_mode(bool on) {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_hw_sync_mode] null handle"});
    }

    return check(hackrf_set_hw_sync_mode(dev_, on ? 1U : 0U), "[Device::set_hw_sync_enable] hackrf_set_hw_sync_mode");
}

Status Device::set_clkout_enable(bool on) noexcept {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_clkout_enable] null handle"});
    }

    const std::uint8_t v = on ? 1U : 0U;
    const int rc = hackrf_set_clkout_enable(dev_, v);
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::set_clkout_enable] hackrf_set_clkout_enable"));
    }
    return ok();
}

Result<std::uint8_t> Device::clkin_detected() {
    if (dev_ == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::clkin_detected] null handle"});
    }
    std::uint8_t status = 0;
    const int rc = hackrf_get_clkin_status(dev_, &status);
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::clkin_detected] hackrf_get_clkin_status"));
    }
    return status;
}

} // namespace xxrf::core