#include <cstdlib>
#include <hackrf.h>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <xxrf/core/context.hpp>

int main() {
    static_assert(std::is_move_assignable_v<xxrf::core::Context>);

    [[maybe_unused]] auto create_fn = &xxrf::core::Context::create;
    [[maybe_unused]] auto version_fn = &xxrf::core::Context::version;
    [[maybe_unused]] auto release_fn = &xxrf::core::Context::release;

    auto ctx1 = xxrf::core::Context::create();
    if (!ctx1) {
        return EXIT_SUCCESS;
    }

    const char* raw_version = hackrf_library_version();
    const char* raw_release = hackrf_library_release();

    if (ctx1->version() != std::string(raw_version != nullptr ? raw_version : "")) {
        std::println(stderr, "Context::version mismatch");
        return EXIT_FAILURE;
    }

    if (ctx1->release() != std::string(raw_release != nullptr ? raw_release : "")) {
        std::println(stderr, "Context::release mismatch");
        return EXIT_FAILURE;
    }

    auto ctx2 = xxrf::core::Context::create();
    if (!ctx2) {
        return EXIT_SUCCESS;
    }

    *ctx1 = std::move(*ctx2);

    auto ctx3 = xxrf::core::Context::create();
    if (!ctx3) {
        return EXIT_SUCCESS;
    }

    if (ctx1->version().empty()) {
        std::println(stderr, "Context::version returned empty string after move-assign");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
