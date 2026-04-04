#include <cstdlib>
#include <utility>
#include <xxrf/core/context.hpp>
#include <xxrf/core/device_list.hpp>

int main() {
    auto ctx = xxrf::core::Context::create();
    if (!ctx) {
        return EXIT_SUCCESS;
    }

    auto list1 = xxrf::core::DeviceList::enumerate();
    if (!list1) {
        return EXIT_SUCCESS;
    }

    auto list2 = xxrf::core::DeviceList::enumerate();
    if (!list2) {
        return EXIT_SUCCESS;
    }

    *list1 = std::move(*list2);
    (void)list1->devices();

    return EXIT_SUCCESS;
}
