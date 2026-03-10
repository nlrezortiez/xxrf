
#pragma once

#include "xxrf/aoa/calibration.hpp"
#include "xxrf/aoa/types.hpp"
#include "xxrf/core/error.hpp"

namespace xxrf::aoa {

// AoAProcessor is NOT thread-safe; expected usage: called from a single pipeline thread.
class Processor final {
public:
    static xxrf::core::Result<Processor> create(Config cfg, Calibration cal = {}) noexcept;

    Processor() = delete;
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;

    Processor(Processor&&) noexcept;
    Processor& operator=(Processor&&) noexcept;

    ~Processor() noexcept;

    void reset() noexcept;

    void set_calibration(Calibration c) noexcept;
    Calibration calibration() const noexcept;

    Config config() const noexcept;
    Stats stats() const noexcept;

    // Push paired IQ frame. May emit zero or more estimates through `emit`.
    // Lifetime rule: spans inside emitted Estimate are none; Estimate is value type.
    void push(const InputFrameView& frame, FunctionRef<void(const Estimate&)> emit) noexcept;

private:
    struct Impl;
    explicit Processor(Impl* p) noexcept : impl_(p) {}
    Impl* impl_{nullptr};
};

} // namespace xxrf::aoa