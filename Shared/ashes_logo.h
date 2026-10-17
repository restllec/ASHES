#pragma once

#if __has_include("third_party/imgui/imgui.h")
#include "third_party/imgui/imgui.h"
#elif __has_include("imgui.h")
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>

inline void RenderAshesLogo(ImDrawList* dl, ImVec2 pos, float size, ImU32 color = IM_COL32_WHITE, float glow_alpha = 0.3f)
{
    if(!dl || size <= 0.0f)
        return;

    dl->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

    const float s = size / 106.0f;
    const float offX = pos.x;
    const float offY = pos.y;
    auto pt = [&](float x, float y) -> ImVec2
    {
        return ImVec2(offX + (x - 4.0f) * s, offY + (y - 8.0f) * s);
    };

    // Soft ember/ash glow behind the emblem (as in ASHES Logo UI.svg #bgGlow)
    if(glow_alpha > 0.0f)
    {
        const ImVec2 glowCenter = pt(57.0f, 62.0f);
        const float baseR = size * 0.55f;
        dl->AddCircleFilled(glowCenter, baseR * 1.25f, IM_COL32(58, 21, 24, static_cast<int>(glow_alpha * 80.0f)), 24);
        dl->AddCircleFilled(glowCenter, baseR * 0.75f, IM_COL32(85, 28, 33, static_cast<int>(glow_alpha * 95.0f)), 24);
    }

    const float baseAlpha = static_cast<float>((color >> 24) & 0xFF) / 255.0f;

    // 1. Left inner blade (55,8 -> 12,108 -> 34,108 -> 55,56)
    const ImVec2 pBladeL[] = { pt(55.0f, 8.0f), pt(12.0f, 108.0f), pt(34.0f, 108.0f), pt(55.0f, 56.0f) };
    const ImU32 colBladeL = IM_COL32(235, 238, 245, static_cast<int>(255.0f * baseAlpha));
    dl->AddConvexPolyFilled(pBladeL, 4, colBladeL);

    // 2. Left outer wing highlight (52,15 -> 4,92 -> 12,108 -> 55,8)
    const ImVec2 pWingL[] = { pt(52.0f, 15.0f), pt(4.0f, 92.0f), pt(12.0f, 108.0f), pt(55.0f, 8.0f) };
    const ImU32 colWingL = IM_COL32(255, 255, 255, static_cast<int>(230.0f * baseAlpha));
    dl->AddConvexPolyFilled(pWingL, 4, colWingL);

    // 3. Right inner blade (59,8 -> 59,56 -> 80,108 -> 102,108)
    const ImVec2 pBladeR[] = { pt(59.0f, 8.0f), pt(59.0f, 56.0f), pt(80.0f, 108.0f), pt(102.0f, 108.0f) };
    const ImU32 colBladeR = IM_COL32(190, 195, 210, static_cast<int>(245.0f * baseAlpha));
    dl->AddConvexPolyFilled(pBladeR, 4, colBladeR);

    // 4. Right outer wing shadow (62,15 -> 110,92 -> 102,108 -> 59,8)
    const ImVec2 pWingR[] = { pt(62.0f, 15.0f), pt(110.0f, 92.0f), pt(102.0f, 108.0f), pt(59.0f, 8.0f) };
    const ImU32 colWingR = IM_COL32(138, 142, 160, static_cast<int>(200.0f * baseAlpha));
    dl->AddConvexPolyFilled(pWingR, 4, colWingR);

    // 5. Central spark / floating vertical diamond (57,62 -> 45,84 -> 57,106 -> 69,84)
    const ImVec2 pSpark[] = { pt(57.0f, 62.0f), pt(45.0f, 84.0f), pt(57.0f, 106.0f), pt(69.0f, 84.0f) };
    const ImU32 colSpark = IM_COL32(255, 255, 255, static_cast<int>(255.0f * baseAlpha));
    dl->AddConvexPolyFilled(pSpark, 4, colSpark);
}
