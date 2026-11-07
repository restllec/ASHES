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
#include "../Shared/ashes_editions.h"
#include "admin_database.h"
#include "theme.h"
#include "tram_icons.h"

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
    bool autoLights{false}; // Авто-включение света (L, K)
    bool silentChat{false}; // Скрыть сообщения бота в чате (тихий режим)
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

struct LesorubState
{
    bool loaded{};
    bool enabled{};
    float pointMatchRadius{35.0f};
    float rotateSpeed{0.10f};
    bool debug{false};
    std::string status{"Отключен"};
};
LesorubState g_lesorubState;
LesorubState g_visibleLesorubState;
std::deque<std::string> g_lesorubCommands;

struct TaxiState
{
    bool loaded{false};
    bool enabled{false};
    int maxDistance{1500};
    int pollInterval{500};
    bool soundAlert{false};
    bool shiftActive{false};
    bool hasOrder{false};
    std::string ordersCount{"0"};
    std::string lastOrder{"нет"};
    std::string status{"Ожидание запуска"};
};
TaxiState g_taxiState;
TaxiState g_visibleTaxiState;
std::deque<std::string> g_taxiCommands;
bool g_taxiDirty{true};

struct TaxiClickerConfig
{
    bool enabled{false};
    std::atomic_bool active{false};
    int loc1X{814};
    int loc1Y{405};
    int loc2X{808};
    int loc2Y{698};
    int delayMs{70};
    int hotkey{'2'};
    bool autoStopOnOrder{true};
    std::string status{"Кликер отключен"};
};
TaxiClickerConfig g_taxiClicker;
char g_targetResource[128]{"province_ac"};
std::uintptr_t g_selectedThread{};
std::uintptr_t g_pendingUnload{};
bool g_luaPending{};
bool g_eventsDirty{};
bool g_threadsDirty{};
bool g_tramDirty{true};
bool g_jbkDirty{true};
bool g_tobaccoDirty{true};
bool g_lesorubDirty{true};
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
    AdminDatabase::Instance().Initialize();
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

void QueueLesorubCommand(std::string command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_lesorubCommands.size() >= 64)
        g_lesorubCommands.pop_front();
    g_lesorubCommands.push_back(std::move(command));
}

void SyncLesorubState()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_lesorubDirty)
        return;
    g_visibleLesorubState = g_lesorubState;
    g_lesorubDirty = false;
}

void QueueTaxiCommand(std::string command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_taxiCommands.size() >= 64)
        g_taxiCommands.pop_front();
    g_taxiCommands.push_back(std::move(command));
}

void SyncTaxiState()
{
    std::scoped_lock lock(g_bridgeMutex);
    if(!g_taxiDirty)
        return;
    g_visibleTaxiState = g_taxiState;
    g_taxiDirty = false;
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
void DrawWoodBot(ImVec2 position, ImVec2 size, float scale);
void DrawTaxiTab(ImVec2 position, ImVec2 size, float scale);

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

static bool s_themeLoaded = false;

void LoadThemeSettings()
{
    if(s_themeLoaded) return;
    std::wstring ini = GetSettingsIniPath();
    wchar_t buf[64]{};
    GetPrivateProfileStringW(L"Theme", L"AccentR", L"0.92", buf, 64, ini.c_str());
    float r = std::wcstof(buf, nullptr);
    GetPrivateProfileStringW(L"Theme", L"AccentG", L"0.18", buf, 64, ini.c_str());
    float g = std::wcstof(buf, nullptr);
    GetPrivateProfileStringW(L"Theme", L"AccentB", L"0.49", buf, 64, ini.c_str());
    float b = std::wcstof(buf, nullptr);
    if(r > 0.0f || g > 0.0f || b > 0.0f)
    {
        g_Theme.accentColor = ImVec4(r, g, b, 1.0f);
        g_Theme.UpdateDerived();
    }
    s_themeLoaded = true;
}

void SaveThemeToConfig()
{
    std::wstring ini = GetSettingsIniPath();
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.3f", g_Theme.accentColor.x);
    WritePrivateProfileStringW(L"Theme", L"AccentR", buf, ini.c_str());
    swprintf_s(buf, L"%.3f", g_Theme.accentColor.y);
    WritePrivateProfileStringW(L"Theme", L"AccentG", buf, ini.c_str());
    swprintf_s(buf, L"%.3f", g_Theme.accentColor.z);
    WritePrivateProfileStringW(L"Theme", L"AccentB", buf, ini.c_str());
}

void ResetThemeSettings()
{
    g_Theme.accentColor = ImVec4(0.92f, 0.18f, 0.49f, 1.0f);
    g_Theme.UpdateDerived();
    SaveThemeToConfig();
}

struct TramRouteInfo {
    const char* label;
    const char* key;
    int subMode; // 0 = Passenger (TramBot), 1 = JTU (jtu_bot), 2 = PolivTram (PolivTram)
    bool requiresPro;
    const char* category;
};

static const TramRouteInfo s_tramRoutes[] = {
    { "Не выбран (IDLE)",                  "",        0, false, nullptr },
    { "Маршрут #3 (Мирный)",               "mirka3",  0, false, "Пассажирские (Мирный)" },
    { "Маршрут #8 (Мирный)",               "mirka8",  0, false, "Пассажирские (Мирный)" },
    { "Маршрут #2 (Приволжск)",            "priva2",  0, false, "Пассажирские (Приволжск)" },
    { "Маршрут #7 (Приволжск)",            "priva7",  0, false, "Пассажирские (Приволжск)" },
    { "Маршрут #8 (Приволжск)",            "priva8",  0, false, "Пассажирские (Приволжск)" },
    { "Маршрут #100 (Невский)",            "neva100", 0, false, "Пассажирские (Невский)" },
    { "ЖТУ (Служебный трамвай)",           "jtu",     1, true,  "Специальные / Служебные" },
    { "Поливочный трамвай (PolivTram)",    "current", 2, true,  "Специальные / Служебные" },
    { "Поливалка (Мирный)",                "mirny",   2, true,  "Специальные / Служебные" },
    { "Поливалка (Невский)",               "nevsky",  2, true,  "Специальные / Служебные" }
};
constexpr int s_numTramRoutes = static_cast<int>(sizeof(s_tramRoutes) / sizeof(s_tramRoutes[0]));

static std::vector<std::string> s_tramLog;

static void AddTramLog(const std::string& msg)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    std::scoped_lock lock(g_bridgeMutex);
    if(s_tramLog.size() >= 100)
        s_tramLog.erase(s_tramLog.begin());
    s_tramLog.push_back(timeBuf + msg);
}

static bool s_tramLegitMouse = true;
static bool s_tramAutoStartOnJob = true;
static int  s_tramDtpReaction = 0;
static bool s_tramDtpSiren = true;
static int  s_tramRayMode = 0;
static bool s_tramSeeVehiclesAndPeds = true;
static bool s_tramSirenBlock20s = true;
static bool s_tramSirenGeneral = true;
static bool s_tramIgnoreNearChat = true;
static bool s_tramPassengerAlert = true;
static bool s_tramTestAdmin = false;
static bool s_tramEchoToChat = false;
static bool s_tramSpaceStop = true;

void LoadTramSettings(int& autoStopMin, int& distMode, bool& autoDist, int& trampDistMode, bool& trampAutoDist, bool& autoLights, bool& silentChat, int& selectedRoute)
{
    std::wstring ini = GetSettingsIniPath();
    autoStopMin = GetPrivateProfileIntW(L"TramBot", L"AutoStopMinutes", 180, ini.c_str());
    distMode = GetPrivateProfileIntW(L"TramBot", L"DistanceMode", 2, ini.c_str());
    autoDist = GetPrivateProfileIntW(L"TramBot", L"AutoDistance", 1, ini.c_str()) != 0;
    autoLights = GetPrivateProfileIntW(L"TramBot", L"AutoLights", 0, ini.c_str()) != 0;
    silentChat = GetPrivateProfileIntW(L"TramBot", L"SilentChat", 0, ini.c_str()) != 0;
    selectedRoute = GetPrivateProfileIntW(L"TramBot", L"SelectedRoute", 0, ini.c_str());
    if(selectedRoute < 0 || selectedRoute >= s_numTramRoutes)
        selectedRoute = 0;
    trampDistMode = GetPrivateProfileIntW(L"PolivTram", L"DistanceMode", 2, ini.c_str());
    trampAutoDist = GetPrivateProfileIntW(L"PolivTram", L"AutoDistance", 1, ini.c_str()) != 0;

    s_tramLegitMouse = GetPrivateProfileIntW(L"TramBot", L"LegitMouse", 1, ini.c_str()) != 0;
    s_tramAutoStartOnJob = GetPrivateProfileIntW(L"TramBot", L"AutoStartOnJob", 1, ini.c_str()) != 0;
    s_tramDtpReaction = GetPrivateProfileIntW(L"TramBot", L"DtpReaction", 0, ini.c_str());
    s_tramDtpSiren = GetPrivateProfileIntW(L"TramBot", L"DtpSiren", 1, ini.c_str()) != 0;
    s_tramRayMode = GetPrivateProfileIntW(L"TramBot", L"RayMode", 0, ini.c_str());
    s_tramSeeVehiclesAndPeds = GetPrivateProfileIntW(L"TramBot", L"SeeVehiclesAndPeds", 1, ini.c_str()) != 0;
    s_tramSirenBlock20s = GetPrivateProfileIntW(L"TramBot", L"SirenBlock20s", 1, ini.c_str()) != 0;
    s_tramSirenGeneral = GetPrivateProfileIntW(L"TramBot", L"SirenGeneral", 1, ini.c_str()) != 0;
    s_tramIgnoreNearChat = GetPrivateProfileIntW(L"TramBot", L"IgnoreNearChat", 1, ini.c_str()) != 0;
    s_tramPassengerAlert = GetPrivateProfileIntW(L"TramBot", L"PassengerAlert", 1, ini.c_str()) != 0;
    s_tramEchoToChat = GetPrivateProfileIntW(L"TramBot", L"EchoToChat", 0, ini.c_str()) != 0;
    s_tramSpaceStop = GetPrivateProfileIntW(L"TramBot", L"SpaceStop", 1, ini.c_str()) != 0;
}

