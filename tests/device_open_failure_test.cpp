#include <cstdlib>
#include <print>
#include <string>
#include <string_view>
#include <xxrf/core/context.hpp>
#include <xxrf/core/device.hpp>

int main() {
    auto ctx = xxrf::core::Context::create();
    if (!ctx) {
        return EXIT_SUCCESS;
    }

    constexpr std::string_view invalid_serial = "xxrf-invalid-serial-regression";
    auto dev = xxrf::core::Device::open_by_serial(invalid_serial);
    if (dev) {
        std::println(stderr, "Device::open_by_serial unexpectedly succeeded");
        return EXIT_FAILURE;
    }

    if (dev.error().code == -1) {
        std::println(stderr, "Device::open_by_serial returned synthesized null-handle error instead of libhackrf error");
        return EXIT_FAILURE;
    }

    if (dev.error().message.find("hackrf_open_by_serial") == std::string::npos) {
        std::println(stderr, "Device::open_by_serial error message lost callsite context: {}", dev.error().message);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
