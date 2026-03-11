#include "xxrf/core/context.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <xxrf/core/device.hpp>

namespace xxrf::core {

static inline Result<void> check(int rc, const char* where) {
    if (rc == HACKRF_SUCCESS) {
        return ok();
    }

    return std::unexpected(make_error(rc, where));
}

struct Device::State final {
    explicit State(hackrf_device* device, Context ctx) noexcept : ctx_guard(std::move(ctx)), dev(device) {}

    ~State() noexcept {
        if (dev == nullptr) {
            return;
        }

        std::scoped_lock lk(api_mtx);

        const int streaming = hackrf_is_streaming(dev);
        if (streaming == HACKRF_TRUE) {
            (void)hackrf_stop_rx(dev);
            (void)hackrf_stop_tx(dev);
        }

        (void)hackrf_close(dev);
        dev = nullptr;
    }

    std::mutex api_mtx;
    std::optional<Context> ctx_guard;
    hackrf_device* dev{nullptr};
};

Result<Device> Device::open_first() {
    auto ctx_guard = Context::create();
    if (!ctx_guard) {
        return std::unexpected(ctx_guard.error());
    }

    hackrf_device* dev = nullptr;
    const int rc = hackrf_open(&dev);
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::open_first] hackrf_open()"));
    }
    return Device{std::make_shared<State>(dev, std::move(*ctx_guard))};
}

Result<Device> Device::open_by_serial(std::string_view serial) {
    auto ctx_guard = Context::create();
    if (!ctx_guard) {
        return std::unexpected(ctx_guard.error());
    }

    const std::string serial_copy{serial};
    hackrf_device* dev = nullptr;
    const int rc = hackrf_open_by_serial(serial_copy.c_str(), &dev);
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::open_by_serial] hackrf_open_by_serial"));
    }
    return Device{std::make_shared<State>(dev, std::move(*ctx_guard))};
}

Device::Device(Device&& other) noexcept : state_(std::move(other.state_)) {}

Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

Device::~Device() = default;

bool Device::is_open() const noexcept { return state_ != nullptr && state_->dev != nullptr; }

hackrf_device* Device::native_handle() noexcept { return state_ ? state_->dev : nullptr; }

const hackrf_device* Device::native_handle() const noexcept { return state_ ? state_->dev : nullptr; }

Status Device::stop_rx() {
    if (!state_ || state_->dev == nullptr) {
        return xxrf::core::ok();
    }

    std::scoped_lock lk(state_->api_mtx);
    const int streaming = hackrf_is_streaming(state_->dev);
    if (streaming == HACKRF_TRUE) {
        const int rc = hackrf_stop_rx(state_->dev);
        if (rc != HACKRF_SUCCESS) {
            return std::unexpected(make_error(rc, "[Device::stop_rx] hackrf_stop_rx"));
        }
    }
    return ok();
}

Status Device::set_sample_rate(const double hz) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_sample_rate] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_sample_rate(state_->dev, hz), "[Device::set_sample_rate] hackrf_set_sample_rate");
}

Status Device::set_center_freq(const std::uint64_t hz) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_center_freq] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_freq(state_->dev, hz), "[Device::set_center_freq] hackrf_set_freq");
}

Status Device::set_lna_gain(std::uint32_t db) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_lna_gain] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_lna_gain(state_->dev, db), "[Device::set_lna_gain] hackrf_set_lna_gain");
}

Status Device::set_vga_gain(std::uint32_t db) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_vga_gain] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_vga_gain(state_->dev, db), "[Device::set_vga_gain] hackrf_set_vga_gain");
}

Status Device::set_amp_enable(bool on) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_amp_enable] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_amp_enable(state_->dev, on ? 1U : 0U), "[Device::set_amp_enable] hackrf_set_amp_enable");
}

Status Device::set_bias_tee_enable(bool on) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_bias_tee_enable] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_antenna_enable(state_->dev, on ? 1U : 0U),
                 "[Device::set_bias_tee_enable] hackrf_set_antenna_enable");
}

Status Device::set_hw_sync_mode(bool on) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_hw_sync_mode] null handle"});
    }

    std::scoped_lock lk(state_->api_mtx);
    return check(hackrf_set_hw_sync_mode(state_->dev, on ? 1U : 0U),
                 "[Device::set_hw_sync_enable] hackrf_set_hw_sync_mode");
}

Status Device::set_clkout_enable(bool on) {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::set_clkout_enable] null handle"});
    }

    std::scoped_lock lk(state_->api_mtx);
    const std::uint8_t v = on ? 1U : 0U;
    const int rc = hackrf_set_clkout_enable(state_->dev, v);
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::set_clkout_enable] hackrf_set_clkout_enable"));
    }
    return ok();
}

Result<std::uint8_t> Device::clkin_detected() {
    if (!state_ || state_->dev == nullptr) {
        return std::unexpected(Error{.code = -1, .message = "[Device::clkin_detected] null handle"});
    }
    std::scoped_lock lk(state_->api_mtx);
    std::uint8_t status = 0;
    const int rc = hackrf_get_clkin_status(state_->dev, &status);
    if (rc != HACKRF_SUCCESS) {
        return std::unexpected(make_error(rc, "[Device::clkin_detected] hackrf_get_clkin_status"));
    }
    return status;
}

} 
