#include "gui.h"
#include "license_manager.h"

#include "input_utils.h"
#include "logger.h"
#include "lua_bridge.h"
#include "pilot_telemetry.h"
#include "netc_hooks.h"
#include "plow_bot.h"
#include "resource.h"
#include "ui_widgets.h"
#include "../Shared/ashes_logo.h"

#include <MinHook.h>
#define DIRECTINPUT_VERSION 0x0800
#include <d3d9.h>
#include <dinput.h>
#include <wincodec.h>
#include <shellapi.h>

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_dx9.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"
#include "third_party/ImGuiColorTextEdit/TextEditor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window,
    UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
constexpr float CanvasWidth = 1536.0f;
constexpr float CanvasHeight = 1024.0f;
constexpr GUID DirectInput8WGuid = {0xBF798031, 0x483A, 0x4DA2,
    {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID SystemMouseGuid = {0x6F1D2B60, 0xD5A0, 0x11CF,
    {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using PresentFn = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*,
    const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using Direct3DCreate9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
using CreateDeviceFn = HRESULT(__stdcall*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
    DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using CreateDeviceExFn = HRESULT(__stdcall*)(IDirect3D9Ex*, UINT, D3DDEVTYPE,
    HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using PresentExFn = HRESULT(__stdcall*)(IDirect3DDevice9Ex*, const RECT*,
    const RECT*, HWND, const RGNDATA*, DWORD);
using ResetExFn = HRESULT(__stdcall*)(IDirect3DDevice9Ex*,
    D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID,
    void**, IUnknown*);
using CreateInputDeviceFn = HRESULT(__stdcall*)(IDirectInput8W*, REFGUID,
    IDirectInputDevice8W**, IUnknown*);
using GetDeviceStateFn = HRESULT(__stdcall*)(IDirectInputDevice8W*, DWORD, void*);
using GetDeviceDataFn = HRESULT(__stdcall*)(IDirectInputDevice8W*, DWORD,
    DIDEVICEOBJECTDATA*, DWORD*, DWORD);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);

HMODULE g_module{};
PresentFn g_present{};
ResetFn g_reset{};
Direct3DCreate9Fn g_create9{};
Direct3DCreate9ExFn g_create9Ex{};
CreateDeviceFn g_createDevice{};
CreateDeviceExFn g_createDeviceEx{};
PresentExFn g_presentEx{};
ResetExFn g_resetEx{};
DirectInput8CreateFn g_directInput8Create{};
CreateInputDeviceFn g_createInputDevice{};
GetDeviceStateFn g_getDeviceState{};
GetDeviceDataFn g_getDeviceData{};
SetCursorPosFn g_setCursorPos{};
ClipCursorFn g_clipCursor{};
HWND g_window{};
WNDPROC g_oldWndProc{};
IDirect3DTexture9* g_background{};
IDirect3DTexture9* g_banner{};
ImFont* g_titleFont{};
ImFont* g_tabFont{};
ImFont* g_buttonFont{};
ImFont* g_codeFont{};
std::unique_ptr<TextEditor> g_editor;
std::array<char, 65536> g_events{};
std::mutex g_bridgeMutex;
std::string g_pendingLua;
std::string g_pendingResource;
std::string g_eventText;
std::vector<GuiLuaThread> g_threads;
std::vector<GuiLuaThread> g_visibleThreads;
inline bool IsValidUtf8(std::string_view str) {
    int c, i, ix, n, j;
    for (i = 0, ix = (int)str.length(); i < ix; i++) {
        c = (unsigned char)str[i];
        if (0x00 <= c && c <= 0x7f) n = 0;
        else if ((c & 0xE0) == 0xC0) n = 1;
        else if (c == 0xed && i < (ix - 1) && ((unsigned char)str[i + 1] & 0xa0) == 0xa0) return false;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0) n = 3;
        else return false;
        for (j = 0; j < n && i < ix; j++) {
            if ((++i == ix) || (((unsigned char)str[i] & 0xC0) != 0x80)) return false;
        }
    }
    return true;
}

inline std::string EnsureUtf8(std::string_view str) {
    if (str.empty() || IsValidUtf8(str)) return std::string(str);
    int wlen = MultiByteToWideChar(1251, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (wlen <= 0) return std::string(str);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(1251, 0, str.data(), static_cast<int>(str.size()), &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return std::string(str);
    std::string ustr(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &ustr[0], ulen, nullptr, nullptr);
    if (!ustr.empty() && ustr.back() == '\0') ustr.pop_back();
    return ustr;
}

struct TramState
{
    bool loaded{};
    bool bot{};
    bool traffic{};
    bool debug{};
    bool siren{true};
    bool force{};
    int subMode{0}; // 0 = Городской, 1 = ЖТУ, 2 = Поливалка (трамвай)
    int targetCycles{1};
    int completedCycles{0};
    int trampCompleted{0};
    int trampTarget{1};
    int trampBrake{3};
    bool trampTraffic{false};
    bool trampSprinkler{false};
    bool trampAutoDistance{true};
    int trampDistanceMode{2};
    std::string route{"не выбран"};
    std::string direction{"forward"};
    std::string status{"ожидание ресурса"};
    std::string money{"0"};
    std::string catcher{"не зарегистрирован"};
    bool autoDistance{true};
    int distanceMode{2}; // 1 = 1 вагон (~18м), 2 = 2 вагона (~36м), 3 = 3 вагона (~54м)
    int autoStopMinutes{180}; // 180 мин (3 ч), 0 = без ограничений
};
TramState g_tramState;
TramState g_visibleTramState;
std::deque<std::string> g_tramCommands;
struct JbkState
{
    bool loaded{};
    bool bot{};
    bool bhop{true};
    bool autoDisable{true};
    bool siren{true};
    bool stopOnAdmin{true};
    bool strict{};
    bool adminHit{};
    bool walk{};
    int rotationSpeed{5};
    int bhopDelay{1000};
    float bhopDistance{30.0f};
    float maxZ{395.0f};
    float maxJump{30.0f};
    int resumeMinutes{3};
    float stuckDistance{1.5f};
    int stuckTime{4000};
    int stuckMax{2};
    float hitRadius{8.0f};
    int hitCooldown{30};
    int walkInterval{1200};
    int walkDuration{200};
    float walkDistance{15.0f};
    std::string status{"ожидание ресурса"};
    std::string money{"0"};
};
JbkState g_jbkState;
JbkState g_visibleJbkState;
std::deque<std::string> g_jbkCommands;
struct TobaccoState
{
    bool loaded{};
    bool bot{};
    bool sprintToStorage{false};
    bool siren{false};
    bool autoDisable{false};
    bool autoAnswer{false};
    int rotationSpeed{6};
    int answerDelay{50};
    std::string status{"ожидание запуска"};
    std::string money{"0"};
};
TobaccoState g_tobaccoState;
TobaccoState g_visibleTobaccoState;
std::deque<std::string> g_tobaccoCommands;
char g_targetResource[128]{"province_ac"};
std::uintptr_t g_selectedThread{};
std::uintptr_t g_pendingUnload{};
bool g_luaPending{};
bool g_eventsDirty{};
bool g_threadsDirty{};
bool g_tramDirty{true};
bool g_jbkDirty{true};
bool g_tobaccoDirty{true};
std::atomic_bool g_started{};
std::atomic_bool g_visible{};
std::atomic_long g_factoryState{};
std::atomic_long g_factoryExState{};
std::atomic_long g_deviceState{};
std::atomic_long g_deviceExState{};
std::atomic_long g_inputFactoryState{};
std::atomic_long g_inputDeviceState{};
bool g_initialized{};
int g_activeTab{};
int g_trafficState{3};

ImTextureRef Texture(IDirect3DTexture9* texture)
{
    return ImTextureRef(reinterpret_cast<void*>(texture));
}

bool InputMessage(UINT message)
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
        || (message >= WM_KEYFIRST && message <= WM_KEYLAST)
        || message == WM_INPUT;
}

LRESULT CALLBACK GuiWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if(g_visible.load() && ImGui::GetCurrentContext())
    {
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
        if(InputMessage(message))
            return 1;
    }
    return g_oldWndProc
        ? CallWindowProcW(g_oldWndProc, window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

BOOL WINAPI HookSetCursorPos(int x, int y)
{
    return g_visible.load() ? TRUE : g_setCursorPos(x, y);
}

BOOL WINAPI HookClipCursor(const RECT* rectangle)
{
    return g_visible.load() ? g_clipCursor(nullptr) : g_clipCursor(rectangle);
}

HRESULT __stdcall HookGetDeviceState(IDirectInputDevice8W* device,
    DWORD size, void* data)
{
    const HRESULT result = g_getDeviceState(device, size, data);
    if(g_visible.load() && SUCCEEDED(result) && data && size)
        std::memset(data, 0, size);
    return result;
}

HRESULT __stdcall HookGetDeviceData(IDirectInputDevice8W* device,
    DWORD objectSize, DIDEVICEOBJECTDATA* data, DWORD* count, DWORD flags)
{
    const HRESULT result = g_getDeviceData(device, objectSize, data, count, flags);
    if(g_visible.load() && SUCCEEDED(result) && count)
        *count = 0;
    return result;
}

bool InstallInputDeviceHooks(IDirectInputDevice8W* device)
{
    long expected{};
    if(!device || !g_inputDeviceState.compare_exchange_strong(expected, 1))
        return expected == 2;
    auto** table = *reinterpret_cast<void***>(device);
    void* stateTarget = table[9];
    void* dataTarget = table[10];
    const MH_STATUS stateCreated = MH_CreateHook(stateTarget,
        reinterpret_cast<void*>(&HookGetDeviceState),
        reinterpret_cast<void**>(&g_getDeviceState));
    const MH_STATUS dataCreated = MH_CreateHook(dataTarget,
        reinterpret_cast<void*>(&HookGetDeviceData),
        reinterpret_cast<void**>(&g_getDeviceData));
    const bool ready = stateCreated == MH_OK && dataCreated == MH_OK
        && MH_EnableHook(stateTarget) == MH_OK
        && MH_EnableHook(dataTarget) == MH_OK;
    if(!ready)
    {
        if(stateCreated == MH_OK)
        {
            MH_DisableHook(stateTarget);
            MH_RemoveHook(stateTarget);
        }
        if(dataCreated == MH_OK)
        {
            MH_DisableHook(dataTarget);
            MH_RemoveHook(dataTarget);
        }
        g_getDeviceState = nullptr;
        g_getDeviceData = nullptr;
    }
    g_inputDeviceState.store(ready ? 2 : 0);
    return ready;
}

HRESULT __stdcall HookCreateInputDevice(IDirectInput8W* api, REFGUID guid,
    IDirectInputDevice8W** output, IUnknown* outer)
{
    const HRESULT result = g_createInputDevice(api, guid, output, outer);
    if(SUCCEEDED(result) && output && *output)
        InstallInputDeviceHooks(*output);
    return result;
}

bool InstallInputFactoryHook(IDirectInput8W* api)
{
    long expected{};
    if(!api || !g_inputFactoryState.compare_exchange_strong(expected, 1))
        return expected == 2;
    auto** table = *reinterpret_cast<void***>(api);
    void* target = table[3];
    const MH_STATUS created = MH_CreateHook(target,
        reinterpret_cast<void*>(&HookCreateInputDevice),
        reinterpret_cast<void**>(&g_createInputDevice));
    const bool ready = created == MH_OK && MH_EnableHook(target) == MH_OK;
    if(!ready)
    {
        if(created == MH_OK)
        {
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
        g_createInputDevice = nullptr;
    }
    g_inputFactoryState.store(ready ? 2 : 0);
    return ready;
}

HRESULT WINAPI HookDirectInput8Create(HINSTANCE instance, DWORD version,
    REFIID iid, void** output, IUnknown* outer)
{
    const HRESULT result = g_directInput8Create(instance, version, iid, output, outer);
    if(SUCCEEDED(result) && output && *output)
        InstallInputFactoryHook(static_cast<IDirectInput8W*>(*output));
    return result;
}

void InstallGameInputHooks()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void* setCursorPos = user32 ? reinterpret_cast<void*>(
        GetProcAddress(user32, "SetCursorPos")) : nullptr;
    void* clipCursor = user32 ? reinterpret_cast<void*>(
        GetProcAddress(user32, "ClipCursor")) : nullptr;
    const bool cursorReady = setCursorPos && clipCursor
        && MH_CreateHook(setCursorPos, reinterpret_cast<void*>(&HookSetCursorPos),
            reinterpret_cast<void**>(&g_setCursorPos)) == MH_OK
        && MH_CreateHook(clipCursor, reinterpret_cast<void*>(&HookClipCursor),
            reinterpret_cast<void**>(&g_clipCursor)) == MH_OK
        && MH_EnableHook(setCursorPos) == MH_OK
        && MH_EnableHook(clipCursor) == MH_OK;
    Log::Write(cursorReady
        ? L"[gui] cursor recenter blocking installed"
        : L"[gui] cursor recenter blocking failed");
}

bool LoadTexture(IDirect3DDevice9* device, int resourceId,
    IDirect3DTexture9** output)
{
    if(!device || !output)
        return false;
    HRSRC resource = FindResourceW(g_module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(g_module, resource) : nullptr;
    const DWORD resourceSize = resource ? SizeofResource(g_module, resource) : 0;
    auto* bytes = loaded ? static_cast<BYTE*>(LockResource(loaded)) : nullptr;
    if(!bytes || !resourceSize)
        return false;

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory{};
    IWICStream* stream{};
    IWICBitmapDecoder* decoder{};
    IWICBitmapFrameDecode* frame{};
    IWICFormatConverter* converter{};
    IDirect3DTexture9* texture{};
    bool success{};

    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->CreateStream(&stream))
        && SUCCEEDED(stream->InitializeFromMemory(bytes, resourceSize))
        && SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnLoad, &decoder))
        && SUCCEEDED(decoder->GetFrame(0, &frame))
        && SUCCEEDED(factory->CreateFormatConverter(&converter))
        && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT width{}, height{};
        if(SUCCEEDED(converter->GetSize(&width, &height))
            && SUCCEEDED(device->CreateTexture(width, height, 1, 0,
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)))
        {
            D3DLOCKED_RECT locked{};
            if(SUCCEEDED(texture->LockRect(0, &locked, nullptr, 0)))
            {
                success = SUCCEEDED(converter->CopyPixels(nullptr,
                    static_cast<UINT>(locked.Pitch),
                    static_cast<UINT>(locked.Pitch) * height,
                    static_cast<BYTE*>(locked.pBits)));
                texture->UnlockRect(0);
            }
        }
    }

    if(converter) converter->Release();
    if(frame) frame->Release();
    if(decoder) decoder->Release();
    if(stream) stream->Release();
    if(factory) factory->Release();
    if(comResult == S_OK || comResult == S_FALSE)
        CoUninitialize();
    if(!success && texture)
    {
        texture->Release();
        texture = nullptr;
    }
    *output = texture;
    return success;
}

void ConfigureEditor()
{
    g_editor = std::make_unique<TextEditor>();
    g_editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    g_editor->SetTabSize(4);
    auto palette = TextEditor::GetDarkPalette();
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Default)] = IM_COL32(230, 230, 230, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Keyword)] = IM_COL32(200, 200, 200, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Number)] = IM_COL32(160, 190, 215, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::String)] = IM_COL32(195, 195, 140, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::CharLiteral)] = IM_COL32(195, 195, 140, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Punctuation)] = IM_COL32(180, 180, 180, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Identifier)] = IM_COL32(220, 220, 220, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::KnownIdentifier)] = IM_COL32(150, 190, 225, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Comment)] = IM_COL32(110, 110, 110, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::MultiLineComment)] = IM_COL32(110, 110, 110, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Background)] = IM_COL32(12, 12, 12, 240);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Cursor)] = IM_COL32(245, 245, 245, 255);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::Selection)] = IM_COL32(65, 65, 65, 170);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::LineNumber)] = IM_COL32(110, 110, 110, 230);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::CurrentLineFill)] = IM_COL32(25, 25, 25, 190);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::CurrentLineFillInactive)] = IM_COL32(18, 18, 18, 160);
    palette[static_cast<unsigned>(TextEditor::PaletteIndex::CurrentLineEdge)] = IM_COL32(60, 60, 60, 200);
    g_editor->SetPalette(palette);
    g_editor->SetText(
        "--[[\n"
        "  Dark Flame Lua Injector\n"
        "  Write or paste your Lua script below.\n"
        "]]--\n\n"
        "-- Example\n"
        "local player = localPlayer\n"
        "outputChatBox(\"Dark Flame is active.\")\n\n"
        "-- Your script here...\n");
}

void ConfigureFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const ImWchar* glyphs = io.Fonts->GetGlyphRangesCyrillic();
    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 14.0f, nullptr, glyphs);
    if(!mainFont)
        mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 14.0f, nullptr, glyphs);
    if(!mainFont)
        mainFont = io.Fonts->AddFontDefault();
    io.FontDefault = mainFont;

    g_titleFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f, nullptr, glyphs);
    if(!g_titleFont)
        g_titleFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 16.0f, nullptr, glyphs);

    g_tabFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 14.0f, nullptr, glyphs);
    if(!g_tabFont)
        g_tabFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 14.0f, nullptr, glyphs);

    g_buttonFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 15.0f, nullptr, glyphs);
    if(!g_buttonFont)
        g_buttonFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arialbd.ttf", 15.0f, nullptr, glyphs);

    g_codeFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\consola.ttf", 13.0f, nullptr, glyphs);
    if(!g_codeFont)
        g_codeFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\cour.ttf", 13.0f, nullptr, glyphs);

    if(!g_titleFont) g_titleFont = mainFont ? mainFont : io.Fonts->AddFontDefault();
    if(!g_tabFont) g_tabFont = mainFont ? mainFont : io.Fonts->AddFontDefault();
    if(!g_buttonFont) g_buttonFont = mainFont ? mainFont : io.Fonts->AddFontDefault();
    if(!g_codeFont) g_codeFont = mainFont ? mainFont : io.Fonts->AddFontDefault();
}

void ConfigureStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 1.0f;
    style.WindowRounding = 10.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.ScrollbarSize = 8.0f;
    style.ItemSpacing = ImVec2(6.0f, 6.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.09f, 0.12f, 0.96f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.04f, 0.06f, 0.6f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.30f, 0.8f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.60f, 0.60f, 0.70f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
}

bool InitializeGui(IDirect3DDevice9* device)
{
    if(!device)
        return false;
    D3DDEVICE_CREATION_PARAMETERS parameters{};
    if(FAILED(device->GetCreationParameters(&parameters)) || !parameters.hFocusWindow)
        return false;
    HWND targetWindow = parameters.hFocusWindow;
    if(!IsWindow(targetWindow))
        return false;

    g_window = targetWindow;
    InputUtil::SetGameWindow(g_window);
    LoadTexture(device, IDR_DARK_FLAME_BACKGROUND, &g_background);
    LoadTexture(device, IDR_DARK_FLAME_BANNER, &g_banner);
    const auto releaseTextures = []
    {
        if(g_background) g_background->Release();
        if(g_banner) g_banner->Release();
        g_background = nullptr;
        g_banner = nullptr;
    };
    IMGUI_CHECKVERSION();
    if(!ImGui::GetCurrentContext())
        ImGui::CreateContext();
    ConfigureStyle();
    ConfigureFonts();
    ConfigureEditor();
    if(!ImGui_ImplWin32_Init(g_window))
    {
        ImGui::DestroyContext();
        releaseTextures();
        return false;
    }
    if(!ImGui_ImplDX9_Init(device))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        releaseTextures();
        return false;
    }
    g_oldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_window,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GuiWndProc)));
    if(!g_oldWndProc)
    {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        releaseTextures();
        return false;
    }
    g_initialized = true;
    Log::Write(L"[gui] ImGui DX9 menu ready; toggle with Shift+R");
    return true;
}

void DrawFlame(ImDrawList* draw, ImVec2 center, float size)
{
    const ImU32 glow = IM_COL32(140, 140, 140, 80);
    const ImU32 purple = IM_COL32(200, 200, 200, 255);
    const ImU32 white = IM_COL32(245, 245, 245, 255);
    draw->AddCircleFilled(center, size * 0.55f, glow, 24);
    const ImVec2 outer[] = {
        {center.x, center.y - size},
        {center.x + size * 0.62f, center.y - size * 0.08f},
        {center.x + size * 0.35f, center.y + size * 0.7f},
        {center.x, center.y + size},
        {center.x - size * 0.48f, center.y + size * 0.45f},
        {center.x - size * 0.6f, center.y - size * 0.16f}};
    draw->AddConvexPolyFilled(outer, 6, purple);
    const ImVec2 inner[] = {
        {center.x + size * 0.12f, center.y - size * 0.42f},
        {center.x + size * 0.3f, center.y + size * 0.18f},
        {center.x, center.y + size * 0.55f},
        {center.x - size * 0.18f, center.y + size * 0.08f}};
    draw->AddConvexPolyFilled(inner, 4, white);
}

void DrawCodeIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color)
{
    draw->AddLine({center.x - size * 0.15f, center.y - size * 0.65f},
        {center.x - size * 0.75f, center.y}, color, 2.2f);
    draw->AddLine({center.x - size * 0.75f, center.y},
        {center.x - size * 0.15f, center.y + size * 0.65f}, color, 2.2f);
    draw->AddLine({center.x + size * 0.15f, center.y - size * 0.65f},
        {center.x + size * 0.75f, center.y}, color, 2.2f);
    draw->AddLine({center.x + size * 0.75f, center.y},
        {center.x + size * 0.15f, center.y + size * 0.65f}, color, 2.2f);
    draw->AddLine({center.x + size * 0.18f, center.y - size * 0.88f},
        {center.x - size * 0.18f, center.y + size * 0.88f}, color, 2.0f);
}

void DrawTab(const char* id, const char* label, int tab, ImVec2 position,
    ImVec2 size, bool active, ImDrawList* draw, float scale)
{
    ImGui::SetCursorScreenPos(position);
    if(ImGui::InvisibleButton(id, size))
        g_activeTab = tab;
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 color = active ? IM_COL32(255, 255, 255, 255)
        : hovered ? IM_COL32(215, 215, 215, 255) : IM_COL32(140, 140, 140, 255);
    if(hovered || active)
    {
        draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
            active ? IM_COL32(38, 38, 38, 200) : IM_COL32(28, 28, 28, 160), 6.0f * scale);
    }
    if(active)
    {
        draw->AddRectFilled({position.x, position.y + size.y - 3.0f * scale},
            {position.x + size.x, position.y + size.y}, IM_COL32(255, 255, 255, 255));
    }
    const float fontSize = g_tabFont->LegacySize * scale;
    const ImVec2 textSize = g_tabFont->CalcTextSizeA(fontSize,
        FLT_MAX, 0.0f, label);
    draw->AddText(g_tabFont, fontSize,
        {position.x + (size.x - textSize.x) * 0.5f,
        position.y + (size.y - textSize.y) * 0.48f}, color, label);
}

void DrawVerticalTab(const char* id, const char* label, int tab, ImVec2 position,
    ImVec2 size, bool active, ImDrawList* draw, float scale)
{
    ImGui::SetCursorScreenPos(position);
    if(ImGui::InvisibleButton(id, size))
        g_activeTab = tab;
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 color = active ? IM_COL32(255, 255, 255, 255)
        : hovered ? IM_COL32(215, 215, 215, 255) : IM_COL32(140, 140, 140, 255);
    if(hovered || active)
    {
        draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
            active ? IM_COL32(40, 40, 40, 220) : IM_COL32(26, 26, 26, 170), 6.0f * scale);
    }
    if(active)
    {
        draw->AddRectFilled({position.x, position.y},
            {position.x + 4.0f * scale, position.y + size.y}, IM_COL32(255, 255, 255, 255));
    }
    const float fontSize = g_tabFont->LegacySize * scale;
    const ImVec2 textSize = g_tabFont->CalcTextSizeA(fontSize,
        FLT_MAX, 0.0f, label);
    draw->AddText(g_tabFont, fontSize,
        {position.x + (size.x - textSize.x) * 0.5f,
        position.y + (size.y - textSize.y) * 0.48f}, color, label);
}

