#include "viewer_ui.hpp"

#include "viewer_actions.hpp"
#include "widgets/compass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <numbers>
#include <string>

namespace {

ImFont* g_font_ui = nullptr;
ImFont* g_font_ui_large = nullptr;
ImFont* g_font_ui_small = nullptr;

std::string find_font_path(const char* filename) {
    namespace fs = std::filesystem;

    const std::array<fs::path, 3> candidates = {
        fs::path(XXRF_SOURCE_DIR) / "assets" / filename,
        fs::current_path() / "assets" / filename,
        fs::current_path().parent_path() / "assets" / filename,
    };

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            return path.string();
        }
    }
    return {};
}

std::string shorten_serial(const std::string& serial) {
    if (serial.empty()) {
        return "-";
    }
    constexpr std::size_t tail_len = 5;
    if (serial.size() <= tail_len) {
        return serial;
    }
    return std::format("...{}", serial.substr(serial.size() - tail_len));
}

void begin_panel(const char* id, const ImVec2& size, const ImVec4& bg) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.760f, 0.810f, 0.860f, 0.980f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().ChildRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void end_panel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void draw_section_title(const char* title, const char* subtitle = nullptr) {
    if (g_font_ui_large != nullptr) {
        ImGui::PushFont(g_font_ui_large);
    }
    ImGui::TextColored(ImVec4(0.120f, 0.170f, 0.235f, 1.0f), "%s", title);
    if (g_font_ui_large != nullptr) {
        ImGui::PopFont();
    }
    if (subtitle != nullptr && subtitle[0] != '\0') {
        if (g_font_ui_small != nullptr) {
            ImGui::PushFont(g_font_ui_small);
        }
        ImGui::TextColored(ImVec4(0.280f, 0.360f, 0.450f, 1.0f), "%s", subtitle);
        if (g_font_ui_small != nullptr) {
            ImGui::PopFont();
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

const char* allowed_side_label(AllowedAoASide side) {
    switch (side) {
    case AllowedAoASide::Both:
        return "Обе";
    case AllowedAoASide::Positive:
        return "Положительная (+)";
    case AllowedAoASide::Negative:
        return "Отрицательная (-)";
    }
    return "Обе";
}

void draw_status_chip(const char* label, const ImVec4& text_col, const ImVec4& bg_col) {
    ImGui::PushStyleColor(ImGuiCol_Button, bg_col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_col);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, text_col);
    ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

void draw_compact_row(const char* label, const std::string& value, const ImVec4& value_col) {
    ImGui::PushID(label);
    if (ImGui::BeginTable("##compact_row", 2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX |
                              ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.64f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (g_font_ui_small != nullptr) {
            ImGui::PushFont(g_font_ui_small);
        }
        ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "%s", label);
        if (g_font_ui_small != nullptr) {
            ImGui::PopFont();
        }

        ImGui::TableSetColumnIndex(1);
        const float text_w = ImGui::CalcTextSize(value.c_str()).x;
        const float col_w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + col_w - text_w));
        ImGui::TextColored(value_col, "%s", value.c_str());
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void draw_summary_tile(const char* id, const char* label, const std::string& value, const char* hint,
                       const ImVec2& size) {
    begin_panel(id, size, ImVec4(0.965f, 0.974f, 0.984f, 1.0f));
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.38f, 0.37f, 0.33f, 1.0f), "%s", label);
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    if (g_font_ui_large != nullptr) {
        ImGui::PushFont(g_font_ui_large);
    }
    ImGui::TextColored(ImVec4(0.18f, 0.17f, 0.15f, 1.0f), "%s", value.c_str());
    if (g_font_ui_large != nullptr) {
        ImGui::PopFont();
    }
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.46f, 0.44f, 0.40f, 1.0f), "%s", hint);
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    end_panel();
}

void draw_empty_block(const char* id, const char* title, const char* message, const ImVec2& size) {
    begin_panel(id, size, ImVec4(0.953f, 0.965f, 0.976f, 1.0f));
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "%s", title);
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 8.0f);
    ImGui::TextColored(ImVec4(0.470f, 0.550f, 0.630f, 1.0f), "%s", message);
    ImGui::PopTextWrapPos();
    end_panel();
}

void draw_hint_box(const char* id, const char* title, const char* message, float height) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
    ImGui::PushID(id);
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.952f, 0.964f, 0.975f, 1.0f));
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(ImVec4(0.760f, 0.810f, 0.860f, 0.92f));
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 14.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border, 14.0f, 0, 1.0f);
    ImFont* title_font = (g_font_ui_small != nullptr) ? g_font_ui_small : ImGui::GetFont();
    ImFont* body_font = ImGui::GetFont();
    const float title_font_size = (g_font_ui_small != nullptr) ? 12.5f : ImGui::GetFontSize();
    const float body_font_size = ImGui::GetFontSize();
    float body_y = pos.y + 16.0f;
    if (title != nullptr && title[0] != '\0') {
        dl->AddText(title_font, title_font_size, ImVec2(pos.x + 16.0f, pos.y + 12.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.300f, 0.380f, 0.470f, 1.0f)), title);
        body_y = pos.y + 40.0f;
    }
    dl->AddText(body_font, body_font_size, ImVec2(pos.x + 16.0f, body_y),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.470f, 0.550f, 0.630f, 1.0f)), message, nullptr, size.x - 32.0f);
    ImGui::PopID();
}

