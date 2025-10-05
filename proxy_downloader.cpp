// proxy_downloader.cpp
// Build: cl /EHsc proxy_downloader.cpp winhttp.lib
//
// Behavior changed per user request:
//  - Read list of target directories from file.txt (next to exe)
//  - Download proxies once
//  - Distribute downloaded proxy lines across target directories (round-robin)
//  - For each target directory, write file: <targetDir>\proxy.txt (overwritten)
//  - install-task / uninstall-task and loop behavior unchanged
//
// Usage:
//  - file.txt lines: each line is a directory (no trailing \proxy). Example:
//      C:\Users\Administrator\Desktop\G2G\01
//      C:\Users\Administrator\Desktop\G2G\02
//
//  - Build: cl /EHsc proxy_downloader.cpp winhttp.lib
//  - Run: proxy_downloader.exe
//  - Run loop: proxy_downloader.exe loop
//  - Install task: proxy_downloader.exe install-task 02:00

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

static std::wstring GetLastErrorMessageW(DWORD errCode) {
    if (errCode == 0) return L"(no error)";
    LPWSTR buf = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buf, 0, NULL);
    std::wstring msg;
    if (len && buf) { msg.assign(buf, len); LocalFree(buf); }
    else msg = L"(unknown error)";
    return msg;
}

static int RunCommandPrintRC(const std::wstring& cmd) {
    std::wcout << L"[CMD] " << cmd << L"\n";
    int rc = _wsystem(cmd.c_str());
    std::wcout << L"[CMD rc=" << rc << L"]\n";
    return rc;
}

bool EnsureDirectoryExists(const std::wstring& dir) {
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;

    std::wstring path = dir;
    if (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();

    size_t pos = 0;
    if (path.size() >= 2 && path[1] == L':') pos = 3;
    else if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') pos = 2;
    else pos = 0;

    while (pos <= path.size()) {
        size_t next = path.find_first_of(L"\\/", pos);
        std::wstring comp = (next == std::wstring::npos) ? path : path.substr(0, next);
        if (!comp.empty()) {
            DWORD a = GetFileAttributesW(comp.c_str());
            if (a == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(comp.c_str(), NULL)) {
                    DWORD e = GetLastError();
                    if (e != ERROR_ALREADY_EXISTS) {
                        std::wcerr << L"CreateDirectoryW failed for: " << comp << L" (" << e << L") " << GetLastErrorMessageW(e) << L"\n";
                        return false;
                    }
                }
            }
            else {
                if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wcerr << L"Path component exists and is not a directory: " << comp << L"\n";
                    return false;
                }
            }
        }
        if (next == std::wstring::npos) break;
        pos = next + 1;
    }
    return true;
}

bool DownloadToString(const std::wstring& url_w, std::string& outData, std::string& outErr) {
    URL_COMPONENTS urlComp; ZeroMemory(&urlComp, sizeof(urlComp)); urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszScheme = new wchar_t[16]; urlComp.dwSchemeLength = 16;
    urlComp.lpszHostName = new wchar_t[512]; urlComp.dwHostNameLength = 512;
    urlComp.lpszUrlPath = new wchar_t[4096]; urlComp.dwUrlPathLength = 4096;
    urlComp.lpszExtraInfo = new wchar_t[4096]; urlComp.dwExtraInfoLength = 4096;

    if (!WinHttpCrackUrl(url_w.c_str(), (DWORD)url_w.length(), 0, &urlComp)) {
        outErr = "WinHttpCrackUrl failed";
        delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo;
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength) + std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    INTERNET_PORT port = urlComp.nPort;
    BOOL secure = (_wcsicmp(urlComp.lpszScheme, L"https") == 0);

    HINTERNET hSession = WinHttpOpen(L"C++ ProxyDownloader/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { outErr = "WinHttpOpen failed"; delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo; return false; }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { outErr = "WinHttpConnect failed"; WinHttpCloseHandle(hSession); delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo; return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { outErr = "WinHttpOpenRequest failed"; WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo; return false; }

    WinHttpAddRequestHeaders(hRequest, L"User-Agent: proxy-downloader/1.0\r\n", -1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        outErr = "WinHttpSendRequest failed"; WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo; return false;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        outErr = "WinHttpReceiveResponse failed"; WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo; return false;
    }

    outData.clear();
    const DWORD bufferSize = 8192; std::vector<char> buffer(bufferSize); DWORD bytesRead = 0;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        DWORD toRead = (available < bufferSize) ? available : bufferSize;
        if (!WinHttpReadData(hRequest, buffer.data(), toRead, &bytesRead)) break;
        if (bytesRead == 0) break;
        outData.append(buffer.data(), buffer.data() + bytesRead);
    }

    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    delete[] urlComp.lpszScheme; delete[] urlComp.lpszHostName; delete[] urlComp.lpszUrlPath; delete[] urlComp.lpszExtraInfo;
    return true;
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\r') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } if (i + 1 < s.size() && s[i + 1] == '\n') ++i; }
        else if (c == '\n') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool SaveStringToFile(const std::wstring& pathW, const std::string& data, std::string& outErr) {
    size_t pos = pathW.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        std::wstring dir = pathW.substr(0, pos);
        if (!EnsureDirectoryExists(dir)) { outErr = "Ensure parent directory failed"; return false; }
    }

    DWORD attr = GetFileAttributesW(pathW.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) { outErr = "Target path exists and is a directory"; return false; }
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(pathW.c_str(), FILE_ATTRIBUTE_NORMAL);

    HANDLE hFile = CreateFileW(pathW.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        std::wcerr << L"[Error] CreateFileW(" << pathW << L") failed: " << e << L" - " << GetLastErrorMessageW(e) << L"\n";
        std::ostringstream ss; ss << "CreateFileW failed code " << (int)e; outErr = ss.str();
        return false;
    }

    DWORD written = 0; BOOL ok = WriteFile(hFile, data.data(), (DWORD)data.size(), &written, NULL);
    CloseHandle(hFile);
    if (!ok || written != data.size()) { outErr = "WriteFile failed"; return false; }
    return true;
}

