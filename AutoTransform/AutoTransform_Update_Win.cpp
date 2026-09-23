#include "AutoTransform_Update.h"

#include <Windows.h>
#include <shellapi.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

bool AT_FetchLatestReleaseJSON(std::string& response) {
    HINTERNET session = WinHttpOpen(L"MasterDuel Transform Update Check/0.1.2",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 3000);

    HINTERNET connection = WinHttpConnect(session, L"api.github.com",
                                          INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, L"GET", L"/repos/Chamhyul/Masterduel-Transform/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE) : nullptr;

    bool success = false;
    if (request) {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                    WINHTTP_NO_HEADER_INDEX) && status == 200) {
                char buffer[4096];
                DWORD read = 0;
                bool readOk = true;
                while (response.size() < 65536) {
                    if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
                        readOk = false;
                        break;
                    }
                    if (read == 0) break;
                    response.append(buffer, read);
                }
                success = readOk && read == 0 && !response.empty() && response.size() < 65536;
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return success;
}

void AT_OpenLatestReleasePage() {
    ShellExecuteW(nullptr, L"open",
                  L"https://github.com/Chamhyul/Masterduel-Transform/releases/latest",
                  nullptr, nullptr, SW_SHOWNORMAL);
}
