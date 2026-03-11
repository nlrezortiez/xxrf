#pragma once

#include "viewer_state.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <xxrf/xxrf.hpp>

std::uint64_t now_monotonic_ns() noexcept;
std::vector<std::string> enumerate_hackrf_serials();
xxrf::core::Result<xxrf::aoa::rt::Stream> start_stream(ViewerState& vs) noexcept;
void stop_stream(ViewerState& vs) noexcept;
