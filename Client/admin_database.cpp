#include "admin_database.h"
#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <regex>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(std::wstring_view wide)
{
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string StripHtmlTags(const std::string& in)
{
    std::string out;
    bool inTag = false;
    for (char c : in)
    {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out += c;
    }
    return out;
}

} // namespace

AdminDatabase& AdminDatabase::Instance()
{
    static AdminDatabase instance;
    return instance;
}

AdminDatabase::AdminDatabase()
{
    PopulateDefaults();
}

std::wstring AdminDatabase::GetCacheFilePath() const
{
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        std::wstring s = path;
        auto sep = s.find_last_of(L"\\/");
        if (sep != std::wstring::npos)
            return s.substr(0, sep + 1) + L"admins_cache.json";
    }
    return L"admins_cache.json";
}

std::wstring AdminDatabase::GetServer7FilePath() const
{
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        std::wstring s = path;
        auto sep = s.find_last_of(L"\\/");
        if (sep != std::wstring::npos)
            return s.substr(0, sep + 1) + L"admins_server7.json";
    }
    return L"admins_server7.json";
}

void AdminDatabase::Initialize()
{
    LoadSettings();
    LoadCache();
    LoadServer7File();
    m_status = "База: загружено " + std::to_string(m_admins[m_server].size())
        + " админов (Сервер " + std::to_string(m_server) + ")";
}

