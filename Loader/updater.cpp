#include "updater.h"
#include "path_utils.h"

#include <Windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <shellapi.h>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace AshesUpdater
{
namespace
{
std::string ToLower(std::string_view s)
{
    std::string res;
    res.reserve(s.size());
    for (char c : s)
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return res;
}

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

std::string CalculateSha256(const std::wstring& filePath)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return "";

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        CloseHandle(hFile);
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return "";
    }

    BYTE buffer[8192];
    DWORD bytesRead = 0;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
    {
        CryptHashData(hHash, buffer, bytesRead, 0);
    }
    CloseHandle(hFile);

    BYTE hash[32];
    DWORD hashLen = sizeof(hash);
    std::string hexHash;
    if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0))
    {
        std::ostringstream ss;
        for (DWORD i = 0; i < hashLen; ++i)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        hexHash = ss.str();
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return ToLower(hexHash);
}

bool HttpDownloadData(const std::wstring& fullUrl, std::vector<BYTE>& outData)
{
    outData.clear();

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t hostName[256]{};
    wchar_t urlPath[2048]{};
    components.lpszHostName = hostName;
    components.dwHostNameLength = ARRAYSIZE(hostName);
    components.lpszUrlPath = urlPath;
    components.dwUrlPathLength = ARRAYSIZE(urlPath);

    if (!WinHttpCrackUrl(fullUrl.c_str(), static_cast<DWORD>(fullUrl.length()), 0, &components))
        return false;

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AshesUpdater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 5000);

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

    bool success = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200)
        {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do
            {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                    break;
                if (dwSize == 0)
                {
                    success = true;
                    break;
                }

                size_t prevSize = outData.size();
                outData.resize(prevSize + dwSize);
                if (!WinHttpReadData(hRequest, outData.data() + prevSize, dwSize, &dwDownloaded))
                {
                    outData.resize(prevSize);
                    success = false;
                    break;
                }
                outData.resize(prevSize + dwDownloaded);
            } while (dwSize > 0);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}

bool HttpDownloadFile(const std::wstring& fullUrl, const std::wstring& targetPath)
{
    std::vector<BYTE> data;
    if (!HttpDownloadData(fullUrl, data) || data.empty())
        return false;

    HANDLE hFile = CreateFileW(targetPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    BOOL res = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(hFile);
    return res && (written == data.size());
}

std::string FindJsonStringValue(std::string_view json, std::string_view key)
{
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";

    pos = json.find('\"', pos + 1);
    if (pos == std::string::npos) return "";

    size_t end = json.find('\"', pos + 1);
    if (end == std::string::npos) return "";

    return std::string(json.substr(pos + 1, end - pos - 1));
}

UpdateManifest ParseManifest(const std::string& json)
{
    UpdateManifest manifest{};

    size_t loaderObjPos = json.find("\"loader\"");
    if (loaderObjPos != std::string::npos)
    {
        size_t blockStart = json.find('{', loaderObjPos);
        size_t blockEnd = json.find('}', blockStart);
        if (blockStart != std::string::npos && blockEnd != std::string::npos)
        {
            std::string_view loaderBlock(json.data() + blockStart, blockEnd - blockStart + 1);
            manifest.loaderVersion = FindJsonStringValue(loaderBlock, "version");
            manifest.loaderUrl = Utf8ToWide(FindJsonStringValue(loaderBlock, "url"));
        }
    }

    size_t filesPos = json.find("\"files\"");
    if (filesPos != std::string::npos)
    {
        size_t arrStart = json.find('[', filesPos);
        size_t arrEnd = json.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos)
        {
            size_t cur = arrStart + 1;
            while (cur < arrEnd)
            {
                size_t objStart = json.find('{', cur);
                if (objStart == std::string::npos || objStart >= arrEnd) break;
                size_t objEnd = json.find('}', objStart);
                if (objEnd == std::string::npos || objEnd > arrEnd) break;

                std::string_view fileBlock(json.data() + objStart, objEnd - objStart + 1);
                ManifestFile file{};
                file.name = Utf8ToWide(FindJsonStringValue(fileBlock, "name"));
                file.url = Utf8ToWide(FindJsonStringValue(fileBlock, "url"));
                file.sha256 = ToLower(FindJsonStringValue(fileBlock, "sha256"));

                if (!file.name.empty() && !file.url.empty())
                {
                    manifest.files.push_back(std::move(file));
                }
                cur = objEnd + 1;
            }
        }
    }

    manifest.valid = (!manifest.loaderVersion.empty() || !manifest.files.empty());
    return manifest;
}

