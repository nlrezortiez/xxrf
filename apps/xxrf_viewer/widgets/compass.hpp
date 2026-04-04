#pragma once

#include <cfloat>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <imgui.h>

namespace xxrf_viewer {

inline void DrawThetaCompass(ImDrawList* dl, ImVec2 center, float radius, float theta_rad, float coherence,
                             bool has_fix, bool highlight_missing_devices = false, float alpha_min = 0.15f,
                             const char* inactive_label = "НЕТ ОЦЕНКИ") {
    if (!dl)
        return;

    if (highlight_missing_devices) {
        dl->AddCircleFilled(center, radius - 2.0f, IM_COL32(255, 160, 160, 46), 64);
    }

    
    const ImU32 col_circle = IM_COL32(180, 180, 180, 255);
    dl->AddCircle(center, radius, col_circle, 64, 2.0f);

    
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * 0.72f;
    const float tick_minor = radius * 0.045f;
    const float tick_major = radius * 0.085f;
    const float label_radius = radius + (radius * 0.15f);

    for (int deg = -180; deg < 180; deg += 15) {
        const float ang = static_cast<float>(deg) * (std::numbers::pi_v<float> / 180.0f);
        const float s = std::sin(ang);
        const float c = std::cos(ang);
        const float dx_tick = s;
        const float dy_tick = -c;

        const bool major = (deg % 45) == 0;
        const float tick_len = major ? tick_major : tick_minor;
        const ImU32 tick_col = major ? IM_COL32(170, 185, 195, 220) : IM_COL32(105, 120, 130, 180);

        const ImVec2 tick_outer(center.x + dx_tick * radius, center.y + dy_tick * radius);
        const ImVec2 tick_inner(center.x + dx_tick * (radius - tick_len), center.y + dy_tick * (radius - tick_len));
        dl->AddLine(tick_inner, tick_outer, tick_col, major ? 1.6f : 1.0f);

        char label[16];
        if (deg > 0) {
            std::snprintf(label, sizeof(label), "+%d", deg);
        } else {
            std::snprintf(label, sizeof(label), "%d", deg);
        }

        const ImVec2 label_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
        const ImVec2 label_pos(center.x + dx_tick * label_radius - (label_size.x * 0.5f),
                               center.y + dy_tick * label_radius - (label_size.y * 0.5f));
        const ImU32 label_col = major ? IM_COL32(210, 220, 228, 235) : IM_COL32(145, 158, 168, 210);
        dl->AddText(font, font_size, label_pos, label_col, label);
    }

    
    dl->AddLine(center, ImVec2(center.x, center.y - radius), IM_COL32(180, 180, 180, 255), 1.0f);

    
    dl->AddLine(center, ImVec2(center.x + radius, center.y), IM_COL32(120, 120, 120, 255), 1.0f);
    dl->AddLine(center, ImVec2(center.x - radius, center.y), IM_COL32(120, 120, 120, 255), 1.0f);
    dl->AddLine(center, ImVec2(center.x, center.y + radius), IM_COL32(120, 120, 120, 255), 1.0f);

    
    if (!has_fix) {
        const char* label = (inactive_label != nullptr && inactive_label[0] != '\0') ? inactive_label : "НЕТ ОЦЕНКИ";
        const ImVec2 text_size = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(center.x - (text_size.x * 0.5f), center.y - (text_size.y * 0.5f)),
                    IM_COL32(255, 120, 120, 255), label);
        return;
    }

    
    float a = (coherence < 0.0f) ? 0.0f : (coherence > 1.0f ? 1.0f : coherence);
    a = alpha_min + (1.0f - alpha_min) * a;

    
    
    
    const float dx = std::sin(theta_rad);
    const float dy = -std::cos(theta_rad);

    const float len = radius * 0.85f;
    const ImVec2 tip(center.x + dx * len, center.y + dy * len);

    const ImU32 col_arrow = IM_COL32(80, 200, 255, int(255.0f * a));
    dl->AddLine(center, tip, col_arrow, 3.0f);

    
    const float head = radius * 0.12f;
    
    const float px = -dy;
    const float py = dx;

    const ImVec2 left(tip.x - dx * head + px * head * 0.6f, tip.y - dy * head + py * head * 0.6f);
    const ImVec2 right(tip.x - dx * head - px * head * 0.6f, tip.y - dy * head - py * head * 0.6f);

    dl->AddTriangleFilled(tip, left, right, col_arrow);

    char theta_buf[32];
    const double theta_deg = static_cast<double>(theta_rad) * (180.0 / std::numbers::pi);
    std::snprintf(theta_buf, sizeof(theta_buf), "%+.1f°", theta_deg);

    const ImVec2 text_size = ImGui::CalcTextSize(theta_buf);
    const float label_gap = radius * 0.10f;
    const float pad_x = 8.0f;
    const float pad_y = 5.0f;

    ImVec2 label_center(tip.x + dx * (label_gap + text_size.x * 0.3f), tip.y + dy * (label_gap + text_size.y * 0.3f));
    label_center.x += px * (text_size.x * 0.18f);
    label_center.y += py * (text_size.y * 0.18f);

    const ImVec2 label_min(label_center.x - (text_size.x * 0.5f) - pad_x, label_center.y - (text_size.y * 0.5f) - pad_y);
    const ImVec2 label_max(label_center.x + (text_size.x * 0.5f) + pad_x, label_center.y + (text_size.y * 0.5f) + pad_y);

    const ImU32 col_label_bg = IM_COL32(8, 14, 20, 220);
    const ImU32 col_label_border = IM_COL32(80, 200, 255, 235);
    const ImU32 col_label_text = IM_COL32(225, 245, 255, 255);
    const ImU32 col_anchor = IM_COL32(80, 200, 255, 180);

    dl->AddLine(tip, label_center, col_anchor, 1.5f);
    dl->AddCircleFilled(tip, 3.0f, col_anchor, 12);
    dl->AddRectFilled(label_min, label_max, col_label_bg, 8.0f);
    dl->AddRect(label_min, label_max, col_label_border, 8.0f, 0, 1.5f);
    dl->AddText(ImVec2(label_center.x - (text_size.x * 0.5f), label_center.y - (text_size.y * 0.5f)), col_label_text,
                theta_buf);
}

} 
