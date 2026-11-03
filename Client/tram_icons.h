#pragma once

#include "third_party/imgui/imgui.h"
#include <cmath>

namespace TramIcons
{
    inline void DrawTram(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Pantograph
        dl->AddLine(ImVec2(p.x + s*0.35f, p.y + s*0.22f), ImVec2(p.x + s*0.50f, p.y + s*0.06f), col, 1.6f);
        dl->AddLine(ImVec2(p.x + s*0.65f, p.y + s*0.22f), ImVec2(p.x + s*0.50f, p.y + s*0.06f), col, 1.6f);
        dl->AddLine(ImVec2(p.x + s*0.30f, p.y + s*0.06f), ImVec2(p.x + s*0.70f, p.y + s*0.06f), col, 1.8f);

        // Tram body
        dl->AddRect(ImVec2(p.x + s*0.14f, p.y + s*0.22f), ImVec2(p.x + s*0.86f, p.y + s*0.84f), col, s*0.14f, 0, 1.8f);

        // Windshield
        dl->AddRect(ImVec2(p.x + s*0.24f, p.y + s*0.32f), ImVec2(p.x + s*0.76f, p.y + s*0.54f), col, s*0.08f, 0, 1.4f);
        dl->AddLine(ImVec2(p.x + s*0.50f, p.y + s*0.32f), ImVec2(p.x + s*0.50f, p.y + s*0.54f), col, 1.2f);

        // Headlights
        dl->AddCircleFilled(ImVec2(p.x + s*0.30f, p.y + s*0.70f), s*0.07f, col);
        dl->AddCircleFilled(ImVec2(p.x + s*0.70f, p.y + s*0.70f), s*0.07f, col);

        // Wheels
        dl->AddLine(ImVec2(p.x + s*0.22f, p.y + s*0.92f), ImVec2(p.x + s*0.38f, p.y + s*0.92f), col, 2.0f);
        dl->AddLine(ImVec2(p.x + s*0.62f, p.y + s*0.92f), ImVec2(p.x + s*0.78f, p.y + s*0.92f), col, 2.0f);
    }

    inline void DrawGear(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        ImVec2 c(p.x + s*0.50f, p.y + s*0.50f);
        float r_inner = s * 0.16f;
        float r_mid = s * 0.30f;
        float r_outer = s * 0.44f;
        dl->AddCircle(c, r_mid, col, 16, 1.8f);
        dl->AddCircleFilled(c, r_inner, col, 12);
        for(int i = 0; i < 8; ++i)
        {
            float a = i * (3.14159265f / 4.0f);
            float ca = std::cos(a), sa = std::sin(a);
            dl->AddLine(ImVec2(c.x + ca * r_mid, c.y + sa * r_mid),
                        ImVec2(c.x + ca * r_outer, c.y + sa * r_outer), col, 2.4f);
        }
    }

    inline void DrawCrash(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Collision spark star
        ImVec2 c(p.x + s*0.50f, p.y + s*0.32f);
        for(int i = 0; i < 8; ++i)
        {
            float a = i * (3.14159265f / 4.0f);
            float r = (i % 2 == 0) ? s * 0.28f : s * 0.12f;
            dl->AddLine(c, ImVec2(c.x + std::cos(a)*r, c.y + std::sin(a)*r), col, 1.8f);
        }
        // Vehicle roof / hood below
        dl->AddRect(ImVec2(p.x + s*0.15f, p.y + s*0.55f), ImVec2(p.x + s*0.85f, p.y + s*0.85f), col, s*0.12f, 0, 1.6f);
        dl->AddCircleFilled(ImVec2(p.x + s*0.30f, p.y + s*0.88f), s*0.08f, col);
        dl->AddCircleFilled(ImVec2(p.x + s*0.70f, p.y + s*0.88f), s*0.08f, col);
    }

    inline void DrawCar(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Car profile/front
        dl->AddRect(ImVec2(p.x + s*0.15f, p.y + s*0.40f), ImVec2(p.x + s*0.85f, p.y + s*0.78f), col, s*0.14f, 0, 1.8f);
        // Cabin
        dl->AddRect(ImVec2(p.x + s*0.26f, p.y + s*0.22f), ImVec2(p.x + s*0.74f, p.y + s*0.45f), col, s*0.10f, 0, 1.5f);
        // Headlights
        dl->AddCircleFilled(ImVec2(p.x + s*0.28f, p.y + s*0.60f), s*0.07f, col);
        dl->AddCircleFilled(ImVec2(p.x + s*0.72f, p.y + s*0.60f), s*0.07f, col);
        // Wheels
        dl->AddCircleFilled(ImVec2(p.x + s*0.28f, p.y + s*0.86f), s*0.08f, col);
        dl->AddCircleFilled(ImVec2(p.x + s*0.72f, p.y + s*0.86f), s*0.08f, col);
    }

