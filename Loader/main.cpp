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
constexpr int WindowWidth = 720;
constexpr int WindowHeight = 490;

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
    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.FramePadding = {8.0f, 6.0f};
    style.ItemSpacing = {8.0f, 6.0f};
    style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.65f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.035f, 0.043f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.55f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.133f, 0.133f, 0.165f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.10f, 0.13f, 0.70f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.14f, 0.19f, 0.85f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.20f, 0.26f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.14f, 0.19f, 0.75f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.22f, 0.29f, 0.90f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.27f, 0.29f, 0.38f, 1.0f);
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

    // Version Badge: FREE or PRO
    const bool isPro = g_license.is_pro;
    const char* badgeText = isPro ? "PRO" : "FREE";
    const ImVec2 badgeSize(isPro ? 48.0f : 54.0f, 22.0f);
    const ImVec2 badgePos(origin.x + width - badgeSize.x - 46.0f, origin.y + 16.0f);
    const ImU32 badgeBg = isPro ? IM_COL32(165, 110, 255, 220) : IM_COL32(36, 38, 48, 190);
    const ImU32 badgeTextCol = isPro ? IM_COL32(15, 15, 20, 255) : IM_COL32(165, 170, 185, 255);
    draw->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), badgeBg, 11.0f);
    if(isPro)
        draw->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), IM_COL32(200, 160, 255, 255), 11.0f, 0, 1.0f);
    const ImVec2 bTextSz = ImGui::CalcTextSize(badgeText);
    draw->AddText(ImVec2(badgePos.x + (badgeSize.x - bTextSz.x) * 0.5f, badgePos.y + (badgeSize.y - bTextSz.y) * 0.5f),
        badgeTextCol, badgeText);

    // Close button
    const ImVec2 closePos(origin.x + width - 36.0f, origin.y + 15.0f);
    const ImVec2 closeSize(22.0f, 22.0f);
    ImGui::SetCursorScreenPos(closePos);
    if(ImGui::InvisibleButton("##close", closeSize))
        PostMessageW(g_window, WM_CLOSE, 0, 0);
    const bool closeHovered = ImGui::IsItemHovered();
    const ImU32 closeCol = closeHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 165, 180, 255);
    if(closeHovered)
        draw->AddRectFilled(closePos, ImVec2(closePos.x + closeSize.x, closePos.y + closeSize.y), IM_COL32(255, 255, 255, 25), 5.0f);
    draw->AddLine(ImVec2(closePos.x + 6.0f, closePos.y + 6.0f), ImVec2(closePos.x + 16.0f, closePos.y + 16.0f), closeCol, 1.8f);
    draw->AddLine(ImVec2(closePos.x + 16.0f, closePos.y + 6.0f), ImVec2(closePos.x + 6.0f, closePos.y + 16.0f), closeCol, 1.8f);
}

