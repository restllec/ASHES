#include "forensic_hooks.h"
#include "gui.h"
#include "license_manager.h"
#include "logger.h"
#include "lua_args_hook.h"
#include "memory_module_dumper.h"
#include "module_utils.h"
#include "netc_hooks.h"
#include "privacy_hooks.h"
#include "script_dumper.h"
#include "../Shared/bootstrap_protocol.h"
#include "../Shared/crash_handler.h"
#include "../Shared/environment.h"

#include <Windows.h>
#include <MinHook.h>
#include <winternl.h>

#include <atomic>
#include <string>
#include <string_view>

namespace
{
using LdrLoadDllFn = NTSTATUS(NTAPI*)(PWSTR, ULONG, PUNICODE_STRING, PHANDLE);
using LdrUnloadDllFn = NTSTATUS(NTAPI*)(HMODULE);

constexpr NTSTATUS DllInitFailed = static_cast<NTSTATUS>(0xC0000142L);

HMODULE g_module{};
void* g_ldrLoadDllTarget{};
LdrLoadDllFn g_ldrLoadDll{};
void* g_ldrUnloadDllTarget{};
LdrUnloadDllFn g_ldrUnloadDll{};
std::atomic<HMODULE> g_clientModule{};
std::atomic<HMODULE> g_netcModule{};
std::wstring g_loaderDirectory;
bool g_antiShadow{true};
bool g_scriptsDumper{};
bool g_setSerial{true};
bool g_randomSerial{};
std::string g_publicSerial{"E97AEE3B69AAE6B8E699D851A6289902"};

bool ConfigFlag(const wchar_t* name, bool fallback)
{
    const std::wstring value = Environment::Read(name);
    if(value.empty())
        return fallback;
    return value == L"1";
}

std::string Ascii(std::wstring_view value)
{
    std::string output;
    output.reserve(value.size());
    for(wchar_t ch : value)
    {
        if(ch > 0x7F)
            return {};
        output.push_back(static_cast<char>(ch));
    }
    return output;
}

void ScheduleClientDump(HMODULE client)
{
    if(!client || g_loaderDirectory.empty())
    {
        Log::Write(L"[dump] client.dll dump skipped: loader directory unavailable");
        return;
    }
    const std::wstring path = g_loaderDirectory + L"\\client.unpacked.dll";
    if(MemoryModuleDumper::Schedule(client, path))
        Log::Write(L"[dump] client.dll memory dump scheduled in 5 seconds");
    else
        Log::Write(L"[dump] client.dll memory dump scheduling failed");
}

bool IsLoadedModule(const UNICODE_STRING* moduleName, std::wstring_view expected)
{
    if (!moduleName || !moduleName->Buffer || !moduleName->Length)
        return false;
    const std::wstring_view name(moduleName->Buffer, moduleName->Length / sizeof(wchar_t));
    return ModuleUtils::HasFileName(name, expected);
}

void HandleClientLoaded(HMODULE client)
{
    if(!ModuleUtils::IsExecutableImage(client))
        return;
    const HMODULE previous = g_clientModule.exchange(client,
        std::memory_order_acq_rel);
    if(g_antiShadow)
        UpdatePrivacyClient(client);
    if(previous != client)
    {
        Log::Write(L"[loader] client.dll found");
        ScheduleClientDump(client);
    }
    if(!StartLuaArgsHook(client, g_loaderDirectory))
       Log::Write(L"[lua-hook] client hook startup failed");
}

NTSTATUS NTAPI HookLdrLoadDll(PWSTR searchPath, ULONG flags,
    PUNICODE_STRING moduleName, PHANDLE moduleHandle)
{
    const NTSTATUS status = g_ldrLoadDll(searchPath, flags, moduleName, moduleHandle);
    if (status < 0)
        return status;

    if(IsLoadedModule(moduleName, L"client.dll"))
    {
        const HMODULE client = moduleHandle
            ? static_cast<HMODULE>(*moduleHandle) : GetModuleHandleW(L"client.dll");
        if(ModuleUtils::IsExecutableImage(client))
            HandleClientLoaded(client);
        else
            Log::Write(L"[loader] ignored non-executable client.dll mapping");
    }

    if (IsLoadedModule(moduleName, L"netc.dll"))
    {
        const HMODULE netc = moduleHandle ? static_cast<HMODULE>(*moduleHandle) : nullptr;
        if(!ModuleUtils::IsExecutableImage(netc))
        {
            Log::Write(L"[loader] ignored non-executable netc.dll mapping");
            return status;
        }
        if (!InstallNetcHooks(netc))
        {
            Log::Write(L"[bootstrap] netc hooks failed; continuing without net hooks to avoid 1114");
        }
        else
        {
            g_netcModule.store(netc, std::memory_order_release);
            Log::Write(L"[bootstrap] netc hooks ready before LdrLoadDll returned");
        }
    }
    return status;
}

NTSTATUS NTAPI HookLdrUnloadDll(HMODULE module)
{
    const bool trackedClient = module
        && g_clientModule.load(std::memory_order_acquire) == module;
    const bool trackedNetc = module
        && g_netcModule.load(std::memory_order_acquire) == module;
    const NTSTATUS status = g_ldrUnloadDll(module);
    if(status < 0)
        return status;

    if(trackedClient)
    {
        const HMODULE live = GetModuleHandleW(L"client.dll");
        if(live == module && ModuleUtils::IsExecutableImage(live))
        {
            static std::atomic_uint32_t ignored{};
            if(ignored.fetch_add(1, std::memory_order_relaxed) < 4)
                Log::Write(L"[loader] client.dll reference released; executable mapping retained");
        }
        else
        {
            if(g_antiShadow)
                UpdatePrivacyClient(nullptr);
            g_clientModule.store(nullptr, std::memory_order_release);
            Log::Write(L"[loader] executable client.dll mapping removed");
        }
    }
    if(trackedNetc)
    {
        const HMODULE live = GetModuleHandleW(L"netc.dll");
        if(live != module || !ModuleUtils::IsExecutableImage(live))
            g_netcModule.store(nullptr, std::memory_order_release);
    }
    return status;
}

bool InstallLoaderHook()
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    g_ldrLoadDllTarget = ntdll
        ? reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrLoadDll")) : nullptr;
    g_ldrUnloadDllTarget = ntdll
        ? reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrUnloadDll")) : nullptr;
    if(!g_ldrLoadDllTarget || !g_ldrUnloadDllTarget)
        return false;