bool MoveFileReplace(const std::wstring& src, const std::wstring& dst, std::string& outErr) {
    if (!MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD e = GetLastError();
        std::wcerr << L"[Error] MoveFileExW failed from " << src << L" to " << dst << L" : " << e << L" - " << GetLastErrorMessageW(e) << L"\n";
        std::ostringstream ss; ss << "MoveFileExW failed code " << (int)e; outErr = ss.str();
        return false;
    }
    return true;
}

// Helpers to load target dirs from "file.txt" (next to EXE)
static std::wstring TrimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

bool LoadTargetDirsFromFile(const std::wstring& filePath, std::vector<std::wstring>& outDirs) {
    outDirs.clear();
    std::ifstream f(std::string(filePath.begin(), filePath.end()));
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == ' ' || line[a] == '\t')) ++a;
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) --b;
        std::string s = (a < b) ? line.substr(a, b - a) : std::string();
        if (s.empty()) continue;
        if (s.rfind("#", 0) == 0) continue;
        if (s.rfind("//", 0) == 0) continue;

        // convert to wide
        std::wstring ws(s.begin(), s.end());
        ws = TrimW(ws);
        if (!ws.empty()) outDirs.push_back(ws);
    }
    return !outDirs.empty();
}

// Scheduled Task helpers (unchanged)
bool CreateScheduledTaskDailyWithLog(const std::wstring& taskName, const std::wstring& exePath, const std::wstring& startTime, std::wstring& outMsg) {
    if (startTime.size() != 5 || startTime[2] != L':') { outMsg = L"Invalid time format (HH:MM)"; return false; }
    size_t pos = exePath.find_last_of(L"\\/");
    std::wstring exeDir = (pos == std::wstring::npos) ? L"." : exePath.substr(0, pos);
    std::wstring runLog = exeDir + L"\\run.log";
    std::wstring inner = L"\"" + exePath + L"\" > \"" + runLog + L"\" 2>&1";
    std::wstring tr = L"\"cmd /c " + inner + L"\"";
    std::wstring cmd = L"schtasks /Create /TN \"" + taskName + L"\" /TR " + tr + L" /SC DAILY /ST " + startTime + L" /RL HIGHEST /F";
    int rc = RunCommandPrintRC(cmd);
    if (rc != 0) { outMsg = L"schtasks returned code " + std::to_wstring(rc); return false; }
    outMsg = L"Task created (or overwritten). Runs daily at " + startTime + L". Log: " + runLog;
    return true;
}

bool DeleteScheduledTask(const std::wstring& taskName, std::wstring& outMsg) {
    std::wstring cmd = L"schtasks /Delete /TN \"" + taskName + L"\" /F";
    int rc = RunCommandPrintRC(cmd);
    if (rc != 0) { outMsg = L"Delete returned code " + std::to_wstring(rc); return false; }
    outMsg = L"Task deleted.";
    return true;
}