void DrawLicenseCard(ImDrawList* draw, ImVec2 origin, float width)
{
    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 56.0f);
    const ImVec2 cardSize(width, 138.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    // Semi-transparent glass card background & border
    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(18, 19, 26, 150), 8.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);

    // Section title
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 8.0f), IM_COL32(135, 140, 155, 255), "ЛИЦЕНЗИЯ И УСТРОЙСТВО");

    // 1. HWID Plashka
    const ImVec2 hwidPos(cardPos.x + 10.0f, cardPos.y + 26.0f);
    const ImVec2 hwidSize(cardSize.x - 20.0f, 32.0f);
    const ImVec2 hwidEnd(hwidPos.x + hwidSize.x, hwidPos.y + hwidSize.y);
    draw->AddRectFilled(hwidPos, hwidEnd, IM_COL32(24, 25, 34, 130), 6.0f);
    draw->AddRect(hwidPos, hwidEnd, IM_COL32(255, 255, 255, 22), 6.0f, 0, 1.0f);

    draw->AddText(ImVec2(hwidPos.x + 10.0f, hwidPos.y + 8.0f), IM_COL32(160, 165, 180, 255), "HWID:");
    draw->AddText(g_codeFont, 13.0f, ImVec2(hwidPos.x + 55.0f, hwidPos.y + 9.0f), IM_COL32(255, 255, 255, 255), g_license.hwid.c_str());

    // Copy HWID button
    const char* copyBtnText = (g_copiedTimer > 0.0f) ? "Скопировано!" : "Копировать";
    ImGui::SetCursorScreenPos(ImVec2(hwidEnd.x - 95.0f, hwidPos.y + 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 44, 58, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 66, 88, 240));
    if(ImGui::Button(copyBtnText, ImVec2(88.0f, 24.0f)))
    {
        ImGui::SetClipboardText(g_license.hwid.c_str());
        g_copiedTimer = 2.0f;
    }
    ImGui::PopStyleColor(2);

    // 2. Key Input & Activate button
    const float inputY = cardPos.y + 66.0f;
    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0f, inputY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(24, 25, 32, 150));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::SetNextItemWidth(cardSize.x - 116.0f);
    ImGui::InputTextWithHint("##lic_key", "Ключ активации ASHES-PRO-...", g_keyInput, sizeof(g_keyInput));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::SetCursorScreenPos(ImVec2(cardEnd.x - 98.0f, inputY));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(165, 110, 255, 210));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(185, 135, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(140, 90, 230, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(15, 15, 20, 255));
    if(ImGui::Button("Активация", ImVec2(88.0f, 28.0f)))
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
    ImGui::PopStyleColor(4);

    // 3. Status Line & TG Link
    const float statusY = cardPos.y + 104.0f;
    const ImU32 statCol = g_license.is_pro ? IM_COL32(140, 230, 160, 255) : IM_COL32(180, 185, 200, 255);
    draw->AddText(ImVec2(cardPos.x + 12.0f, statusY + 6.0f), statCol, g_license.status_msg.c_str());

    ImGui::SetCursorScreenPos(ImVec2(cardEnd.x - 118.0f, statusY));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(35, 38, 50, 180));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 55, 75, 220));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 140, 255, 255));
    if(ImGui::Button("PRO в TG ->", ImVec2(108.0f, 24.0f)))
    {
        ShellExecuteA(nullptr, "open", "https://t.me/ashes_rst_bot", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::PopStyleColor(3);
}

void DrawSettingsCard(ImDrawList* draw, ImVec2 origin, float width)
{
    const ImVec2 cardPos(origin.x + 16.0f, origin.y + 204.0f);
    const ImVec2 cardSize(width, 270.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(18, 19, 26, 150), 8.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 8.0f), IM_COL32(135, 140, 155, 255), "ПАРАМЕТРЫ СЕТИ И СПУФИНГА");

    auto drawRow = [&](float y, const char* label, const char* toggleId, bool* val)
    {
        const ImVec2 pPos(cardPos.x + 10.0f, y);
        const ImVec2 pSize(cardSize.x - 20.0f, 28.0f);
        const ImVec2 pEnd(pPos.x + pSize.x, pPos.y + pSize.y);
        draw->AddRectFilled(pPos, pEnd, IM_COL32(24, 25, 34, 130), 5.0f);
        draw->AddRect(pPos, pEnd, IM_COL32(255, 255, 255, 20), 5.0f, 0, 1.0f);
        draw->AddText(ImVec2(pPos.x + 10.0f, pPos.y + 6.0f), IM_COL32(220, 225, 235, 255), label);
        ImGui::SetCursorScreenPos(ImVec2(pEnd.x - 44.0f, pPos.y + 5.0f));
        if(CustomToggle(toggleId, val))
        {
            SaveConfig();
        }
    };

    drawRow(cardPos.y + 28.0f, "Anti-Shadow (Защита железа)", "##anti_shadow", &g_config.antiShadow);
    drawRow(cardPos.y + 60.0f, "Black Mirror (Спуфинг серийника)", "##black_mirror", &g_config.setSerial);
    drawRow(cardPos.y + 92.0f, "Random Serial (Случайный серийник)", "##random_serial", &g_config.randomSerial);
    drawRow(cardPos.y + 124.0f, "Scripts Dumper (Дамп скриптов)", "##scripts_dumper", &g_config.scriptsDumper);

    const ImVec2 serPos(cardPos.x + 10.0f, cardPos.y + 158.0f);
    const ImVec2 serSize(cardSize.x - 20.0f, 60.0f);
    const ImVec2 serEnd(serPos.x + serSize.x, serPos.y + serSize.y);
    draw->AddRectFilled(serPos, serEnd, IM_COL32(24, 25, 32, 150), 6.0f);
    draw->AddRect(serPos, serEnd, IM_COL32(255, 255, 255, 20), 6.0f, 0, 1.0f);
    draw->AddText(ImVec2(serPos.x + 10.0f, serPos.y + 6.0f), IM_COL32(160, 165, 180, 255), "Серийник (Serial / 32 HEX):");

    static char s_serialBuf[36]{};
    static bool s_serialInit = false;
    if(!s_serialInit)
    {
        std::snprintf(s_serialBuf, sizeof(s_serialBuf), "%s", g_config.publicSerial.c_str());
        s_serialInit = true;
    }

    ImGui::SetCursorScreenPos(ImVec2(serPos.x + 8.0f, serPos.y + 28.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(16, 17, 22, 180));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
    ImGui::SetNextItemWidth(serSize.x - 16.0f);
    if(ImGui::InputText("##public_serial", s_serialBuf, sizeof(s_serialBuf),
        ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CallbackCharFilter, FilterSerial))
    {
        g_config.publicSerial = s_serialBuf;
        SaveConfig();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 242.0f), IM_COL32(140, 145, 160, 255),
        "Привязан к вашему аккаунту Province");
}

