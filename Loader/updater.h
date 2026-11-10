#pragma once

#include <string>
#include <vector>
#include <functional>

namespace AshesUpdater
{
inline constexpr const char* CURRENT_LOADER_VERSION = "1.8.7";
inline constexpr const wchar_t* DEFAULT_MANIFEST_URL =
    L"https://github.com/restllec/ASHES/releases/latest/download/version.json";

using LogCallback = std::function<void(const std::wstring&)>;

struct ManifestFile
{
    std::wstring name;
    std::wstring url;
    std::string sha256;
    bool optional = false;
};

struct UpdateManifest
{
    std::string loaderVersion;
    std::wstring loaderUrl;
    std::vector<ManifestFile> files;
    bool valid = false;
};

// Р вЂ™РЎвЂ№Р С—Р С•Р В»Р Р…РЎРЏР ВµРЎвЂљ Р С—РЎР‚Р С•Р Р†Р ВµРЎР‚Р С”РЎС“ Р С‘ Р С•Р В±Р Р…Р С•Р Р†Р В»Р ВµР Р…Р С‘Р Вµ. Р вЂ™Р С•Р В·Р Р†РЎР‚Р В°РЎвЂ°Р В°Р ВµРЎвЂљ true, Р ВµРЎРѓР В»Р С‘ Р В·Р В°Р С—РЎС“РЎвЂ°Р ВµР Р… self-update Р С—РЎР‚Р С•РЎвЂ Р ВµРЎРѓРЎРѓ (РЎвЂљР С•Р С–Р Т‘Р В° Р Р†РЎвЂ№Р В·РЎвЂ№Р Р†Р В°РЎР‹РЎвЂ°Р С‘Р в„– Р Т‘Р С•Р В»Р В¶Р ВµР Р… Р В·Р В°Р Р†Р ВµРЎР‚РЎв‚¬Р С‘РЎвЂљРЎРЉРЎРѓРЎРЏ).
bool CheckAndPerformUpdates(const std::wstring& directory, const LogCallback& log);
}