void AdminDatabase::PopulateDefaults()
{
    // Сервер 1 (185.71.66.80)
    m_admins[1] = {
        {"Emily_Quincy", "Главный администратор", 7},
        {"Dmitriy_Ogonkov", "Зам. главного администратора", 6},
        {"Alim_Komarov", "Куратор администрации", 5},
        {"Jack_Morozov", "Куратор администрации", 5},
        {"Sergey_Streltsov", "Старший администратор", 4},
        {"Adrian_Litvintsev", "Старший администратор", 4},
        {"Alex_Morrison", "Администратор", 3},
        {"Alexander_Grozniy", "Администратор", 3},
        {"Andrey_Semp", "Администратор", 3},
        {"Andrey_Valyaev", "Администратор", 3},
        {"Artem_Augustov", "Младший администратор", 2},
        {"Artemiy_Lisiuk", "Младший администратор", 2},
        {"Egor_Sobakevich", "Младший администратор", 2},
        {"Ivan_Gryb", "Модератор", 1},
        {"Kamilla_Florenz", "Модератор", 1},
        {"Maksim_Harlamow", "Модератор", 1},
        {"Melody_Wayne", "Модератор", 1},
        {"Nestor_Rutherford", "Модератор", 1},
        {"Nick_Kotik", "Модератор", 1},
        {"Vladimir_Smash", "Модератор", 1},
        {"Wolfgang_Schneiderhan", "Модератор", 1},
        {"Yuriy_Sokolovskiy", "Модератор", 1},
        {"Johnny_Smith", "Модератор", 1},
        {"Kiyotaki_Darkness", "Модератор", 1},
        {"Pavel_Toporov", "Модератор", 1},
        {"David_Seliverstov", "Модератор", 1},
        {"Ivan_Prahodskiy", "Модератор", 1},
        {"Matthew_Rutherford", "Модератор", 1},
        {"Veynar_Halvardsen", "Модератор", 1},
        {"Aleksey_Bombovich2", "Модератор", 1}
    };

    // Сервер 2 (185.71.66.70)
    m_admins[2] = {
        {"Maria_Alekseeva", "Главный администратор", 7},
        {"Rodion_Topolskiy", "Зам. главного администратора", 6},
        {"Alexander_Krutov", "Куратор администрации", 5},
        {"Arseniy_Maltsev", "Куратор администрации", 5},
        {"William_Great", "Старший администратор", 4},
        {"Alexander_Gasanov", "Старший администратор", 4},
        {"Antonio_Zubenko", "Администратор", 3},
        {"Ksenia_Groznaya", "Администратор", 3},
        {"Franklin_White", "Администратор", 3},
        {"Daniil_Lantratov", "Администратор", 3},
        {"Evgeny_Grossman", "Младший администратор", 2},
        {"Illya_Santiz", "Младший администратор", 2},
        {"Jack_Turner", "Младший администратор", 2},
        {"Vladislav_Alen", "Младший администратор", 2},
        {"Ludwig_Salvatore", "Модератор", 1},
        {"Timofey_Fabin", "Модератор", 1},
        {"Karolina_Kaspiyskaya", "Модератор", 1},
        {"Leo_Guerra", "Модератор", 1},
        {"Max_Dante", "Модератор", 1},
        {"Roman_Lisov", "Модератор", 1},
        {"Vladislav_Townley", "Модератор", 1},
        {"Maxim_Gornadzorov", "Модератор", 1},
        {"Maksim_Alferov", "Модератор", 1},
        {"Mia_Krutova", "Модератор", 1},
        {"Aleksandr_Grozniy", "Модератор", 1},
        {"Aleksandr_Biketov", "Модератор", 1},
        {"Don_Vice", "Модератор", 1},
        {"Daniil_Wolskiy", "Модератор", 1},
        {"Dmitriy_Ostrovskiy", "Модератор", 1},
        {"Nik_Romadin", "Модератор", 1},
        {"Pablo_King", "Модератор", 1},
        {"Vyacheslav_Rublev", "Модератор", 1},
        {"Dmitry_Pretty", "Модератор", 1},
        {"Stefan_King", "Модератор", 1},
        {"Danil_Naumow", "Модератор", 1},
        {"Tony_King", "Модератор", 1},
        {"Kita_Kayman", "Модератор", 1},
        {"Reimondo_Grossman", "Модератор", 1}
    };

    // Сервер 3 (185.71.66.79)
    m_admins[3] = {
        {"Vladislav_Kiselev", "Главный администратор", 7},
        {"Eric_Collins", "Зам. главного администратора", 6},
        {"Danil_Astov", "Куратор администрации", 5},
        {"Dmitriy_Scheglov", "Куратор администрации", 5},
        {"Eduard_Vysotskiy", "Старший администратор", 4},
        {"Fedor_Khalifa", "Старший администратор", 4},
        {"Prokhor_Lukoyanov", "Старший администратор", 4},
        {"Vladislav_Sutagin", "Администратор", 3},
        {"Alex_Trushin", "Администратор", 3},
        {"Alexandr_Yankee", "Администратор", 3},
        {"Alina_Solntsevskaya", "Администратор", 3},
        {"Anastasia_Crossman", "Младший администратор", 2},
        {"Dmitriy_Dennica", "Младший администратор", 2},
        {"George_Gaiduk", "Младший администратор", 2},
        {"Ivan_Ryzov", "Модератор", 1},
        {"Kirill_Mirnyy", "Модератор", 1},
        {"Marat_Sorokin", "Модератор", 1},
        {"Radmir_Pyatkin", "Модератор", 1},
        {"Rodion_Vistnik", "Модератор", 1},
        {"Sergei_Black", "Модератор", 1},
        {"Thomas_Barinov", "Модератор", 1},
        {"Vladislav_Macalister", "Модератор", 1},
        {"Walter_Neal", "Модератор", 1},
        {"Balthazar_Moskov", "Модератор", 1},
        {"Elisey_Marlboro", "Модератор", 1},
        {"Francesco_Grizzly", "Модератор", 1},
        {"Mironu_Kataray", "Модератор", 1},
        {"Sergei_Yamrazh", "Модератор", 1},
        {"Daniil_Shock", "Модератор", 1},
        {"Ilya_Anillov", "Модератор", 1},
        {"Roman_Ray", "Модератор", 1},
        {"Aaron_Campbell", "Модератор", 1},
        {"Artur_Iskandarov", "Модератор", 1},
        {"Aurora_Vlasova", "Модератор", 1},
        {"Danil_Moskov", "Модератор", 1},
        {"Daniil_Tairov", "Модератор", 1},
        {"Egor_Hill", "Модератор", 1},
        {"Jevgeni_Djagilev", "Модератор", 1},
        {"Maksim_Kancler", "Модератор", 1},
        {"Richard_Frank", "Модератор", 1},
        {"Yuko_Mori", "Модератор", 1}
    };

    // Сервер 4 (185.71.66.64)
    m_admins[4] = {
        {"Claus_Nevskiy", "Главный администратор", 7},
        {"Anastasia_MacAlister", "Зам. главного администратора", 6},
        {"Sevil_Esposito", "Куратор администрации", 5},
        {"Melissa_Witty", "Куратор администрации", 5},
        {"Evgeniy_Holmes", "Старший администратор", 4},
        {"Serg_Antonov", "Старший администратор", 4},
        {"Augustine_Morgan", "Администратор", 3},
        {"Artem_Fedukov", "Администратор", 3},
        {"Pavel_Morello", "Администратор", 3},
        {"Robert_Dobrov", "Администратор", 3},
        {"Mirella_Mayers", "Младший администратор", 2},
        {"Varlam_Bobko", "Младший администратор", 2},
        {"Matthew_Esposito", "Младший администратор", 2},
        {"Anthony_Manrique", "Модератор", 1},
        {"Benjamin_Watson", "Модератор", 1},
        {"Alexander_Potok", "Модератор", 1},
        {"Dmitriy_Prostorov", "Модератор", 1},
        {"Saburo_Itto", "Модератор", 1},
        {"Anatoliy_Vercetti", "Модератор", 1},
        {"Nick_Tverskoy", "Модератор", 1},
        {"Yaroslav_Mayers", "Модератор", 1},
        {"Xavier_Manchine", "Модератор", 1},
        {"Vasiliy_Tverskoy", "Модератор", 1},
        {"Anna_Tverskaya", "Модератор", 1},
        {"Anatoliy_Nesterov", "Модератор", 1},
        {"Anton_Sambur", "Модератор", 1},
        {"Ali_Henderson", "Модератор", 1},
        {"Maximilian_Watanabe", "Модератор", 1},
        {"Chad_Morgan", "Модератор", 1},
        {"Floren_Winners", "Модератор", 1},
        {"Aman_Dubrovskiy", "Модератор", 1},
        {"Kirill_Reall", "Модератор", 1},
        {"Norman_Excellent", "Модератор", 1}
    };

    // Сервер 5 (185.71.66.66)
    m_admins[5] = {
        {"Platon_Seven", "Главный администратор", 7},
        {"Kai_Mironov", "Зам. главного администратора", 6},
        {"Alexandr_Silych", "Куратор администрации", 5},
        {"Daniil_Caffrey", "Куратор администрации", 5},
        {"Dmitriy_Polanski", "Старший администратор", 4},
        {"Gottfried_Boyarin", "Старший администратор", 4},
        {"Sergey_Belikov", "Администратор", 3},
        {"Aleksey_Khrusch", "Администратор", 3},
        {"Alex_Gutmann", "Администратор", 3},
        {"Alex_Vatkov", "Администратор", 3},
        {"Alexandr_Venevtsev", "Младший администратор", 2},
        {"Artem_Tyhkanov", "Младший администратор", 2},
        {"Diana_Creighton", "Младший администратор", 2},
        {"Dmitriy_Repin", "Модератор", 1},
        {"Dmitry_Tomin", "Модератор", 1},
        {"Emmanuel_Deus", "Модератор", 1},
        {"Mitsuo_Fox", "Модератор", 1},
        {"Robert_Sychev", "Модератор", 1},
        {"Rostislav_Imenov", "Модератор", 1},
        {"Alex_Robinson", "Модератор", 1},
        {"Alexander_McCartney", "Модератор", 1},
        {"Amin_Cherry", "Модератор", 1},
        {"Danil_Polaneychick", "Модератор", 1},
        {"Aleksandr_McLaren", "Модератор", 1},
        {"Artem_Bobrovskiy", "Модератор", 1},
        {"Giuseppe_Gazdeliani", "Модератор", 1},
        {"Mark_Andrusenko", "Модератор", 1},
        {"Artem_Rose", "Модератор", 1},
        {"Konstantin_Kurochkin", "Модератор", 1},
        {"Mehad_Still", "Модератор", 1}
    };

    // Сервер 6 (185.71.66.81)
    m_admins[6] = {
        {"Denis_Manafort", "Главный администратор", 7},
        {"Andrey_Novak", "Зам. главного администратора", 6},
        {"Markus_Berg", "Куратор администрации", 5},
        {"Elizaveta_Berg", "Куратор администрации", 5},
        {"Alexander_Good", "Старший администратор", 4},
        {"Paul_Good", "Старший администратор", 4},
        {"David_Bystrov", "Администратор", 3},
        {"Artem_Darmin", "Администратор", 3},
        {"Georgiy_Zhilin", "Администратор", 3},
        {"Maksim_KuIiy", "Администратор", 3},
        {"Arthur_Daniels", "Младший администратор", 2},
        {"Averardo_Capone", "Младший администратор", 2},
        {"Vladislav_Manarskiy", "Младший администратор", 2},
        {"Polter_Sokirovskiy", "Модератор", 1},
        {"Oliver_Capone", "Модератор", 1},
        {"Savva_Sharkov", "Модератор", 1},
        {"Hades_Manarskiy", "Модератор", 1},
        {"Andrew_Maguire", "Модератор", 1},
        {"Shiro_Vi", "Модератор", 1},
        {"Sergey_Berg", "Модератор", 1},
        {"Osiris_Reinhardt", "Модератор", 1},
        {"Egoriy_Bobryshev", "Модератор", 1},
        {"Yuriy_Topolskin", "Модератор", 1},
        {"Hugo_Wolf", "Модератор", 1},
        {"Agato_Massino", "Модератор", 1},
        {"Andrey_Loverd", "Модератор", 1},
        {"Ilya_Bobryshev", "Модератор", 1},
        {"Emmanuel_Capone", "Модератор", 1},
        {"Vladislav_Verkalo", "Модератор", 1},
        {"Lee_Capone", "Модератор", 1},
        {"Selvester_Loverd", "Модератор", 1},
        {"Shai_Massino", "Модератор", 1},
        {"Ajay_Jackson", "Модератор", 1},
        {"Monte_Good", "Модератор", 1},
        {"August_Hoffmann", "Модератор", 1},
        {"Dany_Good", "Модератор", 1},
        {"Moki_Nellson", "Модератор", 1},
        {"Attaviano_Capone", "Модератор", 1},
        {"Mike_McCoy", "Модератор", 1},
        {"Vadim_Good", "Модератор", 1}
    };

    // Сервер 7 (185.71.66.88 - Discord / локальный)
    m_admins[7] = {
        {"Anthony_Paris", "Главный администратор", 7},
        {"Arnold_Fenix", "Зам. главного администратора", 6},
        {"Stanislav_Zybinskiy", "Куратор администрации", 5},
        {"Artemiy_Kornyakov", "Куратор администрации", 5},
        {"Leonid_Bosow", "Старший администратор", 4},
        {"Ivan_Homyakov", "Старший администратор", 4},
        {"Anton_Marshalov", "Администратор", 3},
        {"Artemiy_Mikado", "Администратор", 3},
        {"Denis_Milize", "Администратор", 3},
        {"Anton_Zalutcki", "Администратор", 3},
        {"Daniel_Harrington", "Младший администратор", 2},
        {"Aleksey_Novgorodskiy", "Младший администратор", 2},
        {"Wyatt_Yeat", "Младший администратор", 2},
        {"Nikolay_Bosow", "Модератор", 1},
        {"Jet_Rakhimov", "Модератор", 1},
        {"August_Verstappen", "Модератор", 1},
        {"Kevin_Reichelderfer", "Модератор", 1},
        {"Mark_Admiralov", "Модератор", 1},
        {"Michael_Mikado", "Модератор", 1},
        {"Sergey_Sheremetev", "Модератор", 1},
        {"Mars_Holmes", "Модератор", 1},
        {"Valentin_Mikado", "Модератор", 1},
        {"Zagid_Mikado", "Модератор", 1},
        {"Nathaniel_Revazov", "Модератор", 1},
        {"Felix_Kogut", "Модератор", 1},
        {"Lina_Reichelderfer", "Модератор", 1},
        {"Daniele_Homyakov", "Модератор", 1},
        {"Nikolay_Wolf", "Модератор", 1},
        {"Potap_Hennessy", "Модератор", 1},
        {"Fedor_Santoni", "Модератор", 1},
        {"Matteo_Williams", "Модератор", 1},
        {"Cary_Mikado", "Модератор", 1},
        {"Maxim_Sharganov", "Модератор", 1},
        {"Aleksey_Akimov", "Модератор", 1},
        {"Chapman_Navarro", "Модератор", 1},
        {"Pavel_Homyakov", "Модератор", 1},
        {"Arsenii_Harrington", "Модератор", 1},
        {"Ryan_Lindberg", "Модератор", 1},
        {"Hariton_Bosow", "Модератор", 1},
        {"Anthony_Verov", "Модератор", 1},
        {"Mark_Lotkov", "Модератор", 1},
        {"Nikita_Muver", "Модератор", 1},
        {"Ethan_Mikado", "Модератор", 1},
        {"Christopher_Winchester", "Модератор", 1},
        {"Khavr_Zakharov", "Модератор", 1}
    };
}