void SaveTramSettings(int autoStopMin, int distMode, bool autoDist, bool autoLights, bool silentChat, int selectedRoute)
{
    std::wstring ini = GetSettingsIniPath();
    WritePrivateProfileStringW(L"TramBot", L"AutoStopMinutes", std::to_wstring(autoStopMin).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"DistanceMode", std::to_wstring(distMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"AutoDistance", autoDist ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"AutoLights", autoLights ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"SilentChat", silentChat ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"SelectedRoute", std::to_wstring(selectedRoute).c_str(), ini.c_str());

    WritePrivateProfileStringW(L"TramBot", L"LegitMouse", s_tramLegitMouse ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"AutoStartOnJob", s_tramAutoStartOnJob ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"DtpReaction", std::to_wstring(s_tramDtpReaction).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"DtpSiren", s_tramDtpSiren ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"RayMode", std::to_wstring(s_tramRayMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"SeeVehiclesAndPeds", s_tramSeeVehiclesAndPeds ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"SirenBlock20s", s_tramSirenBlock20s ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"SirenGeneral", s_tramSirenGeneral ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"IgnoreNearChat", s_tramIgnoreNearChat ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"PassengerAlert", s_tramPassengerAlert ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"EchoToChat", s_tramEchoToChat ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TramBot", L"SpaceStop", s_tramSpaceStop ? L"1" : L"0", ini.c_str());
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
    static int s_selectedTramRouteIndex = 0;
    static bool s_settingsLoaded = false;
    SyncTramState();
    TramState& state = g_visibleTramState;
    if(!s_settingsLoaded)
    {
        LoadTramSettings(state.autoStopMinutes, state.distanceMode, state.autoDistance, state.trampDistanceMode, state.trampAutoDistance, state.autoLights, state.silentChat, s_selectedTramRouteIndex);
        g_tramState.autoStopMinutes = state.autoStopMinutes;
        g_tramState.distanceMode = state.distanceMode;
        g_tramState.autoDistance = state.autoDistance;
        g_tramState.autoLights = state.autoLights;
        g_tramState.silentChat = state.silentChat;
        g_tramState.trampDistanceMode = state.trampDistanceMode;
        g_tramState.trampAutoDistance = state.trampAutoDistance;
        s_settingsLoaded = true;
    }

    const auto& curRoute = s_tramRoutes[s_selectedTramRouteIndex];
    state.subMode = curRoute.subMode;

    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(28, 28, 28, 245));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(48, 48, 48, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(75, 75, 75, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(40, 40, 40, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(65, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, g_Theme.AccentU32());
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##tram_bot", size, ImGuiChildFlags_Borders);

    const bool isPro = IsProLicense();

    // 1. Верхняя панель (Top Header): иконка + заголовок/подзаголовок, селектор маршрута + статус, акцентная кнопка запуска
    const float availW = ImGui::GetContentRegionAvail().x;
    const float actionBtnW = 210.0f * scale;
    const float actionBtnH = 38.0f * scale;

    // Слева: Розовая иконка трамвая + заголовок/подзаголовок
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 topIconPos = ImGui::GetCursorScreenPos();
    TramIcons::DrawTram(dl, ImVec2(topIconPos.x, topIconPos.y + 4.0f * scale), 30.0f * scale, g_Theme.AccentU32());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 36.0f * scale);
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.98f, 0.98f, 1.0f, 1.0f), "TramBot");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f * scale);
    ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "Автоматизация работы трамвая в игре");
    ImGui::EndGroup();

    // По центру: Индикатор состояния
    ImGui::SameLine(0.0f, 24.0f * scale);
    ImGui::BeginGroup();
    ImVec2 pulsePos = ImGui::GetCursorScreenPos();
    TramIcons::DrawPulse(dl, ImVec2(pulsePos.x, pulsePos.y + 6.0f * scale), 18.0f * scale, g_Theme.AccentU32());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f * scale);
    ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.70f, 1.0f), "Состояние:");
    ImGui::SameLine(0.0f, 8.0f * scale);

    const bool isRunning = state.bot;
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 23, 30, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(32, 34, 46, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(44, 46, 60, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, isRunning ? IM_COL32(60, 210, 100, 220) : g_Theme.AccentU32(0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * scale, 4.0f * scale));
    ImGui::Button(isRunning ? " В РАБОТЕ " : " IDLE ");
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::EndGroup();

    // Справа: Большая акцентная розовая/пурпурная кнопка
    ImGui::SameLine();
    const float rightBtnX = availW - actionBtnW;
    if(ImGui::GetCursorPosX() < rightBtnX)
        ImGui::SetCursorPosX(rightBtnX);

    const char* actionBtnLabel = isRunning ? " ⏹  Остановить бота" : " ⏻  Запустить бота";
    const ImU32 actionBtnColor = isRunning ? IM_COL32(215, 55, 55, 255) : g_Theme.AccentU32();
    const ImU32 actionBtnHover = isRunning ? IM_COL32(240, 75, 75, 255) : g_Theme.HoverU32();
    const ImU32 actionBtnActive = isRunning ? IM_COL32(185, 40, 40, 255) : g_Theme.ActiveU32();

    ImGui::PushStyleColor(ImGuiCol_Button, actionBtnColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, actionBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, actionBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
    if(ImGui::Button(actionBtnLabel, ImVec2(actionBtnW, actionBtnH)))
    {
        if(isRunning)
        {
            AddTramLog("[TramBot] Остановка по команде пользователя");
            QueueTramCommand("bot:0");
            QueueTramCommand("jtu_stop");
            QueueTramCommand("tramp_stop");
            QueueTramCommand("force_stop");
            state.bot = false;
        }
        else if(s_selectedTramRouteIndex > 0)
        {
            const auto& sel = s_tramRoutes[s_selectedTramRouteIndex];
            if(sel.requiresPro && !isPro)
            {
                AddTramLog(std::string("[TramBot] Ошибка: режим ") + sel.label + " доступен только в PRO");
            }
            else
            {
                AddTramLog(std::string("[TramBot] Запуск маршрута: ") + sel.label + "...");
                state.subMode = sel.subMode;
                {
                    std::scoped_lock lock(g_bridgeMutex);
                    g_tramState.subMode = sel.subMode;
                    g_visibleTramState.subMode = sel.subMode;
                }
                QueueTramCommand("submode:" + std::to_string(sel.subMode));

                if(sel.subMode == 0)
                {
                    QueueTramCommand("route:" + std::string(sel.key));
                    QueueTramCommand("bot:1");
                }
                else if(sel.subMode == 1)
                {
                    QueueTramCommand("jtu_cycles:" + std::to_string(state.targetCycles));
                    QueueTramCommand(std::string("siren:") + (state.siren ? "1" : "0"));
                    QueueTramCommand("jtu_start");
                }
                else if(sel.subMode == 2)
                {
                    if(sel.key && sel.key[0])
                        QueueTramCommand("tramp_route:" + std::string(sel.key));
                    QueueTramCommand("tramp_cycles:" + std::to_string(state.trampTarget));
                    QueueTramCommand("tramp_brake:" + std::to_string(state.trampBrake));
                    QueueTramCommand(std::string("tramp_traffic:") + (state.trampTraffic ? "1" : "0"));
                    QueueTramCommand(std::string("tramp_autodist:") + (state.trampAutoDistance ? "1" : "0"));
                    QueueTramCommand("tramp_distmode:" + std::to_string(state.trampDistanceMode));
                    QueueTramCommand("tramp_start");
                }
                state.bot = true;
            }
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float contentW = ImGui::GetContentRegionAvail().x;
    const float leftColW = std::floor((contentW - 14.0f * scale) * 0.45f);
    const float rightColW = contentW - leftColW - 14.0f * scale;

    auto BeginCard = [&](const char* id, void (*drawIcon)(ImDrawList*, ImVec2, float, ImU32), const char* title, const char* subtitle, float height = 0.0f) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 14));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 8.0f * scale));
        ImGuiChildFlags childFlags = ImGuiChildFlags_Borders;
        if(height == 0.0f)
            childFlags |= ImGuiChildFlags_AutoResizeY;
        ImGui::BeginChild(id, ImVec2(-1.0f, height), childFlags, ImGuiWindowFlags_NoScrollbar);
        if(title && title[0])
        {
            ImDrawList* curDl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            if(drawIcon)
            {
                drawIcon(curDl, ImVec2(p.x, p.y + 1.0f * scale), 20.0f * scale, g_Theme.AccentU32());
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 26.0f * scale);
            }
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.96f, 0.96f, 0.98f, 1.0f), "%s", title);
            if(subtitle && subtitle[0])
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f * scale);
                ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "%s", subtitle);
            }
            ImGui::EndGroup();
            ImGui::Spacing();
        }
    };

    auto EndCard = [&]() {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
    };

    auto SaveCurrentSettings = [&]() {
        SaveTramSettings(state.autoStopMinutes, state.distanceMode, state.autoDistance, state.autoLights, state.silentChat, s_selectedTramRouteIndex);
    };

    // Левая колонка (45% ширины): 7 карточек параметров и безопасности
    ImGui::BeginChild("##tram_col_left", ImVec2(leftColW, 0.0f), false, ImGuiWindowFlags_NoScrollbar);

    // 1. Карточка «Работа»
    BeginCard("##card_tram_work", TramIcons::DrawGear, "Работа", "Основные настройки запуска и маршрута");
    if(ImGui::Checkbox("Легит: окна и «Да» мышью", &s_tramLegitMouse))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("legit_mouse:") + (s_tramLegitMouse ? "1" : "0"));
    }
    if(ImGui::Checkbox("Выбрал маршрут в игре — бот поехал сам", &s_tramAutoStartOnJob))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("auto_start_job:") + (s_tramAutoStartOnJob ? "1" : "0"));
    }
    ImGui::Spacing();

    const float availWorkW = ImGui::GetContentRegionAvail().x;
    const float takeBtnW = 110.0f * scale;
    const float innerComboW = availWorkW - takeBtnW - 6.0f * scale;
    ImGui::SetNextItemWidth(innerComboW);
    if(ImGui::BeginCombo("##tram_inner_route_combo", curRoute.label))
    {
        const char* lastCat = nullptr;
        for(int i = 0; i < s_numTramRoutes; ++i)
        {
            const auto& r = s_tramRoutes[i];
            if(r.category != nullptr && (lastCat == nullptr || std::strcmp(lastCat, r.category) != 0))
            {
                lastCat = r.category;
                std::string catHeader = std::string("-- ") + lastCat + " --";
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.65f, 1.0f));
                ImGui::Selectable(catHeader.c_str(), false, ImGuiSelectableFlags_Disabled);
                ImGui::PopStyleColor();
                ImGui::Separator();
            }

            const bool isSel = (s_selectedTramRouteIndex == i);
            const float itemPad = (r.category != nullptr) ? (14.0f * scale) : (4.0f * scale);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + itemPad);
            if(ImGui::Selectable(r.label, isSel))
            {
                s_selectedTramRouteIndex = i;
                SaveCurrentSettings();
                if(r.subMode == 2 && r.key && r.key[0])
                {
                    QueueTramCommand(std::string("tramp_route:") + r.key);
                }
            }
            if(isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 6.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Button, g_Theme.AccentU32());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_Theme.HoverU32());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_Theme.ActiveU32());
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if(ImGui::Button("▶ Взять маршрут", ImVec2(takeBtnW, 0.0f)))
    {
        const auto& sel = s_tramRoutes[s_selectedTramRouteIndex];
        if(sel.subMode == 2 && sel.key && sel.key[0])
        {
            QueueTramCommand(std::string("tramp_route:") + sel.key);
        }
        QueueTramCommand("take_route");
    }
    ImGui::PopStyleColor(4);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "Выбор в списке не отправляет команду автоматически");
    EndCard();

    // 2. Карточка «ДТП»
    BeginCard("##card_tram_dtp", TramIcons::DrawCrash, "ДТП", "Действия при аварийных ситуациях");
    static const char* s_dtpOptions[] = { "Встать, постоять и ехать дальше", "Остановка", "Уведомление" };
    const float dtpCheckW = 145.0f * scale;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - dtpCheckW - 8.0f * scale);
    if(ImGui::Combo("##tram_dtp_reaction_combo", &s_tramDtpReaction, s_dtpOptions, 3))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("dtp_reaction:") + std::to_string(s_tramDtpReaction));
    }
    ImGui::SameLine(0, 8.0f * scale);
    if(ImGui::Checkbox("Сирена при ДТП", &s_tramDtpSiren))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("siren_dtp:") + (s_tramDtpSiren ? "1" : "0"));
    }
    EndCard();

    // 3. Карточка «Помехи»
    BeginCard("##card_tram_obstacles", TramIcons::DrawCar, "Помехи", "Реакция на препятствия");
    static const char* s_rayOptions[] = { "Auto", "Manual" };
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.78f, 1.0f), "Лучи:");
    ImGui::SetNextItemWidth(100.0f * scale);
    if(ImGui::Combo("##tram_ray_combo", &s_tramRayMode, s_rayOptions, 2))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("ray_mode:") + std::to_string(s_tramRayMode));
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 14.0f * scale);
    ImGui::BeginGroup();
    if(ImGui::Checkbox("Видеть машины и людей", &s_tramSeeVehiclesAndPeds))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("see_peds_cars:") + (s_tramSeeVehiclesAndPeds ? "1" : "0"));
    }
    if(ImGui::Checkbox("Сирена: путь перекрыт 20 с", &s_tramSirenBlock20s))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("siren_block_20s:") + (s_tramSirenBlock20s ? "1" : "0"));
    }
    ImGui::EndGroup();
    EndCard();

    // 4. Карточка «Тревоги»
    BeginCard("##card_tram_alarms", TramIcons::DrawBell, "Тревоги", "Сигналы и уведомления");
    if(ImGui::BeginTable("##tram_alarms_grid", 2, ImGuiTableFlags_None))
    {
        ImGui::TableNextColumn();
        if(ImGui::Checkbox("Сирена (общая)", &s_tramSirenGeneral))
        {
            SaveCurrentSettings();
            QueueTramCommand(std::string("siren:") + (s_tramSirenGeneral ? "1" : "0"));
        }
        if(ImGui::Checkbox("Кто-то сел в трамвай", &s_tramPassengerAlert))
        {
            SaveCurrentSettings();
            QueueTramCommand(std::string("passenger_alert:") + (s_tramPassengerAlert ? "1" : "0"));
        }

        ImGui::TableNextColumn();
        if(ImGui::Checkbox("Игнорировать чат рядом", &s_tramIgnoreNearChat))
        {
            SaveCurrentSettings();
            QueueTramCommand(std::string("ignore_near_chat:") + (s_tramIgnoreNearChat ? "1" : "0"));
        }
        if(ImGui::Checkbox("Тест админа", &s_tramTestAdmin))
        {
            QueueTramCommand("test_admin");
        }
        ImGui::EndTable();
    }
    EndCard();

    // 5. Карточка «Сообщения»
    BeginCard("##card_tram_msg", TramIcons::DrawChat, "Сообщения", "Уведомления в игровой чат");
    if(ImGui::Checkbox("Дублировать события в чат", &s_tramEchoToChat))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("echo_chat:") + (s_tramEchoToChat ? "1" : "0"));
    }
    EndCard();

    // 6. Карточка «Прочее»
    BeginCard("##card_tram_misc", TramIcons::DrawKeyboard, "Прочее", "Дополнительные настройки");
    if(ImGui::Checkbox("Пробел: мгновенная остановка", &s_tramSpaceStop))
    {
        SaveCurrentSettings();
        QueueTramCommand(std::string("space_stop:") + (s_tramSpaceStop ? "1" : "0"));
    }
    EndCard();

    // 7. Карточка «Отладка»
    BeginCard("##card_tram_dbg", TramIcons::DrawWrench, "Отладка", "Инструменты для диагностики");
    if(ImGui::Checkbox("Отладка", &state.debug))
    {
        QueueTramCommand(std::string("debug:") + (state.debug ? "1" : "0"));
    }
    ImGui::SameLine(0, 16.0f * scale);
    if(ImGui::Button("Тест сирены", ImVec2(105.0f * scale, 0.0f)))
    {
        QueueTramCommand("test_siren");
    }
    ImGui::SameLine(0, 4.0f * scale);
    ImGui::Button(" v ##dbg_dropdown", ImVec2(24.0f * scale, 0.0f));
    EndCard();

    ImGui::EndChild(); // ##tram_col_left

    ImGui::SameLine(0, 10.0f * scale);

    // Правая колонка (55% ширины): Мониторинг и Журнал событий
    ImGui::BeginChild("##tram_col_right", ImVec2(rightColW, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // 1. Карточка «Текущее состояние»
    BeginCard("##card_tram_status", TramIcons::DrawChart, "Текущее состояние", nullptr, 136.0f * scale);
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 145.0f * scale);
        if(state.bot)
            ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "• В работе");
        else
            ImGui::TextColored(g_Theme.accentColor, "• В режиме ожидания");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if(ImGui::BeginTable("##tram_telemetry_table", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##tram_tel_c1", ImGuiTableColumnFlags_WidthStretch, 0.46f);
            ImGui::TableSetupColumn("##tram_tel_c2", ImGuiTableColumnFlags_WidthStretch, 0.54f);

            ImDrawList* curDl = ImGui::GetWindowDrawList();

            // Строка 1: Маршрут | Впереди
            ImGui::TableNextColumn();
            ImVec2 p1 = ImGui::GetCursorScreenPos();
            TramIcons::DrawTram(curDl, ImVec2(p1.x, p1.y + 1.0f * scale), 16.0f * scale, g_Theme.AccentU32());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f * scale);
            ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Маршрут:");
            ImGui::SameLine(0, 6.0f * scale);
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%s", s_selectedTramRouteIndex == 0 ? "не выбран" : curRoute.label);

            ImGui::TableNextColumn();
            ImVec2 p2 = ImGui::GetCursorScreenPos();
            TramIcons::DrawRoad(curDl, ImVec2(p2.x, p2.y + 1.0f * scale), 16.0f * scale, g_Theme.AccentU32());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f * scale);
            ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Впереди:");
            ImGui::SameLine(0, 6.0f * scale);
            ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.95f, 1.0f), "нет данных");

            // Разделитель
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Separator();
            ImGui::TableNextColumn();
            ImGui::Separator();

            // Строка 2: Состояние | Внимание водителя
            ImGui::TableNextColumn();
            ImVec2 p3 = ImGui::GetCursorScreenPos();
            TramIcons::DrawPulse(curDl, ImVec2(p3.x, p3.y + 1.0f * scale), 16.0f * scale, g_Theme.AccentU32());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f * scale);
            ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Состояние:");
            ImGui::SameLine(0, 6.0f * scale);
            ImGui::PushStyleColor(ImGuiCol_Border, state.bot ? IM_COL32(60, 210, 100, 220) : g_Theme.AccentU32(0.85f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * scale, 2.0f * scale));
            const std::string stBadge = state.bot ? (!state.status.empty() ? state.status : "В РАБОТЕ") : "IDLE";
            ImGui::Button(stBadge.c_str());
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImVec2 p4 = ImGui::GetCursorScreenPos();
            TramIcons::DrawEye(curDl, ImVec2(p4.x, p4.y + 1.0f * scale), 16.0f * scale, g_Theme.AccentU32());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f * scale);
            ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Внимание:");
            ImGui::SameLine(0, 6.0f * scale);
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "0.80");

            // Разделитель
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Separator();
            ImGui::TableNextColumn();
            ImGui::Separator();

            // Строка 3: Работа | Заработано
            ImGui::TableNextColumn();
            ImVec2 p5 = ImGui::GetCursorScreenPos();
            TramIcons::DrawGear(curDl, ImVec2(p5.x, p5.y + 1.0f * scale), 16.0f * scale, g_Theme.AccentU32());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f * scale);
            ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Работа:");
            ImGui::SameLine(0, 6.0f * scale);
            ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.95f, 1.0f), "%s", state.bot ? "движение по маршруту" : "ожидание команды");

            ImGui::TableNextColumn();
            ImVec2 p6 = ImGui::GetCursorScreenPos();
            TramIcons::DrawCoins(curDl, ImVec2(p6.x, p6.y + 1.0f * scale), 16.0f * scale, g_Theme.AccentU32());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f * scale);
            if(curRoute.subMode == 2)
            {
                ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Циклы:");
                ImGui::SameLine(0, 6.0f * scale);
                ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.30f, 1.0f), "%d / %d", state.trampCompleted, state.trampTarget);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "Заработано:");
                ImGui::SameLine(0, 6.0f * scale);
                ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.30f, 1.0f), "%s р.", state.money.empty() ? "0" : state.money.c_str());
            }

            ImGui::EndTable();
        }

        if(curRoute.subMode == 1) // ЖТУ
        {
            ImGui::Spacing();
            std::string cycleStr = std::to_string(state.completedCycles) + " / " + std::to_string(state.targetCycles);
            ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.0f, 1.0f), "Выполнено циклов ЖТУ: %s", cycleStr.c_str());
        }
        else if(curRoute.subMode == 2) // PolivTram
        {
            ImGui::Spacing();
            std::string cycleStr = std::to_string(state.trampCompleted) + " / " + std::to_string(state.trampTarget);
            ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.0f, 1.0f), "Выполнено рейсов PolivTram: %s | Установка: %s",
                cycleStr.c_str(), state.trampSprinkler ? "ВКЛЮЧЕНА" : "ВЫКЛЮЧЕНА");
        }
    }
    EndCard();

    // 2. Карточка «Журнал событий»
    BeginCard("##card_tram_log", TramIcons::DrawDoc, "Журнал событий", nullptr, -1.0f);
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 145.0f * scale);
        if(XoneButton("🗑 Очистить локально##log", scale, false, 140.0f * scale, 22.0f))
        {
            std::scoped_lock lock(g_bridgeMutex);
            s_tramLog.clear();
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(12, 13, 18, 220));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 6.0f * scale));

        if(ImGui::BeginChild("##tram_event_log_scroll", ImVec2(-1.0f, -1.0f), true))
        {
            ImDrawList* logDl = ImGui::GetWindowDrawList();
            ImVec2 logPos = ImGui::GetWindowPos();
            ImVec2 logSz = ImGui::GetWindowSize();
            logDl->AddRectFilled(ImVec2(logPos.x + logSz.x - 3.0f * scale, logPos.y + 4.0f * scale),
                                 ImVec2(logPos.x + logSz.x - 1.0f * scale, logPos.y + logSz.y - 4.0f * scale),
                                 g_Theme.AccentU32(), 2.0f * scale);

            std::vector<std::string> logCopy;
            {
                std::scoped_lock lock(g_bridgeMutex);
                logCopy = s_tramLog;
            }

            if(logCopy.empty())
            {
                ImGui::TextColored(ImVec4(0.48f, 0.50f, 0.56f, 1.0f), "Событий пока нет");
            }
            else
            {
                for(const auto& logMsg : logCopy)
                {
                    ImVec4 col(0.78f, 0.80f, 0.85f, 1.0f);
                    if(logMsg.find("Запуск") != std::string::npos)
                        col = ImVec4(0.35f, 0.90f, 0.50f, 1.0f);
                    else if(logMsg.find("Остановка") != std::string::npos)
                        col = ImVec4(1.0f, 0.55f, 0.35f, 1.0f);
                    else if(logMsg.find("Выбран") != std::string::npos)
                        col = g_Theme.accentColor;
                    else if(logMsg.find("Ошибка") != std::string::npos)
                        col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);

                    ImGui::TextColored(col, "%s", logMsg.c_str());
                }
                if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
                    ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
    EndCard();

    ImGui::EndChild(); // ##tram_col_right

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
    ImGui::SetNextItemWidth(85.0f * scale);
    if(!ImGui::InputInt(label, &value, 1, 10))
        return;
    value = std::clamp(value, minimum, maximum);
    QueueJbkCommand(std::string(command) + ":" + std::to_string(value));
}

