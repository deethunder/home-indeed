#pragma once
#include <string>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace HomeIn {
    class HttpClient {
    public:
        static std::string Get(const std::string& url);
        static bool DownloadFile(const std::string& url, const std::string& save_path, std::function<void(float)> progress_callback = nullptr);
    };
}