bool DrawActionButton(const char* id, const char* label, ImVec2 position,
    ImVec2 size, ImDrawList* draw, float scale)
{
    ImGui::SetCursorScreenPos(position);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 glow = hovered ? IM_COL32(255, 255, 255, 60)
        : IM_COL32(180, 180, 180, 25);
    const ImU32 border = hovered ? IM_COL32(255, 255, 255, 255)
        : IM_COL32(140, 140, 140, 220);
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
        IM_COL32(18, 18, 18, 245), 14.0f * scale);
    draw->AddRect(position, {position.x + size.x, position.y + size.y}, glow,
        14.0f * scale, 0, 10.0f * scale);
    draw->AddRect({position.x + 5.0f * scale, position.y + 5.0f * scale},
        {position.x + size.x - 5.0f * scale,
        position.y + size.y - 5.0f * scale}, border,
        12.0f * scale, 0, 3.0f * scale);
    const float fontSize = g_buttonFont->LegacySize * scale;
    const ImVec2 textSize = g_buttonFont->CalcTextSizeA(fontSize,
        FLT_MAX, 0.0f, label);
    draw->AddText(g_buttonFont, fontSize,
        {position.x + (size.x - textSize.x) * 0.5f,
        position.y + (size.y - textSize.y) * 0.43f},
        IM_COL32(250, 250, 250, 255), label);
    for(int side : {-1, 1})
    {
        const float base = side < 0 ? position.x + 72.0f * scale
            : position.x + size.x - 72.0f * scale;
        for(int index = 0; index < 3; ++index)
        {
            const float offset = index * 14.0f * scale * side;
            draw->AddLine({base + offset - 9.0f * scale * side,
                position.y + size.y * 0.36f},
                {base + offset, position.y + size.y * 0.5f}, border, 3.0f * scale);
            draw->AddLine({base + offset, position.y + size.y * 0.5f},
                {base + offset - 9.0f * scale * side,
                position.y + size.y * 0.64f}, border, 3.0f * scale);
        }
    }
    return clicked;
}

void QueueEditorCode()
{
    if(!g_editor)
        return;
    std::scoped_lock lock(g_bridgeMutex);
    g_pendingLua = g_editor->GetText();
    g_pendingResource = g_targetResource[0] ? g_targetResource : "province_ac";
    g_luaPending = true;
}

void SyncEvents()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_eventsDirty)
        return;
    const std::size_t length = std::min(g_eventText.size(), g_events.size() - 1);
    std::memcpy(g_events.data(), g_eventText.data(), length);
    g_events[length] = '\0';
    g_eventsDirty = false;
}

void SyncThreads()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_threadsDirty)
        return;
    g_visibleThreads = g_threads;
    g_threadsDirty = false;
    if(g_selectedThread && std::none_of(g_visibleThreads.begin(),
        g_visibleThreads.end(), [](const GuiLuaThread& thread)
        {
            return thread.id == g_selectedThread;
        }))
    {
        g_selectedThread = 0;
    }
}

void QueueTramCommand(std::string command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_tramCommands.size() >= 64)
        g_tramCommands.pop_front();
    g_tramCommands.push_back(std::move(command));
}

void QueueJbkCommand(std::string command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_jbkCommands.size() >= 64)
        g_jbkCommands.pop_front();
    g_jbkCommands.push_back(std::move(command));
}

void SyncTramState()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_tramDirty)
        return;
    const int preservedSubMode = g_visibleTramState.subMode;
    g_visibleTramState = g_tramState;
    g_visibleTramState.subMode = preservedSubMode;
    g_tramDirty = false;
}

void SyncJbkState()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_jbkDirty)
        return;
    g_visibleJbkState = g_jbkState;
    g_jbkDirty = false;
}

void QueueTobaccoCommand(std::string command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_tobaccoCommands.size() >= 64)
        g_tobaccoCommands.pop_front();
    g_tobaccoCommands.push_back(std::move(command));
}

void SyncTobaccoState()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_tobaccoDirty)
        return;
    g_visibleTobaccoState = g_tobaccoState;
    g_tobaccoDirty = false;
}

void TramButton(const char* label, const char* command)
{
    if(ImGui::Button(label))
        QueueTramCommand(command);
}

void TramToggle(const char* label, bool& value, const char* command)
{
    if(ImGui::Checkbox(label, &value))
        QueueTramCommand(std::string(command) + (value ? ":1" : ":0"));
}

void RenderProLockOverlay(ImVec2 origin, ImVec2 size, float scale);
void DrawLicenseTab(ImVec2 position, ImVec2 size, float scale);