    const MH_STATUS loadCreated = MH_CreateHook(g_ldrLoadDllTarget,
        reinterpret_cast<void*>(&HookLdrLoadDll), reinterpret_cast<void**>(&g_ldrLoadDll));
    if(loadCreated != MH_OK)
        return false;
    const MH_STATUS unloadCreated = MH_CreateHook(g_ldrUnloadDllTarget,
        reinterpret_cast<void*>(&HookLdrUnloadDll),
        reinterpret_cast<void**>(&g_ldrUnloadDll));
    if(unloadCreated != MH_OK)
    {
        MH_RemoveHook(g_ldrLoadDllTarget);
        return false;
    }
    if(MH_QueueEnableHook(g_ldrLoadDllTarget) == MH_OK
        && MH_QueueEnableHook(g_ldrUnloadDllTarget) == MH_OK
        && MH_ApplyQueued() == MH_OK)
    {
        return true;
    }

    MH_RemoveHook(g_ldrLoadDllTarget);
    MH_RemoveHook(g_ldrUnloadDllTarget);
    g_ldrLoadDllTarget = nullptr;
    g_ldrLoadDll = nullptr;
    g_ldrUnloadDllTarget = nullptr;
    g_ldrUnloadDll = nullptr;
    return false;
}

void RemoveLoaderHook()
{
    if(g_ldrLoadDllTarget)
    {
        MH_DisableHook(g_ldrLoadDllTarget);
        MH_RemoveHook(g_ldrLoadDllTarget);
    }
    if(g_ldrUnloadDllTarget)
    {
        MH_DisableHook(g_ldrUnloadDllTarget);
        MH_RemoveHook(g_ldrUnloadDllTarget);
    }
    g_ldrLoadDllTarget = nullptr;
    g_ldrLoadDll = nullptr;
    g_ldrUnloadDllTarget = nullptr;
    g_ldrUnloadDll = nullptr;
}

