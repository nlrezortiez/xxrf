#include <cstdlib>
#include <string>
#include <xxrf/core/device_list.hpp>

int main() {
    auto list = xxrf::core::DeviceList::enumerate();
    if (!list) {
        return (list.error().message.find("Context is not active") == std::string::npos) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    (void)list->devices();
    return EXIT_SUCCESS;
}
