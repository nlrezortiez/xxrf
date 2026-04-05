#include "telemetry.hpp"

#include <cstdlib>
#include <print>

int main() {
    xxrf_viewer::SpscRing<4> ring;

    xxrf_viewer::AoASample sample;
    sample.sample_index = 1;
    ring.push_drop_oldest(sample);
    sample.sample_index = 2;
    ring.push_drop_oldest(sample);
    sample.sample_index = 3;
    ring.push_drop_oldest(sample);
    sample.sample_index = 4;
    ring.push_drop_oldest(sample);

    if (ring.drops() != 1) {
        std::println(stderr, "unexpected drops count after overflow: {}", ring.drops());
        return EXIT_FAILURE;
    }

    xxrf_viewer::AoASample out;
    if (!ring.pop(out) || out.sample_index != 2) {
        std::println(stderr, "unexpected first popped sample: {}", out.sample_index);
        return EXIT_FAILURE;
    }
    if (!ring.pop(out) || out.sample_index != 3) {
        std::println(stderr, "unexpected second popped sample: {}", out.sample_index);
        return EXIT_FAILURE;
    }
    if (!ring.pop(out) || out.sample_index != 4) {
        std::println(stderr, "unexpected third popped sample: {}", out.sample_index);
        return EXIT_FAILURE;
    }
    if (ring.pop(out)) {
        std::println(stderr, "ring unexpectedly returned extra sample");
        return EXIT_FAILURE;
    }

    ring.reset();
    if (ring.drops() != 0 || ring.pop(out)) {
        std::println(stderr, "reset did not clear ring state");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