DWORD WINAPI HookWatchdog(void*)
{
    for(;;)
    {
        HMODULE client = GetModuleHandleW(L"client.dll");
        if(!ModuleUtils::IsExecutableImage(client))
            client = nullptr;
        const HMODULE tracked = g_clientModule.load(std::memory_order_acquire);
        if(client)
        {
            if(client != tracked)
                HandleClientLoaded(client);
            else
            {
                if(g_antiShadow)
                    UpdatePrivacyClient(client);
                if(!RepairLuaArgsHook(client))
                   StartLuaArgsHook(client, g_loaderDirectory);
            }
        }
        else if(tracked)
        {
            if(g_antiShadow)
                UpdatePrivacyClient(nullptr);
            g_clientModule.store(nullptr, std::memory_order_release);
        }

        HMODULE netc = GetModuleHandleW(L"netc.dll");
        if(!ModuleUtils::IsExecutableImage(netc))
            netc = nullptr;
        if(netc)
        {
            g_netcModule.store(netc, std::memory_order_release);
            InstallNetcHooks(netc);
        }
        if(g_antiShadow)
            RepairPrivacyHooks();
        Sleep(1000);
    }
}

bool StartHookWatchdog()
{
    HANDLE thread = CreateThread(nullptr, 0, &HookWatchdog, nullptr, 0, nullptr);
    if(!thread)
        return false;
    CloseHandle(thread);
    return true;
}

bool InitializeRuntime()
{
    Log::Initialize(g_module);

    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        Log::Write(L"[bootstrap] MinHook initialization failed");
        return false;
    }
    if(!ConfigureNetcHooks(g_setSerial, g_randomSerial, g_publicSerial))
    {
        Log::Write(L"[serial] invalid PUBLIC_SERIAL configuration");
        return false;
    }
    if(!ConfigureScriptDumper(g_scriptsDumper, g_loaderDirectory))
    {
        Log::Write(L"[scripts-dumper] invalid output configuration");
        return false;
    }
    if (!InstallLoaderHook())
    {
        Log::Write(L"[bootstrap] LdrLoadDll hook failed");
        MH_Uninitialize();
        return false;
    }

    if(!StartGui(g_module))
        Log::Write(L"[gui] startup failed");

    if(!StartForensicHooks(GetModuleHandleW(L"client.dll"), g_loaderDirectory))
        Log::Write(L"[process-filter] early hook startup failed");

    const HMODULE client = GetModuleHandleW(L"client.dll");
    if(g_antiShadow)
    {
        if(!StartPrivacyHooks(client))
            Log::Write(L"[privacy] hook startup failed");
    }
    else
    {
        Log::Write(L"[privacy] disabled by ANTI_SHADOW=0");
    }

    const HMODULE netc = GetModuleHandleW(L"netc.dll");
    if (netc && !InstallNetcHooks(netc))
    {
        Log::Write(L"[bootstrap] preloaded netc hook installation failed (non-fatal)");
    }

    if(!StartScriptDumperWorkers())
    {
        Log::Write(L"[scripts-dumper] worker startup failed (non-fatal)");
    }

    if(client)
        HandleClientLoaded(client);

    if(!StartHookWatchdog())
        Log::Write(L"[bootstrap] hook watchdog startup failed");

    Log::Write(L"[bootstrap] AshesClient ready before gta_sa.exe resume");
    return true;
}

