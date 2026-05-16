#include "homein-http.hpp"
#include <vector>
#include <fstream>

#ifdef _WIN32
// ---------------------------------------------------------------
// Windows implementation using WinHTTP
// ---------------------------------------------------------------
namespace HomeIn {
    std::string HttpClient::Get(const std::string& url) {
        std::string result;
        HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

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

        hSession = WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            L"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession)
            hConnect = WinHttpConnect(hSession, wHost.c_str(), urlComp.nPort, 0);
        if (hConnect) {
            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), NULL,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        }
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
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

    bool HttpClient::DownloadFile(const std::string& url, const std::string& save_path,
                                   std::function<void(float)> progress_callback) {
        HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
        bool success = false;

        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;

        wchar_t wUrl[2048];
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wUrl, 2048);

        if (!WinHttpCrackUrl(wUrl, 0, 0, &urlComp)) return false;

        std::wstring wHost(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring wPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

        hSession = WinHttpOpen(L"HomeIndeed/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY,
                             &redirectPolicy, sizeof(redirectPolicy));
            hConnect = WinHttpConnect(hSession, wHost.c_str(), urlComp.nPort, 0);
        }
        if (hConnect) {
            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), NULL,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        }
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                if (WinHttpReceiveResponse(hRequest, NULL)) {
                    wchar_t szContentLength[32];
                    DWORD dwBufferLength = sizeof(szContentLength);
                    DWORD dwContentLength = 0;
                    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                                            NULL, szContentLength, &dwBufferLength, NULL))
                        dwContentLength = _wtoi(szContentLength);

                    std::ofstream outFile(save_path, std::ios::binary);
                    if (outFile.is_open()) {
                        DWORD dwSize = 0;
                        DWORD dwTotalDownloaded = 0;
                        do {
                            dwSize = 0;
                            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                            if (dwSize == 0) break;
                            std::vector<char> buffer(dwSize);
                            DWORD dwDownloaded = 0;
                            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                            outFile.write(buffer.data(), dwDownloaded);
                            dwTotalDownloaded += dwDownloaded;
                            if (progress_callback && dwContentLength > 0)
                                progress_callback((float)dwTotalDownloaded / (float)dwContentLength);
                        } while (dwSize > 0);
                        outFile.close();
                        success = (dwContentLength == 0 || dwTotalDownloaded == dwContentLength);
                    }
                }
            }
        }
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return success;
    }

} // namespace HomeIn

#else
// ---------------------------------------------------------------
// macOS / Linux implementation using libcurl
// ---------------------------------------------------------------
#include <curl/curl.h>

namespace {
    // Internal write callback — appends received bytes to a std::string
    size_t CurlWriteString(void* contents, size_t size, size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

    // Internal write callback — writes received bytes directly to a file
    size_t CurlWriteFile(void* contents, size_t size, size_t nmemb, std::ofstream* out) {
        out->write(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

    // Internal progress callback — bridges curl progress to our float callback
    struct ProgressData {
        std::function<void(float)> callback;
    };

    int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
        if (dltotal <= 0) return 0;
        auto* data = static_cast<ProgressData*>(clientp);
        if (data && data->callback)
            data->callback(static_cast<float>(dlnow) / static_cast<float>(dltotal));
        return 0;
    }
}

namespace HomeIn {
    std::string HttpClient::Get(const std::string& url) {
        std::string result;
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);   // follow redirects
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);          // 30s total timeout
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);   // 10s connect timeout
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);    // enforce SSL
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
            "AppleWebKit/537.36 (KHTML, like Gecko) "
            "Chrome/120.0.0.0 Safari/537.36");

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) return "";
        return result;
    }

    bool HttpClient::DownloadFile(const std::string& url, const std::string& save_path,
                                   std::function<void(float)> progress_callback) {
        std::ofstream outFile(save_path, std::ios::binary);
        if (!outFile.is_open()) return false;

        CURL* curl = curl_easy_init();
        if (!curl) return false;

        ProgressData prog{progress_callback};

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);         // 5 min for large model downloads
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "HomeIndeed/1.0");

        if (progress_callback) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
        }

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        outFile.close();

        return res == CURLE_OK;
    }

} // namespace HomeIn

#endif // _WIN32