#pragma once

#include <string>

struct LicenseState
{
    bool is_pro = false;
    std::string hwid;
    std::string mac;
    std::string key;
    std::string expire_date{"Не активирована"};
    std::string status_msg{"FREE (Базовый доступ)"};
};

void SetLicenseDirectory(const std::wstring& directory);
void InitializeLicense();
const LicenseState& GetLicenseState();
bool IsProLicense();
bool ActivateLicenseKey(const std::string& key);
void DeactivateLicense();
std::string GetDeviceHWID();
std::string GetDeviceMAC();
