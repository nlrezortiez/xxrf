#include <print>
#include <xxrf/xxrf.hpp>

int main() {
    auto list = xxrf::core::DeviceList::enumerate();
    if (!list) {
        std::println(stderr, "Enumerate failed: {}", list.error().message);
        return EXIT_FAILURE;
    }

    auto devices = list->devices();
    std::println("Found {} device(s)", devices.size());
    for (const auto& d : devices) {
        std::println("  serial='{}', usb_bus_sharing={}", d.serial, d.usb_bus_sharing_count);
    }

    return EXIT_SUCCESS;
}
