#pragma once

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include "../Shared/ashes_editions.h"

struct DarkFlameConfig
{
    bool antiShadow{true};
    bool scriptsDumper{};
    bool setSerial{true};
    bool randomSerial{};
    bool serialValid{true};
    std::string publicSerial{"E97AEE3B69AAE6B8E699D851A6289902"};
    bool autoInject{true};
    std::string licenseKey;

    // Параметры «Прочее» (Движение и FPS)
    bool gameSpeedEnabled{false};
    int gameSpeedKey{VK_OEM_3}; // '~' / Ё по умолчанию
    bool gameSpeedHold{false};  // false = переключение, true = удержание
    float gameSpeedMultiplier{1.2f};

    bool smoothAccelEnabled{false};
    int smoothAccelKey{VK_LMENU}; // Left Alt по умолчанию
    bool smoothAccelHold{true};
    float smoothAccelPercent{50.0f};

    bool brakeEnabled{false};
    int brakeKey{'X'};            // 'X' по умолчанию
    bool brakeHold{true};
    float brakePercent{80.0f};

    bool fpsLimitEnabled{false};
    int fpsLimit{144};
};

namespace Config
{
inline constexpr std::string_view Defaults =
    "ANTI_SHADOW=1\n"
    "SCRIPTS_DUMPER=0\n"
    "SET_SERIAL=1\n"
    "RANDOM_SERIAL=0\n"
    "PUBLIC_SERIAL=E97AEE3B69AAE6B8E699D851A6289902\n";

inline std::string Trim(std::string value)
{
    const auto space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
    return value;
}

inline void Upper(std::string& value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::toupper(ch));
    });
}

inline bool ValidSerial(std::string_view value)
{
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char ch)
    {
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');
    });
}

