#include "license_manager.h"
#include "logger.h"

#include <Windows.h>
#include <iphlpapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winhttp.lib")

namespace
{
    LicenseState g_license{};
    bool g_initialized{};

    std::string ToLower(std::string_view s)
    {
        std::string res;
        res.reserve(s.size());
        for (char ch : s)
            res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        return res;
    }

    std::string ReadMachineGuid()
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &key) != ERROR_SUCCESS)
                return "00000000-0000-0000-0000-000000000000";
        }

        wchar_t buffer[128]{};
        DWORD size = sizeof(buffer);
        DWORD type = REG_SZ;
        if (RegQueryValueExW(key, L"MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            return "00000000-0000-0000-0000-000000000000";
        }
        RegCloseKey(key);

        char ascii[128]{};
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, ascii, sizeof(ascii), nullptr, nullptr);
        return ascii;
    }

    std::string QueryPhysicalMacAddress()
    {
        ULONG outBufLen = sizeof(IP_ADAPTER_INFO);
        std::vector<BYTE> buffer(outBufLen);
        PIP_ADAPTER_INFO adapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());

        if (GetAdaptersInfo(adapterInfo, &outBufLen) == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(outBufLen);
            adapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
        }

        if (GetAdaptersInfo(adapterInfo, &outBufLen) == NO_ERROR)
        {
            for (PIP_ADAPTER_INFO adapter = adapterInfo; adapter; adapter = adapter->Next)
            {
                std::string desc = ToLower(adapter->Description);
                std::string name = ToLower(adapter->AdapterName);

                // Skip virtual, vpn, loopback, tunnel adapters
                if (desc.find("virtual") != std::string::npos || desc.find("vpn") != std::string::npos
                    || desc.find("hyper-v") != std::string::npos || desc.find("vmware") != std::string::npos
                    || desc.find("tap") != std::string::npos || desc.find("wireguard") != std::string::npos
                    || desc.find("tailscale") != std::string::npos || desc.find("zerotier") != std::string::npos
                    || desc.find("bluetooth") != std::string::npos || desc.find("loopback") != std::string::npos
                    || desc.find("npcap") != std::string::npos || desc.find("pseudo") != std::string::npos)
                {
                    continue;
                }

                if (adapter->AddressLength == 6)
                {
                    char macStr[32]{};
                    std::snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                        adapter->Address[0], adapter->Address[1], adapter->Address[2],
                        adapter->Address[3], adapter->Address[4], adapter->Address[5]);
                    return macStr;
                }
            }
        }
        return "00:1A:2B:3C:4D:5E";
    }

    uint64_t Fnv1aHash(const std::string& str)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : str)
        {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    static std::wstring g_licenseDirectory;

    std::wstring GetConfigPath()
    {
        std::wstring dir;
        if (!g_licenseDirectory.empty())
        {
            dir = g_licenseDirectory;
        }
        else
        {
            wchar_t modulePath[MAX_PATH]{};
            HMODULE hMod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&InitializeLicense), &hMod);
            GetModuleFileNameW(hMod, modulePath, MAX_PATH);
            std::wstring path = modulePath;
            auto pos = path.find_last_of(L"\\/");
            if (pos != std::wstring::npos)
                dir = path.substr(0, pos);
        }
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
            dir.push_back(L'\\');

        std::wstring ashesPath = dir + L"Ashes_license.cfg";
        if (GetFileAttributesW(ashesPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            return ashesPath;
        std::wstring legacyPath = dir + L"DarkFlame_license.cfg";
        if (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            return legacyPath;
        return ashesPath;
    }

    void SaveKeyToFile(const std::string& key)
    {
        std::wstring dir;
        if (!g_licenseDirectory.empty())
            dir = g_licenseDirectory;
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
            dir.push_back(L'\\');

        for (const wchar_t* name : { L"Ashes_license.cfg", L"DarkFlame_license.cfg" })
        {
            std::ofstream file(dir + name);
            if (file.is_open())
            {
                file << key;
                file.close();
            }
        }
    }

    std::string LoadKeyFromFile()
    {
        std::wstring dir;
        if (!g_licenseDirectory.empty())
            dir = g_licenseDirectory;
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
            dir.push_back(L'\\');

        for (const wchar_t* name : { L"Ashes_license.cfg", L"DarkFlame_license.cfg" })
        {
            std::ifstream file(dir + name);
            if (file.is_open())
            {
                std::string key;
                std::getline(file, key);
                file.close();
                while (!key.empty() && (key.back() == '\r' || key.back() == '\n' || key.back() == ' '))
                    key.pop_back();
                if (!key.empty())
                    return key;
            }
        }

        for (const wchar_t* name : { L"Ashes.cfg", L"DarkFlame.cfg" })
        {
            std::ifstream cf(dir + name);
            if (cf.is_open())
            {
                std::string line;
                while (std::getline(cf, line))
                {
                    if (line.rfind("LICENSE_KEY=", 0) == 0)
                    {
                        std::string key = line.substr(12);
                        while (!key.empty() && (key.back() == '\r' || key.back() == '\n' || key.back() == ' '))
                            key.pop_back();
                        if (!key.empty())
                            return key;
                    }
                }
            }
        }
        return "";
    }

    bool CheckLicenseOnServer(const std::string& hwid, const std::string& key)
    {
        // Standard PRO prefix check
        if ((key.rfind("ASHES-PRO-", 0) != 0 || key.size() < 14) &&
            (key.rfind("DF-PRO-", 0) != 0 || key.size() < 12))
        {
            g_license.is_pro = false;
            g_license.status_msg = "Неверный формат ключа (требуется ASHES-PRO-...)";
            return false;
        }

        // WinHTTP client verification
        HINTERNET hSession = WinHttpOpen(L"ASHES-Client/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession)
        {
            // If offline, accept valid format provisionally or mark local valid
            g_license.is_pro = true;
            g_license.expire_date = "Локальная верификация";
            g_license.status_msg = "PRO (Локальный ключ)";
            return true;
        }

        // 3 sec timeout so UI doesn't hitch
        WinHttpSetTimeouts(hSession, 1500, 2000, 3000, 3000);

        HINTERNET hConnect = WinHttpConnect(hSession, L"ashes-licensing.vercel.app", INTERNET_DEFAULT_HTTPS_PORT, 0);
        bool verified = false;
        std::string serverError;
        std::string serverExpires;
        if (hConnect)
        {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/check_license",
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hRequest)
            {
                std::string jsonBody = "{\"key\":\"" + key + "\",\"hwid\":\"" + hwid + "\"}";
                const wchar_t* headers = L"Content-Type: application/json\r\n";
                if (WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1L),
                    const_cast<char*>(jsonBody.data()), static_cast<DWORD>(jsonBody.size()),
                    static_cast<DWORD>(jsonBody.size()), 0) && WinHttpReceiveResponse(hRequest, nullptr))
                {
                    DWORD statusCode = 0;
                    DWORD size = sizeof(statusCode);
                    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
                    if (statusCode == 200)
                    {
                        std::string response;
                        DWORD bytesAvailable = 0;
                        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
                        {
                            std::vector<char> buffer(bytesAvailable + 1);
                            DWORD bytesRead = 0;
                            if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0)
                            {
                                response.append(buffer.data(), bytesRead);
                            }
                        }

                        if (response.find("\"success\":true") != std::string::npos || response.find("\"pro\":true") != std::string::npos)
                        {
                            verified = true;
                            const auto expPos = response.find("\"expires_at\":\"");
                            if (expPos != std::string::npos)
                            {
                                const auto expEnd = response.find("\"", expPos + 14);
                                if (expEnd != std::string::npos)
                                    serverExpires = response.substr(expPos + 14, expEnd - (expPos + 14));
                            }
                        }
                        else
                        {
                            const auto errPos = response.find("\"error\":\"");
                            if (errPos != std::string::npos)
                            {
                                const auto errEnd = response.find("\"", errPos + 9);
                                if (errEnd != std::string::npos)
                                    serverError = response.substr(errPos + 9, errEnd - (errPos + 9));
                            }
                        }
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);

        if (verified)
        {
            g_license.is_pro = true;
            g_license.expire_date = serverExpires.empty() ? "Активна (Vercel)" : serverExpires.substr(0, 10);
            g_license.status_msg = "PRO (Лицензия подтверждена)";
            return true;
        }

        if (!serverError.empty())
        {
            g_license.is_pro = false;
            g_license.expire_date = "Отклонена";
            g_license.status_msg = "Ошибка: " + serverError;
            return false;
        }

        // Offline / Unreachable fallback: accept valid format key
        g_license.is_pro = true;
        g_license.expire_date = "Автономный режим";
        g_license.status_msg = "PRO (Ключ принят)";
        return true;
    }
}

void InitializeLicense()
{
    if (g_initialized)
        return;
    g_initialized = true;

    try
    {
        g_license.mac = QueryPhysicalMacAddress();
        std::string guid = ReadMachineGuid();

        uint64_t hash = Fnv1aHash(g_license.mac + "|" + guid);
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "ASH-%04X-%04X",
            static_cast<unsigned int>((hash >> 16) & 0xFFFF),
            static_cast<unsigned int>(hash & 0xFFFF));
        g_license.hwid = buf;

        std::string savedKey = LoadKeyFromFile();
        if (!savedKey.empty())
        {
            g_license.key = savedKey;
            g_license.is_pro = true;
            g_license.expire_date = "Проверка...";
            g_license.status_msg = "PRO (Активация...)";

            // Asynchronous verification to prevent DllMain deadlocks and network timeouts
            struct VerifyContext { std::string hwid; std::string key; };
            auto* ctx = new VerifyContext{ g_license.hwid, savedKey };
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                auto* c = static_cast<VerifyContext*>(param);
                if (c)
                {
                    CheckLicenseOnServer(c->hwid, c->key);
                    delete c;
                }
                return 0;
            }, ctx, 0, nullptr);
            if (hThread)
                CloseHandle(hThread);
            else
                delete ctx;
        }
        else
        {
            g_license.is_pro = false;
            g_license.status_msg = "FREE (Базовый доступ)";
        }
    }
    catch (...)
    {
        g_license.is_pro = false;
        g_license.status_msg = "FREE (Локальный доступ)";
    }
}

void SetLicenseDirectory(const std::wstring& directory)
{
    g_licenseDirectory = directory;
}

const LicenseState& GetLicenseState()
{
    if (!g_initialized)
        InitializeLicense();
    return g_license;
}

bool IsProLicense()
{
    if (!g_initialized)
        InitializeLicense();
    return g_license.is_pro;
}

bool ActivateLicenseKey(const std::string& key)
{
    if (!g_initialized)
        InitializeLicense();

    g_license.key = key;
    bool ok = CheckLicenseOnServer(g_license.hwid, key);
    if (ok)
    {
        SaveKeyToFile(key);
    }
    return ok;
}

void DeactivateLicense()
{
    g_license.is_pro = false;
    g_license.key.clear();
    g_license.expire_date = "Не активирована";
    g_license.status_msg = "FREE (Базовый доступ)";
    SaveKeyToFile("");
}

std::string GetDeviceHWID()
{
    if (!g_initialized)
        InitializeLicense();
    return g_license.hwid;
}

std::string GetDeviceMAC()
{
    if (!g_initialized)
        InitializeLicense();
    return g_license.mac;
}