    inline void DrawBell(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Top loop
        dl->AddCircle(ImVec2(p.x + s*0.50f, p.y + s*0.15f), s*0.08f, col, 8, 1.4f);
        // Dome
        dl->AddBezierQuadratic(ImVec2(p.x + s*0.22f, p.y + s*0.72f),
                               ImVec2(p.x + s*0.24f, p.y + s*0.28f),
                               ImVec2(p.x + s*0.50f, p.y + s*0.22f), col, 1.8f);
        dl->AddBezierQuadratic(ImVec2(p.x + s*0.50f, p.y + s*0.22f),
                               ImVec2(p.x + s*0.76f, p.y + s*0.28f),
                               ImVec2(p.x + s*0.78f, p.y + s*0.72f), col, 1.8f);
        dl->AddLine(ImVec2(p.x + s*0.18f, p.y + s*0.74f), ImVec2(p.x + s*0.82f, p.y + s*0.74f), col, 2.0f);
        // Clapper
        dl->AddCircleFilled(ImVec2(p.x + s*0.50f, p.y + s*0.86f), s*0.08f, col);
    }

    inline void DrawChat(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        dl->AddRect(ImVec2(p.x + s*0.12f, p.y + s*0.18f), ImVec2(p.x + s*0.88f, p.y + s*0.72f), col, s*0.16f, 0, 1.8f);
        // Tail
        dl->AddTriangleFilled(ImVec2(p.x + s*0.25f, p.y + s*0.70f),
                              ImVec2(p.x + s*0.45f, p.y + s*0.70f),
                              ImVec2(p.x + s*0.22f, p.y + s*0.90f), col);
        // Inner dots
        dl->AddCircleFilled(ImVec2(p.x + s*0.35f, p.y + s*0.45f), s*0.05f, col);
        dl->AddCircleFilled(ImVec2(p.x + s*0.50f, p.y + s*0.45f), s*0.05f, col);
        dl->AddCircleFilled(ImVec2(p.x + s*0.65f, p.y + s*0.45f), s*0.05f, col);
    }

    inline void DrawKeyboard(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        dl->AddRect(ImVec2(p.x + s*0.12f, p.y + s*0.22f), ImVec2(p.x + s*0.88f, p.y + s*0.78f), col, s*0.12f, 0, 1.8f);
        // Key rows
        for(int r = 0; r < 2; ++r)
        {
            float yk = p.y + s*(0.35f + r * 0.16f);
            for(int k = 0; k < 4; ++k)
            {
                float xk = p.x + s*(0.22f + k * 0.15f);
                dl->AddRectFilled(ImVec2(xk, yk), ImVec2(xk + s*0.10f, yk + s*0.10f), col, 1.0f);
            }
        }
        // Spacebar
        dl->AddRectFilled(ImVec2(p.x + s*0.30f, p.y + s*0.66f), ImVec2(p.x + s*0.70f, p.y + s*0.72f), col, 1.0f);
    }

    inline void DrawWrench(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Diagonal shaft
        dl->AddLine(ImVec2(p.x + s*0.25f, p.y + s*0.75f), ImVec2(p.x + s*0.65f, p.y + s*0.35f), col, 2.5f);
        // Head
        dl->AddCircle(ImVec2(p.x + s*0.72f, p.y + s*0.28f), s*0.18f, col, 12, 2.0f);
        dl->AddCircleFilled(ImVec2(p.x + s*0.22f, p.y + s*0.78f), s*0.08f, col);
    }

    inline void DrawChart(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // 3 vertical bars
        float bw = s * 0.16f;
        float base = p.y + s * 0.88f;
        // Bar 1 (left)
        dl->AddRectFilled(ImVec2(p.x + s*0.16f, base - s*0.36f), ImVec2(p.x + s*0.16f + bw, base), col, s*0.05f);
        // Bar 2 (mid)
        dl->AddRectFilled(ImVec2(p.x + s*0.42f, base - s*0.60f), ImVec2(p.x + s*0.42f + bw, base), col, s*0.05f);
        // Bar 3 (right)
        dl->AddRectFilled(ImVec2(p.x + s*0.68f, base - s*0.80f), ImVec2(p.x + s*0.68f + bw, base), col, s*0.05f);
    }