void JbkFloat(const char* label, float& value, float minimum, float maximum,
    const char* command, float scale)
{
    ImGui::SetNextItemWidth(85.0f * scale);
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
    ImGui::PushStyleColor(ImGuiCol_CheckMark, g_Theme.AccentU32());
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##jbk_bot", size, ImGuiChildFlags_Borders);
    float segW = (ImGui::GetContentRegionAvail().x - 12.0f * scale) / 3.0f;
    if(XoneButton("Завод ЖБК", scale, g_factoryMode == 0, segW, 26.0f)) g_factoryMode = 0;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Табачная фабрика", scale, g_factoryMode == 1, segW, 26.0f)) g_factoryMode = 1;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Лесоруб", scale, g_factoryMode == 2, segW, 26.0f)) g_factoryMode = 2;
    ImGui::Spacing();

    const bool isRunning = state.bot;
    const char* actionBtnLabel = isRunning ? " ⏹  ОСТАНОВИТЬ БОТА (ЖБК)" : " ⏻  ЗАПУСТИТЬ БОТА (ЖБК)";
    const ImU32 actionBtnColor = isRunning ? IM_COL32(215, 55, 55, 255) : g_Theme.AccentU32();
    const ImU32 actionBtnHover = isRunning ? IM_COL32(240, 75, 75, 255) : g_Theme.HoverU32();
    const ImU32 actionBtnActive = isRunning ? IM_COL32(185, 40, 40, 255) : g_Theme.ActiveU32();

    ImGui::PushStyleColor(ImGuiCol_Button, actionBtnColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, actionBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, actionBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    if(ImGui::Button(actionBtnLabel, ImVec2(-1.0f, 38.0f * scale)))
        QueueJbkCommand(isRunning ? "bot:0" : "bot:1");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
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
    ImGui::PushStyleColor(ImGuiCol_CheckMark, g_Theme.AccentU32());
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##tobacco_bot", size, ImGuiChildFlags_Borders);
    float segW = (ImGui::GetContentRegionAvail().x - 12.0f * scale) / 3.0f;
    if(XoneButton("Завод ЖБК", scale, g_factoryMode == 0, segW, 26.0f)) g_factoryMode = 0;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Табачная фабрика", scale, g_factoryMode == 1, segW, 26.0f)) g_factoryMode = 1;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Лесоруб", scale, g_factoryMode == 2, segW, 26.0f)) g_factoryMode = 2;
    ImGui::Spacing();

    const bool isRunning = state.bot;
    const char* actionBtnLabel = isRunning ? " ⏹  ОСТАНОВИТЬ БОТА (Табачка)" : " ⏻  ЗАПУСТИТЬ БОТА (Табачка)";
    const ImU32 actionBtnColor = isRunning ? IM_COL32(215, 55, 55, 255) : g_Theme.AccentU32();
    const ImU32 actionBtnHover = isRunning ? IM_COL32(240, 75, 75, 255) : g_Theme.HoverU32();
    const ImU32 actionBtnActive = isRunning ? IM_COL32(185, 40, 40, 255) : g_Theme.ActiveU32();

    ImGui::PushStyleColor(ImGuiCol_Button, actionBtnColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, actionBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, actionBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    if(ImGui::Button(actionBtnLabel, ImVec2(-1.0f, 38.0f * scale)))
        QueueTobaccoCommand(isRunning ? "bot:0" : "bot:1");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
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
    else if(g_factoryMode == 1)
        DrawTobaccoBot(position, size, scale);
    else
        DrawWoodBot(position, size, scale);
}

