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

inline void SaveLicenseToFile(const std::wstring& directory, const std::string& key)
{
    for (const wchar_t* name : { L"\\Ashes_license.cfg", L"\\DarkFlame_license.cfg" })
    {
        std::wstring licPath = directory + name;
        std::ofstream lf(licPath);
        if (lf.is_open())
        {
            lf << key;
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
    if (key == "ASHES-DEV-RESTLESS" || key == "DF-DEV-RESTLESS")
    {
        state.is_pro = true;
        state.expire_date = "DEV / Без ограничений";
        state.status_msg = "PRO (Активирован дев-ключ)";
        return true;
    }

    if ((key.rfind("ASHES-PRO-", 0) == 0 && key.size() >= 14) ||
        (key.rfind("DF-PRO-", 0) == 0 && key.size() >= 12))
    {
        state.is_pro = true;
        state.expire_date = "Активна (PRO)";
        state.status_msg = "PRO (Лицензия подтверждена)";
        return true;
    }

    state.is_pro = false;
    state.expire_date = "Не активирована";
    state.status_msg = key.empty() ? "FREE (Базовый доступ)" : "Неверный ключ (требуется ASHES-PRO-...)";
    return false;
}
}