void AdminDatabase::SetServer(int serverNum)
{
    if (serverNum < 1) serverNum = 1;
    if (serverNum > 7) serverNum = 7;
    {
        std::scoped_lock lock(m_mutex);
        m_server = serverNum;
        m_status = "База: загружено " + std::to_string(m_admins[m_server].size())
            + " админов (Сервер " + std::to_string(m_server) + ")";
    }
    SaveSettings();
}

int AdminDatabase::GetServer() const
{
    std::scoped_lock lock(m_mutex);
    return m_server;
}

bool AdminDatabase::IsUpdating() const
{
    return m_updating.load();
}

std::string AdminDatabase::GetStatusString() const
{
    std::scoped_lock lock(m_mutex);
    return m_status;
}

std::vector<AdminRecord> AdminDatabase::GetCurrentServerAdmins() const
{
    std::scoped_lock lock(m_mutex);
    if (m_server >= 1 && m_server <= 7)
        return m_admins[m_server];
    return {};
}

bool AdminDatabase::FindAdmin(const std::string& nick, AdminRecord& outInfo) const
{
    std::scoped_lock lock(m_mutex);
    if (m_server < 1 || m_server > 7) return false;

    std::string needle = nick;
    std::replace(needle.begin(), needle.end(), ' ', '_');

    for (const auto& a : m_admins[m_server])
    {
        if (_stricmp(a.nick.c_str(), needle.c_str()) == 0)
        {
            outInfo = a;
            return true;
        }
    }
    return false;
}

