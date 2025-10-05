// proxy_downloader.cpp
// Build: cl /EHsc proxy_downloader.cpp winhttp.lib
//
// Features:
//  - Download URL (WinHTTP) to C:\Users\Administrator\Desktop\G2G\proxy\Webshare 100 proxies.txt
//  - Split list into two files: ...\01\proxy\proxy.txt and ...\02\proxy\proxy.txt
//  - install-task HH:MM  -> create a Scheduled Task "DownloadWebshareProxies" daily at HH:MM (default 02:00)
//  - uninstall-task -> delete the Scheduled Task
//  - run as normal: proxy_downloader.exe
//  - run as "proxy_downloader.exe loop" -> loop every 6 hours (not recommended vs Task Scheduler)

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

// Scheduled Task helpers
bool CreateScheduledTaskDailyWithLog(const std::wstring& taskName, const std::wstring& exePath, const std::wstring& startTime, std::wstring& outMsg) {
    // validate HH:MM
    if (startTime.size() != 5 || startTime[2] != L':') { outMsg = L"Invalid time format (HH:MM)"; return false; }

    // Build TR that runs cmd /c "exePath > run.log 2>&1"
    // Compose run.log path next to exe
    size_t pos = exePath.find_last_of(L"\\/");
    std::wstring exeDir = (pos == std::wstring::npos) ? L"." : exePath.substr(0, pos);
    std::wstring runLog = exeDir + L"\\run.log";

    // Need to pass TR as: cmd /c "<exePath> > <runLog> 2>&1"
    // And schtasks expects /TR "cmd /c \"...\""
    std::wstring inner = L"\"" + exePath + L"\" > \"" + runLog + L"\" 2>&1";
    std::wstring tr = L"\"cmd /c " + inner + L"\"";

    // Full schtasks command
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

// core wrapper (download and split)
int wmain_wrapper(int argc, wchar_t* argv[]) {
    std::wstring url = L"https://proxy.webshare.io/api/v2/proxy/list/download/aywcsahndhodhcrsegrqyjhxynzldqanalskuirv/-/any/username/direct/-/";
    std::wstring baseDir = L"C:\\Users\\Administrator\\Desktop\\G2G";
    std::wstring downloadDir = baseDir + L"\\proxy";
    std::wstring downloadFileName = L"Webshare 100 proxies.txt";
    std::wstring downloadFilePath = downloadDir + L"\\" + downloadFileName;

    std::wstring dest1 = baseDir + L"\\01\\proxy\\proxy.txt";
    std::wstring dest2 = baseDir + L"\\02\\proxy\\proxy.txt";

    if (!EnsureDirectoryExists(downloadDir)) { std::wcerr << L"Failed ensure: " << downloadDir << L"\n"; return 1; }
    if (!EnsureDirectoryExists(baseDir + L"\\01\\proxy")) { std::wcerr << L"Failed ensure: " << baseDir + L"\\01\\proxy\n"; return 1; }
    if (!EnsureDirectoryExists(baseDir + L"\\02\\proxy")) { std::wcerr << L"Failed ensure: " << baseDir + L"\\02\\proxy\n"; return 1; }

    std::string data; std::string errmsg;
    if (!DownloadToString(url, data, errmsg)) {
        std::wcerr << L"Download failed: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n";
        return 2;
    }

    bool saved = SaveStringToFile(downloadFilePath, data, errmsg);
    if (saved) {
        std::wcout << L"Saved master file to: " << downloadFilePath << L"\n";
    }
    else {
        std::wcerr << L"Save master to target failed: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n";
        wchar_t curDirBuf[MAX_PATH]; DWORD len = GetCurrentDirectoryW(MAX_PATH, curDirBuf);
        std::wstring curDir = (len > 0) ? std::wstring(curDirBuf) : std::wstring(L".");
        std::wstring fallback = curDir + L"\\" + downloadFileName;
        std::wcerr << L"Attempting fallback at: " << fallback << L"\n";
        if (SaveStringToFile(fallback, data, errmsg)) {
            std::wcout << L"Saved fallback master file to: " << fallback << L"\n";
            if (MoveFileReplace(fallback, downloadFilePath, errmsg)) {
                std::wcout << L"Moved fallback file into proxy folder: " << downloadFilePath << L"\n";
            }
            else {
                std::wcerr << L"Failed to move fallback into proxy folder: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n";
            }
        }
        else {
            std::wcerr << L"Failed to save fallback file too: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n";
            return 3;
        }
    }

    auto lines = SplitLines(data);
    std::vector<std::string> cleaned; cleaned.reserve(lines.size());
    for (auto& ln : lines) {
        std::string t = ln;
        while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) t.pop_back();
        size_t p = 0; while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) ++p;
        if (p > 0) t = t.substr(p);
        if (!t.empty()) cleaned.push_back(t);
    }
    size_t total = cleaned.size();
    if (total == 0) {
        std::wcout << L"No proxies found.\n";
        SaveStringToFile(dest1, std::string(), errmsg);
        SaveStringToFile(dest2, std::string(), errmsg);
        return 0;
    }
    size_t half = total / 2; if (total % 2 != 0) ++half;
    std::ostringstream s1, s2;
    for (size_t i = 0; i < total; ++i) { if (i < half) s1 << cleaned[i] << "\r\n"; else s2 << cleaned[i] << "\r\n"; }

    if (!SaveStringToFile(dest1, s1.str(), errmsg)) { std::wcerr << L"Failed write dest1: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n"; return 4; }
    if (!SaveStringToFile(dest2, s2.str(), errmsg)) { std::wcerr << L"Failed write dest2: " << std::wstring(errmsg.begin(), errmsg.end()) << L"\n"; return 5; }

    std::wcout << L"Wrote " << half << L" proxies to " << dest1 << L"\n";
    std::wcout << L"Wrote " << (total - half) << L" proxies to " << dest2 << L"\n";
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    // handle install/uninstall args first
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

    // normal run or loop
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