void DrawWoodBot(ImVec2 position, ImVec2 size, float scale)
{
    SyncLesorubState();
    LesorubState& state = g_visibleLesorubState;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::PushStyleColor(ImGuiCol_Button, state.enabled ? IM_COL32(160, 40, 40, 245) : IM_COL32(35, 120, 60, 245));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, state.enabled ? IM_COL32(190, 50, 50, 255) : IM_COL32(45, 150, 75, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, state.enabled ? IM_COL32(220, 60, 60, 255) : IM_COL32(55, 180, 90, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(40, 40, 40, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(65, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, g_Theme.AccentU32());
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(80, 80, 80, 180));
    ImGui::BeginChild("##lesorub_bot", size, ImGuiChildFlags_Borders);
    float segW = (ImGui::GetContentRegionAvail().x - 12.0f * scale) / 3.0f;
    if(XoneButton("Завод ЖБК", scale, g_factoryMode == 0, segW, 26.0f)) g_factoryMode = 0;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Табачная фабрика", scale, g_factoryMode == 1, segW, 26.0f)) g_factoryMode = 1;
    ImGui::SameLine(0, 6.0f * scale);
    if(XoneButton("Лесоруб", scale, g_factoryMode == 2, segW, 26.0f)) g_factoryMode = 2;
    ImGui::Spacing();

    const bool isRunning = state.enabled;
    const char* btnLabel = isRunning ? " ⏹  Остановить Лесоруба" : " ⏻  Запустить Лесоруба";
    const ImU32 actionBtnColor = isRunning ? IM_COL32(215, 55, 55, 255) : g_Theme.AccentU32();
    const ImU32 actionBtnHover = isRunning ? IM_COL32(240, 75, 75, 255) : g_Theme.HoverU32();
    const ImU32 actionBtnActive = isRunning ? IM_COL32(185, 40, 40, 255) : g_Theme.ActiveU32();

    ImGui::PushStyleColor(ImGuiCol_Button, actionBtnColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, actionBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, actionBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    if(ImGui::Button(btnLabel, ImVec2(-1.0f, 40.0f * scale)))
    {
        state.enabled = !state.enabled;
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_lesorubState.enabled = state.enabled;
            if(!state.enabled)
                g_lesorubState.status = "Отключен";
        }
        QueueLesorubCommand(state.enabled ? "bot:1" : "bot:0");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::Spacing();

    ImGui::TextColored(state.loaded ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
        state.loaded ? "[LUA ONLINE]" : "[LUA STANDBY]");
    ImGui::SameLine();
    ImGui::Text("| Состояние: %s", state.status.c_str());
    ImGui::Separator();

    ImGui::Text("Настройки поиска (CFG):");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(220.0f * scale);
    if(ImGui::SliderFloat("Радиус поиска метки", &state.pointMatchRadius, 10.0f, 50.0f, "%.1f м"))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_lesorubState.pointMatchRadius = state.pointMatchRadius;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "radius:%.2f", state.pointMatchRadius);
        QueueLesorubCommand(buf);
    }

    ImGui::SetNextItemWidth(220.0f * scale);
    if(ImGui::SliderFloat("Скорость поворота", &state.rotateSpeed, 0.01f, 0.50f, "%.2f"))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_lesorubState.rotateSpeed = state.rotateSpeed;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "rotatespeed:%.3f", state.rotateSpeed);
        QueueLesorubCommand(buf);
    }

    if(ImGui::Checkbox("Режим отладки (Debug лог в чат)", &state.debug))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_lesorubState.debug = state.debug;
        }
        QueueLesorubCommand(state.debug ? "debug:1" : "debug:0");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("На текущем этапе перемещение по меткам автоматизировано.");
    ImGui::TextDisabled("Мини-игры и сруб дерева выполняются вручную.");

    ImGui::EndChild();
    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar();
}


static bool g_adminAutoStop = true;
static bool g_adminBeep = true;
static int g_adminChatType = 1; // 0 = /say, 1 = /b, 2 = random
static int g_adminDelaySec = 3;
static char g_adminTriggers[256] = "вы тут, тут?, ответ в /b, проверка, вы здесь";
static char g_adminAnswers[256] = "тут\nда, тут\nя тут\nздесь я\nда, работаю";

