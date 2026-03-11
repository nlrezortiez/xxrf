#include <cstdlib>
#include <string_view>
#include <xxrf/core/device.hpp>

int main() {
    constexpr std::string_view invalid_serial = "xxrf-invalid-serial-without-context";
    auto dev = xxrf::core::Device::open_by_serial(invalid_serial);
    if (dev) {
        return EXIT_FAILURE;
    }

    return (dev.error().code == -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}
