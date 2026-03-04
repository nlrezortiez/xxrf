#pragma once

#include <expected>
#include <format>
#include <hackrf.h>
#include <string>
#include <string_view>

namespace xxrf::core {

struct Error final {

    /* Native libhackrf error code */
    int code{};

    /* Human readable message */
    std::string message{};
};

template <typename T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

inline Error make_error(int code, std::string_view where) {
    const char* what = hackrf_error_name(static_cast<hackrf_error>(code));
    std::string msg = std::format("{}: {} ({})\n", where, (what ? what : "(unknown)"), code);

    return Error{.code = code, .message = std::move(msg)};
}

inline Status ok() { return {}; }

} // namespace xxrf::core