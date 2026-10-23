#include "launcher.h"

#include "bootstrap_ipc.h"
#include "config.h"
#include "path_utils.h"
#include "loader_resources.h"
#include "../Shared/manual_map.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <string>

namespace
{
constexpr wchar_t AgentName[] = L"AshesAgent.dll";
constexpr wchar_t ClientName[] = L"AshesClient.dll";
constexpr wchar_t LegacyAgentName[] = L"DarkFlameAgent.dll";
constexpr wchar_t LegacyClientName[] = L"DarkFlameClient.dll";
constexpr const wchar_t* GameTargets[] = {
    L"proxy_sa.exe",
    L"gta_sa.exe",
    L"Multi Theft Auto.exe"
};
constexpr DWORD PollIntervalMs = 50;

struct ProcessHandle
{
    HANDLE value{};

    ~ProcessHandle()
    {
        if(value)
            CloseHandle(value);
    }
};

std::wstring ErrorText(std::wstring_view prefix, DWORD error)
{
    return std::wstring(prefix) + std::to_wstring(error);
}

std::wstring ResolvePayloadPath(const std::wstring& ownDirectory, const std::wstring& filename,
    const std::wstring& legacyFilename)
{
    const std::wstring direct = ownDirectory + L"\\" + filename;
    if(LoaderPath::FileExists(direct))
        return direct;
    const std::wstring legacy = ownDirectory + L"\\" + legacyFilename;
    if(LoaderPath::FileExists(legacy))
        return legacy;
    return direct;
}

DWORD FindGameProcess(std::wstring* outName = nullptr)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD processId{};
    if(Process32FirstW(snapshot, &entry))
    {
        do
        {
            for(const wchar_t* target : GameTargets)
            {
                if(!_wcsicmp(entry.szExeFile, target))
                {
                    processId = entry.th32ProcessID;
                    if(outName)
                        *outName = entry.szExeFile;
                    break;
                }
            }
            if(processId)
                break;
        } while(Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processId;
}

HANDLE OpenGameProcess(DWORD processId)
{
    constexpr DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE;
    return OpenProcess(access, FALSE, processId);
}

std::wstring GetProcessExeName(DWORD processId)
{
    std::wstring result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if(Process32FirstW(snapshot, &entry))
        {
            do
            {
                if(entry.th32ProcessID == processId)
                {
                    result = entry.szExeFile;
                    break;
                }
            } while(Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return result;
}
}

Launcher::ProcessInfo Launcher::CheckProcess()
{
    ProcessInfo info{};
    info.pid = FindGameProcess(&info.exeName);
    info.found = (info.pid != 0);
    return info;
}

int Launcher::Run(std::stop_token stop, const LogSink& log)
{
    const std::wstring ownDirectory = LoaderPath::OwnDirectory();
    const std::wstring agent = ResolvePayloadPath(ownDirectory, AgentName, LegacyAgentName);
    const std::wstring client = ResolvePayloadPath(ownDirectory, ClientName, LegacyClientName);
    if(!LoaderPath::FileExists(agent) || !LoaderPath::FileExists(client))
    {
        log(L"[error] required file is missing");
        if(!LoaderPath::FileExists(agent))
            log(L"[error] agent: " + agent);
        if(!LoaderPath::FileExists(client))
            log(L"[error] client: " + client);
        return 2;
    }

    log(L"[info] waiting for target process (safe poll)...");
    ProcessHandle process;
    DWORD processId{};
    std::wstring exeName;
    while(!stop.stop_requested())
    {
        processId = FindGameProcess(&exeName);
        if(processId)
            process.value = OpenGameProcess(processId);
        if(process.value)
            break;
        Sleep(PollIntervalMs);
    }
    if(stop.stop_requested())
    {
        log(L"[info] agent loading cancelled");
        return 0;
    }

    log(L"[info] target process found: " + exeName + L" (PID "
        + std::to_wstring(processId) + L")");
    const DarkFlameConfig config = Config::Load(ownDirectory);
    log(config.antiShadow ? L"[config] Anti-Shadow enabled"
        : L"[config] Anti-Shadow disabled");
    log(config.scriptsDumper ? L"[config] Scripts Dumper enabled"
        : L"[config] Scripts Dumper disabled");
    log(config.setSerial ? L"[config] Black Mirror enabled"
        : L"[config] Black Mirror disabled");
    log(config.randomSerial ? L"[config] Random Serial enabled"
        : L"[config] Random Serial disabled");

    BootstrapIpc ipc(ownDirectory, agent, client, config);
    if(!ipc.Valid())
    {
        log(ErrorText(L"[error] bootstrap setup failed: ", ipc.Error()));
        return 3;
    }
    const auto& payload = ipc.Payload();
    bool exceptionSupport{};
    bool injected = false;
    DWORD injectionError = 0;

    const bool isGameProcess = (exeName.find(L"proxy_sa") != std::wstring::npos
        || exeName.find(L"gta_sa") != std::wstring::npos);

    if(isGameProcess)
    {
        log(L"[info] waiting for game window and display stabilization...");
        HWND gameHwnd = nullptr;
        for(int attempt = 0; attempt < 150 && !stop.stop_requested(); ++attempt)
        {
            gameHwnd = FindWindowA("Grand theft auto San Andreas", nullptr);
            if(gameHwnd && IsWindow(gameHwnd))
                break;
            Sleep(100);
        }
        if(gameHwnd)
            log(L"[info] game window detected, waiting 4.0s for graphics stack to settle...");
        else
            log(L"[warn] game window not detected yet, applying safe delay of 4.0s...");

        for(int i = 0; i < 40 && !stop.stop_requested(); ++i)
            Sleep(100);

        if(stop.stop_requested())
            return 0;

        log(L"[info] mapping AshesClient.dll directly into game...");
        injected = ManualMap::Map(process.value, client, &payload,
            sizeof(payload), &exceptionSupport);
        injectionError = GetLastError();
        if(injected)
        {
            const DWORD wait = WaitForSingleObject(ipc.ClientLoadedEvent(), 6000);
            if(wait != WAIT_OBJECT_0)
            {
                log(L"[warn] client confirmation event timed out, continuing");
            }
        }
    }
    else
    {
        log(L"[info] mapping AshesAgent.dll into launcher...");
        injected = ManualMap::Map(process.value, agent, &payload,
            sizeof(payload), &exceptionSupport);
        injectionError = GetLastError();
        if(injected)
        {
            const DWORD wait = WaitForSingleObject(ipc.ReadyEvent(), 5000);
            if(wait != WAIT_OBJECT_0)
            {
                injected = false;
                injectionError = wait == WAIT_FAILED ? GetLastError() : WAIT_TIMEOUT;
            }
        }
    }

    if(!injected)
    {
        log(ErrorText(L"[error] injection failed: ", injectionError));
        return 4;
    }

    if(isGameProcess)
    {
        log(L"[ok] ASHES successfully injected into game process");
        return 0;
    }

    log(L"[ok] AshesAgent.dll manually mapped and ready");
    log(exceptionSupport
        ? L"[ok] agent exception support registered without PEB entry"
        : L"[warn] agent exception support unavailable");
    log(L"[info] waiting for AshesClient.dll mapping...");
    const HANDLE waits[]{ipc.ClientLoadedEvent(), process.value};
    while(!stop.stop_requested())
    {
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 50);
        if(wait == WAIT_OBJECT_0)
        {
            log(L"[ok] AshesClient.dll mapping reported successful");
            return 0;
        }
        if(wait == WAIT_OBJECT_0 + 1)
        {
            log(L"[error] Multi Theft Auto.exe exited before client mapping");
            return 5;
        }
        if(wait == WAIT_FAILED)
        {
            log(ErrorText(L"[error] client event wait failed: ", GetLastError()));
            return 5;
        }
    }
    log(L"[info] loader closed while MTA continues running");
    return 0;
}

int Launcher::InjectOnce(const LogSink& log, DWORD specificPid)
{
    const std::wstring ownDirectory = LoaderPath::OwnDirectory();
    const std::wstring agent = ResolvePayloadPath(ownDirectory, AgentName, LegacyAgentName);
    const std::wstring client = ResolvePayloadPath(ownDirectory, ClientName, LegacyClientName);
    if(!LoaderPath::FileExists(agent) || !LoaderPath::FileExists(client))
    {
        log(L"[error] required file is missing (AshesAgent.dll / AshesClient.dll)");
        return 2;
    }

    DWORD processId = specificPid;
    std::wstring exeName;
    if(!processId)
        processId = FindGameProcess(&exeName);
    else
        exeName = GetProcessExeName(processId);

    if(!processId)
    {
        log(L"[warn] process not found for manual injection");
        return 1;
    }

    ProcessHandle process;
    process.value = OpenGameProcess(processId);
    if(!process.value)
    {
        log(ErrorText(L"[error] failed to open game process PID: ", GetLastError()));
        return 1;
    }

    log(L"[info] target process found: " + exeName + L" (PID " + std::to_wstring(processId) + L")");
    const DarkFlameConfig config = Config::Load(ownDirectory);
    BootstrapIpc ipc(ownDirectory, agent, client, config);
    if(!ipc.Valid())
    {
        log(ErrorText(L"[error] bootstrap setup failed: ", ipc.Error()));
        return 3;
    }
    const auto& payload = ipc.Payload();
    bool exceptionSupport{};
    bool injected = false;
    DWORD injectionError = 0;

    const bool isGameProcess = (exeName.find(L"proxy_sa") != std::wstring::npos
        || exeName.find(L"gta_sa") != std::wstring::npos);

    if(isGameProcess)
    {
        log(L"[info] waiting for game window and display stabilization...");
        HWND gameHwnd = nullptr;
        for(int attempt = 0; attempt < 150; ++attempt)
        {
            gameHwnd = FindWindowA("Grand theft auto San Andreas", nullptr);
            if(gameHwnd && IsWindow(gameHwnd))
                break;
            Sleep(100);
        }
        if(gameHwnd)
            log(L"[info] game window detected, waiting 4.0s for graphics stack to settle...");
        else
            log(L"[warn] game window not detected yet, applying safe delay of 4.0s...");
        Sleep(4000);

        log(L"[info] mapping AshesClient.dll directly into game...");
        injected = ManualMap::Map(process.value, client, &payload,
            sizeof(payload), &exceptionSupport);
        injectionError = GetLastError();
        if(injected)
        {
            const DWORD wait = WaitForSingleObject(ipc.ClientLoadedEvent(), 6000);
            if(wait != WAIT_OBJECT_0)
            {
                log(L"[warn] client confirmation event timed out, continuing");
            }
        }
    }
    else
    {
        log(L"[info] mapping AshesAgent.dll into launcher...");
        injected = ManualMap::Map(process.value, agent, &payload,
            sizeof(payload), &exceptionSupport);
        injectionError = GetLastError();
        if(injected)
        {
            const DWORD wait = WaitForSingleObject(ipc.ReadyEvent(), 5000);
            if(wait != WAIT_OBJECT_0)
            {
                injected = false;
                injectionError = wait == WAIT_FAILED ? GetLastError() : WAIT_TIMEOUT;
            }
        }
    }

    if(!injected)
    {
        log(ErrorText(L"[error] injection failed: ", injectionError));
        return 4;
    }

    log(L"[ok] ASHES successfully injected into process");
    return 0;
}
