#pragma once

#include <Windows.h>
#include <filesystem>
#include <string>

// 1. Система профилей сборки (Build Editions / Feature Flags)
#define ASHES_EDITION_CORE     1 // Базовая сборка (проверенные маршрутные скрипты)
#define ASHES_EDITION_EXTENDED 2 // Базовая + вкладка внешнего модуля расширения (Plugin Module)
#define ASHES_EDITION_ULTIMATE 3 // Полная сборка (включая менеджер сессий «Снежок»)

#ifndef ASHES_BUILD_EDITION
#define ASHES_BUILD_EDITION ASHES_EDITION_ULTIMATE
#endif

enum class BuildEdition : int
{
    Core = ASHES_EDITION_CORE,
    Extended = ASHES_EDITION_EXTENDED,
    Ultimate = ASHES_EDITION_ULTIMATE
};

namespace Editions
{
inline constexpr BuildEdition Current() noexcept
{
    return static_cast<BuildEdition>(ASHES_BUILD_EDITION);
}

inline const char* Name() noexcept
{
#if ASHES_BUILD_EDITION == ASHES_EDITION_CORE
    return "CORE";
#elif ASHES_BUILD_EDITION == ASHES_EDITION_EXTENDED
    return "EXTENDED";
#else
    return "ULTIMATE";
#endif
}

inline bool HasPluginModule(const std::filesystem::path& baseDir = L".")
{
#if (ASHES_BUILD_EDITION >= ASHES_EDITION_EXTENDED)
    std::error_code ec;
    return std::filesystem::exists(baseDir / L"plugin.dll", ec) ||
           std::filesystem::exists(baseDir / L"modules" / L"plugin.dll", ec) ||
           std::filesystem::exists(baseDir / L"extension.dll", ec) ||
           std::filesystem::exists(baseDir / L"modules" / L"extension.dll", ec);
#else
    (void)baseDir;
    return false;
#endif
}

inline bool HasSnowModule(const std::filesystem::path& baseDir = L".")
{
#if (ASHES_BUILD_EDITION >= ASHES_EDITION_ULTIMATE)
    std::error_code ec;
    return std::filesystem::exists(baseDir / L"WinterCat.exe", ec) ||
           std::filesystem::exists(baseDir / L"modules" / L"WinterCat.exe", ec) ||
           std::filesystem::exists(baseDir / L"bin" / L"Release" / L"x86" / L"WinterCat.exe", ec) ||
           std::filesystem::exists(baseDir / L"bin" / L"Release" / L"WinterCat.exe", ec) ||
           std::filesystem::exists(baseDir / L"WinterCat" / L"bin" / L"Release" / L"WinterCat.exe", ec);
#else
    (void)baseDir;
    return false;
#endif
}

inline std::filesystem::path FindSnowExecutable(const std::filesystem::path& baseDir = L".")
{
    std::error_code ec;
    const std::filesystem::path candidates[] = {
        baseDir / L"WinterCat.exe",
        baseDir / L"modules" / L"WinterCat.exe",
        baseDir / L"bin" / L"Release" / L"x86" / L"WinterCat.exe",
        baseDir / L"bin" / L"Release" / L"WinterCat.exe",
        baseDir / L"WinterCat" / L"bin" / L"Release" / L"WinterCat.exe"
    };
    for (const auto& path : candidates)
    {
        if (std::filesystem::exists(path, ec))
            return path;
    }
    return {};
}
}
