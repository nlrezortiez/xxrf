#include "xxrf/aoa/geometry.hpp"
#include "xxrf/aoa/processor.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <format>
#include <memory>
#include <new>
#include <vector>

namespace xxrf::aoa {

static inline float i8_to_f(std::int8_t v) noexcept {
    
    return static_cast<float>(v) * (1.0F / 128.0F);
}

struct Processor::Impl final {
    Config cfg{};
    Calibration cal{};

    
    std::vector<std::complex<double>> cross_ring;
    std::vector<double> p0_ring;
    std::vector<double> p1_ring;

    std::complex<double> cross_sum{0.0, 0.0};
    double p0_sum = 0.0;
    double p1_sum = 0.0;

    std::size_t ring_pos = 0;
    std::size_t filled = 0;     
    std::size_t since_emit = 0; 

    std::uint64_t expected_next_first = std::numeric_limits<std::uint64_t>::max();

    std::atomic<std::uint64_t> frames_in{0};
    std::atomic<std::uint64_t> samples_used{0};
    std::atomic<std::uint64_t> estimates_emitted{0};
    std::atomic<std::uint64_t> discontinuities{0};
    std::atomic<std::uint64_t> invalid_geometry{0};
    std::atomic<std::uint64_t> below_quality{0};

    void clear_accumulators() noexcept {
        cross_sum = {0.0, 0.0};
        p0_sum = 0.0;
        p1_sum = 0.0;
        ring_pos = 0;
        filled = 0;
        since_emit = 0;
        expected_next_first = std::numeric_limits<std::uint64_t>::max();
        std::fill(cross_ring.begin(), cross_ring.end(), std::complex<double>{0.0, 0.0});
        std::fill(p0_ring.begin(), p0_ring.end(), 0.0);
        std::fill(p1_ring.begin(), p1_ring.end(), 0.0);
    }

    void add_sample(std::complex<double> cross, double p0, double p1, std::uint64_t sample_index,
                    FunctionRef<void(const Estimate&)> emit) {
        
        if (filled < cfg.win.window_samples) {
            cross_ring[ring_pos] = cross;
            p0_ring[ring_pos] = p0;
            p1_ring[ring_pos] = p1;

            cross_sum += cross;
            p0_sum += p0;
            p1_sum += p1;

            ++filled;
            ring_pos = (ring_pos + 1) % cfg.win.window_samples;
        } else {
            
            cross_sum -= cross_ring[ring_pos];
            p0_sum -= p0_ring[ring_pos];
            p1_sum -= p1_ring[ring_pos];

            cross_ring[ring_pos] = cross;
            p0_ring[ring_pos] = p0;
            p1_ring[ring_pos] = p1;

            cross_sum += cross;
            p0_sum += p0;
            p1_sum += p1;

            ring_pos = (ring_pos + 1) % cfg.win.window_samples;
        }

        if (filled < cfg.win.window_samples) {
            return; 
        }

        ++since_emit;
        if (since_emit < cfg.win.hop_samples) {
            return;
        }
        since_emit = 0;

        
        Estimate est{};
        est.raw_phase_rad = std::atan2(cross_sum.imag(), cross_sum.real());

        
        double coh = 0.0;
        if (p0_sum > 0.0 && p1_sum > 0.0) {
            const double num = std::abs(cross_sum);
            const double den = std::sqrt(p0_sum * p1_sum);
            if (den > 0.0) {
                coh = std::clamp(num / den, 0.0, 1.0);
            }
        }

        est.quality.coherence = coh;
        est.quality.mean_p0 = p0_sum / static_cast<double>(cfg.win.window_samples);
        est.quality.mean_p1 = p1_sum / static_cast<double>(cfg.win.window_samples);

        auto ang = phase_to_angle(est.raw_phase_rad, cfg.geom, cfg.center_freq_hz, cfg.clamp_sin);
        if (!ang.ok) {
            invalid_geometry.fetch_add(1, std::memory_order_relaxed);
            est.quality.ok = false;
            
            return;
        }

        est.theta_rad = ang.theta_rad;
        est.azimuth_rad = ang.azimuth_rad;

        const bool ok = (coh >= cfg.min_coherence);
        est.quality.ok = ok;

        
        
        const std::uint64_t half_span =
            static_cast<std::uint64_t>((cfg.win.window_samples / 2) * cfg.win.sample_stride);
        est.sample_index = (sample_index >= half_span) ? (sample_index - half_span) : sample_index;

        if (!ok) {
            below_quality.fetch_add(1, std::memory_order_relaxed);
            if (!cfg.emit_below_quality) {
                return;
            }
        }

        emit(est);
        estimates_emitted.fetch_add(1, std::memory_order_relaxed);
    }
};

xxrf::core::Result<Processor> Processor::create(Config cfg, Calibration cal) {
    
    if (cfg.method != Method::PhaseInterferometry) {
        return std::unexpected(
            xxrf::core::Error{.code = -1, .message = "AoA Processor: only PhaseInterferometry is implemented"});
    }
    if (!(cfg.sample_rate_hz > 0.0)) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "AoA Processor: sample_rate_hz must be > 0"});
    }
    if (cfg.center_freq_hz == 0) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "AoA Processor: center_freq_hz must be > 0"});
    }
    if (!(cfg.geom.baseline_m > 0.0)) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "AoA Processor: geom.baseline_m must be > 0"});
    }
    const double max_baseline_m = max_unambiguous_baseline_m(cfg.center_freq_hz);
    if (!(cfg.geom.baseline_m <= max_baseline_m)) {
        return std::unexpected(xxrf::core::Error{
            .code = -1,
            .message =
                std::format("AoA Processor: geom.baseline_m must be <= lambda/2 ({:.6f} m at {} Hz)",
                            max_baseline_m, cfg.center_freq_hz),
        });
    }
    if (cfg.win.window_samples == 0 || cfg.win.hop_samples == 0) {
        return std::unexpected(
            xxrf::core::Error{.code = -1, .message = "AoA Processor: window_samples and hop_samples must be > 0"});
    }
    if (cfg.win.hop_samples > cfg.win.window_samples) {
        return std::unexpected(
            xxrf::core::Error{.code = -1, .message = "AoA Processor: hop_samples must be <= window_samples"});
    }
    if (cfg.win.sample_stride == 0) {
        return std::unexpected(xxrf::core::Error{.code = -1, .message = "AoA Processor: sample_stride must be >= 1"});
    }
    if (cfg.min_coherence < 0.0 || cfg.min_coherence > 1.0) {
        return std::unexpected(
            xxrf::core::Error{.code = -1, .message = "AoA Processor: min_coherence must be in [0,1]"});
    }

    auto impl = std::make_unique<Impl>();
    impl->cfg = cfg;
    impl->cal = cal;

    impl->cross_ring.resize(cfg.win.window_samples);
    impl->p0_ring.resize(cfg.win.window_samples);
    impl->p1_ring.resize(cfg.win.window_samples);
    impl->clear_accumulators();

    return Processor{impl.release()};
}

