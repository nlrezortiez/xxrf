#pragma once

#include "xxrf/core/context.hpp"
#include "xxrf/core/error.hpp"

#include <hackrf.h>
#include <optional>
#include <string>
#include <vector>

namespace xxrf::core {

struct DeviceInfo final {

    std::string serial;

    int usb_bus_sharing_count{0};
};

class DeviceList final {
public:
    static Result<DeviceList> enumerate();

    DeviceList() = delete;
    DeviceList(const DeviceList& other) = delete;
    DeviceList& operator=(const DeviceList& other) = delete;

    DeviceList(DeviceList&& other) noexcept;
    DeviceList& operator=(DeviceList&& other) noexcept;

    ~DeviceList();

    std::vector<DeviceInfo> devices() const;

private:
    explicit DeviceList(hackrf_device_list_t* list, std::optional<Context> ctx_guard) noexcept
        : ctx_guard_(std::move(ctx_guard)), list_(list) {}
    std::optional<Context> ctx_guard_{};
    hackrf_device_list_t* list_{nullptr};
};

} // namespace xxrf::core
