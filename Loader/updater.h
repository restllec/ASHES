#pragma once

#include <string>
#include <vector>
#include <functional>

namespace AshesUpdater
{
inline constexpr const char* CURRENT_LOADER_VERSION = "1.0.4";
inline constexpr const wchar_t* DEFAULT_MANIFEST_URL =
    L"https://github.com/restllec/ASHES/releases/latest/download/version.json";

using LogCallback = std::function<void(const std::wstring&)>;

struct ManifestFile
{
    std::wstring name;
    std::wstring url;
    std::string sha256;
};

struct UpdateManifest
{
    std::string loaderVersion;
    std::wstring loaderUrl;
    std::vector<ManifestFile> files;
    bool valid = false;
};

// Выполняет проверку и обновление. Возвращает true, если запущен self-update процесс (тогда вызывающий должен завершиться).
bool CheckAndPerformUpdates(const std::wstring& directory, const LogCallback& log);
}
