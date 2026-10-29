#pragma once

#include <Windows.h>
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../Shared/ashes_editions.h"
#include "../Shared/snow_ipc.h"
#include "../Client/third_party/imgui/imgui.h"

namespace TerminalMgr
{

struct ServerEndpoint
{
    const char* name;
    const char* ip;
    std::uint16_t port;
};

inline const ServerEndpoint kProvinceServers[] = {
    {"Сервер #1", "185.71.66.80", 22003},
    {"Сервер #2", "185.71.66.70", 22003},
    {"Сервер #3", "185.71.66.79", 22003},
    {"Сервер #4", "185.71.66.64", 22003},
    {"Сервер #5", "185.71.66.66", 22003},
    {"Сервер #6", "185.71.66.81", 22003},
    {"Сервер #7", "185.71.66.88", 22003}
};

struct TaskOption
{
    const char* id;
    const char* displayName;
};

inline const TaskOption kAvailableTasks[] = {
    {"default_route", "Трамвай (Маршрут)"},
    {"snow_plow", "Снегоуборочная (Поливалка)"},
    {"jbk_factory", "Завод ЖТУ (Пешеход)"},
    {"lesorub", "Лесоруб"},
    {"tobacco", "Табачная фабрика"}
};

// 1. Windows DPAPI Encryption for Passwords
inline std::string EncryptPassword(const std::string& plaintext)
{
    if (plaintext.empty()) return {};
    DATA_BLOB inBlob{ static_cast<DWORD>(plaintext.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data())) };
    DATA_BLOB outBlob{};
    if (CryptProtectData(&inBlob, L"AshesAccountPassword", nullptr, nullptr, nullptr, 0, &outBlob))
    {
        std::string hex;
        hex.reserve(outBlob.cbData * 2);
        for (DWORD i = 0; i < outBlob.cbData; ++i)
        {
            char buf[3]{};
            snprintf(buf, sizeof(buf), "%02X", outBlob.pbData[i]);
            hex += buf;
        }
        LocalFree(outBlob.pbData);
        return hex;
    }
    return plaintext;
}

inline std::string DecryptPassword(const std::string& hexCipher)
{
    if (hexCipher.empty() || hexCipher.size() % 2 != 0) return {};
    std::vector<BYTE> data(hexCipher.size() / 2);
    for (size_t i = 0; i < data.size(); ++i)
    {
        unsigned int b = 0;
        if (sscanf_s(hexCipher.c_str() + i * 2, "%02x", &b) != 1) return {};
        data[i] = static_cast<BYTE>(b);
    }
    DATA_BLOB inBlob{ static_cast<DWORD>(data.size()), data.data() };
    DATA_BLOB outBlob{};
    if (CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob))
    {
        std::string plain(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
        LocalFree(outBlob.pbData);
        return plain;
    }
    return {};
}

struct AccountProfile
{
    std::string login{"player_1"};
    std::string encryptedPassword;
    int serverIndex{0};
    int taskIndex{0};
    bool autoReconnect{true};
    int reconnectDelaySec{15};
    std::string proxy;
};

struct TerminalSession
{
    std::string profileLogin;
    std::string serverName;
    std::string taskName;
    std::string status{"OFFLINE"};
    int cycles{0};
    SnowIPC::SnowTelemetry telemetry{};
    std::mutex chatMutex;
    std::deque<SnowIPC::ChatMessage> chatMessages;
    HANDLE hProcess{nullptr};
    HANDLE hStdInWrite{nullptr};
    HANDLE hStdOutRead{nullptr};
    std::thread readerThread;
    std::mutex logMutex;
    std::deque<std::string> logLines;
    bool isRunning{false};
    bool showConsole{false};
    bool userStopped{false};

    void SendCommand(const std::string& cmd)
    {
        if (!hStdInWrite || !isRunning) return;
        std::string full = cmd + "\n";
        DWORD written = 0;
        WriteFile(hStdInWrite, full.c_str(), static_cast<DWORD>(full.size()), &written, nullptr);
        FlushFileBuffers(hStdInWrite);
    }

