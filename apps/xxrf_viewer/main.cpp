#include "viewer_actions.hpp"
#include "viewer_state.hpp"
#include "viewer_ui.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cmath>
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

        xxrf_viewer::AoASample s;
        while (vs.q.pop(s)) {
            vs.last = s;
            vs.has_fix = true;
            vs.emitted_estimates++;
            push_history_sample(vs.theta_history, vs.theta_head, vs.theta_count,
                                s.theta_rad * (180.0F / std::numbers::pi_v<float>));
            push_history_sample(vs.coherence_history, vs.coherence_head, vs.coherence_count, s.coherence);

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
