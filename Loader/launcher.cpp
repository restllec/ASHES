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
constexpr DWORD PollIntervalMs = 1;

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

std::wstring ResolveOrExtractPayload(const std::wstring& ownDirectory, const std::wstring& filename,
    const std::wstring& legacyFilename, int resourceId, const Launcher::LogSink* log = nullptr)
{
    const std::wstring direct = ownDirectory + L"\\" + filename;
    if(LoaderPath::FileExists(direct))
        return direct;
    const std::wstring legacy = ownDirectory + L"\\" + legacyFilename;
    if(LoaderPath::FileExists(legacy))
        return legacy;

    wchar_t tempDirBuf[MAX_PATH]{};
    const DWORD len = GetTempPathW(MAX_PATH, tempDirBuf);
    if(len == 0 || len >= MAX_PATH)
        return direct;

    const std::wstring targetDir = std::wstring(tempDirBuf) + L"Ashes";
    CreateDirectoryW(targetDir.c_str(), nullptr);
    SetFileAttributesW(targetDir.c_str(), FILE_ATTRIBUTE_HIDDEN);

    const std::wstring extractedPath = targetDir + L"\\" + filename;

    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if(res)
    {
        HGLOBAL hGlobal = LoadResource(nullptr, res);
        if(hGlobal)
        {
            const void* pData = LockResource(hGlobal);
            const DWORD size = SizeofResource(nullptr, res);
            if(pData && size > 0)
            {
                HANDLE hFile = CreateFileW(extractedPath.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
                if(hFile != INVALID_HANDLE_VALUE)
                {
                    DWORD written = 0;
                    WriteFile(hFile, pData, size, &written, nullptr);
                    CloseHandle(hFile);
                    if(log)
                        (*log)(L"[portable] extracted embedded payload: " + filename);
                    return extractedPath;
                }
            }
        }
    }
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
    const std::wstring agent = ResolveOrExtractPayload(ownDirectory, AgentName, LegacyAgentName, IDR_ASHES_AGENT_PAYLOAD, &log);
    const std::wstring client = ResolveOrExtractPayload(ownDirectory, ClientName, LegacyClientName, IDR_ASHES_CLIENT_PAYLOAD, &log);
    if(!LoaderPath::FileExists(agent) || !LoaderPath::FileExists(client))
    {
        log(L"[error] required file is missing");
        if(!LoaderPath::FileExists(agent))
            log(L"[error] agent: " + agent);
        if(!LoaderPath::FileExists(client))
            log(L"[error] client: " + client);
        return 2;
    }

    log(L"[info] waiting for Multi Theft Auto.exe (1 ms poll)...");
    ProcessHandle process;
    DWORD processId{};
    while(!stop.stop_requested())
    {
        processId = FindGameProcess();
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

    log(L"[info] Multi Theft Auto.exe found, PID "
        + std::to_wstring(processId));
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
    bool injected = ManualMap::Map(process.value, agent, &payload,
        sizeof(payload), &exceptionSupport);
    DWORD injectionError = GetLastError();
    if(injected)
    {
        const DWORD wait = WaitForSingleObject(ipc.ReadyEvent(), 5000);
        if(wait != WAIT_OBJECT_0)
        {
            injected = false;
            injectionError = wait == WAIT_FAILED ? GetLastError() : WAIT_TIMEOUT;
        }
    }
    if(!injected)
    {
        log(ErrorText(L"[error] agent bootstrap failed: ", injectionError));
        return 4;
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
    const std::wstring agent = ResolveOrExtractPayload(ownDirectory, AgentName, LegacyAgentName, IDR_ASHES_AGENT_PAYLOAD, &log);
    const std::wstring client = ResolveOrExtractPayload(ownDirectory, ClientName, LegacyClientName, IDR_ASHES_CLIENT_PAYLOAD, &log);
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
