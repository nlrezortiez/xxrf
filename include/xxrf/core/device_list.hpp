#pragma once

#include "xxrf/core/error.hpp"

#include <hackrf.h>
#include <string>
#include <vector>

namespace xxrf::core {

struct DeviceInfo final {
    /* serial number of current device */
    std::string serial;

    /* how many devises sharing usb bus with current device */
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
    explicit DeviceList(hackrf_device_list_t* list) noexcept : list_(list) {}
    hackrf_device_list_t* list_{nullptr};
};

} // namespace xxrf::core