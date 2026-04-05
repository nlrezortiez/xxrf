#include "xxrf/core/context.hpp"

#include <string>
#include <utility>

namespace xxrf::core {

Result<Context> Context::create() {
    const unsigned prev = ref_count_.fetch_add(1, std::memory_order_acq_rel);
    if (prev == 0) {
        const int rc = hackrf_init();
        if (rc != HACKRF_SUCCESS) {
            ref_count_.fetch_sub(1, std::memory_order_acq_rel);
            return std::unexpected(make_error(rc, "[Context::create] hackrf_init"));
        }
    }

    return Context{true};
}

bool Context::is_active() noexcept { return ref_count_.load(std::memory_order_acquire) != 0; }

Context::Context(Context&& other) noexcept : active_(std::exchange(other.active_, false)) {}

Context& Context::operator=(Context&& other) noexcept {
    if (this != &other) {
        if (active_) {
            const unsigned prev = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1) {
                hackrf_exit();
            }
        }
        active_ = std::exchange(other.active_, false);
    }

    return *this;
}

std::string Context::version() const {
    const char* v = hackrf_library_version();
    return (v != nullptr) ? std::string{v} : std::string{};
}

std::string Context::release() const {
    const char* v = hackrf_library_release();
    return (v != nullptr) ? std::string{v} : std::string{};
}

Context::~Context() {
    if (!active_) {
        return;
    }

    const unsigned prev = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        hackrf_exit();
    }
}

} // namespace xxrf::core