void AdminDatabase::TriggerToast(const std::string& nick, const std::string& role, int level)
{
    if (!toastEnabled) return;
    std::scoped_lock lock(m_toastMutex);
    m_toast.active = true;
    m_toast.nick = nick;
    m_toast.role = role.empty() ? "Администратор" : role;
    m_toast.level = level > 0 ? level : 3;
    m_toast.startTime = std::chrono::steady_clock::now();
    m_toast.durationSec = 4.5f;
}

AdminToastData AdminDatabase::GetToastData() const
{
    std::scoped_lock lock(m_toastMutex);
    return m_toast;
}

bool AdminDatabase::IsToastActive() const
{
    std::scoped_lock lock(m_toastMutex);
    if (!m_toast.active) return false;
    auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_toast.startTime).count();
    return elapsed < m_toast.durationSec;
}

void AdminDatabase::LoadSettings()
{
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        std::wstring s = path;
        auto sep = s.find_last_of(L"\\/");
        std::wstring ini = (sep != std::wstring::npos ? s.substr(0, sep + 1) : L"") + L"settings.ini";

        m_server = GetPrivateProfileIntW(L"AdminChecker", L"Server", 1, ini.c_str());
        if (m_server < 1 || m_server > 7) m_server = 1;
        hudEnabled = GetPrivateProfileIntW(L"AdminChecker", L"HudEnabled", 1, ini.c_str()) != 0;
        hudConfigMode = GetPrivateProfileIntW(L"AdminChecker", L"HudConfigMode", 0, ini.c_str()) != 0;
        toastEnabled = GetPrivateProfileIntW(L"AdminChecker", L"ToastEnabled", 1, ini.c_str()) != 0;
        int alphaInt = GetPrivateProfileIntW(L"AdminChecker", L"HudAlpha", 85, ini.c_str());
        hudAlpha = static_cast<float>(alphaInt) / 100.0f;
        int scaleInt = GetPrivateProfileIntW(L"AdminChecker", L"HudScale", 100, ini.c_str());
        hudScale = static_cast<float>(scaleInt) / 100.0f;
    }
}

