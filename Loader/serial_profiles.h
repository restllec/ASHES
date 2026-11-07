#pragma once

#include <string>
#include <vector>

struct SerialProfile
{
    std::string name;
    std::string serial;
    std::string note;
};

class SerialProfileManager
{
public:
    int selectedProfile = 0;
    bool isCustom = false;
    std::vector<SerialProfile> profiles;

    bool Load(const std::wstring& directory, const std::string& fallbackSerial);
    bool Save(const std::wstring& directory) const;
    void SelectProfile(int index, std::string& targetSerial);
    void AddProfile(const std::string& name, const std::string& serial, const std::string& note = "");
    bool RemoveProfile(int index, std::string& targetSerial);
    void UpdateCustomStatus(const std::string& currentSerial);
};

extern SerialProfileManager g_serialProfiles;

