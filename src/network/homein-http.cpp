#include "homein-http.hpp"
#include <vector>
#include <obs-module.h>

namespace HomeIn {
    std::string HttpClient::Get(const std::string& url) {
        std::string result;
        HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

        // Parse URL
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;

        wchar_t wUrl[2048];
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wUrl, 2048);

        if (!WinHttpCrackUrl(wUrl, 0, 0, &urlComp)) return "";

        std::wstring wHost(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring wPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

        hSession = WinHttpOpen(L"HomeIndeed/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            hConnect = WinHttpConnect(hSession, wHost.c_str(), urlComp.nPort, 0);
        }
        if (hConnect) {
            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        }

        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                if (WinHttpReceiveResponse(hRequest, NULL)) {
                    DWORD dwSize = 0;
                    do {
                        dwSize = 0;
                        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                        if (dwSize == 0) break;

                        std::vector<char> buffer(dwSize);
                        DWORD dwDownloaded = 0;
                        if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                        result.append(buffer.data(), dwDownloaded);
                    } while (dwSize > 0);
                }
            }
        }

        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);

        return result;
    }
}