void AdminDatabase::SaveSettings()
{
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        std::wstring s = path;
        auto sep = s.find_last_of(L"\\/");
        std::wstring ini = (sep != std::wstring::npos ? s.substr(0, sep + 1) : L"") + L"settings.ini";

        WritePrivateProfileStringW(L"AdminChecker", L"Server", std::to_wstring(m_server).c_str(), ini.c_str());
        WritePrivateProfileStringW(L"AdminChecker", L"HudEnabled", hudEnabled ? L"1" : L"0", ini.c_str());
        WritePrivateProfileStringW(L"AdminChecker", L"HudConfigMode", hudConfigMode ? L"1" : L"0", ini.c_str());
        WritePrivateProfileStringW(L"AdminChecker", L"ToastEnabled", toastEnabled ? L"1" : L"0", ini.c_str());
        WritePrivateProfileStringW(L"AdminChecker", L"HudAlpha", std::to_wstring(static_cast<int>(hudAlpha * 100.0f)).c_str(), ini.c_str());
        WritePrivateProfileStringW(L"AdminChecker", L"HudScale", std::to_wstring(static_cast<int>(hudScale * 100.0f)).c_str(), ini.c_str());
    }
}

void AdminDatabase::SaveCache()
{
    std::wstring path = GetCacheFilePath();
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;

    f << "{\n  \"servers\": {\n";
    for (int s = 1; s <= 6; ++s)
    {
        f << "    \"" << s << "\": [\n";
        for (size_t i = 0; i < m_admins[s].size(); ++i)
        {
            const auto& a = m_admins[s][i];
            f << "      {\"nick\": \"" << a.nick << "\", \"role\": \"" << a.role << "\", \"level\": " << a.level << "}";
            if (i + 1 < m_admins[s].size()) f << ",";
            f << "\n";
        }
        f << "    ]" << (s < 6 ? "," : "") << "\n";
    }
    f << "  }\n}\n";
    f.close();
}

