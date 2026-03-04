#pragma once

#include "xxrf/core/error.hpp"

#include <atomic>
#include <hackrf.h>

namespace xxrf::core {

/*
 *  RAII wrapper above hackrf_init()/hackrf_exit()
 */

class Context final {
public:
    static Result<Context> create();

    Context() = delete;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&&) noexcept;
    Context& operator=(Context&& other) noexcept;

    ~Context();

    std::string version() const noexcept;
    std::string release() const noexcept;

private:
    explicit Context(bool active) noexcept : active_(active) {}
    bool active_{false};
    static inline std::atomic_uint ref_count_{0};
};

} // namespace xxrf::core