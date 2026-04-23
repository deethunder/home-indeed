#pragma once
#include <string>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace HomeIn {
    class HttpClient {
    public:
        static std::string Get(const std::string& url);
    };
}