void AdminDatabase::LoadCache()
{
    std::wstring path = GetCacheFilePath();
    std::ifstream f(path);
    if (!f.is_open())
    {
        SaveCache();
        return;
    }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (content.empty()) return;

    // Simple line-based JSON parsing for servers 1..6
    std::scoped_lock lock(m_mutex);
    for (int s = 1; s <= 6; ++s)
    {
        std::string serverKey = "\"" + std::to_string(s) + "\":";
        size_t sPos = content.find(serverKey);
        if (sPos == std::string::npos) continue;

        size_t arrStart = content.find('[', sPos);
        size_t arrEnd = content.find(']', arrStart);
        if (arrStart == std::string::npos || arrEnd == std::string::npos) continue;

        std::string block = content.substr(arrStart, arrEnd - arrStart);
        std::vector<AdminRecord> list;

        size_t pos = 0;
        while ((pos = block.find('{', pos)) != std::string::npos)
        {
            size_t endObj = block.find('}', pos);
            if (endObj == std::string::npos) break;
            std::string item = block.substr(pos, endObj - pos);

            AdminRecord rec;
            auto getVal = [&](const std::string& key) -> std::string {
                size_t kp = item.find("\"" + key + "\"");
                if (kp == std::string::npos) return "";
                size_t col = item.find(':', kp);
                if (col == std::string::npos) return "";
                size_t q1 = item.find('"', col);
                if (q1 != std::string::npos && q1 < item.size()) {
                    size_t q2 = item.find('"', q1 + 1);
                    if (q2 != std::string::npos) return item.substr(q1 + 1, q2 - q1 - 1);
                }
                return "";
            };
            rec.nick = getVal("nick");
            rec.role = getVal("role");
            size_t lp = item.find("\"level\"");
            if (lp != std::string::npos) {
                size_t col = item.find(':', lp);
                if (col != std::string::npos) {
                    rec.level = std::atoi(item.c_str() + col + 1);
                }
            }
            if (!rec.nick.empty()) {
                list.push_back(rec);
            }
            pos = endObj + 1;
        }

        if (!list.empty())
        {
            m_admins[s] = list;
        }
    }
}