void WriteInitLog(const wchar_t* message)
{
    std::wstring logPath;
    if (!g_loaderDirectory.empty() && g_loaderDirectory != L".")
        logPath = g_loaderDirectory + L"\\ashes_init.log";
    else
        logPath = L"ashes_init.log";

    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        file = CreateFileW(L"ashes_init.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file != INVALID_HANDLE_VALUE)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t timeBuf[64]{};
        swprintf_s(timeBuf, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        std::wstring line = std::wstring(timeBuf) + message + L"\r\n";
        int lenA = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (lenA > 0)
        {
            std::string lineA(lenA, '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, lineA.data(), lenA, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(file, lineA.data(), static_cast<DWORD>(lineA.size() - 1), &written, nullptr);
        }
        CloseHandle(file);
    }
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void* reserved)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    WriteInitLog(L"[DllMain] entered DLL_PROCESS_ATTACH");

    try
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        Log::Initialize(module);
        Log::Write(L"[client] DllMain attached");

        g_loaderDirectory = Environment::Read(BootstrapProtocol::LogDirectoryVariable);
        std::wstring clientPath = Environment::Read(BootstrapProtocol::ClientPathVariable);
        std::wstring loadedEventName = Environment::Read(BootstrapProtocol::ClientLoadedEventVariable);
        g_antiShadow = ConfigFlag(BootstrapProtocol::AntiShadowVariable, true);
        g_scriptsDumper = ConfigFlag(BootstrapProtocol::ScriptsDumperVariable, false);
        g_setSerial = ConfigFlag(BootstrapProtocol::SetSerialVariable, true);
        g_randomSerial = ConfigFlag(BootstrapProtocol::RandomSerialVariable, false);
        const std::wstring publicSerial = Environment::Read(BootstrapProtocol::PublicSerialVariable);
        if(!publicSerial.empty())
            g_publicSerial = Ascii(publicSerial);

        if(reserved)
        {
            const auto& data = *static_cast<const BootstrapProtocol::Data*>(reserved);
            if(data.magic == BootstrapProtocol::DataMagic && data.version == BootstrapProtocol::DataVersion)
            {
                if(data.logDirectory[0]) g_loaderDirectory = data.logDirectory;
                if(data.clientPath[0]) clientPath = data.clientPath;
                if(data.clientLoadedEvent[0]) loadedEventName = data.clientLoadedEvent;
                g_antiShadow = data.antiShadow != 0;
                g_scriptsDumper = data.scriptsDumper != 0;
                g_setSerial = data.setSerial != 0;
                g_randomSerial = data.randomSerial != 0;
                if(data.publicSerial[0]) g_publicSerial = Ascii(data.publicSerial);
            }
        }

        if(clientPath.empty())
            clientPath = L"AshesClient.dll";

        if(g_loaderDirectory.empty() || g_loaderDirectory == L".")
        {
            const auto separator = clientPath.find_last_of(L"\\/");
            if(separator != std::wstring::npos)
                g_loaderDirectory = clientPath.substr(0, separator);
            else
            {
                wchar_t modPath[MAX_PATH]{};
                if(GetModuleFileNameW(module, modPath, MAX_PATH))
                {
                    const auto sep = std::wstring_view(modPath).find_last_of(L"\\/");
                    if(sep != std::wstring_view::npos)
                        g_loaderDirectory.assign(modPath, sep);
                }
                if(g_loaderDirectory.empty())
                    g_loaderDirectory = L".";
            }
        }

        WriteInitLog((L"[DllMain] directories resolved: loader=" + g_loaderDirectory + L", client=" + clientPath).c_str());

        try
        {
            SetLicenseDirectory(g_loaderDirectory);
            InitializeLicense();
            WriteInitLog(L"[DllMain] InitializeLicense() completed");
        }
        catch(const std::exception& ex)
        {
            std::string err = ex.what();
            WriteInitLog((L"[DllMain] InitializeLicense() exception: " + std::wstring(err.begin(), err.end())).c_str());
        }
        catch(...)
        {
            WriteInitLog(L"[DllMain] InitializeLicense() unknown exception caught");
        }

        Environment::Clear(BootstrapProtocol::LogDirectoryVariable);
        Environment::Clear(BootstrapProtocol::AgentPathVariable);
        Environment::Clear(BootstrapProtocol::ClientPathVariable);
        Environment::Clear(BootstrapProtocol::AgentReadyEventVariable);
        Environment::Clear(BootstrapProtocol::ClientLoadedEventVariable);
        Environment::Clear(BootstrapProtocol::AntiShadowVariable);
        Environment::Clear(BootstrapProtocol::ScriptsDumperVariable);
        Environment::Clear(BootstrapProtocol::SetSerialVariable);
        Environment::Clear(BootstrapProtocol::RandomSerialVariable);
        Environment::Clear(BootstrapProtocol::PublicSerialVariable);

        if (!CrashHandler::Install(module, clientPath, L"AshesClient", g_loaderDirectory))
        {
            Log::Write(L"[client] crash handler initialization failed");
            WriteInitLog(L"[DllMain] CrashHandler::Install failed (non-fatal)");
        }
        else
        {
            Log::Write(L"[client] crash handler ready");
            WriteInitLog(L"[DllMain] CrashHandler::Install ready");
        }

        if (InitializeRuntime())
        {
            if(!loadedEventName.empty())
            {
                HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, loadedEventName.c_str());
                if(event)
                {
                    SetEvent(event);
                    CloseHandle(event);
                }
            }
            WriteInitLog(L"[DllMain] InitializeRuntime succeeded, returning TRUE");
            return TRUE;
        }

        Log::Write(L"[client] runtime initialization failed");
        WriteInitLog(L"[DllMain] InitializeRuntime failed, returning TRUE to prevent 1114 crash");
        return TRUE;
    }
    catch(const std::exception& ex)
    {
        std::string err = ex.what();
        WriteInitLog((L"[DllMain] fatal exception caught: " + std::wstring(err.begin(), err.end())).c_str());
        return TRUE;
    }
    catch(...)
    {
        WriteInitLog(L"[DllMain] fatal unknown exception caught, returning TRUE");
        return TRUE;
    }
}