    void SendChatMessage(const std::string& text)
    {
        if (text.empty()) return;
        SendCommand("CMD_SEND_CHAT " + text);

        SnowIPC::ChatMessage msg;
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm tmBuf{};
        localtime_s(&tmBuf, &now);
        char tBuf[16]{};
        strftime(tBuf, sizeof(tBuf), "%H:%M:%S", &tmBuf);
        msg.timestamp = tBuf;
        msg.sender = "Вы (" + profileLogin + ")";
        msg.text = text;
        msg.color = IM_COL32(100, 205, 255, 255);

        std::lock_guard<std::mutex> lock(chatMutex);
        chatMessages.push_back(msg);
        if (chatMessages.size() > 100) chatMessages.pop_front();
    }

    void TogglePause()
    {
        telemetry.isPaused = !telemetry.isPaused;
        if (telemetry.isPaused)
        {
            SendCommand("CMD_PAUSE");
            status = "PAUSED";
            telemetry.status = SnowIPC::SessionStatus::Paused;
        }
        else
        {
            SendCommand("CMD_START");
            status = "WORKING";
            telemetry.status = SnowIPC::SessionStatus::InWork;
        }
    }
};

class TerminalManager
{
public:
    static TerminalManager& Instance()
    {
        static TerminalManager instance;
        return instance;
    }

    void Initialize(const std::filesystem::path& workDir)
    {
        workDir_ = workDir;
        LoadAccounts();
    }

    std::vector<AccountProfile>& GetAccounts() { return accounts_; }
    std::vector<std::shared_ptr<TerminalSession>>& GetSessions() { return sessions_; }

    void SaveAccounts()
    {
        std::filesystem::path path = workDir_ / L"accounts.json";
        std::ofstream file(path);
        if (!file.is_open()) return;

        file << "[\n";
        for (size_t i = 0; i < accounts_.size(); ++i)
        {
            const auto& acc = accounts_[i];
            file << "  {\n"
                 << "    \"login\": \"" << acc.login << "\",\n"
                 << "    \"password\": \"" << acc.encryptedPassword << "\",\n"
                 << "    \"server\": " << acc.serverIndex << ",\n"
                 << "    \"task\": " << acc.taskIndex << ",\n"
                 << "    \"auto_reconnect\": " << (acc.autoReconnect ? "true" : "false") << ",\n"
                 << "    \"reconnect_delay\": " << acc.reconnectDelaySec << ",\n"
                 << "    \"proxy\": \"" << acc.proxy << "\"\n"
                 << "  }" << (i + 1 < accounts_.size() ? "," : "") << "\n";
        }
        file << "]\n";
    }

    void LoadAccounts()
    {
        accounts_.clear();
        std::filesystem::path path = workDir_ / L"accounts.json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            // Create default profile if empty
            AccountProfile defaultAcc;
            defaultAcc.login = "user_login";
            defaultAcc.encryptedPassword = EncryptPassword("user_password");
            accounts_.push_back(defaultAcc);
            SaveAccounts();
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // Simple line parser for accounts.json
        std::istringstream stream(content);
        std::string line;
        AccountProfile curr;
        bool inObj = false;

        while (std::getline(stream, line))
        {
            if (line.find('{') != std::string::npos)
            {
                curr = AccountProfile{};
                inObj = true;
            }
            if (!inObj) continue;

            auto extractStr = [](const std::string& l, const std::string& key) -> std::string {
                auto pos = l.find("\"" + key + "\"");
                if (pos == std::string::npos) return {};
                auto colon = l.find(':', pos);
                if (colon == std::string::npos) return {};
                auto q1 = l.find('"', colon);
                if (q1 == std::string::npos) return {};
                auto q2 = l.find('"', q1 + 1);
                if (q2 == std::string::npos) return {};
                return l.substr(q1 + 1, q2 - q1 - 1);
            };

            auto extractInt = [](const std::string& l, const std::string& key, int def) -> int {
                auto pos = l.find("\"" + key + "\"");
                if (pos == std::string::npos) return def;
                auto colon = l.find(':', pos);
                if (colon == std::string::npos) return def;
                int val = def;
                sscanf_s(l.c_str() + colon + 1, "%d", &val);
                return val;
            };

            std::string s = extractStr(line, "login");
            if (!s.empty()) curr.login = s;
            s = extractStr(line, "password");
            if (!s.empty()) curr.encryptedPassword = s;
            curr.serverIndex = extractInt(line, "server", curr.serverIndex);
            curr.taskIndex = extractInt(line, "task", curr.taskIndex);
            if (line.find("\"auto_reconnect\"") != std::string::npos)
                curr.autoReconnect = line.find("true") != std::string::npos;
            curr.reconnectDelaySec = extractInt(line, "reconnect_delay", curr.reconnectDelaySec);
            std::string pxy = extractStr(line, "proxy");
            if (!pxy.empty()) curr.proxy = pxy;

            if (line.find('}') != std::string::npos)
            {
                if (!curr.login.empty()) accounts_.push_back(curr);
                inObj = false;
            }
        }
        if (accounts_.empty())
        {
            AccountProfile defaultAcc;
            defaultAcc.login = "user_login";
            defaultAcc.encryptedPassword = EncryptPassword("user_password");
            accounts_.push_back(defaultAcc);
        }
    }