void AdminDatabase::LoadServer7File()
{
    std::wstring path = GetServer7FilePath();
    std::ifstream f(path);
    if (!f.is_open())
    {
        // Write default Server 7 file
        std::ofstream out(path, std::ios::trunc);
        if (out.is_open())
        {
            out << "{\n  \"server\": 7,\n  \"comment\": \"Discord server - manual / offline roster\",\n  \"admins\": [\n";
            for (size_t i = 0; i < m_admins[7].size(); ++i)
            {
                const auto& a = m_admins[7][i];
                out << "    {\"nick\": \"" << a.nick << "\", \"role\": \"" << a.role << "\", \"level\": " << a.level << "}";
                if (i + 1 < m_admins[7].size()) out << ",";
                out << "\n";
            }
            out << "  ]\n}\n";
            out.close();
        }
        return;
    }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (content.empty()) return;

    std::vector<AdminRecord> list;
    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos)
    {
        size_t endObj = content.find('}', pos);
        if (endObj == std::string::npos) break;
        std::string item = content.substr(pos, endObj - pos);

        AdminRecord rec;
        auto getVal = [&](const std::string& key) -> std::string {
            size_t kp = item.find("\"" + key + "\"");
            if (kp == std::string::npos) return "";
            size_t col = item.find(':', kp);
            if (col == std::string::npos) return "";
            size_t q1 = item.find('"', col);
            if (q1 != std::string::npos && q1 < item.size()) {
                size_t q2 = item.find('"', q1 + 1);
                if (q2 != std::string::npos) return item.substr(q1 + 1, q2 - q1 - 1);
            }
            return "";
        };
        rec.nick = getVal("nick");
        rec.role = getVal("role");
        size_t lp = item.find("\"level\"");
        if (lp != std::string::npos) {
            size_t col = item.find(':', lp);
            if (col != std::string::npos) rec.level = std::atoi(item.c_str() + col + 1);
        }
        if (!rec.nick.empty()) list.push_back(rec);
        pos = endObj + 1;
    }

    if (!list.empty())
    {
        std::scoped_lock lock(m_mutex);
        m_admins[7] = list;
    }
}

