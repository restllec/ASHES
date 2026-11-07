#pragma once

#include <string>
#include <vector>
#include <functional>

namespace AshesUpdater
{
inline constexpr const char* CURRENT_LOADER_VERSION = "1.8.1";
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

// Р’С‹РїРѕР»РЅСЏРµС‚ РїСЂРѕРІРµСЂРєСѓ Рё РѕР±РЅРѕРІР»РµРЅРёРµ. Р’РѕР·РІСЂР°С‰Р°РµС‚ true, РµСЃР»Рё Р·Р°РїСѓС‰РµРЅ self-update РїСЂРѕС†РµСЃСЃ (С‚РѕРіРґР° РІС‹Р·С‹РІР°СЋС‰РёР№ РґРѕР»Р¶РµРЅ Р·Р°РІРµСЂС€РёС‚СЊСЃСЏ).
bool CheckAndPerformUpdates(const std::wstring& directory, const LogCallback& log);
}