int ParseVersionComponent(std::string_view s, size_t& offset)
{
    int val = 0;
    while (offset < s.size() && (s[offset] == 'v' || s[offset] == '.' || s[offset] == ' '))
        offset++;
    while (offset < s.size() && std::isdigit(static_cast<unsigned char>(s[offset])))
    {
        val = val * 10 + (s[offset] - '0');
        offset++;
    }
    return val;
}

bool IsNewerVersion(std::string_view remote, std::string_view current)
{
    size_t rOff = 0, cOff = 0;
    for (int i = 0; i < 3; ++i)
    {
        int rVal = ParseVersionComponent(remote, rOff);
        int cVal = ParseVersionComponent(current, cOff);
        if (rVal > cVal) return true;
        if (rVal < cVal) return false;
    }
    return false;
}

void ExecuteSelfUpdate(const std::wstring& directory, const std::wstring& updateExePath)
{
    wchar_t currentExe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, currentExe, MAX_PATH);

    // cmd.exe /c timeout /t 1 /nobreak > nul & del /f /q "current" & move /y "update" "current" & start "" "current"
    std::wstring cmd = L"cmd.exe /c timeout /t 1 /nobreak > nul & del /f /q \"" + std::wstring(currentExe) +
        L"\" & move /y \"" + updateExePath + L"\" \"" + std::wstring(currentExe) +
        L"\" & start \"\" \"" + std::wstring(currentExe) + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        ExitProcess(0);
    }
}
} // namespace

bool CheckAndPerformUpdates(const std::wstring& directory, const LogCallback& log)
{
    log(L"[updater] Проверка обновлений (GitHub Releases)...");

    std::vector<BYTE> manifestData;
    if (!HttpDownloadData(DEFAULT_MANIFEST_URL, manifestData) || manifestData.empty())
    {
        log(L"[updater] Обновления не найдены или сервер недоступен (оффлайн режим)");
        return false;
    }

    std::string jsonStr(reinterpret_cast<const char*>(manifestData.data()), manifestData.size());
    UpdateManifest manifest = ParseManifest(jsonStr);
    if (!manifest.valid)
    {
        log(L"[updater] Манифест версий не содержит данных");
        return false;
    }

    // 1. Проверка самообновления Ashes.exe
    if (!manifest.loaderVersion.empty() && IsNewerVersion(manifest.loaderVersion, CURRENT_LOADER_VERSION))
    {
        log(L"[updater] Найдена новая версия лоадера: v" + Utf8ToWide(manifest.loaderVersion));
        std::wstring updateExe = directory + L"\\Ashes_update.exe";
        if (HttpDownloadFile(manifest.loaderUrl, updateExe))
        {
            log(L"[updater] Лоадер загружен, выполняется перезапуск...");
            ExecuteSelfUpdate(directory, updateExe);
            return true;
        }
        else
        {
            log(L"[updater] Ошибка скачивания Ashes_update.exe");
        }
    }

    // 2. Проверка зависимостей (DLL, Lua)
    int updatedCount = 0;
    for (const auto& file : manifest.files)
    {
        std::wstring targetPath = directory + L"\\" + file.name;
        std::string currentHash = CalculateSha256(targetPath);

        if (currentHash != file.sha256)
        {
            std::wstring tempPath = targetPath + L".tmp";
            if (HttpDownloadFile(file.url, tempPath))
            {
                std::string downloadedHash = CalculateSha256(tempPath);
                if (file.sha256.empty() || downloadedHash == file.sha256)
                {
                    if (MoveFileExW(tempPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING))
                    {
                        log(L"[updater] Обновлен: " + file.name);
                        updatedCount++;
                    }
                    else
                    {
                        DeleteFileW(tempPath.c_str());
                    }
                }
                else
                {
                    DeleteFileW(tempPath.c_str());
                    log(L"[updater] Ошибка хеша для " + file.name);
                }
            }
        }
    }

    if (updatedCount > 0)
        log(L"[updater] Успешно обновлено файлов: " + std::to_wstring(updatedCount));
    else
        log(L"[updater] Все файлы актуальны (v" + Utf8ToWide(CURRENT_LOADER_VERSION) + L")");

    return false;
}
} // namespace AshesUpdater