// core wrapper (download and distribute)
int wmain_wrapper(int argc, wchar_t* argv[]) {
    std::wstring url = L"https://proxy.webshare.io/api/v2/proxy/list/download/aywcsahndhodhcrsegrqyjhxynzldqanalskuirv/-/any/username/direct/-/";

    wchar_t exePathBuf[MAX_PATH]; GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::wstring exePath = exePathBuf;
    size_t exep = exePath.find_last_of(L"\\/");
    std::wstring exeDir = (exep == std::wstring::npos) ? L"." : exePath.substr(0, exep);
    std::wstring listFile = exeDir + L"\\file.txt";

    std::vector<std::wstring> targets;
    if (!LoadTargetDirsFromFile(listFile, targets)) {
        // fallback: single default
        std::wstring fallback = L"C:\\Users\\Administrator\\Desktop\\G2G\\01";
        targets.push_back(fallback);
        std::wcout << L"[INFO] file.txt missing/empty -> using fallback target: " << fallback << L"\n";
    }
    else {
        std::wcout << L"[INFO] Loaded " << (unsigned)targets.size() << L" target dir(s) from file.txt\n";
    }

    // Ensure target directories exist (we will write proxy.txt inside each)
    for (const auto& t : targets) {
        if (!EnsureDirectoryExists(t)) {
            std::wcerr << L"Failed to create/ensure target dir: " << t << L"\n";
            return 1;
        }
    }

    std::string data; std::string errmsg;
    if (!DownloadToString(url, data, errmsg)) {
        std::wcerr << L"Download failed: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n";
        return 2;
    }

    // split and clean lines
    auto lines = SplitLines(data);
    std::vector<std::string> cleaned; cleaned.reserve(lines.size());
    for (auto& ln : lines) {
        std::string t = ln;
        while (!t.empty() && (t.back() == '\\r' || t.back() == '\\n' || t.back() == ' ' || t.back() == '\\t')) t.pop_back();
        size_t p = 0; while (p < t.size() && (t[p] == ' ' || t[p] == '\\t')) ++p;
        if (p > 0) t = t.substr(p);
        if (!t.empty()) cleaned.push_back(t);
    }

    if (cleaned.empty()) {
        std::wcout << L"No proxies found in downloaded data. Writing empty proxy.txt to each target.\n";
        for (const auto& t : targets) {
            std::wstring outPath = t + L"\\proxy.txt";
            SaveStringToFile(outPath, std::string(), errmsg);
        }
        return 0;
    }

    // Distribute round-robin across targets
    size_t nTargets = targets.size();
    std::vector<std::vector<std::string>> buckets(nTargets);
    for (size_t i = 0; i < cleaned.size(); ++i) {
        buckets[i % nTargets].push_back(cleaned[i]);
    }

    // Write each bucket to target\proxy.txt
    for (size_t i = 0; i < nTargets; ++i) {
        std::ostringstream ss;
        for (const auto& ln : buckets[i]) ss << ln << "\r\n";
        std::wstring outPath = targets[i] + L"\\proxy.txt";
        if (!SaveStringToFile(outPath, ss.str(), errmsg)) {
            std::wcerr << L"Failed to write " << outPath << L" : " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n";
            return 4;
        }
        std::wcout << L"Wrote " << buckets[i].size() << L" proxies to " << outPath << L"\n";
    }

    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    std::wstring taskName = L"DownloadWebshareProxies";

    if (argc >= 2) {
        std::wstring cmd = argv[1];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::towlower);
        if (cmd == L"install-task") {
            std::wstring time = L"02:00";
            if (argc >= 3) time = argv[2];
            wchar_t exePathBuf[MAX_PATH]; GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
            std::wstring exePath = exePathBuf;
            std::wstring out;
            if (CreateScheduledTaskDailyWithLog(taskName, exePath, time, out)) {
                std::wcout << L"[OK] " << out << L"\n";
                return 0;
            }
            else {
                std::wcerr << L"[ERR] " << out << L"\n";
                return 1;
            }
        }
        else if (cmd == L"uninstall-task") {
            std::wstring out;
            if (DeleteScheduledTask(taskName, out)) {
                std::wcout << L"[OK] " << out << L"\n";
                return 0;
            }
            else {
                std::wcerr << L"[ERR] " << out << L"\n";
                return 1;
            }
        }
    }

    bool loop = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        std::transform(a.begin(), a.end(), a.begin(), ::towlower);
        if (a == L"loop") loop = true;
    }
    if (!loop) return wmain_wrapper(argc, argv);
    while (true) {
        int rc = wmain_wrapper(argc, argv);
        std::wcout << L"Round completed with code " << rc << L". Sleeping 6 hours...\n";
        Sleep(6ULL * 60ULL * 60ULL * 1000ULL);
    }
    return 0;
}