    bool StartTerminal(const AccountProfile& profile, bool showConsole)
    {
        auto session = GetSessionByLogin(profile.login);
        if (session && session->isRunning)
            return false;
        if (!session)
        {
            session = std::make_shared<TerminalSession>();
            session->profileLogin = profile.login;
            sessions_.push_back(session);
        }
        session->userStopped = false;

        std::filesystem::path exePath = Editions::FindSnowExecutable(workDir_);
        if (exePath.empty())
        {
            wchar_t ownModule[MAX_PATH]{};
            GetModuleFileNameW(nullptr, ownModule, MAX_PATH);
            std::filesystem::path ownDir = std::filesystem::path(ownModule).parent_path();
            exePath = Editions::FindSnowExecutable(ownDir);
        }
        if (exePath.empty())
            exePath = Editions::FindSnowExecutable(std::filesystem::current_path());

        if (exePath.empty())
        {
            session->status = "ERROR";
            session->telemetry.status = SnowIPC::SessionStatus::Error;
            std::string err = "[Error] WinterCat.exe не найден в рабочей директории";
            {
                std::lock_guard<std::mutex> lock(session->logMutex);
                session->logLines.push_back(err);
            }
            {
                std::lock_guard<std::mutex> lock(session->chatMutex);
                SnowIPC::ChatMessage msg;
                msg.sender = "Система";
                msg.text = err;
                msg.color = IM_COL32(245, 80, 80, 255);
                session->chatMessages.push_back(msg);
            }
            return false;
        }

        exePath = std::filesystem::absolute(exePath);

        // Generate temporary profile config
        std::filesystem::path cfgPath = std::filesystem::absolute(workDir_ / (L"session_config_" + std::wstring(profile.login.begin(), profile.login.end()) + L".json"));
        std::string plainPass = DecryptPassword(profile.encryptedPassword);
        const auto& srv = kProvinceServers[profile.serverIndex % 7];
        const auto& tsk = kAvailableTasks[profile.taskIndex % 5];

        {
            std::ofstream cfg(cfgPath);
            if (!cfg.is_open())
            {
                session->status = "ERROR";
                session->telemetry.status = SnowIPC::SessionStatus::Error;
                std::string err = "[Error] Не удалось создать session_config: " + cfgPath.string();
                std::lock_guard<std::mutex> lock(session->logMutex);
                session->logLines.push_back(err);
                return false;
            }
            bool isJbk = (std::string(tsk.id) == "jbk_factory");
            float px = isJbk ? 2372.5f : -2745.0f;
            float py = isJbk ? -2426.0f : 2775.0f;
            float pz = isJbk ? 13.5f : 2.4f;

            cfg << "{\n"
                << "  \"account\": {\n"
                << "    \"login\": \"" << profile.login << "\",\n"
                << "    \"password\": \"" << plainPass << "\",\n"
                << "    \"server_ip\": \"" << srv.ip << "\",\n"
                << "    \"server_port\": " << srv.port << "\n"
                << "  },\n"
                << "  \"bot\": {\n"
                << "    \"script_name\": \"" << tsk.id << "\",\n"
                << "    \"route_file\": \"route_recorder_data.log\",\n"
                << "    \"auto_reconnect\": " << (profile.autoReconnect ? "true" : "false") << ",\n"
                << "    \"reconnect_delay\": " << profile.reconnectDelaySec << ",\n"
                << "    \"pickup\": {\n"
                << "      \"enabled\": true,\n"
                << "      \"x\": " << px << ",\n"
                << "      \"y\": " << py << ",\n"
                << "      \"z\": " << pz << ",\n"
                << "      \"radius\": 1.0,\n"
                << "      \"action\": \"alt\"\n"
                << "    }\n"
                << "  }\n"
                << "}\n";
        }

        // Set up anonymous pipes with inherit handle
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE hChildStdOutRead = nullptr, hChildStdOutWrite = nullptr;
        HANDLE hChildStdInRead = nullptr, hChildStdInWrite = nullptr;

        if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &sa, 0) ||
            !SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0))
            return false;

        if (!CreatePipe(&hChildStdInRead, &hChildStdInWrite, &sa, 0) ||
            !SetHandleInformation(hChildStdInWrite, HANDLE_FLAG_INHERIT, 0))
        {
            CloseHandle(hChildStdOutRead);
            CloseHandle(hChildStdOutWrite);
            return false;
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.hStdOutput = hChildStdOutWrite;
        si.hStdError = hChildStdOutWrite;
        si.hStdInput = hChildStdInRead;
        si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi{};
        std::wstring cmdLine = L"\"" + exePath.wstring() + L"\" --config \"" + cfgPath.wstring() + L"\"";
        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(L'\0');

        DWORD createFlags = CREATE_NO_WINDOW;
        BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
            createFlags, nullptr, workDir_.c_str(), &si, &pi);

        CloseHandle(hChildStdOutWrite);
        CloseHandle(hChildStdInRead);

        if (!ok)
        {
            DWORD err = GetLastError();
            CloseHandle(hChildStdOutRead);
            CloseHandle(hChildStdInWrite);
            session->status = "ERROR";
            session->telemetry.status = SnowIPC::SessionStatus::Error;
            std::string errStr = "[Error] Ошибка запуска WinterCat.exe (Код " + std::to_string(err) + ")";
            {
                std::lock_guard<std::mutex> lock(session->logMutex);
                session->logLines.push_back(errStr);
            }
            {
                std::lock_guard<std::mutex> lock(session->chatMutex);
                SnowIPC::ChatMessage msg;
                msg.sender = "Система";
                msg.text = errStr;
                msg.color = IM_COL32(245, 80, 80, 255);
                session->chatMessages.push_back(msg);
            }
            return false;
        }

        CloseHandle(pi.hThread);

        session->serverName = srv.name;
        session->taskName = tsk.displayName;
        session->status = "CONNECTING";
        session->telemetry.status = SnowIPC::SessionStatus::Connecting;
        session->hProcess = pi.hProcess;
        session->hStdInWrite = hChildStdInWrite;
        session->hStdOutRead = hChildStdOutRead;
        session->isRunning = true;
        session->showConsole = showConsole;

        std::filesystem::path debugLogPath = workDir_ / L"winter_cat_debug.log";

        // Background reader thread
        session->readerThread = std::thread([session, debugLogPath]()
        {
            std::ofstream debugFile(debugLogPath, std::ios::app);
            char buffer[512]{};
            std::string lineAccum;
            DWORD bytesRead = 0;

            while (session->isRunning && ReadFile(session->hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                lineAccum.append(buffer, bytesRead);

                size_t nl;
                while ((nl = lineAccum.find('\n')) != std::string::npos)
                {
                    std::string line = lineAccum.substr(0, nl);
                    lineAccum.erase(0, nl + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                        line.pop_back();

                    if (line.empty()) continue;

                    if (debugFile.is_open())
                    {
                        debugFile << "[" << session->profileLogin << "] " << line << "\n";
                        debugFile.flush();
                    }

                    // Parse telemetry & chat
                    if (line.rfind("[TELEMETRY]", 0) == 0)
                    {
                        auto pF = [](const std::string& str, const std::string& k, float def) {
                            auto p = str.find(k + "=");
                            if (p == std::string::npos) return def;
                            float v = def;
                            sscanf_s(str.c_str() + p + k.size() + 1, "%f", &v);
                            return v;
                        };
                        auto pI = [](const std::string& str, const std::string& k, int def) {
                            auto p = str.find(k + "=");
                            if (p == std::string::npos) return def;
                            int v = def;
                            sscanf_s(str.c_str() + p + k.size() + 1, "%d", &v);
                            return v;
                        };
                        session->telemetry.x = pF(line, "x", session->telemetry.x);
                        session->telemetry.y = pF(line, "y", session->telemetry.y);
                        session->telemetry.z = pF(line, "z", session->telemetry.z);
                        session->telemetry.angle = pF(line, "ang", session->telemetry.angle);
                        session->telemetry.health = pF(line, "hp", session->telemetry.health);
                        session->telemetry.ping = pI(line, "ping", session->telemetry.ping);
                        session->telemetry.dimension = pI(line, "dim", session->telemetry.dimension);
                        session->telemetry.interior = pI(line, "int", session->telemetry.interior);
                        session->telemetry.cycles = pI(line, "cyc", session->telemetry.cycles);
                        session->telemetry.targetCheckpoint = pI(line, "chk", session->telemetry.targetCheckpoint);
                        session->telemetry.balance = pI(line, "bal", session->telemetry.balance);
                        session->cycles = session->telemetry.cycles;
                    }
                    else if (line.rfind("[CHAT]", 0) == 0)
                    {
                        std::string chatBody = line.substr(6);
                        while (!chatBody.empty() && chatBody.front() == ' ') chatBody.erase(0, 1);
                        SnowIPC::ChatMessage msg;
                        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        tm tmBuf{};
                        localtime_s(&tmBuf, &now);
                        char tBuf[16]{};
                        strftime(tBuf, sizeof(tBuf), "%H:%M:%S", &tmBuf);
                        msg.timestamp = tBuf;
                        auto colon = chatBody.find(':');
                        if (colon != std::string::npos)
                        {
                            msg.sender = chatBody.substr(0, colon);
                            msg.text = chatBody.substr(colon + 1);
                            while (!msg.text.empty() && msg.text.front() == ' ') msg.text.erase(0, 1);
                        }
                        else { msg.sender = "Чат"; msg.text = chatBody; }

                        if (msg.sender.find("Сервер") != std::string::npos || msg.sender.find("Система") != std::string::npos)
                            msg.color = IM_COL32(235, 195, 60, 255);
                        else if (msg.sender.find("Вы") != std::string::npos)
                            msg.color = IM_COL32(100, 205, 255, 255);
                        else if (msg.sender.find("Админ") != std::string::npos)
                            msg.color = IM_COL32(245, 80, 80, 255);
                        else
                            msg.color = IM_COL32(220, 225, 235, 255);

                        std::lock_guard<std::mutex> lock(session->chatMutex);
                        session->chatMessages.push_back(msg);
                        if (session->chatMessages.size() > 100) session->chatMessages.pop_front();
                    }
                    else if (line.find("[STATUS] CONNECTING") != std::string::npos)
                    {
                        session->status = "CONNECTING";
                        session->telemetry.status = SnowIPC::SessionStatus::Connecting;
                    }
                    else if (line.find("[STATUS] AUTHORIZED") != std::string::npos)
                    {
                        session->status = "AUTHORIZED";
                        session->telemetry.status = SnowIPC::SessionStatus::Authorized;
                    }
                    else if (line.find("[STATUS] WORKING") != std::string::npos)
                    {
                        session->status = "WORKING";
                        session->telemetry.status = SnowIPC::SessionStatus::InWork;
                        auto cPos = line.find("Cycles: ");
                        if (cPos != std::string::npos)
                            sscanf_s(line.c_str() + cPos + 8, "%d", &session->cycles);
                    }
                    else if (line.find("[STATUS] PAUSED") != std::string::npos)
                    {
                        session->status = "PAUSED";
                        session->telemetry.status = SnowIPC::SessionStatus::Paused;
                    }
                    else if (line.find("[STATUS] ERROR") != std::string::npos ||
                             line.find("error:") != std::string::npos ||
                             line.find("[Error]") != std::string::npos ||
                             line.find("Exception") != std::string::npos ||
                             line.find("failed") != std::string::npos)
                    {
                        session->status = "ERROR";
                        session->telemetry.status = SnowIPC::SessionStatus::Error;
                        std::lock_guard<std::mutex> clock(session->chatMutex);
                        if (session->chatMessages.empty() || session->chatMessages.back().text != line)
                        {
                            SnowIPC::ChatMessage emsg;
                            emsg.sender = "Система";
                            emsg.text = line;
                            emsg.color = IM_COL32(245, 80, 80, 255);
                            session->chatMessages.push_back(emsg);
                        }
                    }

                    std::lock_guard<std::mutex> lock(session->logMutex);
                    session->logLines.push_back(line);
                    if (session->logLines.size() > 500)
                        session->logLines.pop_front();
                }
            }

            DWORD exitCode = 0;
            if (session->userStopped)
            {
                session->status = "STOPPED";
                session->telemetry.status = SnowIPC::SessionStatus::Stopped;
            }
            else if (session->hProcess && GetExitCodeProcess(session->hProcess, &exitCode) && exitCode != 0)
            {
                session->status = "ERROR";
                session->telemetry.status = SnowIPC::SessionStatus::Error;
                std::string err = "[Error] Процесс WinterCat завершился с кодом: " + std::to_string(exitCode);
                if (debugFile.is_open())
                {
                    debugFile << "[" << session->profileLogin << "] " << err << "\n";
                    debugFile.flush();
                }
                {
                    std::lock_guard<std::mutex> lock(session->logMutex);
                    session->logLines.push_back(err);
                }
                {
                    std::lock_guard<std::mutex> lock(session->chatMutex);
                    if (session->chatMessages.empty() || session->chatMessages.back().text != err)
                    {
                        SnowIPC::ChatMessage emsg;
                        emsg.sender = "Система";
                        emsg.text = err;
                        emsg.color = IM_COL32(245, 80, 80, 255);
                        session->chatMessages.push_back(emsg);
                    }
                }
            }
            else if (session->status != "ERROR")
            {
                session->status = "STOPPED";
                session->telemetry.status = SnowIPC::SessionStatus::Stopped;
            }
            session->isRunning = false;
        });

        return true;
    }

    void StopTerminal(std::shared_ptr<TerminalSession> session)
    {
        if (!session || !session->isRunning) return;

        session->userStopped = true;
        session->isRunning = false;
        session->status = "STOPPED";
        session->telemetry.status = SnowIPC::SessionStatus::Stopped;

        std::thread([session]() {
            if (session->hStdInWrite)
            {
                const char* cmd = "STOP\n";
                DWORD written = 0;
                WriteFile(session->hStdInWrite, cmd, static_cast<DWORD>(strlen(cmd)), &written, nullptr);
                FlushFileBuffers(session->hStdInWrite);
            }

            if (session->hProcess)
            {
                if (WaitForSingleObject(session->hProcess, 800) == WAIT_TIMEOUT)
                {
                    TerminateProcess(session->hProcess, 0);
                }
                CloseHandle(session->hProcess);
                session->hProcess = nullptr;
            }

            if (session->hStdInWrite)
            {
                CloseHandle(session->hStdInWrite);
                session->hStdInWrite = nullptr;
            }
        }).detach();
    }

    void StartAllActive()
    {
        for (const auto& acc : accounts_)
        {
            auto existing = GetSessionByLogin(acc.login);
            if (!existing || !existing->isRunning)
            {
                StartTerminal(acc, false);
            }
        }
    }

    void StopAllActive()
    {
        for (auto& s : sessions_)
        {
            if (s && s->isRunning)
                StopTerminal(s);
        }
    }

    std::shared_ptr<TerminalSession> GetSessionByLogin(const std::string& login)
    {
        for (auto& s : sessions_)
        {
            if (s && s->profileLogin == login)
                return s;
        }
        return nullptr;
    }

    void AddAccount(const AccountProfile& acc)
    {
        accounts_.push_back(acc);
        SaveAccounts();
    }

    void RemoveAccount(size_t index)
    {
        if (index < accounts_.size())
        {
            auto session = GetSessionByLogin(accounts_[index].login);
            if (session && session->isRunning)
                StopTerminal(session);
            accounts_.erase(accounts_.begin() + index);
            SaveAccounts();
        }
    }

    void ShutdownAll()
    {
        StopAllActive();
    }

private:
    TerminalManager() = default;
    ~TerminalManager() { ShutdownAll(); }

    std::filesystem::path workDir_;
    std::vector<AccountProfile> accounts_;
    std::vector<std::shared_ptr<TerminalSession>> sessions_;
};

} // namespace TerminalMgr
