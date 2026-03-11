#include <cstdlib>
#include <print>
#include <utility>
#include <xxrf/core/context.hpp>
#include <xxrf/core/device_list.hpp>

int main() {
    xxrf::core::Result<xxrf::core::DeviceList> listr = std::unexpected(xxrf::core::Error{});
    {
        auto ctx = xxrf::core::Context::create();
        if (!ctx) {
            return EXIT_SUCCESS;
        }

        listr = xxrf::core::DeviceList::enumerate();
        if (!listr) {
            std::println(stderr, "DeviceList::enumerate failed: {}", listr.error().message);
            return EXIT_FAILURE;
        }
    }

    auto devices = listr->devices();
    if (!devices.empty() && devices.front().serial.empty()) {
        std::println(stderr, "DeviceList::devices returned malformed device info after outer Context destruction");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