void draw_threshold_overlay(float min_v, float max_v, float threshold_v, const ImVec4& color) {
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    if (rect_max.y <= rect_min.y || max_v <= min_v) {
        return;
    }

    const float norm = std::clamp((threshold_v - min_v) / (max_v - min_v), 0.0f, 1.0f);
    const float y = rect_max.y - ((rect_max.y - rect_min.y) * norm);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(rect_min.x, y), ImVec2(rect_max.x, y), ImGui::ColorConvertFloat4ToU32(color), 1.5f);
}

MeasurementSummary compute_window_summary(const std::deque<LiveWindowSample>& samples) {
    MeasurementSummary out;
    if (samples.empty()) {
        return out;
    }

    CalibrationAccumulator acc;
    acc.reset();
    for (const auto& sample : samples) {
        xxrf_viewer::AoASample aoa;
        aoa.t_ns = sample.t_ns;
        aoa.sample_index = sample.sample_index;
        aoa.theta_rad = sample.theta_deg * (std::numbers::pi_v<float> / 180.0f);
        aoa.coherence = sample.coherence;
        aoa.signal_power_dbfs = sample.power_dbfs;
        acc.add(aoa);
    }
    return acc.summary();
}

} // namespace

void load_viewer_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    const std::string regular = find_font_path("JetBrainsMonoNerdFontPropo-Regular.ttf");
    const std::string semibold = find_font_path("JetBrainsMonoNerdFontPropo-SemiBold.ttf");

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    if (!regular.empty()) {
        g_font_ui = io.Fonts->AddFontFromFileTTF(regular.c_str(), 16.0f, &cfg);
        g_font_ui_small = io.Fonts->AddFontFromFileTTF(regular.c_str(), 12.5f, &cfg);
    }
    if (!semibold.empty()) {
        g_font_ui_large = io.Fonts->AddFontFromFileTTF(semibold.c_str(), 22.0f, &cfg);
    }

    if (g_font_ui == nullptr) {
        g_font_ui = io.Fonts->AddFontDefault();
    }
    if (g_font_ui_large == nullptr) {
        g_font_ui_large = g_font_ui;
    }
    if (g_font_ui_small == nullptr) {
        g_font_ui_small = g_font_ui;
    }

    io.FontDefault = g_font_ui;
}

void apply_viewer_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14.0f, 14.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowRounding = 20.0f;
    style.ChildRounding = 16.0f;
    style.FrameRounding = 12.0f;
    style.PopupRounding = 14.0f;
    style.GrabRounding = 12.0f;
    style.ScrollbarRounding = 14.0f;
    style.TabRounding = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.ScrollbarSize = 14.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.947f, 0.957f, 0.968f, 1.000f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.982f, 0.987f, 0.992f, 1.000f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.995f, 0.997f, 0.999f, 0.985f);
    colors[ImGuiCol_Border] = ImVec4(0.690f, 0.755f, 0.825f, 0.880f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.922f, 0.942f, 0.962f, 1.000f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.886f, 0.922f, 0.952f, 1.000f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.852f, 0.904f, 0.944f, 1.000f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.930f, 0.945f, 0.962f, 1.000f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.930f, 0.945f, 0.962f, 1.000f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.938f, 0.950f, 0.966f, 1.000f);
    colors[ImGuiCol_Header] = ImVec4(0.826f, 0.885f, 0.936f, 1.000f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.756f, 0.848f, 0.928f, 1.000f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.686f, 0.808f, 0.916f, 1.000f);
    colors[ImGuiCol_Button] = ImVec4(0.816f, 0.878f, 0.934f, 1.000f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.742f, 0.838f, 0.928f, 1.000f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.668f, 0.800f, 0.920f, 1.000f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.090f, 0.380f, 0.690f, 1.000f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.210f, 0.500f, 0.770f, 1.000f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.140f, 0.430f, 0.710f, 1.000f);
    colors[ImGuiCol_Separator] = ImVec4(0.760f, 0.810f, 0.860f, 0.850f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.520f, 0.660f, 0.790f, 0.280f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.430f, 0.620f, 0.790f, 0.620f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.310f, 0.550f, 0.760f, 0.900f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.930f, 0.944f, 0.958f, 1.000f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.760f, 0.810f, 0.860f, 1.000f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.680f, 0.760f, 0.840f, 1.000f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.600f, 0.715f, 0.820f, 1.000f);
    colors[ImGuiCol_Text] = ImVec4(0.110f, 0.150f, 0.200f, 1.000f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.430f, 0.500f, 0.570f, 1.000f);
    colors[ImGuiCol_Tab] = ImVec4(0.916f, 0.938f, 0.958f, 1.000f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.834f, 0.888f, 0.934f, 1.000f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.760f, 0.842f, 0.922f, 1.000f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.150f, 0.470f, 0.780f, 1.000f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.944f, 0.956f, 0.970f, 1.000f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.860f, 0.906f, 0.948f, 1.000f);
}

