#pragma once

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

inline bool DarkFlameSliderInt(const char* id, int* value, int minimum, int maximum,
    const char* format, float scale)
{
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float valueWidth = 78 * scale;
    const float height = ImGui::GetFrameHeight();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
        ImGui::GetColorU32(ImGuiCol_FrameBg), 6 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18 * scale);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(std::max(36 * scale, width - valueWidth));
    const bool editing = ImGui::TempInputIsActive(ImGui::GetID(id));
    if(editing) ImGui::PopStyleColor();
    const bool changed = ImGui::SliderInt(id, value, minimum, maximum, format,
        ImGuiSliderFlags_AlwaysClamp);
    const ImVec2 end = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsItemFocused();
    const bool textInput = ImGui::TempInputIsActive(ImGui::GetItemID());
    ImGui::PopStyleColor(editing ? 5 : 6);
    ImGui::PopStyleVar();
    if(!textInput)
    {
        const float x0 = start.x + 2 + 9 * scale;
        const float x1 = end.x - 2 - 9 * scale;
        const float y = start.y + height / 2;
        const float fraction = static_cast<float>(std::clamp(*value, minimum, maximum) - minimum)
            / static_cast<float>(maximum - minimum);
        const float x = x0 + (x1 - x0) * fraction;
        draw->AddLine(ImVec2(x0, y), ImVec2(x1, y),
            ImGui::GetColorU32(ImVec4(0.18f, 0.12f, 0.28f, 1)), 6 * scale);
        draw->AddLine(ImVec2(x0, y), ImVec2(x, y),
            ImGui::GetColorU32(ImVec4(0.65f, 0.22f, 0.87f, 1)), 6 * scale);
        draw->AddCircleFilled(ImVec2(x, y), 9 * scale,
            ImGui::GetColorU32(ImVec4(active || hovered ? 0.83f : 0.72f, 0.29f, 1, 1)));
        draw->AddCircle(ImVec2(x, y), 9 * scale,
            ImGui::GetColorU32(ImVec4(0.08f, 0.04f, 0.14f, 1)), 0, scale);
        if(focused) draw->AddCircle(ImVec2(x, y), 12 * scale,
            ImGui::GetColorU32(ImGuiCol_CheckMark), 0, scale);
    }
    char label[64]{};
    std::snprintf(label, sizeof(label), format, *value);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(start.x + width - textSize.x - 10 * scale,
        start.y + (height - textSize.y) / 2), ImGui::GetColorU32(ImGuiCol_Text), label);
    return changed;
}

struct XoneThemeColors
{
    ImU32 windowBg;
    ImU32 windowBorder;
    ImU32 sidebarBg;
    ImU32 sidebarBorder;
    ImU32 plashkaBg;
    ImU32 plashkaBgHover;
    ImU32 plashkaBorder;
    ImU32 plashkaBorderHover;
    ImU32 textPrimary;
    ImU32 textMuted;
    ImU32 accent;
    ImU32 buttonBg;
    ImU32 buttonHover;
    ImU32 buttonActive;
    ImU32 toggleActive;
};

inline XoneThemeColors GetCurrentTheme()
{
    XoneThemeColors c{};
    c.windowBg = IM_COL32(10, 10, 14, 255);
    c.windowBorder = IM_COL32(40, 42, 52, 255);
    c.sidebarBg = IM_COL32(8, 8, 11, 245);
    c.sidebarBorder = IM_COL32(35, 36, 46, 255);
    c.plashkaBg = IM_COL32(24, 25, 32, 150);
    c.plashkaBgHover = IM_COL32(34, 36, 48, 185);
    c.plashkaBorder = IM_COL32(255, 255, 255, 22);
    c.plashkaBorderHover = IM_COL32(255, 255, 255, 45);
    c.textPrimary = IM_COL32(245, 245, 250, 255);
    c.textMuted = IM_COL32(150, 155, 170, 255);
    c.accent = IM_COL32(165, 110, 255, 255);
    c.buttonBg = IM_COL32(32, 34, 46, 170);
    c.buttonHover = IM_COL32(48, 52, 70, 210);
    c.buttonActive = IM_COL32(70, 75, 105, 255);
    c.toggleActive = IM_COL32(165, 110, 255, 255);
    return c;
}