void DrawConsoleCard(ImDrawList* draw, ImVec2 origin, float width, const Launcher::ProcessInfo& proc)
{
    const ImVec2 cardPos(origin.x + 364.0f, origin.y + 56.0f);
    const ImVec2 cardSize(width, 418.0f);
    const ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

    draw->AddRectFilled(cardPos, cardEnd, IM_COL32(18, 19, 26, 150), 8.0f);
    draw->AddRect(cardPos, cardEnd, IM_COL32(255, 255, 255, 22), 8.0f, 0, 1.0f);
    draw->AddText(ImVec2(cardPos.x + 12.0f, cardPos.y + 8.0f), IM_COL32(135, 140, 155, 255), "ВЫВОД ЛОГОВ / СТАТУС");

    // 1. Process Status Plashka
    const ImVec2 statPos(cardPos.x + 10.0f, cardPos.y + 26.0f);
    const ImVec2 statSize(cardSize.x - 20.0f, 32.0f);
    const ImVec2 statEnd(statPos.x + statSize.x, statPos.y + statSize.y);
    draw->AddRectFilled(statPos, statEnd, IM_COL32(24, 25, 34, 130), 6.0f);
    draw->AddRect(statPos, statEnd, IM_COL32(255, 255, 255, 22), 6.0f, 0, 1.0f);

    const float time = static_cast<float>(ImGui::GetTime());
    const ImVec2 dotCenter(statPos.x + 16.0f, statPos.y + 16.0f);

    if(g_injected)
    {
        draw->AddCircleFilled(dotCenter, 4.5f, IM_COL32(80, 220, 255, 255));
        draw->AddText(ImVec2(statPos.x + 28.0f, statPos.y + 7.0f), IM_COL32(240, 245, 255, 255), "ASHES внедрен и активен!");
    }
    else if(proc.found)
    {
        draw->AddCircleFilled(dotCenter, 4.5f, IM_COL32(80, 230, 120, 255));
        char procBuf[64]{};
        std::snprintf(procBuf, sizeof(procBuf), "Игра найдена (PID: %lu) - Готов", proc.pid);
        draw->AddText(ImVec2(statPos.x + 28.0f, statPos.y + 7.0f), IM_COL32(140, 240, 160, 255), procBuf);
    }
    else
    {
        const float pulse = (std::sin(time * 3.5f) + 1.0f) * 0.5f;
        const int dotAlpha = static_cast<int>(120 + pulse * 135);
        draw->AddCircleFilled(dotCenter, 4.5f, IM_COL32(230, 180, 70, dotAlpha));
        draw->AddText(ImVec2(statPos.x + 28.0f, statPos.y + 7.0f), IM_COL32(180, 185, 200, 255), "Ожидание запуска игры...");
    }

    // 2. Console Memo Log Output
    SyncMemo();
    const ImVec2 memoPos(cardPos.x + 10.0f, cardPos.y + 64.0f);
    const ImVec2 memoSize(cardSize.x - 20.0f, 240.0f);
    ImGui::SetCursorScreenPos(memoPos);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(12, 13, 17, 130));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 18));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 210, 225, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::InputTextMultiline("##console_memo", g_memo.data(), g_memo.size(),
        memoSize, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // 3. Auto-inject Toggle Plashka
    const ImVec2 tglPos(cardPos.x + 10.0f, cardPos.y + 312.0f);
    const ImVec2 tglSize(cardSize.x - 20.0f, 32.0f);
    const ImVec2 tglEnd(tglPos.x + tglSize.x, tglPos.y + tglSize.y);
    draw->AddRectFilled(tglPos, tglEnd, IM_COL32(24, 25, 32, 150), 6.0f);
    draw->AddRect(tglPos, tglEnd, IM_COL32(255, 255, 255, 22), 6.0f, 0, 1.0f);

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

    // 4. Big Action Button: ЗАПУСТИТЬ DARKFLAME (INJECT)
    const ImVec2 btnPos(cardPos.x + 10.0f, cardPos.y + 352.0f);
    const ImVec2 btnSize(cardSize.x - 20.0f, 44.0f);
    ImGui::SetCursorScreenPos(btnPos);

    const bool canInject = !g_injected && !g_isInjecting.load();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, canInject ? IM_COL32(165, 110, 255, 225) : IM_COL32(40, 42, 54, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, canInject ? IM_COL32(185, 135, 255, 255) : IM_COL32(40, 42, 54, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, canInject ? IM_COL32(140, 90, 230, 255) : IM_COL32(40, 42, 54, 200));
    ImGui::PushStyleColor(ImGuiCol_Text, canInject ? IM_COL32(15, 15, 20, 255) : IM_COL32(130, 135, 150, 255));

    const char* btnLabel = g_injected ? "ASHES УСПЕШНО ЗАПУЩЕН"
        : (g_isInjecting.load() ? "ИНЖЕКТИРОВАНИЕ..." : "ЗАПУСТИТЬ ASHES (INJECT)");

    if(ImGui::Button(btnLabel, btnSize) && canInject)
    {
        TriggerInject(proc.pid);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    // 5. Subtle log/status line below button
    if(!g_injectStatusMsg.empty())
    {
        const ImVec2 msgSize = ImGui::CalcTextSize(g_injectStatusMsg.c_str());
        draw->AddText(ImVec2(cardPos.x + (cardSize.x - msgSize.x) * 0.5f, cardPos.y + 400.0f),
            g_injected ? IM_COL32(120, 230, 160, 255) : IM_COL32(230, 150, 150, 255),
            g_injectStatusMsg.c_str());
    }
}

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
    ImGui::Begin("##dark_flame_loader", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    const ImVec2 origin = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 1. Deep matte black background (#09090B)
    draw->AddRectFilled(origin, ImVec2(origin.x + display.x, origin.y + display.y),
        IM_COL32(9, 9, 11, 255), 12.0f);

    // 2. Animated white waves (shine through semi-transparent cards)
    DrawAnimatedWaves(draw, origin, display);

    // 3. Thin 1px window border (#22222A)
    draw->AddRect(origin, ImVec2(origin.x + display.x, origin.y + display.y),
        IM_COL32(34, 34, 42, 255), 12.0f, 0, 1.0f);

    // 4. Header (DARKFLAME by restleSS + FREE/PRO badge + close button)
    DrawHeader(draw, origin, display.x);

    // 5. Left column: Лицензия и Параметры
    DrawLicenseCard(draw, origin, 336.0f);
    DrawSettingsCard(draw, origin, 336.0f);

    // 6. Right column: Вывод логов и статус
    DrawConsoleCard(draw, origin, 340.0f, s_procInfo);

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
        if(point.y < 50 && point.x < client.right - 45)
            return HTCAPTION;
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
