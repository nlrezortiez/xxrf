#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace xxrf_viewer {

struct AoASample final {
    std::uint64_t t_ns = 0;
    std::uint64_t sample_index = 0;
    float theta_rad = 0.0f;
    float coherence = 0.0f;
};

template <std::size_t N>
class SpscRing final {
    static_assert((N & (N - 1)) == 0, "N must be power of two");

public:
    bool push_drop_oldest(const AoASample& v) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1u) & mask_;

        std::size_t tail = tail_.load(std::memory_order_acquire);
        if (next == tail) {
            tail = (tail + 1u) & mask_;
            tail_.store(tail, std::memory_order_release);
            drops_.fetch_add(1, std::memory_order_relaxed);
        }

        buf_[head] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(AoASample& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        out = buf_[tail];
        tail_.store((tail + 1u) & mask_, std::memory_order_release);
        return true;
    }

    std::uint64_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }

    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        drops_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t mask_ = N - 1u;
    std::array<AoASample, N> buf_{};

    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::atomic<std::uint64_t> drops_{0};
};

} 