Processor::Processor(Processor&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }

Processor& Processor::operator=(Processor&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

Processor::~Processor() noexcept {
    delete impl_;
    impl_ = nullptr;
}

void Processor::reset() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    impl_->clear_accumulators();
}

void Processor::set_calibration(Calibration c) noexcept {
    if (impl_ == nullptr) {
        return;
    }
    impl_->cal = c;
}

Calibration Processor::calibration() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->cal;
}

Config Processor::config() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->cfg;
}

Stats Processor::stats() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }

    Stats st;
    st.frames_in = impl_->frames_in.load(std::memory_order_relaxed);
    st.samples_used = impl_->samples_used.load(std::memory_order_relaxed);
    st.estimates_emitted = impl_->estimates_emitted.load(std::memory_order_relaxed);
    st.discontinuities = impl_->discontinuities.load(std::memory_order_relaxed);
    st.invalid_geometry = impl_->invalid_geometry.load(std::memory_order_relaxed);
    st.below_quality = impl_->below_quality.load(std::memory_order_relaxed);
    return st;
}

void Processor::push(const InputFrameView& frame, FunctionRef<void(const Estimate&)> emit) {
    if (impl_ == nullptr) {
        return;
    }

    impl_->frames_in.fetch_add(1, std::memory_order_relaxed);

    
    const std::size_t n0 = (frame.iq0_i8q8.size() / 2);
    const std::size_t n1 = (frame.iq1_i8q8.size() / 2);
    const std::size_t n = std::min(n0, n1);

    if (n == 0) {
        return;
    }

    
    if (impl_->cfg.require_contiguous) {
        if (impl_->expected_next_first != std::numeric_limits<std::uint64_t>::max()) {
            if (frame.first_sample_index != impl_->expected_next_first) {
                impl_->discontinuities.fetch_add(1, std::memory_order_relaxed);
                impl_->clear_accumulators();
            }
        }
        
        impl_->expected_next_first = frame.first_sample_index + static_cast<std::uint64_t>(n);
    } else {
        impl_->expected_next_first = std::numeric_limits<std::uint64_t>::max();
    }

    const std::size_t stride = impl_->cfg.win.sample_stride;

    
    
    const std::complex<float> g = impl_->cfg.apply_calibration ? impl_->cal.ch1_gain : std::complex<float>{1.0F, 0.0F};

    for (std::size_t i = 0; i < n; i += stride) {
        const std::size_t b0 = 2 * i;
        const std::size_t b1 = 2 * i;

        const float i0 = i8_to_f(frame.iq0_i8q8[b0 + 0]);
        const float q0 = i8_to_f(frame.iq0_i8q8[b0 + 1]);

        const float i1 = i8_to_f(frame.iq1_i8q8[b1 + 0]);
        const float q1 = i8_to_f(frame.iq1_i8q8[b1 + 1]);

        std::complex<float> x0{i0, q0};
        std::complex<float> x1{i1, q1};
        x1 *= g;

        const std::complex<double> x0d{static_cast<double>(x0.real()), static_cast<double>(x0.imag())};
        const std::complex<double> x1d{static_cast<double>(x1.real()), static_cast<double>(x1.imag())};

        const std::complex<double> cross = std::conj(x0d) * x1d;
        const double p0 = std::norm(x0d);
        const double p1 = std::norm(x1d);

        const std::uint64_t sample_index = frame.first_sample_index + static_cast<std::uint64_t>(i);

        impl_->samples_used.fetch_add(1, std::memory_order_relaxed);
        impl_->add_sample(cross, p0, p1, sample_index, emit);
    }
}

} 
