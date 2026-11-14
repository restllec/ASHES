#pragma once

#include <string>
#include <vector>
#include <functional>

namespace AshesUpdater
{
inline constexpr const char* CURRENT_LOADER_VERSION = "1.8.9";
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

// Р В РІР‚в„ўР РЋРІР‚в„–Р В РЎвЂ”Р В РЎвЂўР В Р’В»Р В Р вЂ¦Р РЋР РЏР В Р’ВµР РЋРІР‚С™ Р В РЎвЂ”Р РЋР вЂљР В РЎвЂўР В Р вЂ Р В Р’ВµР РЋР вЂљР В РЎвЂќР РЋРЎвЂњ Р В РЎвЂ Р В РЎвЂўР В Р’В±Р В Р вЂ¦Р В РЎвЂўР В Р вЂ Р В Р’В»Р В Р’ВµР В Р вЂ¦Р В РЎвЂР В Р’Вµ. Р В РІР‚в„ўР В РЎвЂўР В Р’В·Р В Р вЂ Р РЋР вЂљР В Р’В°Р РЋРІР‚В°Р В Р’В°Р В Р’ВµР РЋРІР‚С™ true, Р В Р’ВµР РЋР С“Р В Р’В»Р В РЎвЂ Р В Р’В·Р В Р’В°Р В РЎвЂ”Р РЋРЎвЂњР РЋРІР‚В°Р В Р’ВµР В Р вЂ¦ self-update Р В РЎвЂ”Р РЋР вЂљР В РЎвЂўР РЋРІР‚В Р В Р’ВµР РЋР С“Р РЋР С“ (Р РЋРІР‚С™Р В РЎвЂўР В РЎвЂ“Р В РўвЂР В Р’В° Р В Р вЂ Р РЋРІР‚в„–Р В Р’В·Р РЋРІР‚в„–Р В Р вЂ Р В Р’В°Р РЋР вЂ№Р РЋРІР‚В°Р В РЎвЂР В РІвЂћвЂ“ Р В РўвЂР В РЎвЂўР В Р’В»Р В Р’В¶Р В Р’ВµР В Р вЂ¦ Р В Р’В·Р В Р’В°Р В Р вЂ Р В Р’ВµР РЋР вЂљР РЋРІвЂљВ¬Р В РЎвЂР РЋРІР‚С™Р РЋР Р‰Р РЋР С“Р РЋР РЏ).
bool CheckAndPerformUpdates(const std::wstring& directory, const LogCallback& log);
}