void render_viewer_ui(ViewerState& vs) {
    const bool running = vs.stream.has_value();
    const xxrf::aoa::rt::StreamStats live_stats = vs.stream ? vs.stream->stats() : xxrf::aoa::rt::StreamStats{};
    const std::uint64_t frame_now_ns = now_monotonic_ns();
    const std::uint64_t raw_sample_age_ns =
        vs.has_fix && (frame_now_ns >= vs.last.t_ns) ? (frame_now_ns - vs.last.t_ns) : 0ULL;
    const std::uint64_t valid_sample_age_ns =
        vs.has_valid_fix && (frame_now_ns >= vs.last_valid.t_ns) ? (frame_now_ns - vs.last_valid.t_ns) : 0ULL;
    const bool fresh_raw_fix = vs.has_fix && (raw_sample_age_ns <= ViewerState::stale_fix_timeout_ns);
    const bool fresh_valid_fix = vs.has_valid_fix && (valid_sample_age_ns <= ViewerState::stale_fix_timeout_ns);
    const bool raw_quality_ok = fresh_raw_fix && vs.last.quality_ok;
    const bool raw_side_ok = fresh_raw_fix && is_theta_side_allowed(vs.last.theta_rad, vs.allowed_side);
    const bool power_ok = fresh_raw_fix && vs.signal_gate_open;
    const bool display_fix = fresh_valid_fix && power_ok && raw_quality_ok && raw_side_ok;
    const MeasurementSummary rolling_summary = compute_window_summary(vs.rolling_samples);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::Begin("Пеленгатор AoA", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    const std::string master_label = (vs.idx_out >= 0 && vs.idx_out < static_cast<int>(vs.serials.size()))
                                         ? shorten_serial(vs.serials[static_cast<std::size_t>(vs.idx_out)])
                                         : "-";
    const std::string slave_label = (vs.idx_in >= 0 && vs.idx_in < static_cast<int>(vs.serials.size()))
                                        ? shorten_serial(vs.serials[static_cast<std::size_t>(vs.idx_in)])
                                        : "-";

    float theta_disp = 0.0F;
    if (display_fix && vs.smooth_init) {
        theta_disp = std::atan2(vs.sx, vs.sy);
    } else if (fresh_valid_fix) {
        theta_disp = vs.last_valid.theta_rad;
    }

    const std::string theta_value = display_fix
                                        ? std::format("{:+.1f}°", double(theta_disp) * (180.0 / std::numbers::pi))
                                        : (!fresh_raw_fix         ? (vs.has_valid_fix ? "устарело" : "нет оценки")
                                           : !vs.signal_gate_open ? "Below Threshold"
                                           : !raw_quality_ok      ? "Низкое качество"
                                                                  : "Другая сторона");
    const std::string coherence_value = fresh_raw_fix ? std::format("{:.3f}", vs.last.coherence) : "--";
    const std::string signal_power_value = fresh_raw_fix ? std::format("{:.1f} dBFS", vs.last.signal_power_dbfs) : "--";
    const std::string sample_age_value =
        display_fix ? std::format("{:.0f} мс", double(valid_sample_age_ns) * 1e-6) : "--";
    const std::string sample_index_value =
        display_fix ? std::format("{}", static_cast<unsigned long long>(vs.last_valid.sample_index)) : "--";
    const std::string rolling_theta_avg =
        rolling_summary.valid ? std::format("{:+.2f}°", rolling_summary.theta_circular_avg_deg) : "--";
    const std::string rolling_theta_span =
        rolling_summary.valid
            ? std::format("{:+.1f} .. {:+.1f}", rolling_summary.theta_min_deg, rolling_summary.theta_max_deg)
            : "--";
    const std::string rolling_coh_avg =
        rolling_summary.valid ? std::format("{:.3f}", rolling_summary.coherence_avg) : "--";
    const std::string rolling_power_avg =
        rolling_summary.valid ? std::format("{:.2f} dBFS", rolling_summary.power_avg_dbfs) : "--";
    const std::string active_fraction_value =
        fresh_raw_fix ? std::format("{:.0f}%", double(vs.last.active_fraction) * 100.0) : "--";
    const std::string phase_std_value = fresh_raw_fix ? std::format("{:.1f}°", vs.last.phase_std_deg) : "--";

    const float header_h = 60.0f;
    begin_panel("header", ImVec2(0.0f, header_h), ImVec4(0.935f, 0.948f, 0.964f, 1.0f));
    ImGui::SetCursorPosY(12.0f);
    ImGui::TextColored(ImVec4(0.110f, 0.160f, 0.220f, 1.0f), "xxrf измерительный пеленгатор");
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.340f, 0.420f, 0.510f, 1.0f), "ведущий %s  ->  ведомый %s", master_label.c_str(),
                       slave_label.c_str());
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }

    const float header_button_gap = 8.0f;
    const char* header_status_label = running ? "ИЗМЕРЕНИЕ" : "ГОТОВ";
    const char* header_toggle_label = vs.show_config ? "Скрыть панель" : "Показать панель";
    const char* header_stream_label = running ? "Остановить поток" : "Запустить поток";
    const ImGuiStyle& style = ImGui::GetStyle();
    const auto calc_button_width = [&](const char* label) {
        return ImGui::CalcTextSize(label).x + (style.FramePadding.x * 2.0f);
    };
    const auto calc_chip_width = [&](const char* label) { return ImGui::CalcTextSize(label).x + 24.0f; };
    const float action_block_w = calc_chip_width(header_status_label) + header_button_gap +
                                 calc_button_width(header_toggle_label) + header_button_gap +
                                 calc_button_width("Обновить устройства") + header_button_gap +
                                 calc_button_width(header_stream_label);
    const float chip_h = ImGui::GetFontSize() + 14.0f;
    const float action_block_h = std::max(chip_h, ImGui::GetFrameHeight());
    const float action_block_x =
        std::max(style.WindowPadding.x, ImGui::GetWindowWidth() - action_block_w - style.WindowPadding.x);
    const float action_block_y = std::max(10.0f, (header_h - action_block_h) * 0.5f);
    ImGui::SetCursorPos(ImVec2(action_block_x, action_block_y));
    draw_status_chip(header_status_label,
                     running ? ImVec4(0.91f, 0.97f, 0.94f, 1.0f) : ImVec4(0.96f, 0.95f, 0.90f, 1.0f),
                     running ? ImVec4(0.12f, 0.42f, 0.28f, 1.0f) : ImVec4(0.44f, 0.38f, 0.18f, 1.0f));
    ImGui::SameLine(0.0f, header_button_gap);
    if (ImGui::Button(header_toggle_label)) {
        vs.show_config = !vs.show_config;
    }
    ImGui::SameLine(0.0f, header_button_gap);
    if (ImGui::Button("Обновить устройства")) {
        vs.serials = enumerate_hackrf_serials();
        if (vs.serials.size() >= 2) {
            vs.idx_out = std::min(vs.idx_out, static_cast<int>(vs.serials.size()) - 1);
            vs.idx_in = std::min(vs.idx_in, static_cast<int>(vs.serials.size()) - 1);
            if (vs.idx_out == vs.idx_in && vs.serials.size() >= 2) {
                vs.idx_in = (vs.idx_out == 0) ? 1 : 0;
            }
        }
    }
    ImGui::SameLine(0.0f, header_button_gap);
    const ImVec4 stream_btn_bg = running ? ImVec4(0.78f, 0.22f, 0.20f, 1.0f) : ImVec4(0.17f, 0.60f, 0.30f, 1.0f);
    const ImVec4 stream_btn_hover = running ? ImVec4(0.86f, 0.28f, 0.26f, 1.0f) : ImVec4(0.22f, 0.68f, 0.36f, 1.0f);
    const ImVec4 stream_btn_active = running ? ImVec4(0.68f, 0.18f, 0.17f, 1.0f) : ImVec4(0.14f, 0.52f, 0.26f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, stream_btn_bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, stream_btn_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, stream_btn_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.99f, 0.99f, 1.0f));
    if (!running) {
        if (ImGui::Button(header_stream_label)) {
            auto sr = start_stream(vs);
            if (!sr) {
                ImGui::OpenPopup("Ошибка запуска");
            } else {
                vs.stream.emplace(std::move(*sr));
            }
        }
    } else if (ImGui::Button(header_stream_label)) {
        stop_stream(vs);
        if (!vs.last_error.empty()) {
            ImGui::OpenPopup("Ошибка остановки");
        }
    }
    ImGui::PopStyleColor(4);
    end_panel();

    if (ImGui::BeginPopupModal("Ошибка остановки", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Не удалось корректно остановить поток.");
        ImGui::Separator();
        ImGui::TextWrapped("%s", vs.last_error.empty() ? "(подробности отсутствуют)" : vs.last_error.c_str());
        if (ImGui::Button("ОК")) {
            vs.last_error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Ошибка запуска", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Не удалось запустить поток.");
        ImGui::Separator();
        ImGui::TextWrapped("%s", vs.last_error.empty() ? "(подробности отсутствуют)" : vs.last_error.c_str());
        if (ImGui::Button("ОК")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const bool sidebar_visible = vs.show_config;
    const float gutter = 12.0f;
    const float bottom_dock_h = 0.0f;
    const float content_h = std::max(320.0f, ImGui::GetContentRegionAvail().y - bottom_dock_h - gutter);
    const float body_total_w = ImGui::GetContentRegionAvail().x;
    float sidebar_w = 0.0f;
    if (sidebar_visible) {
        sidebar_w = 390.0f;
    }
    const float main_body_w = body_total_w - (sidebar_visible ? sidebar_w + gutter : 0.0f);
    float rail_w = std::clamp(376.0f, 340.0f, std::max(340.0f, main_body_w - 620.0f));
    float stage_w = main_body_w - rail_w - gutter;
    if (stage_w < 540.0f) {
        stage_w = 540.0f;
        rail_w = std::max(280.0f, main_body_w - stage_w - gutter);
    }
    rail_w = std::min(rail_w, std::max(280.0f, main_body_w - 420.0f));
    stage_w = std::max(420.0f, main_body_w - rail_w - gutter);

    if (sidebar_visible) {
        begin_panel("sidebar", ImVec2(sidebar_w, content_h), ImVec4(0.978f, 0.984f, 0.990f, 1.0f));
        draw_section_title("Конфигурация", "Параметры тракта и измерения");
        constexpr float control_input_width = 200.0f;
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::BeginTabBar("control_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            if (ImGui::BeginTabItem("Приём")) {
                if (!running) {
                    if (vs.serials.empty()) {
                        draw_empty_block("rx_empty", "приёмники недоступны", "Требуется два устройства HackRF.",
                                         ImVec2(0.0f, 112.0f));
                    } else {
                        constexpr float rx_combo_width = 156.0f;
                        ImGui::PushItemWidth(rx_combo_width);
                        if (ImGui::BeginCombo("Ведущий", master_label.c_str())) {
                            for (int i = 0; i < static_cast<int>(vs.serials.size()); ++i) {
                                const bool selected = (i == vs.idx_out);
                                const std::string item_label = shorten_serial(vs.serials[static_cast<std::size_t>(i)]);
                                if (ImGui::Selectable(item_label.c_str(), selected)) {
                                    vs.idx_out = i;
                                    if (vs.idx_in == vs.idx_out && vs.serials.size() >= 2) {
                                        vs.idx_in = (vs.idx_out == 0) ? 1 : 0;
                                    }
                                }
                                if (selected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginCombo("Ведомый", slave_label.c_str())) {
                            for (int i = 0; i < static_cast<int>(vs.serials.size()); ++i) {
                                const bool selected = (i == vs.idx_in);
                                const std::string item_label = shorten_serial(vs.serials[static_cast<std::size_t>(i)]);
                                if (ImGui::Selectable(item_label.c_str(), selected)) {
                                    vs.idx_in = i;
                                    if (vs.idx_in == vs.idx_out && vs.serials.size() >= 2) {
                                        vs.idx_out = (vs.idx_in == 0) ? 1 : 0;
                                    }
                                }
                                if (selected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.50f, 0.91f, 0.74f, 1.0f),
                                       "Назначение приёмников заблокировано во время потока.");
                    ImGui::Text("Ведущий: %s", master_label.c_str());
                    ImGui::Text("Ведомый: %s", slave_label.c_str());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("РЧ")) {
                if (!running) {
                    constexpr float rf_input_width = 200.0f;
                    ImGui::PushItemWidth(rf_input_width);
                    ImGui::InputDouble("Центр. частота (МГц)", &vs.center_freq_mhz, 0.01, 1.0, "%.6f");
                    ImGui::InputDouble("Частота дискр. (Мвыб/с)", &vs.sample_rate_msps, 0.5, 1.0, "%.3f");
                    ImGui::InputDouble("База d (м)", &vs.baseline_m, 0.01, 0.05, "%.3f");
                    ImGui::InputInt("Усиление LNA (дБ)", &vs.lna_gain);
                    ImGui::InputInt("Усиление VGA (дБ)", &vs.vga_gain);
                    ImGui::PopItemWidth();
                    ImGui::Checkbox("Включить AMP", &vs.amp_enable);
                } else {
                    ImGui::Text("Центр. частота: %.6f МГц", vs.center_freq_mhz);
                    ImGui::Text("Частота дискр.: %.3f Мвыб/с", vs.sample_rate_msps);
                    ImGui::Text("База: %.3f м", vs.baseline_m);
                    ImGui::Text("LNA / VGA: %d / %d дБ", vs.lna_gain, vs.vga_gain);
                    ImGui::Text("AMP: %s", vs.amp_enable ? "включён" : "выключен");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("AoA")) {
                std::array<float, ViewerState::history_size> power_plot{};
                const std::size_t power_points =
                    build_history_plot(vs.power_history, vs.power_head, vs.power_count, power_plot);
                constexpr float power_control_width = 200.0f;
                if (!running) {
                    ImGui::PushItemWidth(control_input_width);
                    ImGui::InputInt("Окно, отсчёты", &vs.window_samples);
                    ImGui::InputInt("Шаг окна, отсчёты", &vs.hop_samples);
                    ImGui::InputInt("Шаг выборки", &vs.sample_stride);
                    ImGui::InputDouble("Мин. когерентность", &vs.min_coherence, 0.01, 0.05, "%.3f");
                    ImGui::InputDouble("Мин. активная доля", &vs.min_active_fraction, 0.05, 0.10, "%.2f");
                    ImGui::InputInt("Подокон проверки фазы", &vs.phase_stability_subwindows);
                    ImGui::InputDouble("Макс. σ фазы (°)", &vs.max_phase_std_deg, 1.0, 5.0, "%.1f");
                    const char* current_side = allowed_side_label(vs.allowed_side);
                    if (ImGui::BeginCombo("Рабочая сторона", current_side)) {
                        for (const AllowedAoASide side :
                             {AllowedAoASide::Both, AllowedAoASide::Positive, AllowedAoASide::Negative}) {
                            const bool selected = (vs.allowed_side == side);
                            if (ImGui::Selectable(allowed_side_label(side), selected)) {
                                vs.allowed_side = side;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();
                } else {
                    ImGui::Text("Окно / Шаг / Выборка: %d / %d / %d", vs.window_samples, vs.hop_samples,
                                vs.sample_stride);
                    ImGui::Text("Мин. когерентность: %.3f", vs.min_coherence);
                    ImGui::Text("Мин. активная доля: %.2f", vs.min_active_fraction);
                    ImGui::Text("Подокон проверки фазы: %d", vs.phase_stability_subwindows);
                    ImGui::Text("Макс. σ фазы: %.1f°", vs.max_phase_std_deg);
                    ImGui::Text("Рабочая сторона: %s", allowed_side_label(vs.allowed_side));
                }
                ImGui::PushItemWidth(control_input_width);
                ImGui::InputDouble("Тау сглаживания (с)", &vs.smooth_tau_s, 0.02, 0.1, "%.3f");
                ImGui::InputDouble("Фазовая поправка (°)", &vs.cal_phase_deg, 0.1, 1.0, "%.3f");
                ImGui::InputDouble("Окно статистики (с)", &vs.rolling_window_s, 0.5, 1.0, "%.1f");
                ImGui::InputFloat("Гистерезис порога (дБ)", &vs.signal_threshold_hysteresis_db, 0.1f, 0.5f, "%.1f");
                ImGui::InputDouble("Калибровка, длит. (с)", &vs.calibration_duration_s, 1.0, 5.0, "%.1f");
                ImGui::PopItemWidth();
                ImGui::PushItemWidth(power_control_width);
                ImGui::SliderFloat("Порог мощности", &vs.signal_threshold_dbfs, -90.0f, 0.0f, "%.1f dBFS");
                ImGui::PopItemWidth();
                if (g_font_ui_small != nullptr) {
                    ImGui::PushFont(g_font_ui_small);
                }
                ImGui::TextColored(power_ok ? ImVec4(0.18f, 0.62f, 0.30f, 1.0f) : ImVec4(0.78f, 0.22f, 0.20f, 1.0f),
                                   "Текущая мощность: %s", signal_power_value.c_str());
                ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "Порог входа/выхода: %.1f / %.1f dBFS",
                                   vs.signal_threshold_dbfs,
                                   vs.signal_threshold_dbfs - std::max(0.1f, vs.signal_threshold_hysteresis_db));
                if (g_font_ui_small != nullptr) {
                    ImGui::PopFont();
                }
                if (g_font_ui_small != nullptr) {
                    ImGui::PushFont(g_font_ui_small);
                }
                ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "История мощности");
                if (g_font_ui_small != nullptr) {
                    ImGui::PopFont();
                }
                const float power_plot_h = 62.0f;
                if (power_points > 1) {
                    const float power_norm = std::clamp((fresh_raw_fix ? vs.last.signal_power_dbfs : -90.0f) -
                                                            vs.signal_threshold_dbfs + 18.0f,
                                                        0.0f, 36.0f) /
                                             36.0f;
                    const ImVec4 power_plot_low(0.78f, 0.22f, 0.20f, 1.0f);
                    const ImVec4 power_plot_high(0.18f, 0.62f, 0.30f, 1.0f);
                    const ImVec4 power_plot_col(
                        power_plot_low.x + ((power_plot_high.x - power_plot_low.x) * power_norm),
                        power_plot_low.y + ((power_plot_high.y - power_plot_low.y) * power_norm),
                        power_plot_low.z + ((power_plot_high.z - power_plot_low.z) * power_norm), 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, power_plot_col);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.942f, 0.953f, 0.965f, 1.0f));
                    ImGui::PlotLines("##power_plot", power_plot.data(), static_cast<int>(power_points), 0, nullptr,
                                     -90.0f, 0.0f, ImVec2(-1.0f, power_plot_h));
                    ImGui::PopStyleColor(2);
                    draw_threshold_overlay(-90.0f, 0.0f, vs.signal_threshold_dbfs, ImVec4(0.78f, 0.22f, 0.20f, 1.0f));
                } else {
                    draw_hint_box("power_empty", "", "Появится после накопления уровня сигнала.", power_plot_h);
                }
                ImGui::Separator();
                if (g_font_ui_small != nullptr) {
                    ImGui::PushFont(g_font_ui_small);
                }
                ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "Калибровка");
                if (g_font_ui_small != nullptr) {
                    ImGui::PopFont();
                }
                if (!running) {
                    ImGui::TextColored(ImVec4(0.470f, 0.550f, 0.630f, 1.0f), "Запусти поток, чтобы начать калибровку.");
                } else if (!vs.calibration_running) {
                    if (ImGui::Button("Старт калибровки")) {
                        vs.calibration_acc.reset();
                        vs.calibration_last = {};
                        vs.calibration_started_ns = frame_now_ns;
                        vs.calibration_running = true;
                    }
                } else {
                    const double elapsed_s = double(frame_now_ns - vs.calibration_started_ns) * 1e-9;
                    const double progress = std::clamp(elapsed_s / std::max(1.0, vs.calibration_duration_s), 0.0, 1.0);
                    ImGui::Text("Идёт калибровка: %.1f / %.1f с", elapsed_s, vs.calibration_duration_s);
                    ImGui::ProgressBar(static_cast<float>(progress), ImVec2(power_control_width, 0.0f));
                    if (ImGui::Button("Отменить")) {
                        vs.calibration_running = false;
                        vs.calibration_acc.reset();
                    }
                }
                if (vs.calibration_last.valid) {
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
                    draw_compact_row("Калибровка θ avg",
                                     std::format("{:+.3f}°", vs.calibration_last.theta_circular_avg_deg),
                                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
                    draw_compact_row("Калибровка coh avg", std::format("{:.4f}", vs.calibration_last.coherence_avg),
                                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
                    draw_compact_row("Калибровка pwr avg",
                                     std::format("{:.2f} dBFS", vs.calibration_last.power_avg_dbfs),
                                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
                    const double residual_phase_deg = theta_bias_to_phase_deg(
                        vs.calibration_last.theta_circular_avg_deg, vs.center_freq_mhz, vs.baseline_m);
                    if (ImGui::Button("Применить как фазовую поправку")) {
                        vs.cal_phase_deg -= residual_phase_deg;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Сбросить отчёт")) {
                        vs.calibration_last = {};
                    }
                    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "Поправка к фазе: %+.3f°",
                                       -residual_phase_deg);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Синхр.")) {
                if (!running) {
                    ImGui::Checkbox("Аппаратный триггер", &vs.hw_trigger);
                    ImGui::Checkbox("CLKOUT на ведущем", &vs.clkout_on_master);
                    ImGui::Checkbox("Требовать CLKIN на ведомом", &vs.require_clkin_on_slave);
                    ImGui::PushItemWidth(control_input_width);
                    ImGui::InputInt("Задержка взвода (мс)", &vs.arm_delay_ms);
                    ImGui::PopItemWidth();
                } else {
                    ImGui::Text("Аппаратный триггер: %s", vs.hw_trigger ? "вкл." : "выкл.");
                    ImGui::Text("CLKOUT: %s", vs.clkout_on_master ? "включён" : "выключен");
                    ImGui::Text("Требовать CLKIN: %s", vs.require_clkin_on_slave ? "да" : "нет");
                    ImGui::Text("Задержка взвода: %d мс", vs.arm_delay_ms);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();
        end_panel();
        ImGui::SameLine(0.0f, gutter);
    }

    ImGui::BeginGroup();
    begin_panel("stage_panel", ImVec2(stage_w, content_h), ImVec4(0.980f, 0.986f, 0.992f, 1.0f));
    draw_section_title("Окно пеленга");
    ImVec2 stage_origin = ImGui::GetCursorScreenPos();
    ImVec2 stage_avail = ImGui::GetContentRegionAvail();
    const float footer_h = 116.0f;
    const float compass_zone_h = std::max(300.0f, stage_avail.y - footer_h - 8.0f);
    const float compass_radius = std::max(184.0f, std::min(stage_avail.x * 0.42f, compass_zone_h * 0.45f));
    const ImVec2 center(stage_origin.x + (stage_avail.x * 0.54f), stage_origin.y + (compass_zone_h * 0.52f));
    const bool missing_devices = vs.serials.size() < 2;
    const char* inactive_label = fresh_raw_fix && !power_ok ? "Below Threshold" : "НЕТ ОЦЕНКИ";
    if (fresh_raw_fix && vs.signal_gate_open && !vs.last.quality_ok) {
        inactive_label = "НИЗКОЕ КАЧЕСТВО";
    } else if (fresh_raw_fix && vs.signal_gate_open && vs.last.quality_ok && !raw_side_ok) {
        inactive_label = "ДРУГАЯ СТОРОНА";
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    xxrf_viewer::DrawThetaCompass(dl, center, compass_radius, theta_disp, display_fix ? vs.last_valid.coherence : 0.0f,
                                  display_fix, missing_devices, 0.15f, inactive_label);

    ImGui::SetCursorScreenPos(ImVec2(stage_origin.x + 16.0f, stage_origin.y + 12.0f));
    begin_panel("stage_readout", ImVec2(214.0f, 104.0f), ImVec4(0.950f, 0.962f, 0.976f, 0.96f));
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "текущий угол");
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    if (g_font_ui_large != nullptr) {
        ImGui::PushFont(g_font_ui_large);
    }
    ImGui::TextColored(ImVec4(0.100f, 0.150f, 0.210f, 1.0f), "%s", theta_value.c_str());
    if (g_font_ui_large != nullptr) {
        ImGui::PopFont();
    }
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "мощность сигнала %s", signal_power_value.c_str());
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    end_panel();

    ImGui::SetCursorScreenPos(ImVec2(stage_origin.x, stage_origin.y + stage_avail.y - footer_h));
    const float tile_gap = 10.0f;
    const float tile_w = (ImGui::GetContentRegionAvail().x - tile_gap) * 0.5f;
    draw_summary_tile("stage_age_tile", "Возраст оценки", sample_age_value, display_fix ? "Актуально" : "Ожидание",
                      ImVec2(tile_w, 84.0f));
    ImGui::SameLine(0.0f, tile_gap);
    draw_summary_tile("stage_sample_tile", "Последний отсчёт", sample_index_value, "Центральный отсчёт окна",
                      ImVec2(tile_w, 84.0f));
    end_panel();

    ImGui::SameLine(0.0f, gutter);

    begin_panel("insight_rail", ImVec2(rail_w, content_h), ImVec4(0.978f, 0.984f, 0.990f, 1.0f));
    draw_section_title("Телеметрия");
    const float rail_inner_w = ImGui::GetContentRegionAvail().x;
    const float rail_gap = 10.0f;
    const float rail_track_h = 182.0f;
    const float rail_health_h = vs.show_diagnostics ? 430.0f : 250.0f;
    const float rail_history_h = 220.0f;
    begin_panel("rail_track", ImVec2(rail_inner_w, rail_track_h), ImVec4(0.968f, 0.976f, 0.985f, 1.0f));
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "состояние решения");
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    if (g_font_ui_large != nullptr) {
        ImGui::PushFont(g_font_ui_large);
    }
    const float coherence_norm =
        std::clamp(display_fix ? static_cast<float>(vs.last_valid.coherence) : 0.0f, 0.0f, 1.0f);
    const ImVec4 coherence_low(0.78f, 0.22f, 0.20f, 1.0f);
    const ImVec4 coherence_high(0.18f, 0.62f, 0.30f, 1.0f);
    const ImVec4 coherence_col(coherence_low.x + ((coherence_high.x - coherence_low.x) * coherence_norm),
                               coherence_low.y + ((coherence_high.y - coherence_low.y) * coherence_norm),
                               coherence_low.z + ((coherence_high.z - coherence_low.z) * coherence_norm), 1.0f);
    ImGui::TextColored(coherence_col, "%s", coherence_value.c_str());
    if (g_font_ui_large != nullptr) {
        ImGui::PopFont();
    }
    ImGui::Separator();
    draw_compact_row("θ avg / окно", rolling_theta_avg, ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Размах θ / окно", rolling_theta_span, ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Coh avg / окно", rolling_coh_avg, ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Pwr avg / окно", rolling_power_avg, ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    end_panel();

    ImGui::Dummy(ImVec2(0.0f, rail_gap));
    begin_panel("rail_history", ImVec2(rail_inner_w, rail_history_h), ImVec4(0.968f, 0.976f, 0.985f, 1.0f));
    std::array<float, ViewerState::history_size> theta_plot{};
    std::array<float, ViewerState::history_size> coherence_plot{};
    const std::size_t theta_points = build_history_plot(vs.theta_history, vs.theta_head, vs.theta_count, theta_plot);
    const std::size_t coherence_points =
        build_history_plot(vs.coherence_history, vs.coherence_head, vs.coherence_count, coherence_plot);
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "история угла");
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    const float history_plot_h = 70.0f;
    const float coherence_plot_h = 70.0f;
    if (theta_points > 1) {
        ImGui::PlotLines("##theta_plot", theta_plot.data(), static_cast<int>(theta_points), 0, nullptr, -180.0f, 180.0f,
                         ImVec2(-1.0f, history_plot_h));
    } else {
        draw_hint_box("theta_empty", "", "График появится после накопления нескольких оценок.", history_plot_h);
    }
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "когерентность");
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    if (coherence_points > 1) {
        ImGui::PlotLines("##coh_plot", coherence_plot.data(), static_cast<int>(coherence_points), 0, nullptr, 0.0f,
                         1.0f, ImVec2(-1.0f, coherence_plot_h));
    } else {
        draw_hint_box("coh_empty", "", "Появится вместе с историей измерений.", coherence_plot_h);
    }
    end_panel();

    ImGui::Dummy(ImVec2(0.0f, rail_gap));
    begin_panel("rail_health", ImVec2(rail_inner_w, rail_health_h), ImVec4(0.968f, 0.976f, 0.985f, 1.0f));
    if (g_font_ui_small != nullptr) {
        ImGui::PushFont(g_font_ui_small);
    }
    ImGui::TextColored(ImVec4(0.300f, 0.380f, 0.470f, 1.0f), "техническое состояние");
    if (g_font_ui_small != nullptr) {
        ImGui::PopFont();
    }
    ImGui::Checkbox("Расширенная диагностика", &vs.show_diagnostics);
    ImGui::Separator();
    draw_compact_row("Выдано пар", std::format("{}", static_cast<unsigned long long>(live_stats.dual.pairs_emitted)),
                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Потери очереди", std::format("{}", static_cast<unsigned long long>(vs.q.drops())),
                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Макс. сдвиг",
                     std::format("{} отсч.", static_cast<unsigned long long>(live_stats.dual.max_abs_skew_samples)),
                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Оценок AoA", std::format("{}", static_cast<unsigned long long>(live_stats.aoa.estimates_emitted)),
                     ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("Активная доля", active_fraction_value, ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    draw_compact_row("σ фазы", phase_std_value, ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    if (vs.show_diagnostics) {
        ImGui::Separator();
        draw_compact_row("Исп. отсчётов",
                         std::format("{}", static_cast<unsigned long long>(live_stats.aoa.samples_used)),
                         ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
        draw_compact_row("Отброшено по качеству",
                         std::format("{}", static_cast<unsigned long long>(live_stats.aoa.below_quality)),
                         ImVec4(0.97f, 0.79f, 0.48f, 1.0f));
        draw_compact_row("Ниже активности",
                         std::format("{}", static_cast<unsigned long long>(live_stats.aoa.below_activity)),
                         ImVec4(0.97f, 0.79f, 0.48f, 1.0f));
        draw_compact_row("Нестабильная фаза",
                         std::format("{}", static_cast<unsigned long long>(live_stats.aoa.unstable_phase)),
                         ImVec4(0.97f, 0.79f, 0.48f, 1.0f));
        draw_compact_row("Разрывы", std::format("{}", static_cast<unsigned long long>(live_stats.aoa.discontinuities)),
                         ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
        draw_compact_row("Потери RX out/in",
                         std::format("{} / {}",
                                     static_cast<unsigned long long>(live_stats.dual.trigger_out.blocks_dropped),
                                     static_cast<unsigned long long>(live_stats.dual.trigger_in.blocks_dropped)),
                         ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
        draw_compact_row("Потери пар out/in",
                         std::format("{} / {}",
                                     static_cast<unsigned long long>(live_stats.dual.drops_pairing_trigger_out),
                                     static_cast<unsigned long long>(live_stats.dual.drops_pairing_trigger_in)),
                         ImVec4(0.94f, 0.98f, 0.995f, 1.0f));
    }
    end_panel();
    end_panel();
    ImGui::EndGroup();

    ImGui::End();
}