namespace
{
std::wstring GetSettingsIniPath()
{
    wchar_t path[MAX_PATH]{};
    if(GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        std::wstring s = path;
        auto sep = s.find_last_of(L"\\/");
        if(sep != std::wstring::npos)
            return s.substr(0, sep + 1) + L"settings.ini";
    }
    return L"settings.ini";
}

void LoadTramSettings(int& autoStopMin, int& distMode, bool& autoDist, int& trampDistMode, bool& trampAutoDist)
{
    std::wstring ini = GetSettingsIniPath();
    autoStopMin = GetPrivateProfileIntW(L"TramBot", L"AutoStopMinutes", 180, ini.c_str());
    distMode = GetPrivateProfileIntW(L"TramBot", L"DistanceMode", 2, ini.c_str());
    autoDist = GetPrivateProfileIntW(L"TramBot", L"AutoDistance", 1, ini.c_str()) != 0;
    trampDistMode = GetPrivateProfileIntW(L"PolivTram", L"DistanceMode", 2, ini.c_str());
    trampAutoDist = GetPrivateProfileIntW(L"PolivTram", L"AutoDistance", 1, ini.c_str()) != 0;
}

void SaveTramSettings(int autoStopMin, int distMode, bool autoDist)
{
    std::wstring ini = GetSettingsIniPath();
    WritePrivateProfileStringW(L"TramBot", L"AutoStopMinutes", std::to_wstring(autoStopMin).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"DistanceMode", std::to_wstring(distMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"AutoDistance", autoDist ? L"1" : L"0", ini.c_str());
}

void SavePolivTramSettings(int distMode, bool autoDist)
{
    std::wstring ini = GetSettingsIniPath();
    WritePrivateProfileStringW(L"PolivTram", L"DistanceMode", std::to_wstring(distMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"PolivTram", L"AutoDistance", autoDist ? L"1" : L"0", ini.c_str());
}
}

void DrawTramBot(ImVec2 position, ImVec2 size, float scale)
{
    static int s_currentTramTab = 0;
    static bool s_settingsLoaded = false;
    SyncTramState();
    TramState& state = g_visibleTramState;
    if(!s_settingsLoaded)
    {
        LoadTramSettings(state.autoStopMinutes, state.distanceMode, state.autoDistance, state.trampDistanceMode, state.trampAutoDistance);
        g_tramState.autoStopMinutes = state.autoStopMinutes;
        g_tramState.distanceMode = state.distanceMode;
        g_tramState.autoDistance = state.autoDistance;
        g_tramState.trampDistanceMode = state.trampDistanceMode;
        g_tramState.trampAutoDistance = state.trampAutoDistance;
        s_settingsLoaded = true;
    }
    state.subMode = s_currentTramTab;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 28, 28, 245));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(48, 48, 48, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(75, 75, 75, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(40, 40, 40, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(65, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(245, 245, 245, 255));
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##tram_bot", size, ImGuiChildFlags_Borders);
    const bool isPro = IsProLicense();
    const char* botModes[] = {
        "Городской трамвай",
        isPro ? "ЖТУ" : "ЖТУ [PRO]",
        isPro ? "Поливалка" : "Поливалка [PRO]"
    };
    float segW = (ImGui::GetContentRegionAvail().x - 12.0f * scale) / 3.0f;
    for(int m = 0; m < 3; ++m)
    {
        if(m > 0) ImGui::SameLine(0, 6.0f * scale);
        const bool isAct = (s_currentTramTab == m);
        if(XoneButton(botModes[m], scale, isAct, segW, 26.0f))
        {
            s_currentTramTab = m;
            state.subMode = m;
            {
                std::scoped_lock lock(g_bridgeMutex);
                g_tramState.subMode = m;
                g_visibleTramState.subMode = m;
            }
            QueueTramCommand("submode:" + std::to_string(m));
        }
    }
    ImGui::Spacing();

    const bool proSubMode = (s_currentTramTab == 1 || s_currentTramTab == 2);
    if(proSubMode && !isPro)
    {
        RenderProLockOverlay(position, size, scale);
    }
    else if(s_currentTramTab == 0)
    {
        float availW = ImGui::GetContentRegionAvail().x;
        float colW = (availW - 14.0f * scale) * 0.5f;

        // Левая колонка — управление и маршруты
        ImGui::BeginChild("##tram_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
        if(XoneButton(state.bot ? "ОСТАНОВИТЬ БОТА" : "ЗАПУСТИТЬ БОТА", scale, true, -1.0f, 40.0f))
            QueueTramCommand(state.bot ? "bot:0" : "bot:1");

        if(XonePlashkaToggle("Игнорировать светофоры", &state.traffic, scale))
            QueueTramCommand(std::string("traffic:") + (state.traffic ? "1" : "0"));
        if(XonePlashkaToggle("Режим отладки", &state.debug, scale))
            QueueTramCommand(std::string("debug:") + (state.debug ? "1" : "0"));
        if(XonePlashkaToggle("Сирена (ДТП / админ)", &state.siren, scale))
            QueueTramCommand(std::string("siren:") + (state.siren ? "1" : "0"));
        if(XonePlashkaToggle("Остановка на пробел", &state.force, scale))
            QueueTramCommand(std::string("force:") + (state.force ? "1" : "0"));

        if(XonePlashkaToggle("Авто-дистанция (Коллизия)", &state.autoDistance, scale))
        {
            {
                std::scoped_lock lock(g_bridgeMutex);
                g_tramState.autoDistance = state.autoDistance;
            }
            QueueTramCommand(std::string("autodist:") + (state.autoDistance ? "1" : "0"));
            SaveTramSettings(state.autoStopMinutes, state.distanceMode, state.autoDistance);
        }

        static const char* s_distOptions[] = { "1 ваг. (~18м)", "2 ваг. (~36м)", "3 ваг. (~54м)" };
        int distIdx = std::clamp(state.distanceMode - 1, 0, 2);
        if(XonePlashkaChoice("Дистанция следования", &distIdx, s_distOptions, 3, scale))
        {
            state.distanceMode = distIdx + 1;
            {
                std::scoped_lock lock(g_bridgeMutex);
                g_tramState.distanceMode = state.distanceMode;
            }
            QueueTramCommand("distmode:" + std::to_string(state.distanceMode));
            SaveTramSettings(state.autoStopMinutes, state.distanceMode, state.autoDistance);
        }

        static const int s_autoStopValues[] = { 0, 15, 30, 45, 60, 90, 120, 150, 180, 240, 300, 360, 480, 600, 720 };
        static const char* s_autoStopLabels[] = {
            "Без ограничений", "15 мин", "30 мин", "45 мин", "1 час", "1.5 часа",
            "2 часа", "2.5 часа", "3 часа", "4 часа", "5 часов", "6 часов", "8 часов", "10 часов", "12 часов"
        };
        int autoStopIdx = 8;
        for(int i = 0; i < 15; ++i)
        {
            if(s_autoStopValues[i] == state.autoStopMinutes)
            {
                autoStopIdx = i;
                break;
            }
        }
        if(XonePlashkaChoice("Автостоп сессии", &autoStopIdx, s_autoStopLabels, 15, scale))
        {
            state.autoStopMinutes = s_autoStopValues[autoStopIdx];
            {
                std::scoped_lock lock(g_bridgeMutex);
                g_tramState.autoStopMinutes = state.autoStopMinutes;
            }
            QueueTramCommand("autostop_min:" + std::to_string(state.autoStopMinutes));
            SaveTramSettings(state.autoStopMinutes, state.distanceMode, state.autoDistance);
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "БЫСТРЫЙ ВЫБОР МАРШРУТА");
        float routeBtnW = (colW - 8.0f * scale) * 0.5f;
        if(XoneButton("ТП Мирка-3", scale, false, routeBtnW, 32.0f)) QueueTramCommand("route:mirka3");
        ImGui::SameLine();
        if(XoneButton("ТП Нева-100", scale, false, routeBtnW, 32.0f)) QueueTramCommand("route:neva100");
        if(XoneButton("ТП Прива-2", scale, false, routeBtnW, 32.0f)) QueueTramCommand("route:priva2");
        ImGui::SameLine();
        if(XoneButton("ТП Прива-7", scale, false, routeBtnW, 32.0f)) QueueTramCommand("route:priva7");
        if(XoneButton("ТП Мирка-8", scale, false, routeBtnW, 32.0f)) QueueTramCommand("route:mirka8");
        ImGui::SameLine();
        if(XoneButton("ТП Прива-8", scale, false, routeBtnW, 32.0f)) QueueTramCommand("route:priva8");

        ImGui::Spacing();
        if(XoneButton("Калибровщик", scale, false, -1.0f, 32.0f)) QueueTramCommand("calibrate");
        if(XoneButton("Тест сирены", scale, false, -1.0f, 32.0f)) QueueTramCommand("test_siren");
        if(XoneButton("Экстренный стоп", scale, false, -1.0f, 32.0f)) QueueTramCommand("force_stop");
        ImGui::EndChild();

        ImGui::SameLine();

        // Правая колонка — показатели и маркеры
        ImGui::BeginChild("##tram_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
        XonePlashkaValue("Маршрут", state.route.empty() ? "—" : state.route.c_str(), scale);
        XonePlashkaValue("Направление", state.direction.empty() ? "—" : state.direction.c_str(), scale);
        XonePlashkaValue("Состояние", state.status.empty() ? "Ожидание" : state.status.c_str(), scale,
            state.bot ? IM_COL32(80, 225, 120, 255) : IM_COL32(230, 80, 80, 255));
        XonePlashkaValue("Заработано", (state.money + " руб.").c_str(), scale, IM_COL32(250, 210, 80, 255));
        XonePlashkaValue("Catcher", state.catcher.c_str(), scale);

        ImGui::Spacing();
        XonePlashkaInt("Светофор (0-9)", &g_trafficState, 0, 9, scale);
        if(XoneButton("Сохранить светофор", scale, false, -1.0f, 32.0f))
            QueueTramCommand("save_traffic:" + std::to_string(g_trafficState));
        if(XoneButton("Сохранить участок", scale, false, -1.0f, 32.0f))
            QueueTramCommand("save_marker");
        if(XoneButton("Тест светофора", scale, false, -1.0f, 32.0f))
            QueueTramCommand("test_traffic");
        if(XoneButton("Очистить таблицу", scale, false, -1.0f, 32.0f))
            QueueTramCommand("clear_route");
        if(XoneButton("Дамп таблицы", scale, false, -1.0f, 32.0f))
            QueueTramCommand("dump_route");
        ImGui::EndChild();
    }
    else if(s_currentTramTab == 1)
    {
        float availW = ImGui::GetContentRegionAvail().x;
        float colW = (availW - 14.0f * scale) * 0.5f;

        // Левая колонка — управление ЖТУ
        ImGui::BeginChild("##jtu_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
        if(XoneButton(state.bot ? "ОСТАНОВИТЬ ЖТУ" : "ЗАПУСТИТЬ ЖТУ", scale, true, -1.0f, 40.0f))
        {
            if(!state.bot)
            {
                QueueTramCommand("jtu_cycles:" + std::to_string(state.targetCycles));
                QueueTramCommand(std::string("siren:") + (state.siren ? "1" : "0"));
                QueueTramCommand("jtu_start");
            }
            else
            {
                QueueTramCommand("jtu_stop");
            }
        }

        if(XonePlashkaInt("Количество циклов (1-99)", &state.targetCycles, 1, 99, scale))
            QueueTramCommand("jtu_cycles:" + std::to_string(state.targetCycles));

        if(XonePlashkaToggle("Сирена", &state.siren, scale))
            QueueTramCommand(std::string("siren:") + (state.siren ? "1" : "0"));

        if(XoneButton("Сбросить счётчик циклов", scale, false, -1.0f, 34.0f))
            QueueTramCommand("jtu_reset_cycles");
        if(XoneButton("Аварийная остановка", scale, false, -1.0f, 34.0f))
            QueueTramCommand("jtu_stop");
        ImGui::EndChild();

        ImGui::SameLine();

        // Правая колонка — статус и телеметрия ЖТУ
        ImGui::BeginChild("##jtu_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
        XonePlashkaValue("Маршрут", "ЖТУ (Жуковский трамвай)", scale);
        XonePlashkaValue("Состояние", state.status.empty() ? "Ожидание" : state.status.c_str(), scale,
            state.bot ? IM_COL32(80, 225, 120, 255) : IM_COL32(230, 80, 80, 255));
        std::string cycleStr = std::to_string(state.completedCycles) + " / " + std::to_string(state.targetCycles);
        XonePlashkaValue("Выполнено циклов", cycleStr.c_str(), scale, IM_COL32(190, 140, 255, 255));
        ImGui::EndChild();
    }
    else
    {
        float availW = ImGui::GetContentRegionAvail().x;
        float colW = (availW - 14.0f * scale) * 0.5f;

        // Левая колонка — управление трамваем-поливалкой
        ImGui::BeginChild("##tramp_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
        if(XoneButton(state.bot ? "ОСТАНОВИТЬ ПОЛИВАЛКУ" : "ЗАПУСТИТЬ ТРАМВАЙ-ПОЛИВАЛКУ", scale, true, -1.0f, 36.0f))
        {
            if(!state.bot)
            {
                QueueTramCommand("tramp_cycles:" + std::to_string(state.trampTarget));
                QueueTramCommand("tramp_brake:" + std::to_string(state.trampBrake));
                QueueTramCommand(std::string("tramp_traffic:") + (state.trampTraffic ? "1" : "0"));
                QueueTramCommand(std::string("tramp_autodist:") + (state.trampAutoDistance ? "1" : "0"));
                QueueTramCommand("tramp_distmode:" + std::to_string(state.trampDistanceMode));
                QueueTramCommand("tramp_start");
            }
            else
            {
                QueueTramCommand("tramp_stop");
            }
        }

        if(XonePlashkaInt("Количество циклов (1-99)", &state.trampTarget, 1, 99, scale))
            QueueTramCommand("tramp_cycles:" + std::to_string(state.trampTarget));

        if(XonePlashkaInt("Тормозная дистанция (м)", &state.trampBrake, 1, 10, scale))
            QueueTramCommand("tramp_brake:" + std::to_string(state.trampBrake));

        if(XonePlashkaToggle("Авто-дистанция (Коллизия)", &state.trampAutoDistance, scale))
        {
            {
                std::scoped_lock lock(g_bridgeMutex);
                g_tramState.trampAutoDistance = state.trampAutoDistance;
            }
            QueueTramCommand(std::string("tramp_autodist:") + (state.trampAutoDistance ? "1" : "0"));
            SavePolivTramSettings(state.trampDistanceMode, state.trampAutoDistance);
        }

        static const char* s_trampDistOptions[] = { "1 ваг. (~18м)", "2 ваг. (~36м)", "3 ваг. (~54м)" };
        int trampDistIdx = std::clamp(state.trampDistanceMode - 1, 0, 2);
        if(XonePlashkaChoice("Дистанция следования", &trampDistIdx, s_trampDistOptions, 3, scale))
        {
            state.trampDistanceMode = trampDistIdx + 1;
            {
                std::scoped_lock lock(g_bridgeMutex);
                g_tramState.trampDistanceMode = state.trampDistanceMode;
            }
            QueueTramCommand("tramp_distmode:" + std::to_string(state.trampDistanceMode));
            SavePolivTramSettings(state.trampDistanceMode, state.trampAutoDistance);
        }

        if(XonePlashkaToggle("Игнорировать светофоры", &state.trampTraffic, scale))
            QueueTramCommand(std::string("tramp_traffic:") + (state.trampTraffic ? "1" : "0"));

        if(XoneButton("Сбросить счётчик циклов", scale, false, -1.0f, 30.0f))
            QueueTramCommand("tramp_reset_cycles");
        if(XoneButton("Пауза / Продолжить", scale, false, -1.0f, 30.0f))
            QueueTramCommand("tramp_pause");
        ImGui::EndChild();

        ImGui::SameLine();

        // Правая колонка — статус и телеметрия трамвая-поливалки
        ImGui::BeginChild("##tramp_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
        XonePlashkaValue("Маршрут", "Трамвай-поливалка (Приволжск-7)", scale);
        XonePlashkaValue("Состояние", state.status.empty() ? "Ожидание" : state.status.c_str(), scale,
            state.bot ? IM_COL32(80, 225, 120, 255) : IM_COL32(230, 80, 80, 255));
        std::string cycleStr = std::to_string(state.trampCompleted) + " / " + std::to_string(state.trampTarget);
        XonePlashkaValue("Выполнено рейсов", cycleStr.c_str(), scale, IM_COL32(190, 140, 255, 255));
        XonePlashkaValue("Поливалка (установка)", state.trampSprinkler ? "ВКЛЮЧЕНА (поливает)" : "ВЫКЛЮЧЕНА", scale,
            state.trampSprinkler ? IM_COL32(80, 225, 120, 255) : IM_COL32(180, 180, 180, 255));
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar();
}

void JbkToggle(const char* label, bool& value, const char* command)
{
    if(ImGui::Checkbox(label, &value))
        QueueJbkCommand(std::string(command) + (value ? ":1" : ":0"));
}

void JbkInt(const char* label, int& value, int minimum, int maximum,
    const char* command, float scale)
{
    ImGui::SetNextItemWidth(105.0f * scale);
    if(!ImGui::InputInt(label, &value, 1, 10))
        return;
    value = std::clamp(value, minimum, maximum);
    QueueJbkCommand(std::string(command) + ":" + std::to_string(value));
}

void JbkFloat(const char* label, float& value, float minimum, float maximum,
    const char* command, float scale)
{
    ImGui::SetNextItemWidth(105.0f * scale);
    if(!ImGui::InputFloat(label, &value, 0.1f, 1.0f, "%.1f"))
        return;
    value = std::clamp(value, minimum, maximum);
    QueueJbkCommand(std::string(command) + ":" + std::to_string(value));
}

static int g_factoryMode = 0;

void DrawJbkBot(ImVec2 position, ImVec2 size, float scale)
{
    SyncJbkState();
    JbkState& state = g_visibleJbkState;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 28, 28, 245));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(48, 48, 48, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(75, 75, 75, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(40, 40, 40, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(65, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(245, 245, 245, 255));
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##jbk_bot", size, ImGuiChildFlags_Borders);
    float segW = (ImGui::GetContentRegionAvail().x - 6.0f * scale) * 0.5f;
    if(XoneButton("Завод ЖБК", scale, g_factoryMode == 0, segW, 26.0f)) g_factoryMode = 0;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Табачная фабрика", scale, g_factoryMode == 1, segW, 26.0f)) g_factoryMode = 1;
    ImGui::Spacing();

    if(XoneButton(state.bot ? "ОСТАНОВИТЬ БОТА (ЖБК)" : "ЗАПУСТИТЬ БОТА (ЖБК)", scale, true, -1.0f, 38.0f))
        QueueJbkCommand(state.bot ? "bot:0" : "bot:1");
    ImGui::Spacing();

    ImGui::TextColored(state.loaded ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
        state.loaded ? "[LUA ONLINE]" : "[LUA OFFLINE]");
    ImGui::SameLine();
    ImGui::Text("| %s | %s | Заработано: %s р.", state.bot ? "работает" : "остановлен", state.status.c_str(), state.money.c_str());
    ImGui::Separator();

    if(ImGui::BeginTable("##jbk_settings", 3,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Основное");
        JbkToggle("Банни-хоп", state.bhop, "bhop");
        JbkToggle("Сирена", state.siren, "siren");
        JbkToggle("Автоотключение", state.autoDisable, "auto_disable");
        JbkToggle("Стоп при админе", state.stopOnAdmin, "stop_admin");
        JbkToggle("Строгий маршрут", state.strict, "strict");
        JbkToggle("Удар по админу", state.adminHit, "admin_hit");
        JbkToggle("Живая походка", state.walk, "walk");

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Маршрут и защита");
        JbkInt("Скорость поворота", state.rotationSpeed, 1, 10,
            "rotation_speed", scale);
        JbkInt("Задержка BH, мс", state.bhopDelay, 100, 10000,
            "bhop_delay", scale);
        JbkFloat("Дистанция BH", state.bhopDistance, 0.0f, 100.0f,
            "bhop_distance", scale);
        JbkFloat("Макс. высота", state.maxZ, 0.0f, 10000.0f,
            "max_z", scale);
        JbkFloat("Макс. телепорт", state.maxJump, 1.0f, 500.0f,
            "max_jump", scale);
        JbkInt("Авторестарт, мин", state.resumeMinutes, 1, 60,
            "resume_minutes", scale);
        JbkFloat("Порог застревания", state.stuckDistance, 0.1f, 20.0f,
            "stuck_distance", scale);
        JbkInt("Таймер застревания", state.stuckTime, 250, 30000,
            "stuck_time", scale);
        JbkInt("Попыток выхода", state.stuckMax, 1, 20,
            "stuck_max", scale);

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Админ и движение");
        JbkFloat("Радиус удара", state.hitRadius, 1.0f, 100.0f,
            "hit_radius", scale);
        JbkInt("Кулдаун удара, сек", state.hitCooldown, 1, 600,
            "hit_cooldown", scale);
        JbkInt("Интервал шага, мс", state.walkInterval, 100, 10000,
            "walk_interval", scale);
        JbkInt("Длина шага, мс", state.walkDuration, 50, 5000,
            "walk_duration", scale);
        JbkFloat("Мин. дистанция шага", state.walkDistance, 0.0f, 100.0f,
            "walk_distance", scale);
        ImGui::Spacing();
        ImGui::TextWrapped("F5 свободна. Окно DarkFlame: Shift+R. "
            "Все кнопки работают через нативный Lua bridge.");
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar();
}

void DrawTobaccoBot(ImVec2 position, ImVec2 size, float scale)
{
    SyncTobaccoState();
    TobaccoState& state = g_visibleTobaccoState;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 28, 28, 245));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(48, 48, 48, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(75, 75, 75, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(40, 40, 40, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(65, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(245, 245, 245, 255));
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##tobacco_bot", size, ImGuiChildFlags_Borders);
    float segW = (ImGui::GetContentRegionAvail().x - 6.0f * scale) * 0.5f;
    if(XoneButton("Завод ЖБК", scale, g_factoryMode == 0, segW, 26.0f)) g_factoryMode = 0;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Табачная фабрика", scale, g_factoryMode == 1, segW, 26.0f)) g_factoryMode = 1;
    ImGui::Spacing();

    if(XoneButton(state.bot ? "ОСТАНОВИТЬ БОТА (Табачка)" : "ЗАПУСТИТЬ БОТА (Табачка)", scale, true, -1.0f, 38.0f))
        QueueTobaccoCommand(state.bot ? "bot:0" : "bot:1");
    ImGui::Spacing();

    ImGui::TextColored(state.loaded ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
        state.loaded ? "[LUA ONLINE]" : "[LUA STANDBY]");
    ImGui::SameLine();
    ImGui::Text("| %s | %s | Заработано: %s р.", state.bot ? "работает" : "остановлен", state.status.c_str(), state.money.c_str());
    ImGui::Separator();

    ImGui::Text("Заработано: %s р.", state.money.c_str());
    ImGui::Separator();

    if(ImGui::Checkbox("Бежать до склада (Sprint)", &state.sprintToStorage))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_tobaccoState.sprintToStorage = state.sprintToStorage;
        }
        QueueTobaccoCommand(std::string("sprint:") + (state.sprintToStorage ? "1" : "0"));
    }
    ImGui::SameLine();
    if(ImGui::Checkbox("Сирена при проверке/ДТП", &state.siren))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_tobaccoState.siren = state.siren;
        }
        QueueTobaccoCommand(std::string("siren:") + (state.siren ? "1" : "0"));
    }
    ImGui::SameLine();
    if(ImGui::Checkbox("Автоотключение (3 часа / ТП)", &state.autoDisable))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_tobaccoState.autoDisable = state.autoDisable;
        }
        QueueTobaccoCommand(std::string("autodisable:") + (state.autoDisable ? "1" : "0"));
    }

    ImGui::SetNextItemWidth(120.0f * scale);
    if(ImGui::SliderInt("Скорость вращения", &state.rotationSpeed, 5, 10))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_tobaccoState.rotationSpeed = state.rotationSpeed;
        }
        QueueTobaccoCommand("rotation:" + std::to_string(state.rotationSpeed));
    }

    if(ImGui::Checkbox("Автоответчик администраторам (ИИ)", &state.autoAnswer))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_tobaccoState.autoAnswer = state.autoAnswer;
        }
        QueueTobaccoCommand(std::string("autoanswer:") + (state.autoAnswer ? "1" : "0"));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f * scale);
    if(ImGui::SliderInt("Задержка ответа, мс", &state.answerDelay, 50, 200))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_tobaccoState.answerDelay = state.answerDelay;
        }
        QueueTobaccoCommand("answer_delay:" + std::to_string(state.answerDelay));
    }

    ImGui::Spacing();
    if(ImGui::Button("Сбросить маршрут склада"))
        QueueTobaccoCommand("reset_wp");

    ImGui::EndChild();
    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar();
}

void DrawFactoryBot(ImVec2 position, ImVec2 size, float scale)
{
    if(g_factoryMode == 0)
        DrawJbkBot(position, size, scale);
    else
        DrawTobaccoBot(position, size, scale);
}

static bool g_adminAutoStop = true;
static bool g_adminBeep = true;
static int g_adminChatType = 1; // 0 = /say, 1 = /b, 2 = random
static int g_adminDelaySec = 3;
static char g_adminTriggers[256] = "вы тут, тут?, ответ в /b, проверка, вы здесь";
static char g_adminAnswers[256] = "тут\nда, тут\nя тут\nздесь я\nда, работаю";

void DrawAdminTab(ImVec2 position, ImVec2 size, float scale)
{
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##admin_tab", size, ImGuiChildFlags_Borders);

    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "ЗАЩИТА ОТ ПРОВЕРОК АДМИНИСТРАЦИИ (АВТООТВЕТЧИК)");
    ImGui::Separator();

    float availW = ImGui::GetContentRegionAvail().x;
    float colW = (availW - 14.0f * scale) * 0.5f;

    // Левая колонка — тогглы и параметры
    ImGui::BeginChild("##admin_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    bool autoReply = GuiAdminAutoReplyEnabled();
    if(XonePlashkaToggle("Включить автоответчик", &autoReply, scale))
    {
        GuiSetAdminAutoReplyEnabled(autoReply);
    }
    XonePlashkaToggle("Авто-стоп бота при проверке", &g_adminAutoStop, scale);
    XonePlashkaToggle("Звуковой сигнал (Beep / Сирена)", &g_adminBeep, scale);

    XonePlashkaInt("Задержка ответа (2-7 сек)", &g_adminDelaySec, 2, 7, scale);

    ImGui::TextUnformatted("Куда отвечать администратору:");
    float chatBtnW = (ImGui::GetContentRegionAvail().x - 12.0f * scale) / 3.0f;
    if(XoneButton("Обычный чат", scale, g_adminChatType == 0, chatBtnW, 26.0f)) g_adminChatType = 0;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Чат /b", scale, g_adminChatType == 1, chatBtnW, 26.0f)) g_adminChatType = 1;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Случайно", scale, g_adminChatType == 2, chatBtnW, 26.0f)) g_adminChatType = 2;