bool AdminDatabase::FetchForumTopic(int serverNum, std::string& outHtml)
{
    outHtml.clear();
    const wchar_t* urls[7] = {
        nullptr,
        L"https://forum.gtaprovince.ru/topic/463294-spisok-administracii-pervogo-servera/",
        L"https://forum.gtaprovince.ru/topic/712741-administraciya-servera/",
        L"https://forum.gtaprovince.ru/topic/352456-province-rp-3-administraciya-servera/",
        L"https://forum.gtaprovince.ru/topic/832968-spisok-administratorov-servera/",
        L"https://forum.gtaprovince.ru/topic/903046-spisok-administracii-servera/",
        L"https://forum.gtaprovince.ru/topic/1094510-administraciya-servera/"
    };
    if (serverNum < 1 || serverNum > 6 || !urls[serverNum]) return false;

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t hostName[256]{};
    wchar_t urlPath[2048]{};
    components.lpszHostName = hostName;
    components.dwHostNameLength = ARRAYSIZE(hostName);
    components.lpszUrlPath = urlPath;
    components.dwUrlPathLength = ARRAYSIZE(urlPath);

    if (!WinHttpCrackUrl(urls[serverNum], static_cast<DWORD>(wcslen(urls[serverNum])), 0, &components))
        return false;

    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 4000, 4000, 4000, 6000);

    HINTERNET hConnect = WinHttpConnect(hSession, components.lpszHostName, components.nPort, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD reqFlags = (components.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", components.lpszUrlPath, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    const wchar_t* headers = L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Language: ru-RU,ru;q=0.9,en-US;q=0.8\r\n";
    BOOL sent = WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    std::vector<char> buffer;
    do
    {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
        size_t prev = buffer.size();
        buffer.resize(prev + dwSize);
        if (!WinHttpReadData(hRequest, buffer.data() + prev, dwSize, &dwDownloaded)) break;
        buffer.resize(prev + dwDownloaded);
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!buffer.empty())
    {
        outHtml.assign(buffer.data(), buffer.size());
        return true;
    }
    return false;
}

void AdminDatabase::ParseTopicHtml(int serverNum, const std::string& html)
{
    if (html.empty() || serverNum < 1 || serverNum > 6) return;

    // Check if forum returned JS challenge instead of topic
    if (html.find("setup_utm") != std::string::npos || html.find("get_jhash") != std::string::npos)
    {
        // Challenge page - keep cached version
        return;
    }

    // Isolate first post content if possible
    std::string post = html;
    size_t postStart = html.find("data-role=\"commentContent\"");
    if (postStart != std::string::npos)
    {
        size_t postEnd = html.find("</article>", postStart);
        if (postEnd != std::string::npos)
            post = html.substr(postStart, postEnd - postStart);
    }

    std::string cleanText = StripHtmlTags(post);
    std::istringstream stream(cleanText);
    std::string line;

    std::vector<AdminRecord> parsed;
    int currentLevel = 3;
    std::string currentRole = "Администратор";

    while (std::getline(stream, line))
    {
        if (line.empty()) continue;

        // Detect section headers
        if (line.find("Главная администрация") != std::string::npos || line.find("Руководство сервера") != std::string::npos)
        {
            currentLevel = 7;
            currentRole = "Главный администратор";
        }
        else if (line.find("Заместитель") != std::string::npos || line.find("ЗГА") != std::string::npos)
        {
            currentLevel = 6;
            currentRole = "Зам. главного администратора";
        }
        else if (line.find("Куратор") != std::string::npos)
        {
            currentLevel = 5;
            currentRole = "Куратор администрации";
        }
        else if (line.find("Старшая администрация") != std::string::npos || line.find("Старшие администраторы") != std::string::npos)
        {
            currentLevel = 4;
            currentRole = "Старший администратор";
        }
        else if (line.find("Администраторы") != std::string::npos)
        {
            currentLevel = 3;
            currentRole = "Администратор";
        }
        else if (line.find("Младшая администрация") != std::string::npos || line.find("Младшие администраторы") != std::string::npos)
        {
            currentLevel = 2;
            currentRole = "Младший администратор";
        }
        else if (line.find("Модераторы") != std::string::npos)
        {
            currentLevel = 1;
            currentRole = "Модератор";
        }

        // Regex scan for Nick_Name in line
        static const std::regex nickRegex(R"([A-ZА-ЯЁ][a-zа-яё]+_[A-ZА-ЯЁ][a-zа-яё]+)");
        std::sregex_iterator it(line.begin(), line.end(), nickRegex);
        std::sregex_iterator end;
        while (it != end)
        {
            std::string nick = it->str();
            bool exists = false;
            for (const auto& existing : parsed)
            {
                if (existing.nick == nick) { exists = true; break; }
            }
            if (!exists)
            {
                parsed.push_back({nick, currentRole, currentLevel});
            }
            ++it;
        }
    }

    if (parsed.size() >= 5)
    {
        std::scoped_lock lock(m_mutex);
        m_admins[serverNum] = parsed;
        SaveCache();
    }
}

void AdminDatabase::StartUpdateAsync()
{
    if (m_updating.exchange(true)) return;

    int serverNum = m_server;
    m_status = "Синхронизация с форумом (Сервер " + std::to_string(serverNum) + ")...";

    std::thread([this, serverNum]() {
        if (serverNum == 7)
        {
            LoadServer7File();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::scoped_lock lock(m_mutex);
            m_status = "База: загружено " + std::to_string(m_admins[7].size()) + " админов (Сервер 7) [из файла]";
            m_updating.store(false);
            return;
        }

        std::string html;
        bool ok = FetchForumTopic(serverNum, html);
        if (ok)
        {
            ParseTopicHtml(serverNum, html);
        }

        {
            std::scoped_lock lock(m_mutex);
            size_t count = m_admins[serverNum].size();
            if (ok && count > 0)
            {
                m_status = "База: загружено " + std::to_string(count) + " админов (Сервер " + std::to_string(serverNum) + ") [обновлено]";
            }
            else
            {
                m_status = "База: загружено " + std::to_string(count) + " админов (Сервер " + std::to_string(serverNum) + ") [кэш]";
            }
        }
        m_updating.store(false);
    }).detach();
}
