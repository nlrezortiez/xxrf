#include "viewer_state.hpp"

#include <cstdlib>
#include <print>

int main() {
    std::array<float, 4> values{};
    std::size_t head = 0;
    std::size_t count = 0;

    push_history_sample(values, head, count, 1.0f);
    push_history_sample(values, head, count, 2.0f);
    push_history_sample(values, head, count, 3.0f);
    push_history_sample(values, head, count, 4.0f);
    push_history_sample(values, head, count, 5.0f);
    push_history_sample(values, head, count, 6.0f);

    std::array<float, 4> plot{};
    const std::size_t plot_count = build_history_plot(values, head, count, plot);
    if (plot_count != 4) {
        std::println(stderr, "unexpected plot_count: {}", plot_count);
        return EXIT_FAILURE;
    }

    const std::array<float, 4> expected{3.0f, 4.0f, 5.0f, 6.0f};
    if (plot != expected) {
        std::println(stderr, "history plot ordering mismatch");
        return EXIT_FAILURE;
    }

    head = 0;
    count = 0;
    values = {};
    const std::size_t empty_count = build_history_plot(values, head, count, plot);
    if (empty_count != 1 || plot[0] != 0.0f) {
        std::println(stderr, "empty history plot mismatch");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
