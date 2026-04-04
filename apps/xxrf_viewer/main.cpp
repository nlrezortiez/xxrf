#include "viewer_actions.hpp"
#include "viewer_state.hpp"
#include "viewer_ui.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cmath>
#include <deque>
#include <imgui.h>
#include <numbers>
#include <print>

int main() {
    if (glfwInit() == 0) {
        std::println(stderr, "glfwInit failed");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 960, "xxrf-peleng", nullptr, nullptr);
    if (window == nullptr) {
        std::println(stderr, "glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::println(stderr, "glewInit failed");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    load_viewer_fonts();
    apply_viewer_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    ViewerState vs;
    vs.serials = enumerate_hackrf_serials();
    if (vs.serials.size() >= 2) {
        vs.idx_out = 0;
        vs.idx_in = 1;
    }

    while (glfwWindowShouldClose(window) == 0) {
        glfwPollEvents();

        const std::uint64_t frame_now_ns = now_monotonic_ns();
        xxrf_viewer::AoASample s;
        while (vs.q.pop(s)) {
            vs.last = s;
            vs.has_fix = true;
            vs.emitted_estimates++;
            push_history_sample(vs.power_history, vs.power_head, vs.power_count, s.signal_power_dbfs);

            const bool gate_was_open = vs.signal_gate_open;
            const float gate_on_dbfs = vs.signal_threshold_dbfs;
            const float gate_off_dbfs = vs.signal_threshold_dbfs - std::max(0.1f, vs.signal_threshold_hysteresis_db);
            if (vs.signal_gate_open) {
                vs.signal_gate_open = s.signal_power_dbfs >= gate_off_dbfs;
            } else {
                vs.signal_gate_open = s.signal_power_dbfs >= gate_on_dbfs;
            }

            const bool power_ok = vs.signal_gate_open;
            const std::uint64_t window_ns =
                static_cast<std::uint64_t>(std::max(0.5, vs.rolling_window_s) * 1'000'000'000.0);
            while (!vs.rolling_samples.empty() && (s.t_ns - vs.rolling_samples.front().t_ns) > window_ns) {
                vs.rolling_samples.pop_front();
            }

            if (!power_ok) {
                if (gate_was_open) {
                    vs.smooth_init = false;
                }
                continue;
            }

            if (!gate_was_open) {
                vs.smooth_init = false;
            }

            vs.last_valid = s;
            vs.has_valid_fix = true;
            push_history_sample(vs.theta_history, vs.theta_head, vs.theta_count,
                                s.theta_rad * (180.0F / std::numbers::pi_v<float>));
            push_history_sample(vs.coherence_history, vs.coherence_head, vs.coherence_count, s.coherence);

            LiveWindowSample ws;
            ws.t_ns = s.t_ns;
            ws.sample_index = s.sample_index;
            ws.theta_deg = s.theta_rad * (180.0F / std::numbers::pi_v<float>);
            ws.coherence = s.coherence;
            ws.power_dbfs = s.signal_power_dbfs;
            vs.rolling_samples.push_back(ws);

            if (vs.calibration_running) {
                vs.calibration_acc.add(s);
            }

            const float nx = std::sin(s.theta_rad);
            const float ny = std::cos(s.theta_rad);

            if (!vs.smooth_init) {
                vs.sx = nx;
                vs.sy = ny;
                vs.smooth_init = true;
                vs.last_t_ns = s.t_ns;
            } else {
                const double dt = (s.t_ns > vs.last_t_ns) ? (double(s.t_ns - vs.last_t_ns) * 1e-9) : 0.0;
                vs.last_t_ns = s.t_ns;

                const double tau = std::max(1e-3, vs.smooth_tau_s);
                const double a = 1.0 - std::exp(-dt / tau);

                vs.sx = static_cast<float>(((1.0 - a) * vs.sx) + (a * nx));
                vs.sy = static_cast<float>(((1.0 - a) * vs.sy) + (a * ny));

                const float norm = std::sqrt((vs.sx * vs.sx) + (vs.sy * vs.sy));
                if (norm > 1e-6F) {
                    vs.sx /= norm;
                    vs.sy /= norm;
                } else {
                    vs.sx = nx;
                    vs.sy = ny;
                }
            }
        }

        if (vs.calibration_running) {
            const std::uint64_t calib_duration_ns =
                static_cast<std::uint64_t>(std::max(1.0, vs.calibration_duration_s) * 1'000'000'000.0);
            if (frame_now_ns >= (vs.calibration_started_ns + calib_duration_ns)) {
                vs.calibration_last = vs.calibration_acc.summary();
                vs.calibration_running = false;
                vs.calibration_acc.reset();
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_viewer_ui(vs);

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08F, 0.08F, 0.09F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    stop_stream(vs);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