    ImGui::Spacing();
    XonePlashkaValue("Статус системы", autoReply ? "АКТИВЕН (сканирует чат)" : "ОТКЛЮЧЕН", scale,
        autoReply ? IM_COL32(80, 225, 120, 255) : IM_COL32(230, 80, 80, 255));
    ImGui::EndChild();

    ImGui::SameLine();

    // Правая колонка — слова и шаблоны
    ImGui::BeginChild("##admin_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    ImGui::TextUnformatted("Триггер-слова в чате (через запятую):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##admin_trg", g_adminTriggers, sizeof(g_adminTriggers));
    ImGui::Spacing();

    ImGui::TextUnformatted("Шаблоны ответов (каждый с новой строки):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##admin_ans", g_adminAnswers, sizeof(g_adminAnswers), ImVec2(-1, 100.0f * scale));
    ImGui::Spacing();

    if(XoneButton("Тест сирены / звукового сигнала", scale, false, -1.0f, 32.0f))
    {
        PlayTramAlertSignal();
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void DrawThreadList(ImVec2 position, ImVec2 size, float scale)
{
    SyncThreads();
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##lua_threads", size, ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if(g_selectedThread)
    {
        char btnText[64];
        std::snprintf(btnText, sizeof(btnText), "Выгрузить выбранный поток 0x%08lX", static_cast<unsigned long>(g_selectedThread));
        if(XoneButton(btnText, scale, true, -1.0f, 30.0f))
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_pendingUnload = g_selectedThread;
        }
        ImGui::Spacing();
    }
    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f),
        "Timestamp              Thread ID        Resource");
    ImGui::Separator();
    for(const GuiLuaThread& thread : g_visibleThreads)
    {
        char label[384]{};
        std::snprintf(label, sizeof(label), "%s            0x%08lX        %s##%08lX",
            thread.timestamp.c_str(), static_cast<unsigned long>(thread.id),
            thread.resource.c_str(), static_cast<unsigned long>(thread.id));
        if(ImGui::Selectable(label, g_selectedThread == thread.id,
            ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, 28.0f * scale)))
        {
            g_selectedThread = thread.id;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void RenderProLockOverlay(ImVec2 origin, ImVec2 size, float scale)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 minPos = origin;
    ImVec2 maxPos = ImVec2(origin.x + size.x, origin.y + size.y);

    draw->AddRectFilled(minPos, maxPos, IM_COL32(12, 13, 18, 255), 6.0f * scale);
    draw->AddRect(minPos, maxPos, IM_COL32(255, 255, 255, 12), 6.0f * scale, 0, 1.0f);

    float cardW = 340.0f * scale;
    float cardH = 175.0f * scale;
    ImVec2 cardMin(origin.x + (size.x - cardW) * 0.5f, origin.y + (size.y - cardH) * 0.5f);
    ImVec2 cardMax(cardMin.x + cardW, cardMin.y + cardH);

    draw->AddRectFilled(cardMin, cardMax, IM_COL32(20, 22, 32, 255), 10.0f * scale);
    draw->AddRect(cardMin, cardMax, IM_COL32(165, 110, 255, 180), 10.0f * scale, 0, 1.5f);

    float badgeW = 48.0f * scale;
    float badgeH = 20.0f * scale;
    ImVec2 bPos(cardMin.x + (cardW - badgeW) * 0.5f, cardMin.y + 18.0f * scale);
    draw->AddRectFilled(bPos, ImVec2(bPos.x + badgeW, bPos.y + badgeH), IM_COL32(140, 70, 255, 220), 4.0f * scale);
    const char* bText = "PRO";
    ImVec2 bSize = ImGui::CalcTextSize(bText);
    draw->AddText(ImVec2(bPos.x + (badgeW - bSize.x) * 0.5f, bPos.y + (badgeH - bSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), bText);

    const char* title = "Доступно в PRO-версии";
    ImVec2 tSize = g_tabFont ? g_tabFont->CalcTextSizeA(16.0f * scale, FLT_MAX, -1.0f, title) : ImGui::CalcTextSize(title);
    if(g_tabFont)
        draw->AddText(g_tabFont, 16.0f * scale, ImVec2(cardMin.x + (cardW - tSize.x) * 0.5f, cardMin.y + 46.0f * scale), IM_COL32(245, 245, 250, 255), title);
    else
        draw->AddText(ImVec2(cardMin.x + (cardW - tSize.x) * 0.5f, cardMin.y + 46.0f * scale), IM_COL32(245, 245, 250, 255), title);

    const char* desc = "Этот модуль доступен только по подписке.";
    ImVec2 dSize = ImGui::CalcTextSize(desc);
    draw->AddText(ImVec2(cardMin.x + (cardW - dSize.x) * 0.5f, cardMin.y + 74.0f * scale), IM_COL32(160, 165, 180, 255), desc);

    float btnW = 200.0f * scale;
    float btnH = 32.0f * scale;
    ImVec2 btnPos(cardMin.x + (cardW - btnW) * 0.5f, cardMin.y + 115.0f * scale);
    ImGui::SetCursorScreenPos(btnPos);
    if(XoneButton("Активировать лицензию", scale, true, btnW, btnH))
    {
        g_activeTab = 8;
    }
}

void DrawLicenseTab(ImVec2 position, ImVec2 size, float scale)
{
    const LicenseState& lic = GetLicenseState();
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##license_tab", size, ImGuiChildFlags_Borders);

    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "УПРАВЛЕНИЕ ЛИЦЕНЗИЕЙ ASHES (FREE / PRO)");
    ImGui::Separator();

    float availW = ImGui::GetContentRegionAvail().x;
    float colW = (availW - 14.0f * scale) * 0.5f;

    ImGui::BeginChild("##lic_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    XonePlashkaValue("Текущий статус", lic.is_pro ? "PRO (Полный доступ)" : "FREE (Базовый доступ)", scale,
        lic.is_pro ? IM_COL32(80, 225, 120, 255) : IM_COL32(235, 160, 60, 255));
    XonePlashkaValue("Срок действия", lic.expire_date.c_str(), scale, IM_COL32(190, 150, 255, 255));
    XonePlashkaValue("Состояние", lic.status_msg.c_str(), scale, IM_COL32(220, 220, 230, 255));

    static char inputKey[64]{};
    static bool keyInitialized = false;
    if(!keyInitialized && !lic.key.empty())
    {
        strncpy_s(inputKey, lic.key.c_str(), sizeof(inputKey) - 1);
        keyInitialized = true;
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Ключ активации (DF-PRO-...):");
    ImGui::SetNextItemWidth(-1.0f);
    XoneThemeColors c = GetCurrentTheme();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.plashkaBg);
    ImGui::InputText("##lic_key_input", inputKey, sizeof(inputKey));
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if(XoneButton("АКТИВИРОВАТЬ КЛЮЧ", scale, true, -1.0f, 34.0f * scale))
    {
        if(inputKey[0])
        {
            ActivateLicenseKey(inputKey);
        }
    }

    if(lic.is_pro)
    {
        ImGui::Spacing();
        if(XoneButton("Деактивировать лицензию", scale, false, -1.0f, 26.0f * scale))
        {
            DeactivateLicense();
            inputKey[0] = '\0';
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##lic_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    XonePlashkaValue("HWID устройства", lic.hwid.c_str(), scale, IM_COL32(255, 255, 255, 255));
    if(XoneButton("Скопировать HWID в буфер", scale, false, -1.0f, 28.0f * scale))
    {
        ImGui::SetClipboardText(lic.hwid.c_str());
    }

    XonePlashkaValue("MAC-адрес", lic.mac.c_str(), scale, IM_COL32(170, 175, 190, 255));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Как получить PRO-версию?");
    ImGui::TextWrapped("PRO-версия открывает доступ к ЖТУ, поливалке на рельсах, защите от админов (автоответчику) и инструментам отладки.");

    ImGui::Spacing();
    if(XoneButton("Купить ключ в Telegram", scale, true, -1.0f, 34.0f * scale))
    {
        ShellExecuteA(nullptr, "open", "https://t.me/ashes_rst_bot", nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void RenderMenu()
{
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = true;
    constexpr float BaseWidth = 740.0f;
    constexpr float BaseHeight = 470.0f;
    const float scale = 1.0f;
    const ImVec2 size(BaseWidth * scale, BaseHeight * scale);
    const ImVec2 centered((io.DisplaySize.x - size.x) * 0.5f,
        (io.DisplaySize.y - size.y) * 0.5f);

    static ImVec2 previousDisplay{};
    const bool displayChanged = previousDisplay.x != io.DisplaySize.x || previousDisplay.y != io.DisplaySize.y;
    ImGui::SetNextWindowPos(centered, displayChanged ? ImGuiCond_Always : ImGuiCond_Once);
    previousDisplay = io.DisplaySize;
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##ashes_menu", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 origin = ImGui::GetWindowPos();
    XoneThemeColors c = GetCurrentTheme();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 1. Окно скругленное 10px
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), c.windowBg, 10.0f * scale);

    // 2. Тема Черная с волнами: анимированные плавные белые линии
    DrawAnimatedWaves(draw, origin, size);

    // 3. Сайдбар слева (155px)
    float sidebarWidth = 155.0f * scale;
    draw->PushClipRect(origin, ImVec2(origin.x + sidebarWidth, origin.y + size.y), true);
    draw->AddRectFilled(origin, ImVec2(origin.x + sidebarWidth, origin.y + size.y), c.sidebarBg, 10.0f * scale);
    draw->AddLine(ImVec2(origin.x + sidebarWidth, origin.y), ImVec2(origin.x + sidebarWidth, origin.y + size.y), c.sidebarBorder, 1.0f * scale);
    draw->PopClipRect();

    // 4. Тонкая рамка окна 1px
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), c.windowBorder, 10.0f * scale, 0, 1.0f);

    // Логотип ASHES
    RenderAshesLogo(draw, ImVec2(origin.x + 12.0f * scale, origin.y + 14.0f * scale), 26.0f * scale);
    const float logoFontSize = 17.0f * scale;
    draw->AddText(g_titleFont, logoFontSize, ImVec2(origin.x + 44.0f * scale, origin.y + 11.0f * scale), IM_COL32(255, 255, 255, 255), "ASHES");
    draw->AddText(ImVec2(origin.x + 44.0f * scale, origin.y + 30.0f * scale), IM_COL32(255, 255, 255, 110), "by restleSS");

    draw->AddLine(ImVec2(origin.x + 10.0f * scale, origin.y + 52.0f * scale),
        ImVec2(origin.x + sidebarWidth - 10.0f * scale, origin.y + 52.0f * scale), c.sidebarBorder, 1.0f * scale);

    // Вертикальные вкладки
    struct TabDef { const char* id; const char* name; int tab; bool is_pro; };
    const TabDef navTabs[]{
        {"##tab_lua", "Lua Injector", 0, true},
        {"##tab_events", "Event Monitor", 1, true},
        {"##tab_threads", "Lua Threads", 2, true},
        {"##tab_tram", "TramBot", 3, false},
        {"##tab_factory", "Бот - Завод", 4, false},
        {"##tab_pilot", "Pilot", 5, false},
        {"##tab_plow", "Поливалка (Авто)", 6, false},
        {"##tab_admin", "Админы", 7, true}
    };
    const bool isPro = IsProLicense();
    for(int i = 0; i < 8; ++i)
    {
        ImVec2 tabPos(origin.x + 8.0f * scale, origin.y + (58.0f + i * 35.0f) * scale);
        ImVec2 tabSize(sidebarWidth - 16.0f * scale, 30.0f * scale);
        bool active = (g_activeTab == navTabs[i].tab);

        ImGui::SetCursorScreenPos(tabPos);
        if(ImGui::InvisibleButton(navTabs[i].id, tabSize))
            g_activeTab = navTabs[i].tab;

        bool hovered = ImGui::IsItemHovered();
        if(active)
        {
            draw->AddRectFilled(tabPos, ImVec2(tabPos.x + tabSize.x, tabPos.y + tabSize.y), c.plashkaBgHover, 6.0f * scale);
            draw->AddRect(tabPos, ImVec2(tabPos.x + tabSize.x, tabPos.y + tabSize.y), c.plashkaBorderHover, 6.0f * scale, 0, 1.0f);
            draw->AddRectFilled(ImVec2(tabPos.x + 2.0f * scale, tabPos.y + 6.0f * scale),
                ImVec2(tabPos.x + 4.0f * scale, tabPos.y + tabSize.y - 6.0f * scale), c.accent, 2.0f * scale);
        }
        else if(hovered)
        {
            draw->AddRectFilled(tabPos, ImVec2(tabPos.x + tabSize.x, tabPos.y + tabSize.y), c.plashkaBg, 6.0f * scale);
        }

        ImVec2 txtSize = g_tabFont->CalcTextSizeA(14.0f * scale, FLT_MAX, -1.0f, navTabs[i].name);
        draw->AddText(g_tabFont, 14.0f * scale, ImVec2(tabPos.x + 12.0f * scale, tabPos.y + (tabSize.y - txtSize.y) * 0.5f),
            active ? c.textPrimary : c.textMuted, navTabs[i].name);

        if(navTabs[i].is_pro && !isPro)
        {
            float bW = 32.0f * scale;
            float bH = 15.0f * scale;
            ImVec2 bPos(tabPos.x + tabSize.x - bW - 6.0f * scale, tabPos.y + (tabSize.y - bH) * 0.5f);
            draw->AddRectFilled(bPos, ImVec2(bPos.x + bW, bPos.y + bH), IM_COL32(145, 80, 255, 200), 3.0f * scale);
            const char* proStr = "PRO";
            ImVec2 pSz = ImGui::CalcTextSize(proStr);
            draw->AddText(ImVec2(bPos.x + (bW - pSz.x) * 0.5f, bPos.y + (bH - pSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), proStr);
        }
    }

    // Статус лицензии внизу сайдбара
    ImVec2 licPos(origin.x + 8.0f * scale, origin.y + size.y - 38.0f * scale);
    ImVec2 licSize(sidebarWidth - 16.0f * scale, 28.0f * scale);
    ImGui::SetCursorScreenPos(licPos);
    const char* licLabel = isPro ? "Лицензия: PRO" : "Лицензия: FREE";
    if(XoneButton(licLabel, scale, (g_activeTab == 8 || isPro), licSize.x, 28.0f))
    {
        g_activeTab = 8;
    }


    // Рабочая область
    const ImVec2 contentPosition(origin.x + sidebarWidth + 10.0f * scale, origin.y + 10.0f * scale);
    const ImVec2 contentSize(size.x - sidebarWidth - 20.0f * scale, size.y - 20.0f * scale);
    ImGui::SetCursorScreenPos(contentPosition);
    if(g_activeTab == 0)
    {
        if(!isPro)
        {
            RenderProLockOverlay(contentPosition, contentSize, scale);
        }
        else
        {
            ImGui::SetCursorScreenPos(contentPosition);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
            ImGui::BeginChild("##editor_tab", contentSize, ImGuiChildFlags_Borders);
            float barH = 36.0f * scale;
            ImVec2 edSize(contentSize.x - 4.0f, contentSize.y - barH - 12.0f * scale);
            g_editor->Render("##lua_editor", edSize, false);
            ImGui::SetCursorScreenPos(ImVec2(contentPosition.x + 4.0f * scale, contentPosition.y + edSize.y + 6.0f * scale));
            ImGui::TextUnformatted("Target resource:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(320.0f * scale);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, c.plashkaBg);
            ImGui::InputText("##target_resource", g_targetResource, sizeof(g_targetResource));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if(XoneButton("Inject Code", scale, true, 130.0f * scale, 28.0f))
                QueueEditorCode();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }
    else if(g_activeTab == 1)
    {
        if(!isPro)
        {
            RenderProLockOverlay(contentPosition, contentSize, scale);
        }
        else
        {
            ImGui::SetCursorScreenPos(contentPosition);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
            ImGui::BeginChild("##events_tab", contentSize, ImGuiChildFlags_Borders);
            SyncEvents();
            bool antiAfk = IsAntiAfkEnabled();
            if(XoneToggle("##tgl_anti_afk", &antiAfk, scale))
            {
                SetAntiAfkEnabled(antiAfk);
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0f * scale);
            ImGui::TextColored(antiAfk ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                antiAfk ? "Anti-AFK: Активен (блокировка province_afktimer)" : "Anti-AFK: Выключен");
            ImGui::SameLine();
            float availW = ImGui::GetContentRegionAvail().x;
            float btnW = 110.0f * scale;
            if(availW > btnW)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - btnW));
            if(XoneButton("Очистить", scale, false, btnW, 22.0f * scale))
            {
                GuiClearEvents();
            }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, c.plashkaBg);
            ImGui::InputTextMultiline("##event_monitor", g_events.data(), g_events.size(),
                ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput);
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }
    else if(g_activeTab == 2)
    {
        if(!isPro)
            RenderProLockOverlay(contentPosition, contentSize, scale);
        else
            DrawThreadList(contentPosition, contentSize, scale);
    }
    else if(g_activeTab == 3)
        DrawTramBot(contentPosition, contentSize, scale);
    else if(g_activeTab == 4)
        DrawFactoryBot(contentPosition, contentSize, scale);
    else if(g_activeTab == 5)
        DrawPilotTelemetry(contentPosition, contentSize, scale);
    else if(g_activeTab == 6)
        DrawPlowBot(contentPosition, contentSize, scale);
    else if(g_activeTab == 7)
    {
        if(!isPro)
            RenderProLockOverlay(contentPosition, contentSize, scale);
        else
            DrawAdminTab(contentPosition, contentSize, scale);
    }
    else if(g_activeTab == 8)
    {
        DrawLicenseTab(contentPosition, contentSize, scale);
    }
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void RenderFrame(IDirect3DDevice9* device)
{
    if(!device)
        return;
    PulseLuaBridge();
    if(!g_initialized && !InitializeGui(device))
        return;
    static bool toggleHeld{};
    const bool toggleDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        && (GetAsyncKeyState('R') & 0x8000);
    if(toggleDown && !toggleHeld)
    {
        const bool visible = !g_visible.load();
        g_visible.store(visible);
        if(visible && g_clipCursor)
            g_clipCursor(nullptr);
    }
    toggleHeld = toggleDown;
    if(!g_visible.load())
    {
        ImGui::GetIO().MouseDrawCursor = false;
        return;
    }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    RenderMenu();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

HRESULT __stdcall HookPresent(IDirect3DDevice9* device, const RECT* source,
    const RECT* destination, HWND window, const RGNDATA* dirty)
{
    if(!device)
        return g_present ? g_present(device, source, destination, window, dirty) : D3DERR_INVALIDCALL;
    RenderFrame(device);
    return g_present(device, source, destination, window, dirty);
}

HRESULT __stdcall HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters)
{
    if(!device || !g_reset)
        return D3DERR_INVALIDCALL;

    // 1. Обязательно освобождаем ресурсы перед Reset
    if(ImGui::GetCurrentContext())
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    // 2. Вызываем оригинальный Reset
    const HRESULT hr = g_reset(device, parameters);

    // 3. Восстанавливаем ресурсы только при успешном результате
    if(SUCCEEDED(hr))
    {
        if(parameters && parameters->hDeviceWindow)
        {
            g_window = parameters->hDeviceWindow;
            InputUtil::SetGameWindow(g_window);
        }
        if(g_initialized && ImGui::GetCurrentContext())
        {
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }
    return hr;
}

HRESULT __stdcall HookPresentEx(IDirect3DDevice9Ex* device, const RECT* source,
    const RECT* destination, HWND window, const RGNDATA* dirty, DWORD flags)
{
    if(!device)
        return g_presentEx ? g_presentEx(device, source, destination, window, dirty, flags) : D3DERR_INVALIDCALL;
    RenderFrame(device);
    return g_presentEx(device, source, destination, window, dirty, flags);
}

HRESULT __stdcall HookResetEx(IDirect3DDevice9Ex* device,
    D3DPRESENT_PARAMETERS* parameters, D3DDISPLAYMODEEX* mode)
{
    if(!device || !g_resetEx)
        return D3DERR_INVALIDCALL;

    // 1. Обязательно освобождаем ресурсы перед Reset
    if(ImGui::GetCurrentContext())
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    // 2. Вызываем оригинальный Reset
    const HRESULT hr = g_resetEx(device, parameters, mode);

    // 3. Восстанавливаем ресурсы только при успешном результате
    if(SUCCEEDED(hr))
    {
        if(parameters && parameters->hDeviceWindow)
        {
            g_window = parameters->hDeviceWindow;
            InputUtil::SetGameWindow(g_window);
        }
        if(g_initialized && ImGui::GetCurrentContext())
        {
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }
    return hr;
}

bool InstallDeviceHooks(IDirect3DDevice9* device)
{
    if(!device)
        return false;
    long expected{};
    if(!g_deviceState.compare_exchange_strong(expected, 1))
        return expected == 2;
    __try
    {
        auto** table = *reinterpret_cast<void***>(device);
        if(!table)
        {
            g_deviceState.store(0);
            return false;
        }
        void* resetTarget = table[16];
        void* presentTarget = table[17];
        if(!resetTarget || !presentTarget)
        {
            g_deviceState.store(0);
            return false;
        }
        const MH_STATUS resetCreated = MH_CreateHook(resetTarget,
            reinterpret_cast<void*>(&HookReset), reinterpret_cast<void**>(&g_reset));
        const MH_STATUS presentCreated = MH_CreateHook(presentTarget,
            reinterpret_cast<void*>(&HookPresent), reinterpret_cast<void**>(&g_present));
        const bool ready = resetCreated == MH_OK && presentCreated == MH_OK
            && MH_EnableHook(resetTarget) == MH_OK && MH_EnableHook(presentTarget) == MH_OK;
        if(!ready)
        {
            if(resetCreated == MH_OK)
            {
                MH_DisableHook(resetTarget);
                MH_RemoveHook(resetTarget);
            }
            if(presentCreated == MH_OK)
            {
                MH_DisableHook(presentTarget);
                MH_RemoveHook(presentTarget);
            }
            g_reset = nullptr;
            g_present = nullptr;
        }
        g_deviceState.store(ready ? 2 : 0);
        Log::Write(ready ? L"[gui] real DX9 device hooks installed"
            : L"[gui] real DX9 device hook installation failed");
        return ready;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_deviceState.store(0);
        Log::Write(L"[gui] exception during InstallDeviceHooks");
        return false;
    }
}

bool InstallDeviceExHooks(IDirect3DDevice9Ex* device)
{
    if(!device)
        return false;
    long expected{};
    if(!g_deviceExState.compare_exchange_strong(expected, 1))
        return expected == 2;
    __try
    {
        auto** table = *reinterpret_cast<void***>(device);
        if(!table)
        {
            g_deviceExState.store(0);
            return false;
        }
        void* presentTarget = table[121];
        void* resetTarget = table[132];
        if(!presentTarget || !resetTarget)
        {
            g_deviceExState.store(0);
            return false;
        }
        const MH_STATUS presentCreated = MH_CreateHook(presentTarget,
            reinterpret_cast<void*>(&HookPresentEx), reinterpret_cast<void**>(&g_presentEx));
        const MH_STATUS resetCreated = MH_CreateHook(resetTarget,
            reinterpret_cast<void*>(&HookResetEx), reinterpret_cast<void**>(&g_resetEx));
        const bool ready = presentCreated == MH_OK && resetCreated == MH_OK
            && MH_EnableHook(presentTarget) == MH_OK && MH_EnableHook(resetTarget) == MH_OK;
        if(!ready)
        {
            if(presentCreated == MH_OK)
            {
                MH_DisableHook(presentTarget);
                MH_RemoveHook(presentTarget);
            }
            if(resetCreated == MH_OK)
            {
                MH_DisableHook(resetTarget);
                MH_RemoveHook(resetTarget);
            }
            g_presentEx = nullptr;
            g_resetEx = nullptr;
        }
        g_deviceExState.store(ready ? 2 : 0);
        Log::Write(ready ? L"[gui] real DX9Ex device hooks installed"
            : L"[gui] real DX9Ex device hook installation failed");
        return ready;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_deviceExState.store(0);
        Log::Write(L"[gui] exception during InstallDeviceExHooks");
        return false;
    }
}

HRESULT __stdcall HookCreateDevice(IDirect3D9* api, UINT adapter,
    D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS* parameters,
    IDirect3DDevice9** output)
{
    const HRESULT result = g_createDevice(api, adapter, type, window, flags,
        parameters, output);
    if(SUCCEEDED(result) && output && *output)
        InstallDeviceHooks(*output);
    return result;
}

HRESULT __stdcall HookCreateDeviceEx(IDirect3D9Ex* api, UINT adapter,
    D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS* parameters,
    D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** output)
{
    const HRESULT result = g_createDeviceEx(api, adapter, type, window, flags,
        parameters, mode, output);
    if(SUCCEEDED(result) && output && *output)
    {
        InstallDeviceHooks(*output);
        InstallDeviceExHooks(*output);
    }
    return result;
}

bool InstallFactoryHooks(IDirect3D9* api)
{
    if(!api)
        return false;
    long expected{};
    if(!g_factoryState.compare_exchange_strong(expected, 1))
        return expected == 2;
    __try
    {
        auto** table = *reinterpret_cast<void***>(api);
        if(!table)
        {
            g_factoryState.store(0);
            return false;
        }
        void* target = table[16];
        if(!target)
        {
            g_factoryState.store(0);
            return false;
        }
        const MH_STATUS created = MH_CreateHook(target,
            reinterpret_cast<void*>(&HookCreateDevice),
            reinterpret_cast<void**>(&g_createDevice));
        const bool ready = created == MH_OK && MH_EnableHook(target) == MH_OK;
        if(!ready)
        {
            if(created == MH_OK)
            {
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
            g_createDevice = nullptr;
        }
        g_factoryState.store(ready ? 2 : 0);
        Log::Write(ready ? L"[gui] IDirect3D9::CreateDevice hooked"
            : L"[gui] IDirect3D9::CreateDevice hook failed");
        return ready;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_factoryState.store(0);
        Log::Write(L"[gui] exception during InstallFactoryHooks");
        return false;
    }
}

bool InstallFactoryExHooks(IDirect3D9Ex* api)
{
    if(!api)
        return false;
    long expected{};
    if(!g_factoryExState.compare_exchange_strong(expected, 1))
        return expected == 2;
    __try
    {
        auto** table = *reinterpret_cast<void***>(api);
        if(!table)
        {
            g_factoryExState.store(0);
            return false;
        }
        void* target = table[20];
        if(!target)
        {
            g_factoryExState.store(0);
            return false;
        }
        const MH_STATUS created = MH_CreateHook(target,
            reinterpret_cast<void*>(&HookCreateDeviceEx),
            reinterpret_cast<void**>(&g_createDeviceEx));
        const bool ready = created == MH_OK && MH_EnableHook(target) == MH_OK;
        if(!ready)
        {
            if(created == MH_OK)
            {
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
            g_createDeviceEx = nullptr;
        }
        g_factoryExState.store(ready ? 2 : 0);
        Log::Write(ready ? L"[gui] IDirect3D9Ex::CreateDeviceEx hooked"
            : L"[gui] IDirect3D9Ex::CreateDeviceEx hook failed");
        return ready;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_factoryExState.store(0);
        Log::Write(L"[gui] exception during InstallFactoryExHooks");
        return false;
    }
}

IDirect3D9* WINAPI HookDirect3DCreate9(UINT sdkVersion)
{
    IDirect3D9* api = g_create9(sdkVersion);
    if(api) InstallFactoryHooks(api);
    return api;
}

HRESULT WINAPI HookDirect3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** output)
{
    const HRESULT result = g_create9Ex(sdkVersion, output);
    if(SUCCEEDED(result) && output && *output)
    {
        InstallFactoryHooks(*output);
        InstallFactoryExHooks(*output);
    }
    return result;
}

DWORD WINAPI HookThread(void*)
{
    InstallGameInputHooks();
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    if(!d3d9) d3d9 = LoadLibraryW(L"d3d9.dll");
    void* create9 = d3d9 ? reinterpret_cast<void*>(
        GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    void* create9Ex = d3d9 ? reinterpret_cast<void*>(
        GetProcAddress(d3d9, "Direct3DCreate9Ex")) : nullptr;
    bool standardCreated{}, standard{}, extendedCreated{}, extended{};
    if(create9)
    {
        standardCreated = MH_CreateHook(create9,
            reinterpret_cast<void*>(&HookDirect3DCreate9),
            reinterpret_cast<void**>(&g_create9)) == MH_OK;
        standard = standardCreated && MH_EnableHook(create9) == MH_OK;
    }
    if(create9Ex)
    {
        extendedCreated = MH_CreateHook(create9Ex,
            reinterpret_cast<void*>(&HookDirect3DCreate9Ex),
            reinterpret_cast<void**>(&g_create9Ex)) == MH_OK;
        extended = extendedCreated && MH_EnableHook(create9Ex) == MH_OK;
    }
    if(!standard && standardCreated)
    {
        MH_DisableHook(create9);
        MH_RemoveHook(create9);
        g_create9 = nullptr;
    }
    if(!extended && extendedCreated)
    {
        MH_DisableHook(create9Ex);
        MH_RemoveHook(create9Ex);
        g_create9Ex = nullptr;
    }
    if(g_deviceState.load() != 2 && create9)
    {
        auto* createDirect3D9 = reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(create9);
        IDirect3D9* dummyD3D = createDirect3D9(D3D_SDK_VERSION);
        if(dummyD3D)
        {
            HWND dummyHwnd = CreateWindowExA(0, "STATIC", "DF_Dummy", WS_OVERLAPPEDWINDOW,
                0, 0, 100, 100, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
            if(dummyHwnd)
            {
                D3DPRESENT_PARAMETERS d3dpp{};
                d3dpp.Windowed = TRUE;
                d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
                d3dpp.hDeviceWindow = dummyHwnd;
                IDirect3DDevice9* dummyDev = nullptr;
                HRESULT hr = dummyD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF,
                    dummyHwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &dummyDev);
                if(FAILED(hr) || !dummyDev)
                {
                    dummyDev = nullptr;
                    hr = dummyD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                        dummyHwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &dummyDev);
                }
                if(SUCCEEDED(hr) && dummyDev)
                {
                    InstallDeviceHooks(dummyDev);
                    dummyDev->Release();
                }
                DestroyWindow(dummyHwnd);
            }
            dummyD3D->Release();
        }
    }
    Log::Write(standard || extended || g_deviceState.load() == 2
        ? L"[gui] DX9 hooks installed; ready for game rendering"
        : L"[gui] DX9 factory hook installation failed");
    return 0;
}
}

bool StartGui(HMODULE module)
{
    if(!module || g_started.exchange(true))
        return false;
    g_module = module;
    HANDLE thread = CreateThread(nullptr, 0, &HookThread, nullptr, 0, nullptr);
    if(!thread)
        return false;
    CloseHandle(thread);
    return true;
}

bool GuiVisible()
{
    return g_visible.load();
}

bool GuiTakeLuaCode(std::string& code, std::string& resource)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_luaPending)
        return false;
    code = std::move(g_pendingLua);
    resource = std::move(g_pendingResource);
    g_pendingLua.clear();
    g_pendingResource.clear();
    g_luaPending = false;
    return true;
}

bool GuiTakeUnloadThread(std::uintptr_t& id)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_pendingUnload)
        return false;
    id = g_pendingUnload;
    g_pendingUnload = 0;
    return true;
}

bool GuiTakeTramCommand(std::string& command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_tramCommands.empty())
        return false;
    command = std::move(g_tramCommands.front());
    g_tramCommands.pop_front();
    return true;
}

int GuiGetTramSubMode()
{
    std::scoped_lock lock(g_bridgeMutex);
    return g_tramState.subMode;
}

bool GuiTakeJbkCommand(std::string& command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_jbkCommands.empty())
        return false;
    command = std::move(g_jbkCommands.front());
    g_jbkCommands.pop_front();
    return true;
}