inline void DrawAnimatedWaves(ImDrawList* draw, ImVec2 origin, ImVec2 size)
{
    float t = static_cast<float>(ImGui::GetTime());
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for(int i = 0; i < 7; ++i)
    {
        float speed = 0.25f + i * 0.07f;
        float amp = 20.0f + i * 5.0f;
        float freq = 0.0030f + i * 0.0006f;
        float phase = i * 1.2f + t * speed;
        float baseY = origin.y + size.y * (0.10f + i * 0.12f);

        ImVec2 prev(origin.x, baseY + std::sin(phase) * amp);
        constexpr int segments = 45;
        float stepX = size.x / static_cast<float>(segments);
        for(int s = 1; s <= segments; ++s)
        {
            float x = origin.x + s * stepX;
            float y = baseY + std::sin((x - origin.x) * freq + phase) * amp
                            + std::cos((x - origin.x) * freq * 0.5f + phase * 0.6f) * (amp * 0.35f);
            ImVec2 curr(x, y);
            draw->AddLine(prev, curr, IM_COL32(255, 255, 255, 42 + (i % 3) * 11), 1.6f);
            prev = curr;
        }
    }
    draw->PopClipRect();
}

inline bool XoneToggle(const char* id, bool* value, float scale)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems) return false;

    const ImGuiID imgui_id = window->GetID(id);
    float w = 38.0f * scale;
    float h = 20.0f * scale;
    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb);
    if(!ImGui::ItemAdd(bb, imgui_id))
        return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, imgui_id, &hovered, &held);
    if(pressed)
    {
        *value = !(*value);
        ImGui::MarkItemEdited(imgui_id);
    }

    static std::unordered_map<ImGuiID, float> anims;
    float& anim = anims[imgui_id];
    float target = *value ? 1.0f : 0.0f;
    float dt = ImGui::GetIO().DeltaTime;
    if(anim < target) anim = std::min(target, anim + dt * 10.0f);
    else if(anim > target) anim = std::max(target, anim - dt * 10.0f);

    ImDrawList* draw = window->DrawList;
    float rounding = h * 0.5f;
    XoneThemeColors c = GetCurrentTheme();

    ImU32 bgCol = *value ? c.toggleActive : (hovered ? IM_COL32(50, 52, 62, 255) : IM_COL32(34, 36, 44, 255));
    draw->AddRectFilled(bb.Min, bb.Max, bgCol, rounding);
    draw->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 30), rounding, 0, 1.0f);

    float knobRadius = (h - 4.0f * scale) * 0.5f;
    float knobMinX = bb.Min.x + 2.0f * scale + knobRadius;
    float knobMaxX = bb.Max.x - 2.0f * scale - knobRadius;
    float knobX = knobMinX + (knobMaxX - knobMinX) * anim;
    float knobY = bb.Min.y + h * 0.5f;

    ImU32 knobCol = *value ? IM_COL32(15, 15, 20, 255) : IM_COL32(210, 210, 220, 255);
    draw->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, knobCol);

    return pressed;
}

inline bool XonePlashkaToggle(const char* label, bool* value, float scale, float height = 40.0f)
{
    float w = ImGui::GetContentRegionAvail().x;
    float h = height * scale;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    XoneThemeColors c = GetCurrentTheme();

    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    draw->AddRectFilled(bb.Min, bb.Max, hovered ? c.plashkaBgHover : c.plashkaBg, 7.0f * scale);
    draw->AddRect(bb.Min, bb.Max, hovered ? c.plashkaBorderHover : c.plashkaBorder, 7.0f * scale, 0, 1.0f);

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(pos.x + 14.0f * scale, pos.y + (h - labelSize.y) * 0.5f), c.textPrimary, label);

    float toggleW = 38.0f * scale;
    float toggleH = 20.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - toggleW - 14.0f * scale, pos.y + (h - toggleH) * 0.5f));
    std::string toggleId = std::string("##tgl_") + label;
    bool changed = XoneToggle(toggleId.c_str(), value, scale);

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h + 6.0f * scale));
    return changed;
}

inline void XonePlashkaValue(const char* label, const char* value, float scale, ImU32 valColor = 0, float height = 40.0f)
{
    float w = ImGui::GetContentRegionAvail().x;
    float h = height * scale;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    XoneThemeColors c = GetCurrentTheme();

    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    draw->AddRectFilled(bb.Min, bb.Max, c.plashkaBg, 7.0f * scale);
    draw->AddRect(bb.Min, bb.Max, c.plashkaBorder, 7.0f * scale, 0, 1.0f);

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(pos.x + 14.0f * scale, pos.y + (h - labelSize.y) * 0.5f), c.textMuted, label);

    const ImVec2 valSize = ImGui::CalcTextSize(value);
    draw->AddText(ImVec2(pos.x + w - valSize.x - 14.0f * scale, pos.y + (h - valSize.y) * 0.5f),
        valColor ? valColor : c.textPrimary, value);

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h + 6.0f * scale));
}