void RenderAdminCheckerHud(float scale)
{
    auto& db = AdminDatabase::Instance();
    if(!db.hudEnabled && !db.hudConfigMode)
        return;

    auto admins = db.GetCurrentServerAdmins();
    int onlineCount = 0;
    for(const auto& a : admins)
    {
        if(a.isOnline || a.isSpectating) onlineCount++;
    }

    if(onlineCount == 0 && !db.hudConfigMode)
        return;

    const bool menuOpen = g_visible.load();
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if(!menuOpen && !db.hudConfigMode)
    {
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    }

    float alpha = db.hudAlpha;
    if(alpha < 0.2f) alpha = 0.2f;
    if(alpha > 1.0f) alpha = 1.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(16, 16, 22, static_cast<int>(240 * alpha)));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(50, 50, 65, static_cast<int>(220 * alpha)));

    if(ImGui::Begin("AdminCheckerHUD", nullptr, flags))
    {
        ImGui::SetWindowFontScale(db.hudScale);
        int s = db.GetServer();

        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "ADMIN CHECKER [Сервер %d]", s);
        if(menuOpen || db.hudConfigMode)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(перемещение)");
        }
        ImGui::Separator();

        if(onlineCount > 0)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Онлайн: %d", onlineCount);
            for(const auto& a : admins)
            {
                if(!a.isOnline && !a.isSpectating) continue;
                ImU32 dotColor = a.isSpectating ? IM_COL32(245, 185, 55, 255) : IM_COL32(80, 225, 120, 255);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f * scale, p.y + 8.0f * scale), 3.5f * scale, dotColor);
                ImGui::Dummy(ImVec2(12.0f * scale, 1.0f));
                ImGui::SameLine();
                ImGui::Text("%s [%s | %d ур.]", a.nick.c_str(), a.role.c_str(), a.level);
            }
        }
        else if(db.hudConfigMode)
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "Режим настройки HUD");
            ImGui::TextDisabled("Перетащите окно в удобное место экрана");
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f), "• Тестовый_Админ [Модератор | 1 ур.]");
        }
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void RenderAdminToast(float scale)
{
    auto& db = AdminDatabase::Instance();
    if(!db.toastEnabled || !db.IsToastActive())
        return;

    AdminToastData toast = db.GetToastData();
    auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - toast.startTime).count();
    if(elapsed >= toast.durationSec)
        return;

    float alpha = 1.0f;
    if(elapsed < 0.3f)
        alpha = elapsed / 0.3f;
    else if(elapsed > (toast.durationSec - 0.8f))
        alpha = (toast.durationSec - elapsed) / 0.8f;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    ImGuiIO& io = ImGui::GetIO();
    float toastW = 390.0f * scale;
    float toastH = 64.0f * scale;
    ImVec2 toastPos((io.DisplaySize.x - toastW) * 0.5f, 38.0f * scale);

    ImGui::SetNextWindowPos(toastPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toastW, toastH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    if(ImGui::Begin("AdminToastAlert", nullptr, flags))
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p0 = toastPos;
        ImVec2 p1(toastPos.x + toastW, toastPos.y + toastH);

        draw->AddRectFilled(p0, p1, IM_COL32(20, 20, 26, static_cast<int>(245 * alpha)), 8.0f * scale);
        draw->AddRect(p0, p1, IM_COL32(60, 60, 75, static_cast<int>(200 * alpha)), 8.0f * scale, 0, 1.0f);

        draw->AddRectFilled(ImVec2(p0.x + 8.0f * scale, p1.y - 3.5f * scale),
            ImVec2(p1.x - 8.0f * scale, p1.y - 1.0f * scale),
            IM_COL32(245, 175, 40, static_cast<int>(255 * alpha)), 2.0f * scale);

        draw->AddCircleFilled(ImVec2(p0.x + 22.0f * scale, p0.y + 26.0f * scale), 6.0f * scale,
            IM_COL32(245, 175, 40, static_cast<int>(240 * alpha)));
        draw->AddCircleFilled(ImVec2(p0.x + 22.0f * scale, p0.y + 26.0f * scale), 2.5f * scale,
            IM_COL32(20, 20, 26, static_cast<int>(255 * alpha)));

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 36.0f * scale, p0.y + 12.0f * scale));
        ImGui::TextColored(ImVec4(0.96f, 0.72f, 0.22f, alpha), "За вами следит администратор");

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 36.0f * scale, p0.y + 32.0f * scale));
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, alpha), "%s (%s | %d ур.)",
            toast.nick.c_str(), toast.role.c_str(), toast.level);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawAdminTab(ImVec2 position, ImVec2 size, float scale)
{
    auto& db = AdminDatabase::Instance();
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##admin_tab", size, ImGuiChildFlags_Borders);

    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "АДМИНИСТРАЦИЯ, ОВЕРЛЕЙ И СИСТЕМА ЗАЩИТЫ");
    ImGui::Separator();

    float availW = ImGui::GetContentRegionAvail().x;
    float colW = (availW - 14.0f * scale) * 0.5f;

    // Левая колонка — База, форум и оверлей (HUD / Toast)
    ImGui::BeginChild("##admin_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "БАЗА ДАННЫХ И ОВЕРЛЕЙ (HUD)");

    int curServer = db.GetServer() - 1;
    const char* serverList[] = {
        "Сервер 1 (185.71.66.80)",
        "Сервер 2 (185.71.66.70)",
        "Сервер 3 (185.71.66.79)",
        "Сервер 4 (185.71.66.64)",
        "Сервер 5 (185.71.66.66)",
        "Сервер 6 (185.71.66.81)",
        "Сервер 7 (Discord / Local)"
    };
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##admin_server", &curServer, serverList, 7))
    {
        db.SetServer(curServer + 1);
    }

    if(XoneButton(db.IsUpdating() ? "Синхронизация..." : "[ ⟳ Обновить базу с форума ]", scale, false, -1.0f, 28.0f))
    {
        db.StartUpdateAsync();
    }

    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "%s", db.GetStatusString().c_str());
    ImGui::Spacing();

    if(XonePlashkaToggle("Включить HUD админчекера", &db.hudEnabled, scale))
        db.SaveSettings();
    if(XonePlashkaToggle("Режим настройки положения HUD", &db.hudConfigMode, scale))
        db.SaveSettings();
    if(XonePlashkaToggle("Всплывающие уведомления (Toast)", &db.toastEnabled, scale))
        db.SaveSettings();

    int alphaPct = static_cast<int>(db.hudAlpha * 100.0f);
    if(XonePlashkaInt("Непрозрачность HUD (%)", &alphaPct, 20, 100, scale))
    {
        db.hudAlpha = static_cast<float>(alphaPct) / 100.0f;
        db.SaveSettings();
    }

    int scalePct = static_cast<int>(db.hudScale * 100.0f);
    if(XonePlashkaInt("Масштаб HUD (%)", &scalePct, 70, 150, scale))
    {
        db.hudScale = static_cast<float>(scalePct) / 100.0f;
        db.SaveSettings();
    }

    if(XoneButton("Тест уведомления о слежке", scale, false, -1.0f, 26.0f))
    {
        db.TriggerToast("Emily_Quincy", "Главный администратор", 7);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Правая колонка — Автоответчик и анти-слежка
    ImGui::BeginChild("##admin_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "АВТООТВЕТЧИК И ПРОВЕРКИ");

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
    ImGui::TextUnformatted("Триггер-слова в чате (через запятую):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##admin_trg", g_adminTriggers, sizeof(g_adminTriggers));
    ImGui::Spacing();

    ImGui::TextUnformatted("Шаблоны ответов (с новой строки):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##admin_ans", g_adminAnswers, sizeof(g_adminAnswers), ImVec2(-1, 80.0f * scale));
    ImGui::Spacing();

    if(XoneButton("Тест сирены / звукового сигнала", scale, false, -1.0f, 26.0f))
    {
        PlayTramAlertSignal();
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

static const char* GetKeyName(int vk);
static bool KeyBindButton(const char* id, int* keyVal, float scale);

static void LoadTaxiSettings()
{
    static bool s_loaded = false;
    if(s_loaded) return;
    std::wstring ini = GetSettingsIniPath();
    g_taxiState.soundAlert = GetPrivateProfileIntW(L"Taxi", L"SoundAlert", 0, ini.c_str()) != 0;
    g_taxiState.enabled = false;
    g_taxiState.maxDistance = GetPrivateProfileIntW(L"Taxi", L"MaxDistance", 1500, ini.c_str());
    g_taxiState.pollInterval = GetPrivateProfileIntW(L"Taxi", L"PollInterval", 500, ini.c_str());
    g_visibleTaxiState.soundAlert = g_taxiState.soundAlert;
    g_visibleTaxiState.enabled = false;
    g_visibleTaxiState.maxDistance = g_taxiState.maxDistance;
    g_visibleTaxiState.pollInterval = g_taxiState.pollInterval;
    s_loaded = true;
}

static void SaveTaxiSettings()
{
    std::wstring ini = GetSettingsIniPath();
    WritePrivateProfileStringW(L"Taxi", L"SoundAlert", g_taxiState.soundAlert ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Taxi", L"MaxDistance", std::to_wstring(g_taxiState.maxDistance).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Taxi", L"PollInterval", std::to_wstring(g_taxiState.pollInterval).c_str(), ini.c_str());
}

static void LoadTaxiClickerSettings()
{
    static bool s_loaded = false;
    if(s_loaded) return;
    std::wstring ini = GetSettingsIniPath();
    g_taxiClicker.enabled = false;
    g_taxiClicker.active.store(false);
    g_taxiClicker.status = "Кликер отключен";
    g_taxiClicker.loc1X = GetPrivateProfileIntW(L"TaxiClicker", L"Loc1X", 814, ini.c_str());
    g_taxiClicker.loc1Y = GetPrivateProfileIntW(L"TaxiClicker", L"Loc1Y", 405, ini.c_str());
    g_taxiClicker.loc2X = GetPrivateProfileIntW(L"TaxiClicker", L"Loc2X", 808, ini.c_str());
    g_taxiClicker.loc2Y = GetPrivateProfileIntW(L"TaxiClicker", L"Loc2Y", 698, ini.c_str());
    g_taxiClicker.delayMs = GetPrivateProfileIntW(L"TaxiClicker", L"DelayMs", 70, ini.c_str());
    g_taxiClicker.hotkey = GetPrivateProfileIntW(L"TaxiClicker", L"Hotkey", '2', ini.c_str());
    g_taxiClicker.autoStopOnOrder = GetPrivateProfileIntW(L"TaxiClicker", L"AutoStop", 1, ini.c_str()) != 0;
    s_loaded = true;
}

static void SaveTaxiClickerSettings()
{
    std::wstring ini = GetSettingsIniPath();
    WritePrivateProfileStringW(L"TaxiClicker", L"Enabled", g_taxiClicker.enabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"Loc1X", std::to_wstring(g_taxiClicker.loc1X).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"Loc1Y", std::to_wstring(g_taxiClicker.loc1Y).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"Loc2X", std::to_wstring(g_taxiClicker.loc2X).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"Loc2Y", std::to_wstring(g_taxiClicker.loc2Y).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"DelayMs", std::to_wstring(g_taxiClicker.delayMs).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"Hotkey", std::to_wstring(g_taxiClicker.hotkey).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"TaxiClicker", L"AutoStop", g_taxiClicker.autoStopOnOrder ? L"1" : L"0", ini.c_str());
}

static std::atomic_bool s_taxiClickerThreadRunning{false};
static int s_captureTarget = 0;
static std::chrono::steady_clock::time_point s_captureStartTime{};

static void SendSimulatedClick(HWND hwnd, int clientX, int clientY)
{
    POINT screenPt{clientX, clientY};
    if(hwnd)
        ClientToScreen(hwnd, &screenPt);

    const int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(vWidth <= 1 || vHeight <= 1)
        return;

    const LONG normX = static_cast<LONG>(((double)(screenPt.x - vLeft) * 65535.0) / (double)(vWidth - 1) + 0.5);
    const LONG normY = static_cast<LONG>(((double)(screenPt.y - vTop) * 65535.0) / (double)(vHeight - 1) + 0.5);
    const DWORD flagsMove = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    const DWORD flagsDown = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    const DWORD flagsUp = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 1. Физическое перемещение курсора в Windows, DirectInput и Raw Input
    INPUT moveInput{};
    moveInput.type = INPUT_MOUSE;
    moveInput.mi.dx = normX;
    moveInput.mi.dy = normY;
    moveInput.mi.dwFlags = flagsMove;
    SendInput(1, &moveInput, sizeof(INPUT));
    mouse_event(flagsMove, normX, normY, 0, 0);

    if(g_setCursorPos)
        g_setCursorPos(screenPt.x, screenPt.y);
    else
        SetCursorPos(screenPt.x, screenPt.y);

    if(hwnd)
    {
        PostMessageA(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(clientX, clientY));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // 2. Нажатие левой кнопки мыши
    INPUT downInput{};
    downInput.type = INPUT_MOUSE;
    downInput.mi.dx = normX;
    downInput.mi.dy = normY;
    downInput.mi.dwFlags = flagsDown;
    SendInput(1, &downInput, sizeof(INPUT));
    mouse_event(flagsDown, normX, normY, 0, 0);

    if(hwnd)
    {
        PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(clientX, clientY));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(18));

    // 3. Отпускание левой кнопки мыши
    INPUT upInput{};
    upInput.type = INPUT_MOUSE;
    upInput.mi.dx = normX;
    upInput.mi.dy = normY;
    upInput.mi.dwFlags = flagsUp;
    SendInput(1, &upInput, sizeof(INPUT));
    mouse_event(flagsUp, normX, normY, 0, 0);

    if(hwnd)
    {
        PostMessageA(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(clientX, clientY));
    }
}

static void TaxiClickerWorker()
{
    s_taxiClickerThreadRunning.store(true);
    while(s_taxiClickerThreadRunning.load())
    {
        if(g_taxiClicker.enabled && g_taxiClicker.active.load())
        {
            if(g_taxiClicker.autoStopOnOrder && (g_taxiState.hasOrder || g_visibleTaxiState.hasOrder))
            {
                g_taxiClicker.active.store(false);
                g_taxiClicker.status = "Заказ пойман! Кликер отключен";
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            HWND hwnd = InputUtil::GameWindow();
            if(hwnd && GetForegroundWindow() == hwnd && !g_visible.load())
            {
                // Клик по Локации 1
                SendSimulatedClick(hwnd, g_taxiClicker.loc1X, g_taxiClicker.loc1Y);

                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, g_taxiClicker.delayMs)));

                if(!g_taxiClicker.active.load())
                    continue;

                if(g_taxiClicker.autoStopOnOrder && (g_taxiState.hasOrder || g_visibleTaxiState.hasOrder))
                {
                    g_taxiClicker.active.store(false);
                    g_taxiClicker.status = "Заказ пойман! Кликер отключен";
                    continue;
                }

                // Клик по Локации 2
                SendSimulatedClick(hwnd, g_taxiClicker.loc2X, g_taxiClicker.loc2Y);

                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, g_taxiClicker.delayMs)));
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
}

static void StartTaxiClickerWorker()
{
    if(!s_taxiClickerThreadRunning.load())
    {
        std::thread(TaxiClickerWorker).detach();
    }
}

static void PulseTaxiClicker()
{
    LoadTaxiClickerSettings();
    StartTaxiClickerWorker();

    static bool s_taxiKeyHeld = false;
    const int key = g_taxiClicker.hotkey > 0 ? g_taxiClicker.hotkey : '2';
    const bool keyDown = (GetAsyncKeyState(key) & 0x8000) != 0;
    if(keyDown && !s_taxiKeyHeld && !g_visible.load())
    {
        s_taxiKeyHeld = true;
        if(g_taxiClicker.enabled)
        {
            const bool next = !g_taxiClicker.active.load();
            g_taxiClicker.active.store(next);
            g_taxiClicker.status = next ? "Кликер активен (поиск заказа)" : "Остановлен клавишей";
            if(next && g_visibleTaxiState.soundAlert)
            {
                PlayTramAlertSignal();
            }
        }
    }
    else if(!keyDown)
    {
        s_taxiKeyHeld = false;
    }

    if(g_taxiClicker.active.load() && g_taxiClicker.autoStopOnOrder)
    {
        if(g_taxiState.hasOrder || g_visibleTaxiState.hasOrder)
        {
            g_taxiClicker.active.store(false);
            g_taxiClicker.status = "Заказ пойман! Кликер отключен";
        }
    }

    if(s_captureTarget > 0)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_captureStartTime).count();
        if(elapsed >= 3000)
        {
            POINT pt;
            if(GetCursorPos(&pt))
            {
                HWND hwnd = InputUtil::GameWindow();
                if(hwnd) ScreenToClient(hwnd, &pt);
                if(s_captureTarget == 1)
                {
                    g_taxiClicker.loc1X = pt.x;
                    g_taxiClicker.loc1Y = pt.y;
                }
                else if(s_captureTarget == 2)
                {
                    g_taxiClicker.loc2X = pt.x;
                    g_taxiClicker.loc2Y = pt.y;
                }
                SaveTaxiClickerSettings();
                PlayTramAlertSignal();
            }
            s_captureTarget = 0;
        }
    }
}