bool GuiTramBotEnabled()
{
    std::scoped_lock lock(g_bridgeMutex);
    return g_tramState.bot;
}

bool GuiTramSirenEnabled()
{
    std::scoped_lock lock(g_bridgeMutex);
    return g_tramState.siren;
}

void GuiQueueTramAdminCaption(std::string caption)
{
    if(caption.empty())
        return;
    std::scoped_lock lock(g_bridgeMutex);
    if(g_tramCommands.size() >= 64)
        g_tramCommands.pop_front();
    g_tramCommands.push_back("admin:" + std::move(caption));
}

namespace
{
std::atomic_bool g_adminAutoReply{true};
}

void GuiQueueJbkAdminCaption(std::string caption)
{
    if(caption.empty())
        return;
    std::scoped_lock lock(g_bridgeMutex);
    if(g_jbkCommands.size() >= 64)
        g_jbkCommands.pop_front();
    g_jbkCommands.push_back("admin:" + std::move(caption));
}

bool GuiAdminAutoReplyEnabled()
{
    return g_adminAutoReply.load(std::memory_order_acquire);
}

void GuiSetAdminAutoReplyEnabled(bool enabled)
{
    g_adminAutoReply.store(enabled, std::memory_order_release);
}

void GuiAddLuaThread(const GuiLuaThread& thread)
{
    std::scoped_lock lock(g_bridgeMutex);
    g_threads.push_back(thread);
    g_threadsDirty = true;
}