inline DarkFlameConfig Load(const std::wstring& directory)
{
    DarkFlameConfig config;
    std::filesystem::path path = std::filesystem::path(directory) / L"Ashes.cfg";
    if(!std::filesystem::exists(path))
    {
        const std::filesystem::path legacy = std::filesystem::path(directory) / L"DarkFlame.cfg";
        if(std::filesystem::exists(legacy))
            path = legacy;
        else
        {
            std::ofstream created(path, std::ios::binary);
            created.write(Defaults.data(), static_cast<std::streamsize>(Defaults.size()));
        }
    }

    std::ifstream file(path, std::ios::binary);
    std::string line;
    while(std::getline(file, line))
    {
        line = Trim(std::move(line));
        if(line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        const std::size_t equals = line.find('=');
        if(equals == std::string::npos)
            continue;
        std::string key = Trim(line.substr(0, equals));
        std::string value = Trim(line.substr(equals + 1));
        Upper(key);
        if(key == "ANTI_SHADOW")
            config.antiShadow = value != "0";
        else if(key == "SCRIPTS_DUMPER")
            config.scriptsDumper = value == "1";
        else if(key == "SET_SERIAL")
            config.setSerial = value == "1";
        else if(key == "RANDOM_SERIAL")
            config.randomSerial = value == "1";
        else if(key == "PUBLIC_SERIAL")
        {
            Upper(value);
            config.publicSerial = std::move(value);
        }
        else if(key == "AUTO_INJECT")
            config.autoInject = value != "0";
        else if(key == "LICENSE_KEY")
            config.licenseKey = std::move(value);
        else if(key == "GAME_SPEED_ENABLED")
            config.gameSpeedEnabled = value == "1";
        else if(key == "GAME_SPEED_KEY")
            config.gameSpeedKey = std::strtol(value.c_str(), nullptr, 10);
        else if(key == "GAME_SPEED_HOLD")
            config.gameSpeedHold = value == "1";
        else if(key == "GAME_SPEED_MULT")
            config.gameSpeedMultiplier = std::strtof(value.c_str(), nullptr);
        else if(key == "SMOOTH_ACCEL_ENABLED")
            config.smoothAccelEnabled = value == "1";
        else if(key == "SMOOTH_ACCEL_KEY")
            config.smoothAccelKey = std::strtol(value.c_str(), nullptr, 10);
        else if(key == "SMOOTH_ACCEL_HOLD")
            config.smoothAccelHold = value == "1";
        else if(key == "SMOOTH_ACCEL_PERCENT")
            config.smoothAccelPercent = std::strtof(value.c_str(), nullptr);
        else if(key == "BRAKE_ENABLED")
            config.brakeEnabled = value == "1";
        else if(key == "BRAKE_KEY")
            config.brakeKey = std::strtol(value.c_str(), nullptr, 10);
        else if(key == "BRAKE_HOLD")
            config.brakeHold = value == "1";
        else if(key == "BRAKE_PERCENT")
            config.brakePercent = std::strtof(value.c_str(), nullptr);
        else if(key == "FPS_LIMIT_ENABLED")
            config.fpsLimitEnabled = value == "1";
        else if(key == "FPS_LIMIT")
            config.fpsLimit = std::strtol(value.c_str(), nullptr, 10);
    }
    if(!ValidSerial(config.publicSerial))
    {
        config.publicSerial = "9F5A1A5F9008ED9327D64B4A700324F3";
        config.serialValid = false;
    }
    if(config.setSerial && config.randomSerial)
        config.randomSerial = false;
    return config;
}

inline bool Save(const std::wstring& directory, const DarkFlameConfig& config)
{
    std::string text = "ANTI_SHADOW=" + std::string(config.antiShadow ? "1\n" : "0\n")
        + "SCRIPTS_DUMPER=" + std::string(config.scriptsDumper ? "1\n" : "0\n")
        + "SET_SERIAL=" + std::string(config.setSerial ? "1\n" : "0\n")
        + "RANDOM_SERIAL=" + std::string(config.randomSerial ? "1\n" : "0\n")
        + "PUBLIC_SERIAL=" + config.publicSerial + "\n"
        + "AUTO_INJECT=" + std::string(config.autoInject ? "1\n" : "0\n")
        + "LICENSE_KEY=" + config.licenseKey + "\n"
        + "GAME_SPEED_ENABLED=" + std::string(config.gameSpeedEnabled ? "1\n" : "0\n")
        + "GAME_SPEED_KEY=" + std::to_string(config.gameSpeedKey) + "\n"
        + "GAME_SPEED_HOLD=" + std::string(config.gameSpeedHold ? "1\n" : "0\n")
        + "GAME_SPEED_MULT=" + std::to_string(config.gameSpeedMultiplier) + "\n"
        + "SMOOTH_ACCEL_ENABLED=" + std::string(config.smoothAccelEnabled ? "1\n" : "0\n")
        + "SMOOTH_ACCEL_KEY=" + std::to_string(config.smoothAccelKey) + "\n"
        + "SMOOTH_ACCEL_HOLD=" + std::string(config.smoothAccelHold ? "1\n" : "0\n")
        + "SMOOTH_ACCEL_PERCENT=" + std::to_string(config.smoothAccelPercent) + "\n"
        + "BRAKE_ENABLED=" + std::string(config.brakeEnabled ? "1\n" : "0\n")
        + "BRAKE_KEY=" + std::to_string(config.brakeKey) + "\n"
        + "BRAKE_HOLD=" + std::string(config.brakeHold ? "1\n" : "0\n")
        + "BRAKE_PERCENT=" + std::to_string(config.brakePercent) + "\n"
        + "FPS_LIMIT_ENABLED=" + std::string(config.fpsLimitEnabled ? "1\n" : "0\n")
        + "FPS_LIMIT=" + std::to_string(config.fpsLimit) + "\n";

    bool allSaved = true;
    for(const wchar_t* cfgName : { L"Ashes.cfg", L"DarkFlame.cfg" })
    {
        const std::filesystem::path path = std::filesystem::path(directory) / cfgName;
        const std::filesystem::path temporary = path.wstring() + L".tmp";
        const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(file == INVALID_HANDLE_VALUE)
        {
            allSaved = false;
            continue;
        }
        DWORD written{};
        const bool saved = WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
            &written, nullptr) && written == static_cast<DWORD>(text.size())
            && FlushFileBuffers(file);
        CloseHandle(file);
        if(!saved || !MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            allSaved = false;
        }
    }
    return allSaved;
}
}
