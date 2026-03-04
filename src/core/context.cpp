#include "xxrf/core/context.hpp"

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

Context::Context(Context&& other) noexcept : active_(std::exchange(other.active_, false)) {}

Context& Context::operator=(Context&& other) noexcept {
    if (this != &other) {
        active_ = other.active_;
        other.active_ = false;
    }

    return *this;
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