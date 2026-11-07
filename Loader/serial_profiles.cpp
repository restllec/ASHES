#include "serial_profiles.h"
#include <fstream>
#include <sstream>
#include <algorithm>

SerialProfileManager g_serialProfiles;

namespace
{
std::string ExtractJsonString(std::string_view obj, std::string_view key)
{
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = obj.find(needle);
    if(pos == std::string_view::npos)
        return {};
    pos = obj.find(':', pos + needle.size());
    if(pos == std::string_view::npos)
        return {};
    pos = obj.find('\"', pos + 1);
    if(pos == std::string_view::npos)
        return {};
    size_t end = obj.find('\"', pos + 1);
    if(end == std::string_view::npos)
        return {};
    return std::string(obj.substr(pos + 1, end - pos - 1));
}

int ExtractJsonInt(std::string_view json, std::string_view key, int defaultVal = 0)
{
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = json.find(needle);
    if(pos == std::string_view::npos)
        return defaultVal;
    pos = json.find(':', pos + needle.size());
    if(pos == std::string_view::npos)
        return defaultVal;
    pos = json.find_first_of("-0123456789", pos + 1);
    if(pos == std::string_view::npos)
        return defaultVal;
    size_t end = json.find_first_not_of("-0123456789", pos);
    std::string numStr = (end == std::string_view::npos)
        ? std::string(json.substr(pos))
        : std::string(json.substr(pos, end - pos));
    try {
        return std::stoi(numStr);
    } catch(...) {
        return defaultVal;
    }
}
} // namespace

bool SerialProfileManager::Load(const std::wstring& directory, const std::string& fallbackSerial)
{
    const std::wstring filePath = directory + L"\\serials.json";
    std::ifstream file(filePath, std::ios::binary);
    if(!file.is_open())
    {
        // Создаем дефолтные профили
        profiles.clear();
        std::string s1 = fallbackSerial.empty() ? "876AEA764D2F487EB3277840517C6224" : fallbackSerial;
        profiles.push_back({"Основной (PC 1)", s1, "Аккаунт Province #1"});
        profiles.push_back({"Твинк (Ноутбук)", "1A2B3C4D5E6F7A8B9C0D1E2F3A4B5C6D", "Аккаунт Province #2"});
        selectedProfile = 0;
        isCustom = false;
        Save(directory);
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    selectedProfile = ExtractJsonInt(json, "selected_profile", 0);
    profiles.clear();

    size_t profilesPos = json.find("\"profiles\"");
    if(profilesPos != std::string::npos)
    {
        size_t arrStart = json.find('[', profilesPos);
        size_t arrEnd = json.rfind(']');
        if(arrStart != std::string::npos && arrEnd != std::string::npos && arrEnd > arrStart)
        {
            size_t cursor = arrStart;
            while(true)
            {
                size_t objStart = json.find('{', cursor);
                if(objStart == std::string::npos || objStart >= arrEnd)
                    break;
                size_t objEnd = json.find('}', objStart);
                if(objEnd == std::string::npos || objEnd > arrEnd)
                    break;

                std::string_view obj(json.data() + objStart, objEnd - objStart + 1);
                SerialProfile prof;
                prof.name = ExtractJsonString(obj, "name");
                prof.serial = ExtractJsonString(obj, "serial");
                prof.note = ExtractJsonString(obj, "note");

                if(!prof.serial.empty())
                {
                    if(prof.name.empty())
                        prof.name = "Профиль " + std::to_string(profiles.size() + 1);
                    profiles.push_back(std::move(prof));
                }

                cursor = objEnd + 1;
            }
        }
    }

    if(profiles.empty())
    {
        std::string s1 = fallbackSerial.empty() ? "876AEA764D2F487EB3277840517C6224" : fallbackSerial;
        profiles.push_back({"Основной (PC 1)", s1, "Аккаунт Province #1"});
        selectedProfile = 0;
    }

    if(selectedProfile < 0 || selectedProfile >= static_cast<int>(profiles.size()))
        selectedProfile = 0;

    UpdateCustomStatus(fallbackSerial);
    return true;
}

bool SerialProfileManager::Save(const std::wstring& directory) const
{
    const std::wstring filePath = directory + L"\\serials.json";
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if(!out.is_open())
        return false;

    out << "{\n";
    out << "  \"selected_profile\": " << selectedProfile << ",\n";
    out << "  \"profiles\": [\n";

    for(size_t i = 0; i < profiles.size(); ++i)
    {
        out << "    {\n";
        out << "      \"name\": \"" << profiles[i].name << "\",\n";
        out << "      \"serial\": \"" << profiles[i].serial << "\",\n";
        out << "      \"note\": \"" << profiles[i].note << "\"\n";
        out << "    }" << (i + 1 < profiles.size() ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return true;
}

void SerialProfileManager::SelectProfile(int index, std::string& targetSerial)
{
    if(index >= 0 && index < static_cast<int>(profiles.size()))
    {
        selectedProfile = index;
        targetSerial = profiles[index].serial;
        isCustom = false;
    }
}

void SerialProfileManager::AddProfile(const std::string& name, const std::string& serial, const std::string& note)
{
    SerialProfile prof;
    prof.name = name.empty() ? ("Профиль " + std::to_string(profiles.size() + 1)) : name;
    prof.serial = serial;
    prof.note = note;
    profiles.push_back(std::move(prof));
    selectedProfile = static_cast<int>(profiles.size()) - 1;
    isCustom = false;
}

bool SerialProfileManager::RemoveProfile(int index, std::string& targetSerial)
{
    if(index < 0 || index >= static_cast<int>(profiles.size()))
        return false;

    profiles.erase(profiles.begin() + index);
    if(profiles.empty())
    {
        profiles.push_back({"Основной", targetSerial.empty() ? "876AEA764D2F487EB3277840517C6224" : targetSerial, ""});
        selectedProfile = 0;
    }
    else
    {
        if(selectedProfile >= static_cast<int>(profiles.size()))
            selectedProfile = static_cast<int>(profiles.size()) - 1;
    }

    targetSerial = profiles[selectedProfile].serial;
    isCustom = false;
    return true;
}

void SerialProfileManager::UpdateCustomStatus(const std::string& currentSerial)
{
    if(profiles.empty())
    {
        isCustom = true;
        return;
    }

    if(selectedProfile >= 0 && selectedProfile < static_cast<int>(profiles.size()))
    {
        if(profiles[selectedProfile].serial == currentSerial)
        {
            isCustom = false;
            return;
        }
    }

    // Проверяем, совпадает ли с другим существующим профилем
    for(size_t i = 0; i < profiles.size(); ++i)
    {
        if(profiles[i].serial == currentSerial)
        {
            selectedProfile = static_cast<int>(i);
            isCustom = false;
            return;
        }
    }

    isCustom = true;
}