    inline void DrawPulse(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        float cy = p.y + s * 0.50f;
        ImVec2 pts[] = {
            ImVec2(p.x + s*0.08f, cy),
            ImVec2(p.x + s*0.32f, cy),
            ImVec2(p.x + s*0.42f, cy - s*0.32f),
            ImVec2(p.x + s*0.58f, cy + s*0.32f),
            ImVec2(p.x + s*0.68f, cy),
            ImVec2(p.x + s*0.92f, cy)
        };
        for(size_t i = 0; i < 5; ++i)
        {
            dl->AddLine(pts[i], pts[i+1], col, 1.8f);
        }
    }

    inline void DrawRoad(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Left & right road borders
        dl->AddLine(ImVec2(p.x + s*0.38f, p.y + s*0.15f), ImVec2(p.x + s*0.12f, p.y + s*0.85f), col, 2.0f);
        dl->AddLine(ImVec2(p.x + s*0.62f, p.y + s*0.15f), ImVec2(p.x + s*0.88f, p.y + s*0.85f), col, 2.0f);
        // Dashed center line
        dl->AddLine(ImVec2(p.x + s*0.50f, p.y + s*0.25f), ImVec2(p.x + s*0.50f, p.y + s*0.45f), col, 1.8f);
        dl->AddLine(ImVec2(p.x + s*0.50f, p.y + s*0.60f), ImVec2(p.x + s*0.50f, p.y + s*0.80f), col, 1.8f);
    }

    inline void DrawEye(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Upper arc
        dl->AddBezierQuadratic(ImVec2(p.x + s*0.10f, p.y + s*0.50f),
                               ImVec2(p.x + s*0.50f, p.y + s*0.18f),
                               ImVec2(p.x + s*0.90f, p.y + s*0.50f), col, 1.8f);
        // Lower arc
        dl->AddBezierQuadratic(ImVec2(p.x + s*0.10f, p.y + s*0.50f),
                               ImVec2(p.x + s*0.50f, p.y + s*0.82f),
                               ImVec2(p.x + s*0.90f, p.y + s*0.50f), col, 1.8f);
        // Iris & pupil
        dl->AddCircle(ImVec2(p.x + s*0.50f, p.y + s*0.50f), s*0.16f, col, 12, 1.6f);
        dl->AddCircleFilled(ImVec2(p.x + s*0.50f, p.y + s*0.50f), s*0.08f, col);
    }

    inline void DrawCoins(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // 3 stacked coin rims
        for(int i = 0; i < 3; ++i)
        {
            float cy = p.y + s*(0.35f + i * 0.22f);
            dl->AddRect(ImVec2(p.x + s*0.20f, cy - s*0.09f), ImVec2(p.x + s*0.80f, cy + s*0.09f), col, s*0.08f, 0, 1.6f);
        }
    }

    inline void DrawDoc(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        dl->AddRect(ImVec2(p.x + s*0.18f, p.y + s*0.14f), ImVec2(p.x + s*0.82f, p.y + s*0.86f), col, s*0.08f, 0, 1.8f);
        // Lines of text
        dl->AddLine(ImVec2(p.x + s*0.30f, p.y + s*0.38f), ImVec2(p.x + s*0.70f, p.y + s*0.38f), col, 1.5f);
        dl->AddLine(ImVec2(p.x + s*0.30f, p.y + s*0.54f), ImVec2(p.x + s*0.70f, p.y + s*0.54f), col, 1.5f);
        dl->AddLine(ImVec2(p.x + s*0.30f, p.y + s*0.70f), ImVec2(p.x + s*0.55f, p.y + s*0.70f), col, 1.5f);
    }

    inline void DrawTrash(ImDrawList* dl, ImVec2 p, float s, ImU32 col)
    {
        if(!dl || s <= 0.0f) return;
        // Lid
        dl->AddLine(ImVec2(p.x + s*0.18f, p.y + s*0.28f), ImVec2(p.x + s*0.82f, p.y + s*0.28f), col, 1.8f);
        dl->AddLine(ImVec2(p.x + s*0.40f, p.y + s*0.20f), ImVec2(p.x + s*0.60f, p.y + s*0.20f), col, 1.8f);
        // Can body
        dl->AddRect(ImVec2(p.x + s*0.26f, p.y + s*0.28f), ImVec2(p.x + s*0.74f, p.y + s*0.84f), col, s*0.06f, 0, 1.6f);
        // Vertical lines
        dl->AddLine(ImVec2(p.x + s*0.42f, p.y + s*0.38f), ImVec2(p.x + s*0.42f, p.y + s*0.74f), col, 1.2f);
        dl->AddLine(ImVec2(p.x + s*0.58f, p.y + s*0.38f), ImVec2(p.x + s*0.58f, p.y + s*0.74f), col, 1.2f);
    }
}