void GuiRemoveLuaThread(std::uintptr_t id)
{
    std::scoped_lock lock(g_bridgeMutex);
    const auto end = std::remove_if(g_threads.begin(), g_threads.end(),
        [id](const GuiLuaThread& thread)
    {
        return thread.id == id;
    });
    g_threads.erase(end, g_threads.end());
    g_threadsDirty = true;
}

void GuiClearLuaThreads()
{
    std::scoped_lock lock(g_bridgeMutex);
    g_threads.clear();
    g_pendingUnload = 0;
    g_threadsDirty = true;
}

void GuiAppendEvent(std::string_view event)
{
    if(event.empty())
        return;
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_eventText.empty())
        g_eventText.push_back('\n');
    g_eventText.append(event);
    constexpr std::size_t MaxEventText = 64000;
    if(g_eventText.size() > MaxEventText)
    {
        const std::size_t excess = g_eventText.size() - MaxEventText;
        const std::size_t newline = g_eventText.find('\n', excess);
        g_eventText.erase(0, newline == std::string::npos ? excess : newline + 1);
    }
    g_eventsDirty = true;
}

void GuiClearEvents()
{
    std::scoped_lock lock(g_bridgeMutex);
    g_eventText.clear();
    g_events[0] = '\0';
    g_eventsDirty = true;
}

