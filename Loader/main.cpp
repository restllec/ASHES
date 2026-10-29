#include "config.h"
#include "launcher.h"
#include "loader_license.h"
#include "path_utils.h"
#include "updater.h"
#include "loader_resources.h"
#include "../Shared/runtime_log.h"
#include "../Shared/ashes_logo.h"

#include <Windows.h>
#include <d3d9.h>
#include <windowsx.h>
#include <shellapi.h>

#include "../Client/third_party/imgui/imgui.h"
#include "../Client/third_party/imgui/imgui_internal.h"
#include "../Client/third_party/imgui/backends/imgui_impl_dx9.h"
#include "../Client/third_party/imgui/backends/imgui_impl_win32.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window,
    UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
constexpr int WindowWidth = 800;
constexpr int WindowHeight = 520;

HWND g_window{};
IDirect3D9* g_d3d{};
IDirect3DDevice9* g_device{};
D3DPRESENT_PARAMETERS g_parameters{};
ImFont* g_titleFont{};
ImFont* g_regularFont{};
ImFont* g_codeFont{};
std::wstring g_directory;
DarkFlameConfig g_config;
LoaderLicense::State g_license;
char g_keyInput[64]{};
float g_copiedTimer = 0.0f;
std::string g_injectStatusMsg;
bool g_injected = false;
std::atomic_bool g_isInjecting = false;
std::jthread g_injectWorker;
constexpr std::size_t MaxMemoBytes = 256 * 1024;
std::mutex g_logMutex;
std::string g_logText;
std::vector<char> g_memo{1, '\0'};
std::atomic_bool g_logDirty{};

ImTextureRef Texture(IDirect3DTexture9* texture)
{
    return ImTextureRef(reinterpret_cast<void*>(texture));
}

std::string Utf8(std::wstring_view value)
{
    if(value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if(size <= 0)
        return {};
    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), size, nullptr, nullptr);
    return output;
}

void AppendLog(std::wstring_view text)
{
    RuntimeLog::Write(text);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t timestamp[24]{};
    swprintf_s(timestamp, L"[%02u:%02u:%02u] ",
        now.wHour, now.wMinute, now.wSecond);
    std::string line = Utf8(timestamp);
    line += Utf8(text);
    line.push_back('\n');
    std::scoped_lock lock(g_logMutex);
    g_logText += line;
    if(g_logText.size() > MaxMemoBytes)
    {
        const std::size_t cut = g_logText.size() - MaxMemoBytes;
        const std::size_t newline = g_logText.find('\n', cut);
        g_logText.erase(0, newline == std::string::npos ? cut : newline + 1);
    }
    g_logDirty.store(true, std::memory_order_release);
}

void SyncMemo()
{
    if(!g_logDirty.exchange(false, std::memory_order_acq_rel))
        return;
    std::scoped_lock lock(g_logMutex);
    g_memo.assign(g_logText.begin(), g_logText.end());
    g_memo.push_back('\0');
}

bool SaveConfig(bool reportError = true)
{
    const bool saved = Config::Save(g_directory, g_config);
    if(!saved && reportError)
        AppendLog(L"[error] Ashes.cfg could not be saved");
    return saved;
}

int FilterSerial(ImGuiInputTextCallbackData* data)
{
    const ImWchar ch = data->EventChar;
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z') ? 0 : 1;
}

void TriggerInject(DWORD pid = 0)
{
    if(g_injected || g_isInjecting.load(std::memory_order_acquire))
        return;

    g_isInjecting.store(true, std::memory_order_release);
    g_injectStatusMsg = "Внедрение ASHES...";
    if(g_injectWorker.joinable())
    {
        g_injectWorker.request_stop();
        g_injectWorker.detach();
    }

    g_injectWorker = std::jthread([pid](std::stop_token)
    {
        const int res = Launcher::InjectOnce([](std::wstring_view log)
        {
            AppendLog(log);
        }, pid);

        if(res == 0)
        {
            g_injected = true;
            g_injectStatusMsg = "ASHES успешно внедрен! Закрытие...";
            Sleep(800);
            PostMessageW(g_window, WM_CLOSE, 0, 0);
        }
        else
        {
            g_injectStatusMsg = "Ошибка внедрения (" + std::to_string(res) + ")";
        }
        g_isInjecting.store(false, std::memory_order_release);
    });
}

void StartAutoLauncher()
{
    if(g_injected || g_isInjecting.load(std::memory_order_acquire))
        return;

    g_isInjecting.store(true, std::memory_order_release);
    if(g_injectWorker.joinable())
    {
        g_injectWorker.request_stop();
        g_injectWorker.detach();
    }

    g_injectWorker = std::jthread([](std::stop_token stop)
    {
        const int res = Launcher::Run(stop, [](std::wstring_view log)
        {
            AppendLog(log);
        });
        if(res == 0 && !stop.stop_requested())
        {
            g_injected = true;
            g_injectStatusMsg = "ASHES успешно внедрен!";
            Sleep(800);
            PostMessageW(g_window, WM_CLOSE, 0, 0);
        }
        g_isInjecting.store(false, std::memory_order_release);
    });
}

void DrawAnimatedWaves(ImDrawList* draw, ImVec2 origin, ImVec2 size)
{
    const float t = static_cast<float>(ImGui::GetTime());
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for(int i = 0; i < 7; ++i)
    {
        const float speed = 0.25f + i * 0.07f;
        const float amp = 16.0f + i * 4.0f;
        const float freq = 0.0035f + i * 0.0007f;
        const float phase = i * 1.2f + t * speed;
        const float baseY = origin.y + size.y * (0.12f + i * 0.13f);

        ImVec2 prev(origin.x, baseY + std::sin(phase) * amp);
        constexpr int segments = 45;
        const float stepX = size.x / static_cast<float>(segments);
        for(int s = 1; s <= segments; ++s)
        {
            const float x = origin.x + s * stepX;
            const float y = baseY + std::sin((x - origin.x) * freq + phase) * amp
                            + std::cos((x - origin.x) * freq * 0.5f + phase * 0.6f) * (amp * 0.35f);
            const ImVec2 curr(x, y);
            draw->AddLine(prev, curr, IM_COL32(255, 255, 255, 42 + (i % 3) * 11), 1.6f);
            prev = curr;
        }
    }
    draw->PopClipRect();
}

bool CustomToggle(const char* id, bool* value)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems) return false;

    const ImGuiID imgui_id = window->GetID(id);
    const float w = 36.0f;
    const float h = 18.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb);
    if(!ImGui::ItemAdd(bb, imgui_id))
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, imgui_id, &hovered, &held);
    if(pressed)
    {
        *value = !(*value);
        ImGui::MarkItemEdited(imgui_id);
    }

    static std::unordered_map<ImGuiID, float> anims;
    float& anim = anims[imgui_id];
    const float target = *value ? 1.0f : 0.0f;
    const float dt = ImGui::GetIO().DeltaTime;
    if(anim < target) anim = (std::min)(target, anim + dt * 10.0f);
    else if(anim > target) anim = (std::max)(target, anim - dt * 10.0f);

    ImDrawList* draw = window->DrawList;
    const float rounding = h * 0.5f;

    const ImU32 bgCol = *value ? IM_COL32(165, 110, 255, 255)
        : (hovered ? IM_COL32(45, 48, 60, 255) : IM_COL32(30, 32, 40, 255));
    draw->AddRectFilled(bb.Min, bb.Max, bgCol, rounding);
    draw->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 30), rounding, 0, 1.0f);

    const float knobRadius = (h - 4.0f) * 0.5f;
    const float knobMinX = bb.Min.x + 2.0f + knobRadius;
    const float knobMaxX = bb.Max.x - 2.0f - knobRadius;
    const float knobX = knobMinX + (knobMaxX - knobMinX) * anim;
    const float knobY = bb.Min.y + h * 0.5f;

    const ImU32 knobCol = *value ? IM_COL32(15, 15, 20, 255) : IM_COL32(210, 215, 225, 255);
    draw->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, knobCol);

    return pressed;
}

void ConfigureStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 1.0f;
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.FramePadding = {8.0f, 6.0f};
    style.ItemSpacing = {8.0f, 6.0f};

    // Классический строгий темно-серый / графитовый стиль ASHES (#121212 / #181818), 100% непрозрачный
    style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.65f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.11f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.17f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.14f, 0.15f, 0.19f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.22f, 0.29f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.27f, 0.29f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.85f, 0.88f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.65f, 0.68f, 0.78f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.88f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.28f, 0.36f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.35f, 0.45f, 1.0f);
}

void ConfigureFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const ImWchar* glyphs = io.Fonts->GetGlyphRangesCyrillic();
    g_regularFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, nullptr, glyphs);
    if(!g_regularFont)
        g_regularFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 15.0f, nullptr, glyphs);
    if(!g_regularFont)
        g_regularFont = io.Fonts->AddFontDefault();
    io.FontDefault = g_regularFont;

    g_titleFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f, nullptr, glyphs);
    if(!g_titleFont)
        g_titleFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 20.0f, nullptr, glyphs);

    g_codeFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\consola.ttf", 13.0f, nullptr, glyphs);
    if(!g_codeFont)
        g_codeFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\cour.ttf", 13.0f, nullptr, glyphs);

    if(!g_titleFont) g_titleFont = g_regularFont ? g_regularFont : io.Fonts->AddFontDefault();
    if(!g_codeFont) g_codeFont = g_regularFont ? g_regularFont : io.Fonts->AddFontDefault();
}

void DrawHeader(ImDrawList* draw, ImVec2 origin, float width)
{
    // Logo and Subtitle (composition from ASHES Logo UI.svg)
    RenderAshesLogo(draw, ImVec2(origin.x + 16.0f, origin.y + 10.0f), 30.0f, IM_COL32_WHITE, 0.45f);
    draw->AddText(g_titleFont, 19.0f, ImVec2(origin.x + 54.0f, origin.y + 8.0f),
        IM_COL32(255, 255, 255, 255), "ASHES");
    const char* p1 = "automation suite";
    const char* p2 = "by restleSS";
    const float p1W = g_regularFont->CalcTextSizeA(12.0f, FLT_MAX, -1.0f, p1, p1 + 16).x;
    draw->AddText(g_regularFont, 12.0f, ImVec2(origin.x + 55.0f, origin.y + 30.0f),
        IM_COL32(142, 146, 162, 255), p1);
    draw->AddText(g_regularFont, 12.0f, ImVec2(origin.x + 55.0f + p1W + 6.0f, origin.y + 30.0f),
        IM_COL32(255, 255, 255, 245), p2);

    // Header right-side controls: Single Version badge (PRO / FREE) and Close button
    const float headerY = origin.y + 14.0f;
    const float badgeH = 22.0f;

    // 1. Close button (right-most)
    const ImVec2 closePos(origin.x + width - 36.0f, headerY);
    const ImVec2 closeSize(22.0f, badgeH);

    // 2. Single Version Badge: PRO or FREE
    const bool isPro = g_license.is_pro;
    const char* badgeText = isPro ? "PRO" : "FREE";
    const ImVec2 bTextSz = ImGui::CalcTextSize(badgeText);
    const float badgeW = isPro ? 46.0f : 50.0f;
    const ImVec2 badgePos(closePos.x - 8.0f - badgeW, headerY);

    ImGui::SetCursorScreenPos(badgePos);
    ImGui::BeginGroup();

    // Render Version Badge
    const ImU32 badgeBg = isPro ? IM_COL32(36, 40, 54, 255) : IM_COL32(26, 28, 36, 255);
    const ImU32 badgeTextCol = isPro ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 165, 180, 255);
    draw->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeW, badgePos.y + badgeH), badgeBg, 4.0f);
    draw->AddRect(badgePos, ImVec2(badgePos.x + badgeW, badgePos.y + badgeH),
        isPro ? IM_COL32(165, 110, 255, 140) : IM_COL32(255, 255, 255, 18), 4.0f, 0, 1.0f);
    draw->AddText(ImVec2(badgePos.x + (badgeW - bTextSz.x) * 0.5f, badgePos.y + (badgeH - bTextSz.y) * 0.5f),
        badgeTextCol, badgeText);

    // Render Close Button
    ImGui::SetCursorScreenPos(closePos);
    if(ImGui::InvisibleButton("##close", closeSize))
        PostMessageW(g_window, WM_CLOSE, 0, 0);
    const bool closeHovered = ImGui::IsItemHovered();
    const ImU32 closeCol = closeHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(155, 160, 175, 255);
    if(closeHovered)
        draw->AddRectFilled(closePos, ImVec2(closePos.x + closeSize.x, closePos.y + closeSize.y), IM_COL32(255, 255, 255, 20), 4.0f);
    draw->AddLine(ImVec2(closePos.x + 6.0f, closePos.y + 6.0f), ImVec2(closePos.x + 16.0f, closePos.y + 16.0f), closeCol, 1.8f);
    draw->AddLine(ImVec2(closePos.x + 16.0f, closePos.y + 6.0f), ImVec2(closePos.x + 6.0f, closePos.y + 16.0f), closeCol, 1.8f);

    ImGui::EndGroup();
}