inline bool XonePlashkaButton(const char* label, const char* btnText, float scale, bool primary = false, float height = 42.0f)
{
    float w = ImGui::GetContentRegionAvail().x;
    float h = height * scale;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    XoneThemeColors c = GetCurrentTheme();

    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    draw->AddRectFilled(bb.Min, bb.Max, c.plashkaBg, 7.0f * scale);
    draw->AddRect(bb.Min, bb.Max, c.plashkaBorder, 7.0f * scale, 0, 1.0f);

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(pos.x + 14.0f * scale, pos.y + (h - labelSize.y) * 0.5f), c.textPrimary, label);

    const ImVec2 btnTextSize = ImGui::CalcTextSize(btnText);
    float btnW = std::max(85.0f * scale, btnTextSize.x + 20.0f * scale);
    float btnH = 28.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - btnW - 12.0f * scale, pos.y + (h - btnH) * 0.5f));

    ImGui::PushStyleColor(ImGuiCol_Button, primary ? IM_COL32(235, 235, 245, 255) : c.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, primary ? IM_COL32(255, 255, 255, 255) : c.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, primary ? IM_COL32(200, 200, 210, 255) : c.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Text, primary ? IM_COL32(15, 15, 20, 255) : c.textPrimary);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);

    std::string btnId = std::string(btnText) + "##" + label;
    bool clicked = ImGui::Button(btnId.c_str(), ImVec2(btnW, btnH));

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h + 6.0f * scale));
    return clicked;
}

inline bool XoneButton(const char* label, float scale, bool primary = false, float width = -1.0f, float height = 36.0f)
{
    float w = (width > 0.0f) ? width : ImGui::GetContentRegionAvail().x;
    float h = height * scale;
    XoneThemeColors c = GetCurrentTheme();

    ImGui::PushStyleColor(ImGuiCol_Button, primary ? IM_COL32(235, 235, 245, 255) : c.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, primary ? IM_COL32(255, 255, 255, 255) : c.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, primary ? IM_COL32(190, 190, 205, 255) : c.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Text, primary ? IM_COL32(14, 15, 20, 255) : c.textPrimary);
    ImGui::PushStyleColor(ImGuiCol_Border, c.plashkaBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    bool clicked = ImGui::Button(label, ImVec2(w, h));

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return clicked;
}

inline bool XonePlashkaInt(const char* label, int* value, int minVal, int maxVal, float scale, float height = 40.0f)
{
    float w = ImGui::GetContentRegionAvail().x;
    float h = height * scale;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    XoneThemeColors c = GetCurrentTheme();

    ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    draw->AddRectFilled(bb.Min, bb.Max, c.plashkaBg, 7.0f * scale);
    draw->AddRect(bb.Min, bb.Max, c.plashkaBorder, 7.0f * scale, 0, 1.0f);

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(pos.x + 14.0f * scale, pos.y + (h - labelSize.y) * 0.5f), c.textPrimary, label);

    char valBuf[32];
    std::snprintf(valBuf, sizeof(valBuf), "%d", *value);
    const ImVec2 valSize = ImGui::CalcTextSize(valBuf);

    float btnSize = 24.0f * scale;
    float totalControlsW = btnSize * 2.0f + 40.0f * scale + 10.0f * scale;
    float startX = pos.x + w - totalControlsW - 12.0f * scale;
    float btnY = pos.y + (h - btnSize) * 0.5f;

    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Button, c.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.buttonActive);

    ImGui::SetCursorScreenPos(ImVec2(startX, btnY));
    std::string minusId = std::string("-##") + label;
    if(ImGui::Button(minusId.c_str(), ImVec2(btnSize, btnSize)))
    {
        if(*value > minVal) { (*value)--; changed = true; }
    }

    draw->AddText(ImVec2(startX + btnSize + (40.0f * scale - valSize.x) * 0.5f, pos.y + (h - valSize.y) * 0.5f),
        c.textPrimary, valBuf);

    ImGui::SetCursorScreenPos(ImVec2(startX + btnSize + 40.0f * scale + 6.0f * scale, btnY));
    std::string plusId = std::string("+##") + label;
    if(ImGui::Button(plusId.c_str(), ImVec2(btnSize, btnSize)))
    {
        if(*value < maxVal) { (*value)++; changed = true; }
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h + 6.0f * scale));
    return changed;
}
