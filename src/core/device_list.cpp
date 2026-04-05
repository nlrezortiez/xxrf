#include "xxrf/core/device_list.hpp"

#include "xxrf/core/context.hpp"

#include <utility>

namespace xxrf::core {

Result<DeviceList> DeviceList::enumerate() {
    auto ctx_guard = Context::create();
    if (!ctx_guard) {
        return std::unexpected<Error>(ctx_guard.error());
    }

    hackrf_device_list_t* list = hackrf_device_list();
    if (list == nullptr) {
        return std::unexpected<Error>(
            Error{.code = -1, .message = "[DeviceList::enumerate] hackrf_device_list(): returned null"});
    }

    return DeviceList{list, std::optional<Context>{std::move(*ctx_guard)}};
}

DeviceList::DeviceList(DeviceList&& other) noexcept
    : ctx_guard_(std::move(other.ctx_guard_)), list_(std::exchange(other.list_, nullptr)) {}

DeviceList& DeviceList::operator=(DeviceList&& other) noexcept {
    if (this != &other) {
        if (list_ != nullptr) {
            hackrf_device_list_free(list_);
        }
        ctx_guard_ = std::move(other.ctx_guard_);
        list_ = std::exchange(other.list_, nullptr);
    }

    return *this;
}

DeviceList::~DeviceList() {
    if (list_ != nullptr) {
        hackrf_device_list_free(list_);
        list_ = nullptr;
    }
}

std::vector<DeviceInfo> DeviceList::devices() const {
    std::vector<DeviceInfo> out;
    if (list_ == nullptr || list_->devicecount <= 0) {
        return out;
    }

    out.reserve(static_cast<std::size_t>(list_->devicecount));
    for (int i = 0; i < list_->devicecount; ++i) {
        const char* sn = nullptr;
        if (list_->serial_numbers != nullptr) {
            sn = list_->serial_numbers[i];
            int sharing_usb_bus = hackrf_device_list_bus_sharing(list_, i);

            out.emplace_back(sn, sharing_usb_bus);
        }
    }

    return out;
}

} // namespace xxrf::core