void DrawLicenseCard(ImDrawList* draw, ImVec2 origin, float width)
{
    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 56.0f);
    const ImVec2 cardSize(width, 142.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 7.0f), IM_COL32(142, 146, 162, 255), "СТАТУС ПОДПИСКИ И ЛИЦЕНЗИЯ");

    // Индикатор статуса подписки (зеленая точка + текст)
    const ImVec2 statPos(cardPos.x + 10.0f, cardPos.y + 24.0f);
    const ImVec2 statSize(cardSize.x - 20.0f, 32.0f);
    const ImVec2 statEnd(statPos.x + statSize.x, statPos.y + statSize.y);
    draw->AddRectFilled(statPos, statEnd, IM_COL32(14, 15, 18, 255), 4.0f);
    draw->AddRect(statPos, statEnd, IM_COL32(255, 255, 255, 16), 4.0f, 0, 1.0f);

    const ImVec2 dotCenter(statPos.x + 14.0f, statPos.y + 16.0f);
    const bool subActive = g_license.is_pro || !g_config.licenseKey.empty();
    const ImU32 dotCol = subActive ? IM_COL32(74, 222, 128, 255) : IM_COL32(248, 113, 113, 255);
    draw->AddCircleFilled(dotCenter, 4.5f, dotCol);
    draw->AddCircle(dotCenter, 6.5f, dotCol & IM_COL32(255, 255, 255, 80), 16, 1.0f);

    const char* subStatusText = subActive
        ? (g_license.is_pro ? "Подписка активна (PRO)" : "Подписка активна (STANDARD)")
        : "Подписка не активна (FREE)";

    // Right-aligned buttons with strict non-overlapping layout
    const float actBtnW = 68.0f;
    const float exitBtnW = 52.0f;
    const float btnGroupW = actBtnW + 4.0f + exitBtnW;
    const float buttonsStartX = statEnd.x - btnGroupW - 6.0f;

    draw->PushClipRect(statPos, ImVec2(buttonsStartX - 4.0f, statEnd.y), true);
    draw->AddText(ImVec2(statPos.x + 24.0f, statPos.y + 8.0f),
        subActive ? IM_COL32(245, 245, 250, 255) : IM_COL32(190, 195, 210, 255), subStatusText);
    draw->PopClipRect();

    // Tooltip over status with full details
    ImGui::SetCursorScreenPos(statPos);
    ImGui::InvisibleButton("##sub_tooltip_btn", ImVec2(buttonsStartX - statPos.x, 32.0f));
    if(ImGui::IsItemHovered())
    {
        std::string expDate = g_license.expire_date.empty() ? "31.12.2026" : g_license.expire_date;
        ImGui::SetTooltip("Срок действия: до %s\nСтатус: %s", expDate.c_str(), subActive ? "Активен" : "Не активен");
    }

    // Кнопки Подробнее и Выйти
    ImGui::SetCursorScreenPos(ImVec2(buttonsStartX, statPos.y + 5.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 44, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(22, 24, 30, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 225, 240, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if(ImGui::Button("Подробнее", ImVec2(actBtnW, 22.0f)))
    {
        ShellExecuteA(nullptr, "open", "https://t.me/ashes_rst_bot", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine(0.0f, 4.0f);
    if(ImGui::Button("Выйти", ImVec2(exitBtnW, 22.0f)))
    {
        g_config.licenseKey.clear();
        g_keyInput[0] = '\0';
        g_license.is_pro = false;
        g_license.status_msg = "Бесплатный доступ";
        LoaderLicense::SaveLicenseToFile(g_directory, "");
        Config::Save(g_directory, g_config);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    // 2. HWID на единой базовой линии и строго по центру кнопки
    const float hwidY = cardPos.y + 64.0f;
    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 12.0f, hwidY + 3.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(142, 146, 162, 255));
    ImGui::TextUnformatted("HWID:");
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 240, 255, 255));
    ImGui::TextUnformatted(g_license.hwid.c_str());
    ImGui::PopStyleColor();

    char copyBtnText[32];
    std::snprintf(copyBtnText, sizeof(copyBtnText), "%s##hwid", (g_copiedTimer > 0.0f) ? "Скопировано!" : "Копировать");
    ImGui::SetCursorScreenPos(ImVec2(cardEnd.x - 96.0f, hwidY));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 44, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(22, 24, 30, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 225, 240, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if(ImGui::Button(copyBtnText, ImVec2(86.0f, 22.0f)))
    {
        ImGui::SetClipboardText(g_license.hwid.c_str());
        g_copiedTimer = 2.0f;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    // 3. Ввод ключа и Активация
    const float inputY = cardPos.y + 96.0f;
    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0f, inputY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(14, 15, 18, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 18));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::SetNextItemWidth(cardSize.x - 116.0f);
    ImGui::InputTextWithHint("##lic_key", "Ключ активации ASHES-...", g_keyInput, sizeof(g_keyInput));
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    ImGui::SetCursorScreenPos(ImVec2(cardEnd.x - 98.0f, inputY));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(165, 110, 255, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(185, 135, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(145, 95, 235, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if(ImGui::Button("Активация", ImVec2(88.0f, 26.0f)))
    {
        std::string key(g_keyInput);
        while(!key.empty() && (key.back() == ' ' || key.back() == '\r' || key.back() == '\n'))
            key.pop_back();
        if(!key.empty())
        {
            LoaderLicense::ValidateKey(g_license, key);
            LoaderLicense::SaveLicenseToFile(g_directory, key);
            g_config.licenseKey = key;
            Config::Save(g_directory, g_config);
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

void DrawSettingsCard(ImDrawList* draw, ImVec2 origin, float width)
{
    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 204.0f);
    const ImVec2 cardSize(width, 270.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 7.0f), IM_COL32(142, 146, 162, 255), "НАСТРОЙКИ ЛОАДЕРА (ФЛАГИ)");

    auto drawCard = [&](float y, const char* title, const char* desc, const char* toggleId, bool* val)
    {
        const ImVec2 pPos(cardPos.x + 10.0f, y);
        const ImVec2 pSize(cardSize.x - 20.0f, 32.0f);
        const ImVec2 pEnd(pPos.x + pSize.x, pPos.y + pSize.y);
        draw->AddRectFilled(pPos, pEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(pPos, pEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(ImVec2(pPos.x + 10.0f, pPos.y + 3.0f), IM_COL32(245, 245, 250, 255), title);
        draw->AddText(g_regularFont, 11.0f, ImVec2(pPos.x + 10.0f, pPos.y + 17.0f), IM_COL32(142, 146, 162, 255), desc);

        ImGui::SetCursorScreenPos(ImVec2(pEnd.x - 44.0f, pPos.y + 7.0f));
        if(CustomToggle(toggleId, val))
        {
            SaveConfig();
        }
    };

    drawCard(cardPos.y + 24.0f, "Anti-Shadow", "Защита железа и теневых детектов", "##anti_shadow", &g_config.antiShadow);
    drawCard(cardPos.y + 59.0f, "Scripts Dumper", "Дамп загружаемых lua-скриптов", "##scripts_dumper", &g_config.scriptsDumper);
    drawCard(cardPos.y + 94.0f, "Black Mirror", "Спуфинг серийного номера железа", "##black_mirror", &g_config.setSerial);
    drawCard(cardPos.y + 129.0f, "Random Serial", "Случайный серийник при запуске", "##random_serial", &g_config.randomSerial);

    // Поле ввода Serial с кнопкой быстрого копирования внутри BeginChild
    const ImVec2 serPos(cardPos.x + 10.0f, cardPos.y + 166.0f);
    const ImVec2 serSize(cardSize.x - 20.0f, 88.0f);

    static char s_serialBuf[36]{};
    static bool s_serialInit = false;
    if(!s_serialInit)
    {
        std::snprintf(s_serialBuf, sizeof(s_serialBuf), "%s", g_config.publicSerial.c_str());
        s_serialInit = true;
    }

    static float s_serialCopiedTimer = 0.0f;
    if(s_serialCopiedTimer > 0.0f)
        s_serialCopiedTimer -= ImGui::GetIO().DeltaTime;

    ImGui::SetCursorScreenPos(serPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 15, 18, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

    if(ImGui::BeginChild("##serial_frame", serSize, true, ImGuiWindowFlags_NoScrollbar))
    {
        // 1. Метка поля
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(142, 146, 162, 255));
        ImGui::TextUnformatted("Серийник (Serial / 32 HEX):");
        ImGui::PopStyleColor();

        // 2. Поле ввода + кнопка копирования
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(12, 13, 16, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 18));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
        if(ImGui::InputText("##public_serial", s_serialBuf, sizeof(s_serialBuf),
            ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CallbackCharFilter, FilterSerial))
        {
            g_config.publicSerial = s_serialBuf;
            SaveConfig();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0.0f, 6.0f);
        char serCopyText[32];
        std::snprintf(serCopyText, sizeof(serCopyText), "%s##serial", (s_serialCopiedTimer > 0.0f) ? "Скопировано!" : "Копировать");
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 30, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 44, 56, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(22, 24, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 225, 240, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if(ImGui::Button(serCopyText, ImVec2(84.0f, 24.0f)))
        {
            ImGui::SetClipboardText(g_config.publicSerial.c_str());
            s_serialCopiedTimer = 2.0f;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        // 3. Подстрочник с отступом ItemSpacing.y от поля и гарантированным отступом снизу
        if(g_regularFont) ImGui::PushFont(g_regularFont);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(130, 135, 150, 255));
        ImGui::TextUnformatted("Привязан к вашему аккаунту MTA Province");
        ImGui::PopStyleColor();
        if(g_regularFont) ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void DrawConsoleCard(ImDrawList* draw, ImVec2 origin, float width, const Launcher::ProcessInfo& proc)
{
    const ImVec2 cardPos(origin.x + 364.0f, origin.y + 56.0f);
    const ImVec2 cardSize(width, 418.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 7.0f), IM_COL32(142, 146, 162, 255), "ВЫВОД ЛОГОВ / СТАТУС");

    // 1. Плашка проверки компонентов («Компоненты на месте»)
    const ImVec2 compPos(cardPos.x + 10.0f, cardPos.y + 24.0f);
    const ImVec2 compSize(cardSize.x - 20.0f, 26.0f);
    const ImVec2 compEnd(compPos.x + compSize.x, compPos.y + compSize.y);
    draw->AddRectFilled(compPos, compEnd, IM_COL32(14, 15, 18, 255), 4.0f);
    draw->AddRect(compPos, compEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);
    draw->AddCircleFilled(ImVec2(compPos.x + 12.0f, compPos.y + 13.0f), 4.0f, IM_COL32(74, 222, 128, 255));
    draw->AddText(g_regularFont, 12.0f, ImVec2(compPos.x + 22.0f, compPos.y + 6.0f),
        IM_COL32(200, 240, 210, 255), "Компоненты на месте (AshesClient.dll, netc.dll)");

    // 2. Статус процесса игры
    const ImVec2 statPos(cardPos.x + 10.0f, cardPos.y + 54.0f);
    const ImVec2 statSize(cardSize.x - 20.0f, 28.0f);
    const ImVec2 statEnd(statPos.x + statSize.x, statPos.y + statSize.y);
    draw->AddRectFilled(statPos, statEnd, IM_COL32(14, 15, 18, 255), 4.0f);
    draw->AddRect(statPos, statEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

    const float time = static_cast<float>(ImGui::GetTime());
    const ImVec2 dotCenter(statPos.x + 12.0f, statPos.y + 14.0f);

    if(g_injected)
    {
        draw->AddCircleFilled(dotCenter, 4.5f, IM_COL32(165, 110, 255, 255));
        draw->AddText(ImVec2(statPos.x + 24.0f, statPos.y + 6.0f), IM_COL32(240, 245, 255, 255), "ASHES внедрен и активен!");
    }
    else if(proc.found)
    {
        draw->AddCircleFilled(dotCenter, 4.5f, IM_COL32(74, 222, 128, 255));
        char procBuf[64]{};
        std::snprintf(procBuf, sizeof(procBuf), "Игра найдена (PID: %lu) - Готов к запуску", proc.pid);
        draw->AddText(ImVec2(statPos.x + 24.0f, statPos.y + 6.0f), IM_COL32(140, 240, 160, 255), procBuf);
    }
    else
    {
        const float pulse = (std::sin(time * 3.5f) + 1.0f) * 0.5f;
        const int dotAlpha = static_cast<int>(120 + pulse * 135);
        draw->AddCircleFilled(dotCenter, 4.5f, IM_COL32(230, 180, 70, dotAlpha));
        draw->AddText(ImVec2(statPos.x + 24.0f, statPos.y + 6.0f), IM_COL32(180, 185, 200, 255), "Ожидание запуска игры...");
    }

    // 3. Вывод логов
    SyncMemo();
    const ImVec2 memoPos(cardPos.x + 10.0f, cardPos.y + 86.0f);
    const ImVec2 memoSize(cardSize.x - 20.0f, 218.0f);
    ImGui::SetCursorScreenPos(memoPos);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(12, 13, 16, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 14));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 210, 225, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::InputTextMultiline("##console_memo", g_memo.data(), g_memo.size(),
        memoSize, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    // 4. Тумблер авто-инжекта
    const ImVec2 tglPos(cardPos.x + 10.0f, cardPos.y + 310.0f);
    const ImVec2 tglSize(cardSize.x - 20.0f, 32.0f);
    const ImVec2 tglEnd(tglPos.x + tglSize.x, tglPos.y + tglSize.y);
    draw->AddRectFilled(tglPos, tglEnd, IM_COL32(14, 15, 18, 255), 4.0f);
    draw->AddRect(tglPos, tglEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

    draw->AddText(ImVec2(tglPos.x + 10.0f, tglPos.y + 7.0f), IM_COL32(230, 230, 240, 255), "Авто-инжект при обнаружении");
    ImGui::SetCursorScreenPos(ImVec2(tglEnd.x - 44.0f, tglPos.y + 7.0f));
    if(CustomToggle("##auto_inject", &g_config.autoInject))
    {
        SaveConfig();
        if(g_config.autoInject)
            StartAutoLauncher();
        else if(g_injectWorker.joinable())
            g_injectWorker.request_stop();
    }

    // 5. Большая кнопка запуска
    const ImVec2 btnPos(cardPos.x + 10.0f, cardPos.y + 348.0f);
    const ImVec2 btnSize(cardSize.x - 20.0f, 44.0f);
    ImGui::SetCursorScreenPos(btnPos);

    const bool canInject = !g_injected && !g_isInjecting.load();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, canInject ? IM_COL32(165, 110, 255, 220) : IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, canInject ? IM_COL32(185, 135, 255, 255) : IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, canInject ? IM_COL32(145, 95, 235, 255) : IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, canInject ? IM_COL32(255, 255, 255, 255) : IM_COL32(130, 135, 150, 255));

    const char* btnLabel = g_injected ? "ASHES ВНЕДРЕН И АКТИВЕН"
        : (g_isInjecting.load() ? "ЗАГРУЗКА..." : "ЗАПУСТИТЬ ASHES (INJECT)");

    if(ImGui::Button(btnLabel, btnSize) && canInject)
    {
        TriggerInject(proc.pid);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    if(!g_injectStatusMsg.empty())
    {
        const ImVec2 msgSize = ImGui::CalcTextSize(g_injectStatusMsg.c_str());
        draw->AddText(ImVec2(cardPos.x + (cardSize.x - msgSize.x) * 0.5f, cardPos.y + 398.0f),
            g_injected ? IM_COL32(74, 222, 128, 255) : IM_COL32(248, 113, 113, 255),
            g_injectStatusMsg.c_str());
    }
}

#if 0
static const char* GetKeyName(int vk)
{
    switch(vk)
    {
    case VK_SHIFT: case VK_LSHIFT: return "Left Shift";
    case VK_RSHIFT: return "Right Shift";
    case VK_CONTROL: case VK_LCONTROL: return "Left Ctrl";
    case VK_RCONTROL: return "Right Ctrl";
    case VK_MENU: case VK_LMENU: return "Left Alt";
    case VK_RMENU: return "Right Alt";
    case VK_SPACE: return "Space";
    case VK_TAB: return "Tab";
    case VK_CAPITAL: return "Caps Lock";
    case VK_OEM_3: return "~ / Ё";
    default:
        static char buf[32];
        if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
        if (vk >= '0' && vk <= '9') { buf[0] = (char)vk; buf[1] = 0; return buf; }
        UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        if (scanCode != 0 && GetKeyNameTextA(scanCode << 16, buf, sizeof(buf)) > 0)
            return buf;
        std::snprintf(buf, sizeof(buf), "0x%02X", vk);
        return buf;
    }
}

static bool KeyBindButton(const char* id, int* keyVal)
{
    static std::string s_activeBindId;
    const bool isBinding = (s_activeBindId == id);
    char label[48];
    if (isBinding)
    {
        std::snprintf(label, sizeof(label), "[ Нажмите клавишу... ]##%s", id);
        for (int k = 1; k < 255; ++k)
        {
            if (k == VK_LBUTTON || k == VK_RBUTTON) continue;
            if (GetAsyncKeyState(k) & 0x8000)
            {
                if (k != VK_ESCAPE)
                {
                    *keyVal = k;
                    SaveConfig();
                }
                s_activeBindId.clear();
                return true;
            }
        }
    }
    else
    {
        std::snprintf(label, sizeof(label), "[ %s ]##%s", GetKeyName(*keyVal), id);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, isBinding ? IM_COL32(165, 110, 255, 230) : IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(185, 135, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, isBinding ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 225, 240, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if (ImGui::Button(label, ImVec2(120.0f, 26.0f)))
    {
        s_activeBindId = isBinding ? "" : id;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return false;
}

static void ModeSelector(const char* id, bool* isHold)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    const bool hold = *isHold;

    ImGui::PushStyleColor(ImGuiCol_Button, !hold ? IM_COL32(165, 110, 255, 230) : IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, !hold ? IM_COL32(185, 135, 255, 255) : IM_COL32(40, 44, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, !hold ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 165, 180, 255));
    char b1[48]; std::snprintf(b1, sizeof(b1), "Переключение##%s", id);
    if (ImGui::Button(b1, ImVec2(100.0f, 26.0f)))
    {
        *isHold = false;
        SaveConfig();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 6.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, hold ? IM_COL32(165, 110, 255, 230) : IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hold ? IM_COL32(185, 135, 255, 255) : IM_COL32(40, 44, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, hold ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 165, 180, 255));
    char b2[48]; std::snprintf(b2, sizeof(b2), "Удержание##%s", id);
    if (ImGui::Button(b2, ImVec2(90.0f, 26.0f)))
    {
        *isHold = true;
        SaveConfig();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void DrawMiscCard(ImDrawList* draw, ImVec2 origin, float fullWidth, float fullHeight)
{
    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 56.0f);
    const float cardHeight = fullHeight - 72.0f;
    const float availableW = fullWidth - 32.0f;
    const float leftW = std::floor((availableW - 14.0f) * 0.55f);
    const float rightW = availableW - leftW - 14.0f;

    // 1. Левая панель: Блок «Движение»
    const ImVec2 leftPos = cardPos;
    const ImVec2 leftEnd(leftPos.x + leftW, leftPos.y + cardHeight);
    draw->AddRectFilled(leftPos, leftEnd, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(leftPos, leftEnd, IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(leftPos.x + 12.0f, leftPos.y + 7.0f), IM_COL32(142, 146, 162, 255), "ДВИЖЕНИЕ (ПАРАМЕТРЫ УПРАВЛЕНИЯ)");

    float curY = leftPos.y + 24.0f;

    // --- Параметр 1: Скорость игры ---
    {
        const ImVec2 bPos(leftPos.x + 10.0f, curY);
        const ImVec2 bEnd(bPos.x + leftW - 20.0f, bPos.y + 115.0f);
        draw->AddRectFilled(bPos, bEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(bPos, bEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(ImVec2(bPos.x + 10.0f, bPos.y + 8.0f), IM_COL32(245, 245, 250, 255), "Скорость игры");
        ImGui::SetCursorScreenPos(ImVec2(bEnd.x - 44.0f, bPos.y + 7.0f));
        if(CustomToggle("##game_speed_tgl", &g_config.gameSpeedEnabled)) SaveConfig();

        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 36.0f));
        KeyBindButton("##game_speed_key", &g_config.gameSpeedKey);
        ImGui::SameLine(0.0f, 8.0f);
        ModeSelector("##game_speed_mode", &g_config.gameSpeedHold);

        draw->AddText(g_regularFont, 12.0f, ImVec2(bPos.x + 10.0f, bPos.y + 72.0f), IM_COL32(142, 146, 162, 255), "Множитель скорости:");
        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 88.0f));
        ImGui::SetNextItemWidth(leftW - 40.0f);
        if(ImGui::SliderFloat("##game_speed_mult", &g_config.gameSpeedMultiplier, 0.5f, 2.0f, "%.2fx"))
            SaveConfig();

        curY += 124.0f;
    }

    // --- Параметр 2: Плавный разгон ---
    {
        const ImVec2 bPos(leftPos.x + 10.0f, curY);
        const ImVec2 bEnd(bPos.x + leftW - 20.0f, bPos.y + 115.0f);
        draw->AddRectFilled(bPos, bEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(bPos, bEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(ImVec2(bPos.x + 10.0f, bPos.y + 8.0f), IM_COL32(245, 245, 250, 255), "Плавный разгон");
        ImGui::SetCursorScreenPos(ImVec2(bEnd.x - 44.0f, bPos.y + 7.0f));
        if(CustomToggle("##smooth_accel_tgl", &g_config.smoothAccelEnabled)) SaveConfig();

        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 36.0f));
        KeyBindButton("##smooth_accel_key", &g_config.smoothAccelKey);
        ImGui::SameLine(0.0f, 8.0f);
        ModeSelector("##smooth_accel_mode", &g_config.smoothAccelHold);

        draw->AddText(g_regularFont, 12.0f, ImVec2(bPos.x + 10.0f, bPos.y + 72.0f), IM_COL32(142, 146, 162, 255), "Плавность разгона:");
        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 88.0f));
        ImGui::SetNextItemWidth(leftW - 40.0f);
        if(ImGui::SliderFloat("##smooth_accel_pct", &g_config.smoothAccelPercent, 0.0f, 100.0f, "%.0f%%"))
            SaveConfig();

        curY += 124.0f;
    }

    // --- Параметр 3: Тормоз ---
    {
        const ImVec2 bPos(leftPos.x + 10.0f, curY);
        const ImVec2 bEnd(bPos.x + leftW - 20.0f, bPos.y + 115.0f);
        draw->AddRectFilled(bPos, bEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(bPos, bEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(ImVec2(bPos.x + 10.0f, bPos.y + 8.0f), IM_COL32(255, 255, 255, 255), "Тормоз (Ручник)");
        ImGui::SetCursorScreenPos(ImVec2(bEnd.x - 44.0f, bPos.y + 7.0f));
        if(CustomToggle("##brake_tgl", &g_config.brakeEnabled)) SaveConfig();

        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 36.0f));
        KeyBindButton("##brake_key", &g_config.brakeKey);
        ImGui::SameLine(0.0f, 8.0f);
        ModeSelector("##brake_mode", &g_config.brakeHold);

        draw->AddText(g_regularFont, 12.0f, ImVec2(bPos.x + 10.0f, bPos.y + 72.0f), IM_COL32(161, 161, 170, 255), "Сила торможения:");
        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 88.0f));
        ImGui::SetNextItemWidth(leftW - 40.0f);
        if(ImGui::SliderFloat("##brake_pct", &g_config.brakePercent, 0.0f, 100.0f, "%.0f%%"))
            SaveConfig();
    }

    // 2. Правая панель: Блок «FPS»
    const ImVec2 rightPos(leftPos.x + leftW + 14.0f, cardPos.y);
    const ImVec2 rightEnd(rightPos.x + rightW, rightPos.y + cardHeight);
    draw->AddRectFilled(rightPos, rightEnd, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(rightPos, rightEnd, IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(rightPos.x + 12.0f, rightPos.y + 7.0f), IM_COL32(142, 146, 162, 255), "FPS (ОГРАНИЧИТЕЛЬ ЧАСТОТЫ КАДРОВ)");

    float rY = rightPos.y + 24.0f;

    // Включение FPS лимита и слайдер
    {
        const ImVec2 bPos(rightPos.x + 10.0f, rY);
        const ImVec2 bEnd(bPos.x + rightW - 20.0f, bPos.y + 88.0f);
        draw->AddRectFilled(bPos, bEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(bPos, bEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(ImVec2(bPos.x + 10.0f, bPos.y + 8.0f), IM_COL32(245, 245, 250, 255), "Лимит частоты кадров");
        ImGui::SetCursorScreenPos(ImVec2(bEnd.x - 44.0f, bPos.y + 7.0f));
        if(CustomToggle("##fps_limit_tgl", &g_config.fpsLimitEnabled)) SaveConfig();

        draw->AddText(g_regularFont, 12.0f, ImVec2(bPos.x + 10.0f, bPos.y + 36.0f), IM_COL32(142, 146, 162, 255), "Точная регулировка:");
        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 54.0f));
        ImGui::SetNextItemWidth(rightW - 40.0f);
        if(ImGui::SliderInt("##fps_limit_slider", &g_config.fpsLimit, 30, 300, "%d FPS"))
            SaveConfig();

        rY += 98.0f;
    }

    // Быстрые пресеты в ряд: [ 100 ] [ 144 ] [ 165 ] [ 180 ] [ 200 ]
    {
        const ImVec2 bPos(rightPos.x + 10.0f, rY);
        const ImVec2 bEnd(bPos.x + rightW - 20.0f, bPos.y + 80.0f);
        draw->AddRectFilled(bPos, bEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(bPos, bEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(ImVec2(bPos.x + 10.0f, bPos.y + 8.0f), IM_COL32(245, 245, 250, 255), "Быстрые пресеты FPS:");

        ImGui::SetCursorScreenPos(ImVec2(bPos.x + 10.0f, bPos.y + 34.0f));
        const int presets[] = { 100, 144, 165, 180, 200 };
        const float btnW = std::floor((rightW - 40.0f - 16.0f) / 5.0f);
        for(int i = 0; i < 5; ++i)
        {
            if(i > 0) ImGui::SameLine(0.0f, 4.0f);
            const bool active = (g_config.fpsLimit == presets[i]);
            char pLabel[32];
            std::snprintf(pLabel, sizeof(pLabel), "%d##fps_p%d", presets[i], i);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(165, 110, 255, 230) : IM_COL32(28, 30, 38, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? IM_COL32(185, 135, 255, 255) : IM_COL32(40, 44, 56, 255));
            ImGui::PushStyleColor(ImGuiCol_Text, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 165, 180, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if(ImGui::Button(pLabel, ImVec2(btnW, 28.0f)))
            {
                g_config.fpsLimit = presets[i];
                SaveConfig();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        }

        rY += 90.0f;
    }

    // Информационная подсказка
    {
        const ImVec2 bPos(rightPos.x + 10.0f, rY);
        const ImVec2 bEnd(bPos.x + rightW - 20.0f, cardHeight + cardPos.y - 12.0f);
        draw->AddRectFilled(bPos, bEnd, IM_COL32(14, 15, 18, 255), 4.0f);
        draw->AddRect(bPos, bEnd, IM_COL32(255, 255, 255, 14), 4.0f, 0, 1.0f);

        draw->AddText(g_regularFont, 11.5f, ImVec2(bPos.x + 10.0f, bPos.y + 10.0f), IM_COL32(161, 161, 170, 255),
            "Ограничитель частоты кадров устраняет\n"
            "рассинхронизацию физики и сетевых\n"
            "пакетов MTA Province при резких скачках,\n"
            "а также предотвращает перегрев видеокарты.");
    }
}

void DrawUpdateModal(ImDrawList* draw, ImVec2 origin, float fullWidth, float fullHeight)
{
    draw->AddRectFilled(origin, ImVec2(origin.x + fullWidth, origin.y + fullHeight), IM_COL32(10, 10, 14, 215));

    const float modalW = 460.0f;
    const float modalH = 220.0f;
    const ImVec2 mMin(origin.x + (fullWidth - modalW) * 0.5f, origin.y + (fullHeight - modalH) * 0.5f);
    const ImVec2 mMax(mMin.x + modalW, mMin.y + modalH);

    draw->AddRectFilled(mMin, mMax, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(mMin, mMax, IM_COL32(255, 255, 255, 20), 6.0f, 0, 1.0f);

    RenderAshesLogo(draw, ImVec2(mMin.x + 18.0f, mMin.y + 16.0f), 22.0f, IM_COL32_WHITE, 0.45f);
    draw->AddText(g_titleFont, 16.0f, ImVec2(mMin.x + 48.0f, mMin.y + 14.0f), IM_COL32(255, 255, 255, 255), "ASHES Updater");
    draw->AddText(g_regularFont, 11.0f, ImVec2(mMin.x + 48.0f, mMin.y + 32.0f), IM_COL32(142, 146, 162, 255), "Система автоматического обновления");

    draw->AddText(g_titleFont, 17.0f, ImVec2(mMin.x + 20.0f, mMin.y + 62.0f), IM_COL32(255, 255, 255, 255),
        "Загружаем проверенное обновление");

    draw->AddText(g_regularFont, 12.0f, ImVec2(mMin.x + 20.0f, mMin.y + 86.0f), IM_COL32(142, 146, 162, 255),
        "Синхронизация компонентов клиента и ядра MTA Province...");

    const ImVec2 barPos(mMin.x + 20.0f, mMin.y + 112.0f);
    const float barW = modalW - 40.0f;
    const float barH = 14.0f;
    const ImVec2 barEnd(barPos.x + barW, barPos.y + barH);

    draw->AddRectFilled(barPos, barEnd, IM_COL32(14, 15, 18, 255), 4.0f);
    draw->AddRect(barPos, barEnd, IM_COL32(255, 255, 255, 16), 4.0f, 0, 1.0f);

    const float time = static_cast<float>(ImGui::GetTime());
    float progress = 0.74f;
    float fillW = barW * progress;
    draw->AddRectFilled(barPos, ImVec2(barPos.x + fillW, barEnd.y), IM_COL32(165, 110, 255, 230), 4.0f);

    float shimmerX = std::fmod(time * 120.0f, fillW + 40.0f) - 20.0f;
    if (shimmerX > 0.0f && shimmerX < fillW)
    {
        draw->AddRectFilled(
            ImVec2(barPos.x + shimmerX, barPos.y),
            ImVec2(barPos.x + (std::min)(shimmerX + 35.0f, fillW), barEnd.y),
            IM_COL32(210, 180, 255, 160), 4.0f);
    }

    draw->AddText(g_regularFont, 11.0f, ImVec2(barEnd.x - 70.0f, barPos.y - 18.0f),
        IM_COL32(185, 135, 255, 255), "74% (14.2 MB)");

    draw->AddText(g_regularFont, 11.0f, ImVec2(mMin.x + 20.0f, mMin.y + 138.0f), IM_COL32(142, 146, 162, 255),
        "Настройки сохранятся. Закрытие окна отменяет загрузку.");

    ImGui::SetCursorScreenPos(ImVec2(mMax.x - 110.0f, mMax.y - 38.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 30, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 44, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(22, 24, 30, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 225, 240, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if(ImGui::Button("Отмена", ImVec2(90.0f, 24.0f)))
    {
        g_showUpdateModal = false;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

void DrawSnowWorkerCard(ImDrawList* draw, ImVec2 origin, float fullWidth, float fullHeight)
{
    SnowDashboard::Draw(draw, origin, fullWidth, fullHeight);
}

#if 0
void DrawSnowWorkerCard_Deprecated(ImDrawList* draw, ImVec2 origin, float fullWidth, float fullHeight)
{
    auto& mgr = TerminalMgr::TerminalManager::Instance();
    auto& accounts = mgr.GetAccounts();
    auto& sessions = mgr.GetSessions();

    static int s_selectedProfile = 0;
    static char s_loginBuf[64]{};
    static char s_passBuf[64]{};
    static bool s_showPass = false;
    static int s_serverIdx = 0;
    static int s_taskIdx = 0;
    static bool s_autoReconnect = true;
    static int s_reconnectDelay = 15;
    static bool s_profileInitialized = false;
    static bool s_showConsole = false;
    static int s_selectedSessionIdx = 0;

    if (!s_profileInitialized && !accounts.empty())
    {
        if (s_selectedProfile >= static_cast<int>(accounts.size()))
            s_selectedProfile = 0;
        const auto& p = accounts[s_selectedProfile];
        strncpy_s(s_loginBuf, p.login.c_str(), sizeof(s_loginBuf) - 1);
        std::string plain = TerminalMgr::DecryptPassword(p.encryptedPassword);
        strncpy_s(s_passBuf, plain.c_str(), sizeof(s_passBuf) - 1);
        s_serverIdx = p.serverIndex;
        s_taskIdx = p.taskIndex;
        s_autoReconnect = p.autoReconnect;
        s_reconnectDelay = p.reconnectDelaySec;
        s_profileInitialized = true;
    }

    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 56.0f);
    const float cardHeight = fullHeight - 72.0f;
    const float leftW = 270.0f;
    const float rightW = fullWidth - leftW - 44.0f;

    // LEFT CARD: Account Profiles
    const ImVec2 leftEnd(cardPos.x + leftW, cardPos.y + cardHeight);
    draw->AddRectFilled(cardPos, leftEnd, IM_COL32(18, 19, 26, 170), 8.0f);
    draw->AddRect(cardPos, leftEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 10.0f), IM_COL32(140, 145, 165, 255), "МЕНЕДЖЕР АККАУНТОВ");

    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 12.0f, cardPos.y + 32.0f));
    ImGui::BeginGroup();

    ImGui::TextUnformatted("Профиль:");
    ImGui::SetNextItemWidth(leftW - 24.0f);
    std::string preview = accounts.empty() ? "<нет>" : accounts[s_selectedProfile].login;
    if (ImGui::BeginCombo("##profiles_combo", preview.c_str()))
    {
        for (int i = 0; i < static_cast<int>(accounts.size()); ++i)
        {
            const bool isSel = (s_selectedProfile == i);
            if (ImGui::Selectable(accounts[i].login.c_str(), isSel))
            {
                s_selectedProfile = i;
                const auto& p = accounts[i];
                strncpy_s(s_loginBuf, p.login.c_str(), sizeof(s_loginBuf) - 1);
                std::string plain = TerminalMgr::DecryptPassword(p.encryptedPassword);
                strncpy_s(s_passBuf, plain.c_str(), sizeof(s_passBuf) - 1);
                s_serverIdx = p.serverIndex;
                s_taskIdx = p.taskIndex;
                s_autoReconnect = p.autoReconnect;
                s_reconnectDelay = p.reconnectDelaySec;
            }
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Логин:");
    ImGui::SetNextItemWidth(leftW - 24.0f);
    ImGui::InputText("##acc_login", s_loginBuf, sizeof(s_loginBuf));

    ImGui::Spacing();
    ImGui::TextUnformatted("Пароль:");
    ImGui::SetNextItemWidth(leftW - 74.0f);
    ImGuiInputTextFlags passFlags = s_showPass ? 0 : ImGuiInputTextFlags_Password;
    ImGui::InputText("##acc_pass", s_passBuf, sizeof(s_passBuf), passFlags);
    ImGui::SameLine();
    if (ImGui::Button(s_showPass ? "Скрыть" : "Показ", ImVec2(46.0f, 0.0f)))
    {
        s_showPass = !s_showPass;
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Сервер:");
    ImGui::SetNextItemWidth(leftW - 24.0f);
    const char* serverNames[] = { "Сервер #1", "Сервер #2", "Сервер #3", "Сервер #4", "Сервер #5", "Сервер #6", "Сервер #7" };
    ImGui::Combo("##acc_server", &s_serverIdx, serverNames, IM_ARRAYSIZE(serverNames));

    ImGui::Spacing();
    ImGui::TextUnformatted("Задача/Маршрут:");
    ImGui::SetNextItemWidth(leftW - 24.0f);
    const char* taskNames[] = { "Трамвай (Маршрут)", "Поливалка (Снег)", "Завод ЖТУ (Пешеход)", "Лесоруб", "Табачная фабрика" };
    ImGui::Combo("##acc_task", &s_taskIdx, taskNames, IM_ARRAYSIZE(taskNames));

    ImGui::Spacing();
    ImGui::Checkbox("Авто-реконнект", &s_autoReconnect);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    ImGui::DragInt("сек##delay", &s_reconnectDelay, 1.0f, 3, 300);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("СОХРАНИТЬ", ImVec2(leftW - 24.0f, 28.0f)))
    {
        if (s_selectedProfile >= 0 && s_selectedProfile < static_cast<int>(accounts.size()))
        {
            auto& p = accounts[s_selectedProfile];
            p.login = s_loginBuf;
            p.encryptedPassword = TerminalMgr::EncryptPassword(s_passBuf);
            p.serverIndex = s_serverIdx;
            p.taskIndex = s_taskIdx;
            p.autoReconnect = s_autoReconnect;
            p.reconnectDelaySec = s_reconnectDelay;
            mgr.SaveAccounts();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Новый", ImVec2((leftW - 28.0f) * 0.5f, 26.0f)))
    {
        TerminalMgr::AccountProfile newAcc;
        newAcc.login = "user_" + std::to_string(accounts.size() + 1);
        newAcc.encryptedPassword = TerminalMgr::EncryptPassword("");
        accounts.push_back(newAcc);
        s_selectedProfile = static_cast<int>(accounts.size()) - 1;
        strncpy_s(s_loginBuf, newAcc.login.c_str(), sizeof(s_loginBuf) - 1);
        s_passBuf[0] = '\0';
        mgr.SaveAccounts();
    }
    ImGui::SameLine();
    if (ImGui::Button("Удалить", ImVec2((leftW - 28.0f) * 0.5f, 26.0f)) && accounts.size() > 1)
    {
        accounts.erase(accounts.begin() + s_selectedProfile);
        s_selectedProfile = 0;
        s_profileInitialized = false;
        mgr.SaveAccounts();
    }

    ImGui::EndGroup();

    // RIGHT CARD: Terminal Table & Logs
    const ImVec2 rightPos(cardPos.x + leftW + 12.0f, cardPos.y);
    const ImVec2 rightEnd(rightPos.x + rightW, cardPos.y + cardHeight);
    draw->AddRectFilled(rightPos, rightEnd, IM_COL32(18, 19, 26, 170), 8.0f);
    draw->AddRect(rightPos, rightEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);
    draw->AddText(ImVec2(rightPos.x + 12.0f, rightPos.y + 10.0f), IM_COL32(140, 145, 165, 255), "ТАБЛИЦА СЕССИЙ ТЕРМИНАЛА («СНЕЖОК»)");

    ImGui::SetCursorScreenPos(ImVec2(rightPos.x + 12.0f, rightPos.y + 32.0f));
    ImGui::BeginGroup();

    ImGui::Checkbox("Показать консоль (отладка)", &s_showConsole);
    ImGui::SameLine();
    if (ImGui::Button("ЗАПУСТИТЬ ТЕРМИНАЛ ДЛЯ ВЫБРАННОГО", ImVec2(270.0f, 24.0f)))
    {
        if (s_selectedProfile >= 0 && s_selectedProfile < static_cast<int>(accounts.size()))
        {
            mgr.StartTerminal(accounts[s_selectedProfile], s_showConsole);
        }
    }

    ImGui::Spacing();

    const float tableH = 170.0f;
    if (ImGui::BeginTable("##terminals_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(rightW - 24.0f, tableH)))
    {
        ImGui::TableSetupColumn("Профиль", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Сервер", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Задача", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(sessions.size()); ++i)
        {
            auto s = sessions[i];
            ImGui::PushID(i);
            ImGui::TableNextRow(0, 24.0f);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(s->profileLogin.c_str(), s_selectedSessionIdx == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                s_selectedSessionIdx = i;

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(s->serverName.c_str());

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(s->taskName.c_str());

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (s->status == "WORKING")
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "WORK (%d)", s->cycles);
            else if (s->status == "AUTHORIZED")
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "AUTHORIZED");
            else if (s->status == "CONNECTING")
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "CONNECTING");
            else if (s->status == "ERROR")
                ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.3f, 1.0f), "ERROR");
            else
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "STOPPED");

            ImGui::TableNextColumn();
            if (s->isRunning)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(195, 45, 45, 215));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(230, 60, 60, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(160, 35, 35, 255));
                if (ImGui::Button("Стоп", ImVec2(75.0f, 21.0f)))
                {
                    mgr.StopTerminal(s);
                }
                ImGui::PopStyleColor(3);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 65, 80, 200));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 85, 105, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(50, 55, 70, 255));
                if (ImGui::Button("Удалить", ImVec2(75.0f, 21.0f)))
                {
                    sessions.erase(sessions.begin() + i);
                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopStyleColor(3);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Логи выбранного терминала:");
    const float logH = cardHeight - 32.0f - tableH - 65.0f;
    ImGui::BeginChild("##terminal_log_view", ImVec2(rightW - 24.0f, logH), true);
    if (s_selectedSessionIdx >= 0 && s_selectedSessionIdx < static_cast<int>(sessions.size()))
    {
        auto s = sessions[s_selectedSessionIdx];
        std::lock_guard<std::mutex> lock(s->logMutex);
        for (const auto& line : s->logLines)
        {
            if (line.find("[STATUS]") != std::string::npos)
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", line.c_str());
            else if (line.find("error") != std::string::npos || line.find("FAIL") != std::string::npos)
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
            else
                ImGui::TextUnformatted(line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    else
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Нет активных логов (выберите сессию или запустите терминал)");
    }
    ImGui::EndChild();

    ImGui::EndGroup();
}
#endif

void DrawPluginsCard(ImDrawList* draw, ImVec2 origin, float fullWidth, float fullHeight)
{
    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 56.0f);
    const ImVec2 cardSize(fullWidth - 32.0f, fullHeight - 72.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(20, 21, 26, 255), 6.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 16.0f, cardPos.y + 12.0f), IM_COL32(142, 146, 162, 255), "ВНЕШНИЕ МОДУЛИ РАСШИРЕНИЯ (PLUGINS)");

    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 16.0f, cardPos.y + 40.0f));
    ImGui::BeginGroup();

    const bool hasPlugin = Editions::HasPluginModule(g_directory);
    if (!hasPlugin)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.2f, 1.0f), "Модуль не установлен в рабочей директории");
        ImGui::Spacing();
        ImGui::TextWrapped("Для использования внешних модулей расширения поместите plugin.dll или extension.dll в папку рядом с Ashes.exe.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Путь к рабочей папке:");
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.8f, 1.0f), "%ls", g_directory.c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 30, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 44, 56, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(22, 24, 30, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("Открыть рабочую папку в Explorer", ImVec2(260.0f, 30.0f)))
        {
            ShellExecuteW(nullptr, L"open", g_directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    else
    {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Модуль обнаружен и активен (plugin.dll)");
        ImGui::Spacing();
        static bool s_enablePlugin = true;
        ImGui::Checkbox("Включить автоматическую интеграцию модуля", &s_enablePlugin);
        static float s_param = 1.0f;
        ImGui::SliderFloat("Чувствительность фильтра пакетов", &s_param, 0.1f, 5.0f);
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 30, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 44, 56, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(22, 24, 30, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("Перезагрузить модуль", ImVec2(180.0f, 28.0f)))
        {
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    ImGui::EndGroup();
}
#endif

void DrawWindow()
{
    static float s_pollTimer = 0.0f;
    static Launcher::ProcessInfo s_procInfo{};
    const float dt = ImGui::GetIO().DeltaTime;
    s_pollTimer += dt;
    if(g_copiedTimer > 0.0f)
        g_copiedTimer -= dt;

    if(s_pollTimer > 0.25f)
    {
        s_pollTimer = 0.0f;
        s_procInfo = Launcher::CheckProcess();
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##ashes_loader", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    const ImVec2 origin = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 1. Классический строгий темно-серый / графитовый фон ASHES #121212 / #181818
    draw->AddRectFilled(origin, ImVec2(origin.x + display.x, origin.y + display.y),
        IM_COL32(18, 18, 20, 255), 6.0f);

    // 2. Волны
    DrawAnimatedWaves(draw, origin, display);

    // 3. Тонкая аккуратная рамка окна
    draw->AddRect(origin, ImVec2(origin.x + display.x, origin.y + display.y),
        IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);

    // 4. Header (ASHES by restleSS + бейджи + закрытие)
    DrawHeader(draw, origin, display.x);

    // Левая колонка: Блок «Лицензия и устройство» + «Настройки лоадера (флаги)»
    const float leftW = 346.0f;
    DrawLicenseCard(draw, origin, leftW);
    DrawSettingsCard(draw, origin, leftW);

    // Правая колонка: Блок «Вывод логов / Статус» + авто-инжект + кнопка запуска
    DrawConsoleCard(draw, origin, display.x - leftW - 44.0f, s_procInfo);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

bool CreateDevice(HWND window)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if(!g_d3d)
        return false;
    g_parameters = {};
    g_parameters.Windowed = TRUE;
    g_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    g_parameters.EnableAutoDepthStencil = FALSE;
    g_parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT result = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_parameters, &g_device);
    if(FAILED(result))
    {
        result = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_parameters, &g_device);
    }
    return SUCCEEDED(result);
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if(SUCCEEDED(g_device->Reset(&g_parameters)))
        ImGui_ImplDX9_CreateDeviceObjects();
}

void RenderFrame()
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawWindow();
    ImGui::EndFrame();
    ImGui::Render();
    g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(9, 9, 11), 1.0f, 0);
    if(SUCCEEDED(g_device->BeginScene()))
    {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_device->EndScene();
    }
    const HRESULT result = g_device->Present(nullptr, nullptr, nullptr, nullptr);
    if(result == D3DERR_DEVICELOST
        && g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
    {
        ResetDevice();
    }
}

void DestroyGraphics()
{
    if(g_device) g_device->Release();
    if(g_d3d) g_d3d->Release();
    g_device = nullptr;
    g_d3d = nullptr;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if(ImGui::GetCurrentContext()
        && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
    {
        return 1;
    }
    switch(message)
    {
    case WM_NCHITTEST:
    {
        const LRESULT hit = DefWindowProcW(window, message, wParam, lParam);
        if(hit != HTCLIENT)
            return hit;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        RECT client{};
        GetClientRect(window, &client);
        // Drag only by logo area (x < 210) or space to the right of tabs (x > 530 && x < client.right - 140)
        // Tab buttons (x >= 210 && x <= 530) and badge/close areas must return HTCLIENT so ImGui gets clicks
        if(point.y < 50)
        {
            if (point.x < 210)
                return HTCAPTION;
            if (point.x > 530 && point.x < client.right - 140)
                return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SYSCOMMAND:
        if((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    RuntimeLog::BeginSession();
    g_directory = LoaderPath::OwnDirectory();
    SetCurrentDirectoryW(g_directory.c_str());
    g_config = Config::Load(g_directory);

    // 1. Initialize License state and load existing key
    g_license.hwid = LoaderLicense::GenerateHWID(g_license.mac);
    std::string savedKey = LoaderLicense::LoadSavedLicense(g_directory);
    if(!savedKey.empty())
    {
        LoaderLicense::ValidateKey(g_license, savedKey);
        std::snprintf(g_keyInput, sizeof(g_keyInput), "%s", savedKey.c_str());
    }
    else if(!g_config.licenseKey.empty())
    {
        LoaderLicense::ValidateKey(g_license, g_config.licenseKey);
        std::snprintf(g_keyInput, sizeof(g_keyInput), "%s", g_config.licenseKey.c_str());
    }

    WNDCLASSEXW windowClass{sizeof(windowClass), CS_CLASSDC, &WindowProc, 0, 0,
        instance, LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1)), nullptr,
        nullptr, nullptr, L"AshesLoaderWindow", nullptr};
    RegisterClassExW(&windowClass);
    const int x = (GetSystemMetrics(SM_CXSCREEN) - WindowWidth) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - WindowHeight) / 2;
    g_window = CreateWindowExW(0, windowClass.lpszClassName, L"ASHES",
        WS_POPUP, x, y, WindowWidth, WindowHeight, nullptr, nullptr, instance, nullptr);
    if(g_window && windowClass.hIcon)
    {
        SendMessageW(g_window, WM_SETICON, ICON_BIG, (LPARAM)windowClass.hIcon);
        SendMessageW(g_window, WM_SETICON, ICON_SMALL, (LPARAM)windowClass.hIcon);
    }
    if(!g_window || !CreateDevice(g_window))
    {
        MessageBoxW(nullptr, L"DirectX 9 initialization failed.", L"ASHES",
            MB_OK | MB_ICONERROR);
        DestroyGraphics();
        UnregisterClassW(windowClass.lpszClassName, instance);
        CoUninitialize();
        return 1;
    }

    // Set 12px smooth rounded corners for Win32 window
    HRGN rgn = CreateRoundRectRgn(0, 0, WindowWidth + 1, WindowHeight + 1, 24, 24);
    SetWindowRgn(g_window, rgn, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ConfigureStyle();
    ConfigureFonts();
    const bool win32Ready = ImGui_ImplWin32_Init(g_window);
    const bool dx9Ready = win32Ready && ImGui_ImplDX9_Init(g_device);
    if(!dx9Ready)
    {
        MessageBoxW(nullptr, L"ASHES UI initialization failed.", L"ASHES",
            MB_OK | MB_ICONERROR);
        if(win32Ready)
            ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        DestroyGraphics();
        DestroyWindow(g_window);
        UnregisterClassW(windowClass.lpszClassName, instance);
        CoUninitialize();
        return 2;
    }

    AppendLog(L"[loader] ASHES GUI ready");
    ShowWindow(g_window, SW_SHOWDEFAULT);
    UpdateWindow(g_window);
    RenderFrame();

    AshesUpdater::CheckAndPerformUpdates(g_directory, AppendLog);

    if(g_config.autoInject)
    {
        StartAutoLauncher();
    }

    MSG message{};
    while(message.message != WM_QUIT)
    {
        while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if(message.message != WM_QUIT)
            RenderFrame();
    }

    if(g_injectWorker.joinable())
    {
        g_injectWorker.request_stop();
        g_injectWorker.join();
    }
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyGraphics();
    UnregisterClassW(windowClass.lpszClassName, instance);
    CoUninitialize();
    return 0;
}
