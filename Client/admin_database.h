#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

struct AdminRecord {
    std::string nick;
    std::string role;
    int level{1};
    bool isOnline{false};
    bool isSpectating{false};
};

struct AdminToastData {
    bool active{false};
    std::string nick;
    std::string role;
    int level{1};
    std::chrono::steady_clock::time_point startTime;
    float durationSec{4.5f};
};

class AdminDatabase {
public:
    static AdminDatabase& Instance();

    void Initialize();
    void SetServer(int serverNum); // 1..7
    int GetServer() const;

    bool IsUpdating() const;
    void StartUpdateAsync();
    std::string GetStatusString() const;

    std::vector<AdminRecord> GetCurrentServerAdmins() const;
    bool FindAdmin(const std::string& nick, AdminRecord& outInfo) const;

    void TriggerToast(const std::string& nick, const std::string& role, int level);
    AdminToastData GetToastData() const;
    bool IsToastActive() const;

    // HUD & Toast Settings
    bool hudEnabled{true};
    bool hudConfigMode{false};
    bool toastEnabled{true};
    float hudAlpha{0.85f};
    float hudScale{1.0f};

    void LoadSettings();
    void SaveSettings();

    // Export & Storage
    void SaveCache();
    void LoadCache();
    void LoadServer7File();

private:
    AdminDatabase();
    ~AdminDatabase() = default;

    AdminDatabase(const AdminDatabase&) = delete;
    AdminDatabase& operator=(const AdminDatabase&) = delete;

    void PopulateDefaults();
    bool FetchForumTopic(int serverNum, std::string& outHtml);
    void ParseTopicHtml(int serverNum, const std::string& html);

    std::wstring GetCacheFilePath() const;
    std::wstring GetServer7FilePath() const;

    mutable std::mutex m_mutex;
    int m_server{1};
    std::atomic<bool> m_updating{false};
    std::string m_status{"База инициализируется..."};

    // Servers 1..7 indexed 1..7
    std::vector<AdminRecord> m_admins[8];

    mutable std::mutex m_toastMutex;
    AdminToastData m_toast;
};
