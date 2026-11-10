#pragma once

#include <Windows.h>
#include <iphlpapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winhttp.lib")

namespace LoaderLicense
{
struct State
{
    bool is_pro = false;
    std::string hwid;
    std::string mac;
    std::string key;
    std::string expire_date{"Не активирована"};
    std::string status_msg{"FREE (Базовый доступ)"};
};

inline std::string ToLower(std::string_view s)
{
    std::string res;
    res.reserve(s.size());
    for (char ch : s)
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return res;
}

inline std::string ReadMachineGuid()
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

inline std::string QueryPhysicalMacAddress()
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

inline uint64_t Fnv1aHash(const std::string& str)
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : str)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::string GenerateHWID(std::string& outMac)
{
    outMac = QueryPhysicalMacAddress();
    std::string guid = ReadMachineGuid();
    uint64_t hash = Fnv1aHash(outMac + "|" + guid);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "ASH-%04X-%04X",
        static_cast<unsigned int>((hash >> 16) & 0xFFFF),
        static_cast<unsigned int>(hash & 0xFFFF));
    return buf;
}

inline std::string ComputeLicenseSignature(const std::string& key, const std::string& hwid, const std::string& expires)
{
    uint64_t hash = Fnv1aHash(key + "|" + hwid + "|" + expires + "|DARKFLAME_ASHES_PRO_2026");
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(hash));
    return buf;
}

inline void SaveLicenseToFile(const std::wstring& directory, const std::string& key, const std::string& hwid = "", const std::string& expires = "")
{
    std::string actualHwid = hwid;
    if (actualHwid.empty() && !key.empty())
    {
        std::string dummy;
        actualHwid = GenerateHWID(dummy);
    }
    std::string sig = key.empty() ? "" : ComputeLicenseSignature(key, actualHwid, expires);

    for (const wchar_t* name : { L"\\Ashes_license.cfg", L"\\DarkFlame_license.cfg" })
    {
        std::wstring licPath = directory + name;
        std::ofstream lf(licPath);
        if (lf.is_open())
        {
            if (!key.empty())
                lf << key << "\n" << expires << "\n" << sig << "\n";
            lf.close();
        }
    }

    for (const wchar_t* name : { L"\\Ashes.cfg", L"\\DarkFlame.cfg" })
    {
        std::wstring cfgPath = directory + name;
        std::vector<std::string> lines;
        bool foundKey = false;
        {
            std::ifstream cf(cfgPath);
            std::string line;
            while (std::getline(cf, line))
            {
                if (line.rfind("LICENSE_KEY=", 0) == 0)
                {
                    lines.push_back("LICENSE_KEY=" + key);
                    foundKey = true;
                }
                else
                {
                    lines.push_back(line);
                }
            }
        }
        if (!foundKey)
            lines.push_back("LICENSE_KEY=" + key);

        std::ofstream out(cfgPath, std::ios::trunc);
        if (out.is_open())
        {
            for (const auto& l : lines)
                out << l << "\n";
        }
    }
}

inline std::string LoadSavedLicense(const std::wstring& directory)
{
    for (const wchar_t* name : { L"\\Ashes_license.cfg", L"\\DarkFlame_license.cfg" })
    {
        std::wstring licPath = directory + name;
        std::ifstream lf(licPath);
        if (lf.is_open())
        {
            std::string key;
            std::getline(lf, key);
            while (!key.empty() && (key.back() == '\r' || key.back() == '\n' || key.back() == ' '))
                key.pop_back();
            if (!key.empty())
                return key;
        }
    }

    for (const wchar_t* name : { L"\\Ashes.cfg", L"\\DarkFlame.cfg" })
    {
        std::wstring cfgPath = directory + name;
        std::ifstream cf(cfgPath);
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

inline bool ValidateKey(State& state, const std::string& key)
{
    state.key = key;
    if (key.empty())
    {
        state.is_pro = false;
        state.expire_date = "Не активирована";
        state.status_msg = "FREE (Базовый доступ)";
        return false;
    }

    if (state.hwid.empty())
    {
        state.hwid = GenerateHWID(state.mac);
    }

    if ((key.rfind("ASHES-PRO-", 0) != 0 || key.size() < 14) &&
        (key.rfind("DF-PRO-", 0) != 0 || key.size() < 12))
    {
        state.is_pro = false;
        state.expire_date = "Не активирована";
        state.status_msg = "Неверный формат ключа";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        state.is_pro = false;
        state.expire_date = "Ошибка сети";
        state.status_msg = "Сетевая ошибка WinHttp";
        return false;
    }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 15000);

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
            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

            std::string jsonBody = "{\"key\":\"" + key + "\",\"hwid\":\"" + state.hwid + "\"}";
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

                    if (response.find("\"success\":true") != std::string::npos && response.find("\"pro\":true") != std::string::npos)
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
                        if (serverError.empty())
                            serverError = "Лицензия не подтверждена";
                    }
                }
                else
                {
                    serverError = "HTTP " + std::to_string(statusCode);
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);

    if (verified)
    {
        state.is_pro = true;
        state.expire_date = serverExpires.empty() ? "Активна (PRO)" : serverExpires.substr(0, 10);
        state.status_msg = "PRO (Лицензия подтверждена)";
        return true;
    }

    state.is_pro = false;
    state.expire_date = "Отклонена";
    state.status_msg = serverError.empty() ? "Сервер лицензий недоступен" : serverError;
    return false;
}
}