void DrawTaxiTab(ImVec2 position, ImVec2 size, float scale)
{
    LoadTaxiSettings();
    SyncTaxiState();
    TaxiState& taxi = g_visibleTaxiState;

    static bool s_taxiSyncedWithLua = false;
    if(taxi.loaded)
    {
        if(!s_taxiSyncedWithLua)
        {
            QueueTaxiCommand(taxi.soundAlert ? "sound:1" : "sound:0");
            QueueTaxiCommand(std::string("max_dist:") + std::to_string(taxi.maxDistance));
            QueueTaxiCommand(std::string("poll_rate:") + std::to_string(taxi.pollInterval));
            s_taxiSyncedWithLua = true;
        }
    }
    else
    {
        s_taxiSyncedWithLua = false;
    }

    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##taxi_tab", size, ImGuiChildFlags_Borders);

    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "ТАКСИ (AUTO-TAXI CATCHER) — ПЕРЕХВАТ ЗАКАЗОВ");
    ImGui::Separator();

    float availW = ImGui::GetContentRegionAvail().x;
    float colW = (availW - 14.0f * scale) * 0.5f;

    // Левая колонка — Управление сменой и перехватом
    ImGui::BeginChild("##taxi_col_left", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "УПРАВЛЕНИЕ И СМЕНА");
    ImGui::Spacing();

    const bool isRunning = taxi.enabled;
    const char* actionBtnLabel = isRunning ? " ⏹  ОСТАНОВИТЬ АВТОПРИЕМ" : " ⏻  ЗАПУСТИТЬ АВТОПРИЕМ";
    const ImU32 actionBtnColor = isRunning ? IM_COL32(215, 55, 55, 255) : g_Theme.AccentU32();
    const ImU32 actionBtnHover = isRunning ? IM_COL32(240, 75, 75, 255) : g_Theme.HoverU32();
    const ImU32 actionBtnActive = isRunning ? IM_COL32(185, 40, 40, 255) : g_Theme.ActiveU32();

    ImGui::PushStyleColor(ImGuiCol_Button, actionBtnColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, actionBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, actionBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    if(ImGui::Button(actionBtnLabel, ImVec2(-1.0f, 38.0f * scale)))
    {
        taxi.enabled = !taxi.enabled;
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_taxiState.enabled = taxi.enabled;
        }
        QueueTaxiCommand(taxi.enabled ? "enable:1" : "enable:0");
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::Spacing();

    if(XonePlashkaToggle("Автоприем заказов в эфире", &taxi.enabled, scale))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_taxiState.enabled = taxi.enabled;
        }
        QueueTaxiCommand(taxi.enabled ? "enable:1" : "enable:0");
    }

    if(XonePlashkaButton("Смена в такси", taxi.shiftActive ? "В смене (Активна)" : "Выйти на смену", scale, !taxi.shiftActive))
    {
        QueueTaxiCommand("start_shift");
    }

    if(XonePlashkaToggle("Звуковой сигнал при взятии (Beep)", &taxi.soundAlert, scale))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_taxiState.soundAlert = taxi.soundAlert;
        }
        SaveTaxiSettings();
        QueueTaxiCommand(taxi.soundAlert ? "sound:1" : "sound:0");
    }

    if(XonePlashkaButton("Текущий заказ", "Сбросить заказ", scale, false))
    {
        QueueTaxiCommand("reset_order");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "КЛИКЕР ПО КООРДИНАТАМ (БИНД \"2\")");
    ImGui::Spacing();

    if(XonePlashkaToggle("Включить кликер по координатам", &g_taxiClicker.enabled, scale))
    {
        SaveTaxiClickerSettings();
    }

    const bool clickerActive = g_taxiClicker.active.load();
    const char* clickerBtnLabel = clickerActive ? " ⏹  ОСТАНОВИТЬ КЛИКЕР (ИЛИ '2')" : " ⏻  ЗАПУСТИТЬ КЛИКЕР (ИЛИ '2')";
    const ImU32 clickerBtnCol = clickerActive ? IM_COL32(215, 55, 55, 255) : IM_COL32(50, 160, 90, 255);
    const ImU32 clickerBtnHov = clickerActive ? IM_COL32(240, 75, 75, 255) : IM_COL32(65, 185, 110, 255);
    const ImU32 clickerBtnAct = clickerActive ? IM_COL32(185, 40, 40, 255) : IM_COL32(40, 140, 75, 255);

    ImGui::PushStyleColor(ImGuiCol_Button, clickerBtnCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clickerBtnHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, clickerBtnAct);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    if(ImGui::Button(clickerBtnLabel, ImVec2(-1.0f, 32.0f * scale)))
    {
        g_taxiClicker.active.store(!clickerActive);
        g_taxiClicker.status = !clickerActive ? "Кликер активен (поиск заказа)" : "Остановлен пользователем";
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::Spacing();

    if(XonePlashkaToggle("Авто-стоп при поимке заказа", &g_taxiClicker.autoStopOnOrder, scale))
    {
        SaveTaxiClickerSettings();
    }

    XonePlashkaValue("Статус кликера", g_taxiClicker.status.c_str(), scale,
        clickerActive ? IM_COL32(80, 225, 120, 255) : IM_COL32(170, 175, 190, 255));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(taxi.loaded ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
        taxi.loaded ? "● Модуль внедрен в province_taxi" : "○ Ожидание загрузки ресурса такси");
    ImGui::TextColored(taxi.hasOrder ? ImVec4(0.95f, 0.75f, 0.2f, 1.0f) : ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
        taxi.hasOrder ? "Статус: На заказе (опрос на паузе)" : "Статус: Свободен (поиск заказов)");

    ImGui::EndChild();

    ImGui::SameLine();

    // Правая колонка — Параметры фильтрации и мониторинг
    ImGui::BeginChild("##taxi_col_right", ImVec2(colW, 0.0f), ImGuiChildFlags_None);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "ПАРАМЕТРЫ ФИЛЬТРА И ТЕЛЕМЕТРИЯ");
    ImGui::Spacing();

    if(XonePlashkaInt("Макс. дистанция подачи (м)", &taxi.maxDistance, 100, 3000, scale))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_taxiState.maxDistance = taxi.maxDistance;
        }
        SaveTaxiSettings();
        QueueTaxiCommand(std::string("max_dist:") + std::to_string(taxi.maxDistance));
    }

    if(XonePlashkaInt("Интервал опроса заказов (мс)", &taxi.pollInterval, 200, 1500, scale))
    {
        {
            std::scoped_lock lock(g_bridgeMutex);
            g_taxiState.pollInterval = taxi.pollInterval;
        }
        SaveTaxiSettings();
        QueueTaxiCommand(std::string("poll_rate:") + std::to_string(taxi.pollInterval));
    }

    ImGui::Spacing();

    XonePlashkaValue("Заказов в эфире", taxi.ordersCount.c_str(), scale, IM_COL32(200, 220, 255, 255));
    XonePlashkaValue("Последний заказ", taxi.lastOrder.c_str(), scale, IM_COL32(245, 185, 55, 255));
    XonePlashkaValue("Состояние бота", taxi.status.c_str(), scale,
        taxi.enabled ? IM_COL32(80, 225, 120, 255) : IM_COL32(180, 180, 190, 255));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "НАСТРОЙКА КООРДИНАТ ТАКСИ БИНДА");
    ImGui::Spacing();

    char loc1Btn[32] = "Выбрать лок. 1";
    if(s_captureTarget == 1)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_captureStartTime).count();
        int remSec = (int)std::max(0LL, 3LL - (elapsed / 1000));
        std::snprintf(loc1Btn, sizeof(loc1Btn), "Наведите (%dс)...", remSec > 0 ? remSec : 1);
    }
    char loc1Str[64];
    std::snprintf(loc1Str, sizeof(loc1Str), "Локация 1: X = %d, Y = %d", g_taxiClicker.loc1X, g_taxiClicker.loc1Y);
    if(XonePlashkaButton(loc1Str, loc1Btn, scale, s_captureTarget == 1))
    {
        s_captureTarget = 1;
        s_captureStartTime = std::chrono::steady_clock::now();
    }

    char loc2Btn[32] = "Выбрать лок. 2";
    if(s_captureTarget == 2)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_captureStartTime).count();
        int remSec = (int)std::max(0LL, 3LL - (elapsed / 1000));
        std::snprintf(loc2Btn, sizeof(loc2Btn), "Наведите (%dс)...", remSec > 0 ? remSec : 1);
    }
    char loc2Str[64];
    std::snprintf(loc2Str, sizeof(loc2Str), "Локация 2: X = %d, Y = %d", g_taxiClicker.loc2X, g_taxiClicker.loc2Y);
    if(XonePlashkaButton(loc2Str, loc2Btn, scale, s_captureTarget == 2))
    {
        s_captureTarget = 2;
        s_captureStartTime = std::chrono::steady_clock::now();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Ручная подстройка (Лок 1: X/Y  |  Лок 2: X/Y):");
    ImGui::SetNextItemWidth(62.0f * scale);
    if(ImGui::InputInt("##l1_x", &g_taxiClicker.loc1X, 0, 0)) SaveTaxiClickerSettings();
    ImGui::SameLine(0, 4.0f * scale);
    ImGui::SetNextItemWidth(62.0f * scale);
    if(ImGui::InputInt("##l1_y", &g_taxiClicker.loc1Y, 0, 0)) SaveTaxiClickerSettings();
    ImGui::SameLine(0, 10.0f * scale);
    ImGui::SetNextItemWidth(62.0f * scale);
    if(ImGui::InputInt("##l2_x", &g_taxiClicker.loc2X, 0, 0)) SaveTaxiClickerSettings();
    ImGui::SameLine(0, 4.0f * scale);
    ImGui::SetNextItemWidth(62.0f * scale);
    if(ImGui::InputInt("##l2_y", &g_taxiClicker.loc2Y, 0, 0)) SaveTaxiClickerSettings();

    if(XonePlashkaInt("Задержка клика (мс)", &g_taxiClicker.delayMs, 20, 500, scale))
    {
        SaveTaxiClickerSettings();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.85f, 1.0f), "Клавиша бинда (по умолчанию '2'):");
    ImGui::SameLine();
    if(KeyBindButton("##taxi_hotkey_btn", &g_taxiClicker.hotkey, scale))
    {
        SaveTaxiClickerSettings();
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

void DrawPluginModuleTab(ImVec2 position, ImVec2 size, float scale)
{
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##plugin_tab", size, ImGuiChildFlags_Borders);

    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "ВНЕШНИЙ МОДУЛЬ РАСШИРЕНИЯ (PLUGIN MODULE)");
    ImGui::Separator();
    ImGui::Spacing();

    const bool hasPlugin = Editions::HasPluginModule();
    if (!hasPlugin)
    {
        XonePlashkaValue("Статус плагина", "Модуль не установлен в рабочей директории", scale, IM_COL32(235, 120, 60, 255));
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.80f, 1.0f),
            "Библиотека расширения (plugin.dll / extension.dll) не найдена.");
        ImGui::TextWrapped("Поместите файл плагина в директорию с игрой или Ashes для разблокировки функционала.");
        ImGui::Spacing();
        if (XoneButton("ПРОВЕРИТЬ НАЛИЧИЕ МОДУЛЯ", scale, false, 240.0f * scale, 30.0f * scale))
        {
        }
    }
    else
    {
        XonePlashkaValue("Статус плагина", "Модуль обнаружен и активен (plugin.dll)", scale, IM_COL32(80, 225, 120, 255));
        ImGui::Spacing();

        static bool s_pluginEnabled = true;
        static int s_syncRate = 60;
        static float s_intensity = 1.0f;
        static int s_operationMode = 0;
        const char* modes[] = { "Синхронный (DirectX)", "Асинхронный (Фоновый поток)", "Пассивный перехват" };

        ImGui::Checkbox("Включить перехватчик событий плагина", &s_pluginEnabled);
        ImGui::Spacing();

        ImGui::SliderInt("Частота обновления (Гц)", &s_syncRate, 10, 144);
        ImGui::SliderFloat("Интенсивность фильтрации", &s_intensity, 0.1f, 5.0f, "%.1f");
        ImGui::Combo("Режим работы", &s_operationMode, modes, IM_ARRAYSIZE(modes));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (XoneButton("СИНХРОНИЗИРОВАТЬ НАСТРОЙКИ С МОДУЛЕМ", scale, true, -1.0f, 32.0f * scale))
        {
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

struct ClientMiscSettings
{
    bool gameSpeedEnabled = false;
    int gameSpeedKey = VK_OEM_3;
    bool gameSpeedHold = false;
    float gameSpeedMultiplier = 1.0f;

    bool smoothAccelEnabled = false;
    int smoothAccelKey = VK_SPACE;
    bool smoothAccelHold = true;
    float smoothAccelPercent = 50.0f;

    bool brakeEnabled = false;
    int brakeKey = VK_CONTROL;
    bool brakeHold = true;
    float brakePercent = 80.0f;

    bool fpsLimitEnabled = false;
    int fpsLimit = 144;
};

static ClientMiscSettings g_clientMisc;
static bool g_clientMiscLoaded = false;

static void LoadClientMiscSettings()
{
    if (g_clientMiscLoaded) return;
    std::wstring ini = GetSettingsIniPath();
    g_clientMisc.gameSpeedEnabled = GetPrivateProfileIntW(L"Misc", L"GameSpeedEnabled", 0, ini.c_str()) != 0;
    g_clientMisc.gameSpeedKey = GetPrivateProfileIntW(L"Misc", L"GameSpeedKey", VK_OEM_3, ini.c_str());
    g_clientMisc.gameSpeedHold = GetPrivateProfileIntW(L"Misc", L"GameSpeedHold", 0, ini.c_str()) != 0;

    wchar_t buf[64]{};
    GetPrivateProfileStringW(L"Misc", L"GameSpeedMultiplier", L"1.00", buf, 64, ini.c_str());
    g_clientMisc.gameSpeedMultiplier = std::wcstof(buf, nullptr);
    if(g_clientMisc.gameSpeedMultiplier <= 0.0f) g_clientMisc.gameSpeedMultiplier = 1.0f;

    g_clientMisc.smoothAccelEnabled = GetPrivateProfileIntW(L"Misc", L"SmoothAccelEnabled", 0, ini.c_str()) != 0;
    g_clientMisc.smoothAccelKey = GetPrivateProfileIntW(L"Misc", L"SmoothAccelKey", VK_SPACE, ini.c_str());
    g_clientMisc.smoothAccelHold = GetPrivateProfileIntW(L"Misc", L"SmoothAccelHold", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"Misc", L"SmoothAccelPercent", L"50.0", buf, 64, ini.c_str());
    g_clientMisc.smoothAccelPercent = std::wcstof(buf, nullptr);

    g_clientMisc.brakeEnabled = GetPrivateProfileIntW(L"Misc", L"BrakeEnabled", 0, ini.c_str()) != 0;
    g_clientMisc.brakeKey = GetPrivateProfileIntW(L"Misc", L"BrakeKey", VK_CONTROL, ini.c_str());
    g_clientMisc.brakeHold = GetPrivateProfileIntW(L"Misc", L"BrakeHold", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"Misc", L"BrakePercent", L"80.0", buf, 64, ini.c_str());
    g_clientMisc.brakePercent = std::wcstof(buf, nullptr);

    g_clientMisc.fpsLimitEnabled = GetPrivateProfileIntW(L"Misc", L"FpsLimitEnabled", 0, ini.c_str()) != 0;
    g_clientMisc.fpsLimit = GetPrivateProfileIntW(L"Misc", L"FpsLimit", 144, ini.c_str());
    g_clientMiscLoaded = true;
}

static void SaveClientMiscSettings()
{
    std::wstring ini = GetSettingsIniPath();
    WritePrivateProfileStringW(L"Misc", L"GameSpeedEnabled", g_clientMisc.gameSpeedEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"GameSpeedKey", std::to_wstring(g_clientMisc.gameSpeedKey).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"GameSpeedHold", g_clientMisc.gameSpeedHold ? L"1" : L"0", ini.c_str());
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.2f", g_clientMisc.gameSpeedMultiplier);
    WritePrivateProfileStringW(L"Misc", L"GameSpeedMultiplier", buf, ini.c_str());

    WritePrivateProfileStringW(L"Misc", L"SmoothAccelEnabled", g_clientMisc.smoothAccelEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"SmoothAccelKey", std::to_wstring(g_clientMisc.smoothAccelKey).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"SmoothAccelHold", g_clientMisc.smoothAccelHold ? L"1" : L"0", ini.c_str());
    swprintf_s(buf, L"%.1f", g_clientMisc.smoothAccelPercent);
    WritePrivateProfileStringW(L"Misc", L"SmoothAccelPercent", buf, ini.c_str());

    WritePrivateProfileStringW(L"Misc", L"BrakeEnabled", g_clientMisc.brakeEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"BrakeKey", std::to_wstring(g_clientMisc.brakeKey).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"BrakeHold", g_clientMisc.brakeHold ? L"1" : L"0", ini.c_str());
    swprintf_s(buf, L"%.1f", g_clientMisc.brakePercent);
    WritePrivateProfileStringW(L"Misc", L"BrakePercent", buf, ini.c_str());

    WritePrivateProfileStringW(L"Misc", L"FpsLimitEnabled", g_clientMisc.fpsLimitEnabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Misc", L"FpsLimit", std::to_wstring(g_clientMisc.fpsLimit).c_str(), ini.c_str());
}

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

static bool KeyBindButton(const char* id, int* keyVal, float scale)
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
                    SaveClientMiscSettings();
                    SaveTaxiClickerSettings();
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

    XoneThemeColors c = GetCurrentTheme();
    ImGui::PushStyleColor(ImGuiCol_Button, isBinding ? c.accent : c.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isBinding ? c.accent : c.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_Text, isBinding ? IM_COL32(255, 255, 255, 255) : c.textPrimary);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
    if (ImGui::Button(label, ImVec2(105.0f * scale, 24.0f * scale)))
    {
        s_activeBindId = isBinding ? "" : id;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return false;
}

static void ModeSelector(const char* id, bool* isHold, float scale)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
    const bool hold = *isHold;
    XoneThemeColors c = GetCurrentTheme();

    ImGui::PushStyleColor(ImGuiCol_Button, !hold ? c.accent : c.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, !hold ? c.accent : c.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_Text, !hold ? IM_COL32(255, 255, 255, 255) : c.textMuted);
    char b1[48]; std::snprintf(b1, sizeof(b1), "Переключение##%s", id);
    if (ImGui::Button(b1, ImVec2(92.0f * scale, 24.0f * scale)))
    {
        *isHold = false;
        SaveClientMiscSettings();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 4.0f * scale);

    ImGui::PushStyleColor(ImGuiCol_Button, hold ? c.accent : c.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hold ? c.accent : c.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_Text, hold ? IM_COL32(255, 255, 255, 255) : c.textMuted);
    char b2[48]; std::snprintf(b2, sizeof(b2), "Удержание##%s", id);
    if (ImGui::Button(b2, ImVec2(80.0f * scale, 24.0f * scale)))
    {
        *isHold = true;
        SaveClientMiscSettings();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void DrawMiscTab(ImVec2 position, ImVec2 size, float scale)
{
    LoadClientMiscSettings();
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 18, 135));
    ImGui::BeginChild("##misc_tab", size, ImGuiChildFlags_Borders);

    XoneThemeColors c = GetCurrentTheme();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float colW = (availW - 14.0f * scale) * 0.5f;

    // Левая колонка: Движение
    ImGui::BeginGroup();
    {
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "ДВИЖЕНИЕ (ПАРАМЕТРЫ УПРАВЛЕНИЯ)");
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Скорость игры
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.plashkaBg);
        ImGui::BeginChild("##box_speed", ImVec2(colW, 108.0f * scale), ImGuiChildFlags_Borders);
        if(ImGui::Checkbox("Скорость игры", &g_clientMisc.gameSpeedEnabled))
            SaveClientMiscSettings();
        ImGui::SameLine(colW - 195.0f * scale);
        KeyBindButton("##speed_key", &g_clientMisc.gameSpeedKey, scale);
        ImGui::SameLine(0, 4.0f * scale);
        ModeSelector("##speed_mode", &g_clientMisc.gameSpeedHold, scale);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Множитель скорости:");
        ImGui::SetNextItemWidth(colW - 20.0f * scale);
        if(ImGui::SliderFloat("##speed_slider", &g_clientMisc.gameSpeedMultiplier, 0.5f, 2.0f, "%.2fx"))
            SaveClientMiscSettings();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 2. Плавный разгон
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.plashkaBg);
        ImGui::BeginChild("##box_accel", ImVec2(colW, 108.0f * scale), ImGuiChildFlags_Borders);
        if(ImGui::Checkbox("Плавный разгон", &g_clientMisc.smoothAccelEnabled))
            SaveClientMiscSettings();
        ImGui::SameLine(colW - 195.0f * scale);
        KeyBindButton("##accel_key", &g_clientMisc.smoothAccelKey, scale);
        ImGui::SameLine(0, 4.0f * scale);
        ModeSelector("##accel_mode", &g_clientMisc.smoothAccelHold, scale);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Плавность разгона:");
        ImGui::SetNextItemWidth(colW - 20.0f * scale);
        if(ImGui::SliderFloat("##accel_slider", &g_clientMisc.smoothAccelPercent, 0.0f, 100.0f, "%.0f%%"))
            SaveClientMiscSettings();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 3. Тормоз
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.plashkaBg);
        ImGui::BeginChild("##box_brake", ImVec2(colW, 108.0f * scale), ImGuiChildFlags_Borders);
        if(ImGui::Checkbox("Тормоз (Ручник)", &g_clientMisc.brakeEnabled))
            SaveClientMiscSettings();
        ImGui::SameLine(colW - 195.0f * scale);
        KeyBindButton("##brake_key", &g_clientMisc.brakeKey, scale);
        ImGui::SameLine(0, 4.0f * scale);
        ModeSelector("##brake_mode", &g_clientMisc.brakeHold, scale);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Сила торможения:");
        ImGui::SetNextItemWidth(colW - 20.0f * scale);
        if(ImGui::SliderFloat("##brake_slider", &g_clientMisc.brakePercent, 0.0f, 100.0f, "%.0f%%"))
            SaveClientMiscSettings();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 14.0f * scale);

    // Правая колонка: FPS
    ImGui::BeginGroup();
    {
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "FPS (ОГРАНИЧИТЕЛЬ ЧАСТОТЫ КАДРОВ)");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.plashkaBg);
        ImGui::BeginChild("##box_fps", ImVec2(colW, 130.0f * scale), ImGuiChildFlags_Borders);
        if(ImGui::Checkbox("Ограничитель частоты кадров", &g_clientMisc.fpsLimitEnabled))
            SaveClientMiscSettings();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Точная регулировка:");
        ImGui::SetNextItemWidth(colW - 20.0f * scale);
        if(ImGui::SliderInt("##fps_slider", &g_clientMisc.fpsLimit, 30, 300, "%d FPS"))
            SaveClientMiscSettings();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Быстрые пресеты FPS:");
        const int presets[] = { 100, 144, 165, 180, 200 };
        const float pBtnW = (colW - 20.0f * scale - 16.0f * scale) / 5.0f;
        for(int i = 0; i < 5; ++i)
        {
            if(i > 0) ImGui::SameLine(0, 4.0f * scale);
            const bool active = (g_clientMisc.fpsLimit == presets[i]);
            char pId[32]; std::snprintf(pId, sizeof(pId), "%d##p%d", presets[i], i);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? c.accent : c.buttonBg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? c.accent : c.buttonHover);
            ImGui::PushStyleColor(ImGuiCol_Text, active ? IM_COL32(255, 255, 255, 255) : c.textMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
            if(ImGui::Button(pId, ImVec2(pBtnW, 24.0f * scale)))
            {
                g_clientMisc.fpsLimit = presets[i];
                SaveClientMiscSettings();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Подсказка
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.plashkaBg);
        ImGui::BeginChild("##box_fps_info", ImVec2(colW, 85.0f * scale), ImGuiChildFlags_Borders);
        ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.85f, 1.0f),
            "Ограничитель частоты кадров устраняет\n"
            "рассинхронизацию физики и сетевых\n"
            "пакетов MTA Province при резких скачках,\n"
            "а также предотвращает перегрев видеокарты.");
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "ОФОРМЛЕНИЕ И ТЕМА");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.plashkaBg);
        ImGui::BeginChild("##box_theme", ImVec2(colW, 85.0f * scale), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Акцентный цвет:");
        ImGui::SameLine();
        if (ImGui::ColorEdit3("##custom_accent", (float*)&g_Theme.accentColor, 
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) 
        {
            g_Theme.UpdateDerived();
            SaveThemeToConfig();
        }
        ImGui::SameLine(0, 8.0f * scale);
        if (XoneButton("По умолчанию##reset_accent", scale, false, 115.0f * scale, 22.0f))
        {
            ResetThemeSettings();
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Мгновенно обновляет цвет кнопок, чекбоксов и индикаторов.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void RenderMenu()
{
    LoadThemeSettings();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = true;
    constexpr float BaseWidth = 790.0f;
    constexpr float BaseHeight = 485.0f;
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
        {"##tab_factory", "Пешеходы", 4, false},
        {"##tab_pilot", "Pilot", 5, false},
        {"##tab_plow", "Поливалка (Авто)", 6, false},
        {"##tab_taxi", "Такси", 10, false},
        {"##tab_admin", "Админы", 7, true},
        {"##tab_settings", "Настройки", 9, false}
    };
    const bool isPro = IsProLicense();
    const int tabCount = 10;
    for(int i = 0; i < tabCount; ++i)
    {
        ImVec2 tabPos(origin.x + 8.0f * scale, origin.y + (54.0f + i * 33.0f) * scale);
        ImVec2 tabSize(sidebarWidth - 16.0f * scale, 28.0f * scale);
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
    else if(g_activeTab == 9)
    {
        DrawMiscTab(contentPosition, contentSize, scale);
    }
    else if(g_activeTab == 10)
    {
        DrawTaxiTab(contentPosition, contentSize, scale);
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
    PulseTaxiClicker();
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

    const bool menuOpen = g_visible.load();
    auto& db = AdminDatabase::Instance();
    int onlineCount = 0;
    if(db.hudEnabled)
    {
        for(const auto& a : db.GetCurrentServerAdmins())
        {
            if(a.isOnline || a.isSpectating) onlineCount++;
        }
    }
    const bool hudActive = (db.hudEnabled && (onlineCount > 0 || db.hudConfigMode));
    const bool toastActive = db.IsToastActive();

    if(!menuOpen && !hudActive && !toastActive)
    {
        ImGui::GetIO().MouseDrawCursor = false;
        return;
    }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if(!menuOpen)
    {
        ImGui::GetIO().MouseDrawCursor = false;
    }
    if(menuOpen)
    {
        RenderMenu();
    }
    RenderAdminCheckerHud(1.0f);
    RenderAdminToast(1.0f);
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

void GuiAddTramLog(std::string_view msg)
{
    AddTramLog(std::string(msg));
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
    else if(key == "autolights") g_tramState.autoLights = enabled;
    else if(key == "silent_chat") g_tramState.silentChat = enabled;
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

bool GuiTakeLesorubCommand(std::string& command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_lesorubCommands.empty())
        return false;
    command = std::move(g_lesorubCommands.front());
    g_lesorubCommands.pop_front();
    return true;
}

void GuiUpdateLesorubState(std::string_view key, std::string_view value)
{
    const bool enabled = value == "1" || value == "true";
    std::scoped_lock lock(g_bridgeMutex);
    if(key == "loaded") g_lesorubState.loaded = enabled;
    else if(key == "enabled") g_lesorubState.enabled = enabled;
    else if(key == "status") g_lesorubState.status = value;
    else if(key == "radius") g_lesorubState.pointMatchRadius = static_cast<float>(std::atof(value.data()));
    else if(key == "rotatespeed") g_lesorubState.rotateSpeed = static_cast<float>(std::atof(value.data()));
    else if(key == "debug") g_lesorubState.debug = enabled;
    else return;
    g_lesorubDirty = true;
}

void GuiResetLesorubState()
{
    std::scoped_lock lock(g_bridgeMutex);
    g_lesorubState = {};
    g_lesorubCommands.clear();
    g_lesorubDirty = true;
}

bool GuiTakeTaxiCommand(std::string& command)
{
    std::scoped_lock lock(g_bridgeMutex);
    if(g_taxiCommands.empty())
        return false;
    command = std::move(g_taxiCommands.front());
    g_taxiCommands.pop_front();
    return true;
}

void GuiUpdateTaxiState(std::string_view key, std::string_view value)
{
    const bool enabled = value == "1" || value == "true";
    std::scoped_lock lock(g_bridgeMutex);
    if(key == "loaded") g_taxiState.loaded = enabled;
    else if(key == "enabled") g_taxiState.enabled = enabled;
    else if(key == "status") g_taxiState.status = value;
    else if(key == "orders_count") g_taxiState.ordersCount = value;
    else if(key == "last_order") g_taxiState.lastOrder = value;
    else if(key == "shift_active") g_taxiState.shiftActive = enabled;
    else if(key == "has_order") g_taxiState.hasOrder = enabled;
    else if(key == "sound" || key == "sound_alert") g_taxiState.soundAlert = enabled;
    else if(key == "max_dist") g_taxiState.maxDistance = std::atoi(std::string(value).c_str());
    else if(key == "poll_rate") g_taxiState.pollInterval = std::atoi(std::string(value).c_str());
    else return;
    g_taxiDirty = true;
}

void GuiResetTaxiState()
{
    std::scoped_lock lock(g_bridgeMutex);
    const bool savedSound = g_taxiState.soundAlert;
    const int savedDist = g_taxiState.maxDistance;
    const int savedPoll = g_taxiState.pollInterval;
    g_taxiState = {};
    g_taxiState.soundAlert = savedSound;
    g_taxiState.maxDistance = savedDist;
    g_taxiState.pollInterval = savedPoll;
    g_taxiCommands.clear();
    g_taxiDirty = true;
}
