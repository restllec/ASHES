#pragma once

#include "third_party/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <string>

struct UIThemeConfig {
    ImVec4 accentColor = ImVec4(0.92f, 0.18f, 0.49f, 1.0f); // #EB2F6E
    ImVec4 accentHover = ImVec4(1.00f, 0.28f, 0.58f, 1.0f);
    ImVec4 accentActive = ImVec4(0.80f, 0.12f, 0.40f, 1.0f);

    void UpdateDerived()
    {
        accentHover = ImVec4(
            std::fmin(accentColor.x * 1.15f, 1.0f),
            std::fmin(accentColor.y * 1.15f, 1.0f),
            std::fmin(accentColor.z * 1.15f, 1.0f),
            1.0f
        );
        accentActive = ImVec4(
            accentColor.x * 0.85f,
            accentColor.y * 0.85f,
            accentColor.z * 0.85f,
            1.0f
        );
    }

    ImU32 AccentU32(float alpha = 1.0f) const {
        ImVec4 col = accentColor;
        col.w = alpha;
        return ImGui::ColorConvertFloat4ToU32(col);
    }
    ImU32 HoverU32(float alpha = 1.0f) const {
        ImVec4 col = accentHover;
        col.w = alpha;
        return ImGui::ColorConvertFloat4ToU32(col);
    }
    ImU32 ActiveU32(float alpha = 1.0f) const {
        ImVec4 col = accentActive;
        col.w = alpha;
        return ImGui::ColorConvertFloat4ToU32(col);
    }
};

inline UIThemeConfig g_Theme;

void LoadThemeSettings();
void SaveThemeToConfig();
void ResetThemeSettings();