void GuiUpdateTramState(std::string_view key, std::string_view value)
{
    const bool enabled = value == "1" || value == "true";
    const std::string text = EnsureUtf8(value);
    std::scoped_lock lock(g_bridgeMutex);
    if(key == "loaded") g_tramState.loaded = enabled;
    else if(key == "bot") g_tramState.bot = enabled;
    else if(key == "traffic") g_tramState.traffic = enabled;
    else if(key == "debug") g_tramState.debug = enabled;
    else if(key == "siren") g_tramState.siren = enabled;
    else if(key == "force") g_tramState.force = enabled;
    else if(key == "route") g_tramState.route = text;
    else if(key == "direction") g_tramState.direction = text;
    else if(key == "status") g_tramState.status = text;
    else if(key == "money") g_tramState.money = text;
    else if(key == "catcher") g_tramState.catcher = text;
    else if(key == "jtu_completed") g_tramState.completedCycles = std::atoi(text.c_str());
    else if(key == "jtu_target") g_tramState.targetCycles = std::atoi(text.c_str());
    else if(key == "tramp_completed") g_tramState.trampCompleted = std::atoi(text.c_str());
    else if(key == "tramp_target") g_tramState.trampTarget = std::atoi(text.c_str());
    else if(key == "tramp_brake") g_tramState.trampBrake = std::atoi(text.c_str());
    else if(key == "tramp_traffic") g_tramState.trampTraffic = enabled;
    else if(key == "tramp_sprinkler") g_tramState.trampSprinkler = enabled;
    else if(key == "tramp_autodist") g_tramState.trampAutoDistance = enabled;
    else if(key == "tramp_distmode") g_tramState.trampDistanceMode = std::atoi(text.c_str());
    else if(key == "submode") g_tramState.subMode = std::atoi(text.c_str());
    else if(key == "autodist") g_tramState.autoDistance = enabled;
    else if(key == "distmode") g_tramState.distanceMode = std::atoi(text.c_str());
    else if(key == "autostop_min") g_tramState.autoStopMinutes = std::atoi(text.c_str());
    else return;
    g_tramDirty = true;
}

void GuiUpdateJbkState(std::string_view key, std::string_view value)
{
    const bool enabled = value == "1" || value == "true";
    const std::string text = EnsureUtf8(value);
    const int integer = std::atoi(text.c_str());
    const float number = std::strtof(text.c_str(), nullptr);
    std::scoped_lock lock(g_bridgeMutex);
    if(key == "loaded") g_jbkState.loaded = enabled;
    else if(key == "bot") g_jbkState.bot = enabled;
    else if(key == "bhop") g_jbkState.bhop = enabled;
    else if(key == "auto_disable") g_jbkState.autoDisable = enabled;
    else if(key == "siren") g_jbkState.siren = enabled;
    else if(key == "stop_admin") g_jbkState.stopOnAdmin = enabled;
    else if(key == "strict") g_jbkState.strict = enabled;
    else if(key == "admin_hit") g_jbkState.adminHit = enabled;
    else if(key == "walk") g_jbkState.walk = enabled;
    else if(key == "rotation_speed") g_jbkState.rotationSpeed = integer;
    else if(key == "bhop_delay") g_jbkState.bhopDelay = integer;
    else if(key == "bhop_distance") g_jbkState.bhopDistance = number;
    else if(key == "max_z") g_jbkState.maxZ = number;
    else if(key == "max_jump") g_jbkState.maxJump = number;
    else if(key == "resume_minutes") g_jbkState.resumeMinutes = integer;
    else if(key == "stuck_distance") g_jbkState.stuckDistance = number;
    else if(key == "stuck_time") g_jbkState.stuckTime = integer;
    else if(key == "stuck_max") g_jbkState.stuckMax = integer;
    else if(key == "hit_radius") g_jbkState.hitRadius = number;
    else if(key == "hit_cooldown") g_jbkState.hitCooldown = integer;
    else if(key == "walk_interval") g_jbkState.walkInterval = integer;
    else if(key == "walk_duration") g_jbkState.walkDuration = integer;
    else if(key == "walk_distance") g_jbkState.walkDistance = number;
    else if(key == "status") g_jbkState.status = text;
    else if(key == "money") g_jbkState.money = text;
    else return;
    g_jbkDirty = true;
}

void GuiResetTramState()
{
    std::scoped_lock lock(g_bridgeMutex);
    const int currentSub = g_tramState.subMode;
    g_tramState = {};
    g_tramState.subMode = currentSub;
    g_tramState.siren = true;
    g_tramState.route = "не выбран";
    g_tramState.direction = "forward";
    g_tramState.status = "ожидание ресурса";
    g_tramState.money = "0";
    g_tramState.catcher = "не зарегистрирован";
    g_tramState.trampAutoDistance = true;
    g_tramState.trampDistanceMode = 2;
    g_tramCommands.clear();
    g_tramDirty = true;
}

void GuiResetJbkState()
{
    std::scoped_lock lock(g_bridgeMutex);
    g_jbkState = {};
    g_jbkCommands.clear();
    g_jbkDirty = true;
}

bool GuiTakeTobaccoCommand(std::string& command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_tobaccoCommands.empty())
        return false;
    command = std::move(g_tobaccoCommands.front());
    g_tobaccoCommands.pop_front();
    return true;
}

void GuiUpdateTobaccoState(std::string_view key, std::string_view value)
{
    const bool enabled = value == "1" || value == "true";
    const std::string text(value);
    const int integer = std::atoi(text.c_str());
    std::scoped_lock lock(g_bridgeMutex);
    if(key == "loaded") g_tobaccoState.loaded = enabled;
    else if(key == "bot") g_tobaccoState.bot = enabled;
    else if(key == "sprint") g_tobaccoState.sprintToStorage = enabled;
    else if(key == "siren") g_tobaccoState.siren = enabled;
    else if(key == "autodisable") g_tobaccoState.autoDisable = enabled;
    else if(key == "autoanswer") g_tobaccoState.autoAnswer = enabled;
    else if(key == "rotation") g_tobaccoState.rotationSpeed = integer;
    else if(key == "answer_delay") g_tobaccoState.answerDelay = integer;
    else if(key == "status") g_tobaccoState.status = value;
    else if(key == "money") g_tobaccoState.money = value;
    else return;
    g_tobaccoDirty = true;
}

void GuiResetTobaccoState()
{
    std::scoped_lock lock(g_bridgeMutex);
    g_tobaccoState = {};
    g_tobaccoCommands.clear();
    g_tobaccoDirty = true;
}
