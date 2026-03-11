
#pragma once

#include "xxrf/aoa/calibration.hpp"
#include "xxrf/aoa/types.hpp"
#include "xxrf/core/error.hpp"

namespace xxrf::aoa {


class Processor final {
public:
    static xxrf::core::Result<Processor> create(Config cfg, Calibration cal = {});

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

    
    
    void push(const InputFrameView& frame, FunctionRef<void(const Estimate&)> emit);

private:
    struct Impl;
    explicit Processor(Impl* p) noexcept : impl_(p) {}
    Impl* impl_{nullptr};
};

} 
