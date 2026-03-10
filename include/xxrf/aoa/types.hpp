#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace xxrf::aoa {

enum class Method : std::uint8_t {
    PhaseInterferometry = 0,
    // TODO: TdoaGccPhat, CrossSpectrum, Music...
};

struct ArrayGeometry final {
    double baseline_m = 0.0;

    double baseline_azimuth_rad = 0.0;
};

struct Windowing final {
    std::size_t window_samples = 4096;
    std::size_t hop_samples = 2048;
    std::size_t sample_stride = 1;
};

struct InputFrameView final {
    std::uint64_t first_sample_index = 0;
    std::span<const std::int8_t> iq0_i8q8;
    std::span<const std::int8_t> iq1_i8q8;
    std::uint64_t skew_samples = 0;
};

struct Quality final {
    double coherence = 0.0;
    double mean_p0 = 0.0;
    double mean_p1 = 0.0;

    bool ok = false;
};

struct Estimate final {
    std::uint64_t sample_index = 0;
    double azimuth_rad = 0.0;
    double theta_rad = 0.0;
    double raw_phase_rad = 0.0;

    Quality quality{};
};

struct Config final {
    Method method = Method::PhaseInterferometry;
    double sample_rate_hz = 0.0;
    std::uint64_t center_freq_hz = 0;

    ArrayGeometry geom{};
    Windowing win{};

    double min_coherence = 0.6;
    bool clamp_sin = false;
    bool require_contiguous = true;
    bool apply_calibration = true;
};

// non-owning function wrapper (no allocations), similar to llvm::function_ref.
template <class Signature>
class FunctionRef;

template <class R, class... Args>
class FunctionRef<R(Args...)> final {
public:
    FunctionRef() = delete;

    template <class F>
    FunctionRef(F&& f) noexcept
        : obj_(static_cast<void*>(std::addressof(f))), call_(&invoke<std::remove_reference_t<F>>) {}

    R operator()(Args... args) const { return call_(obj_, std::forward<Args>(args)...); }

private:
    void* obj_{nullptr};
    R (*call_)(void*, Args...){nullptr};

    template <class F>
    static R invoke(void* obj, Args... args) {
        return (*static_cast<F*>(obj))(std::forward<Args>(args)...);
    }
};

struct Stats final {
    std::uint64_t frames_in = 0;
    std::uint64_t samples_used = 0;
    std::uint64_t estimates_emitted = 0;
    std::uint64_t discontinuities = 0;
    std::uint64_t invalid_geometry = 0;
    std::uint64_t below_quality = 0;
};

} // namespace xxrf::aoa