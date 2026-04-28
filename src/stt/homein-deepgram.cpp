#define NOMINMAX
#include "homein-deepgram.hpp"
#include "../audio/homein-audio.hpp"
#include <obs-module.h>
#include <iostream>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "winhttp.lib")

DeepgramSTTProvider::DeepgramSTTProvider() {}

DeepgramSTTProvider::~DeepgramSTTProvider() {
    Stop();
}

bool DeepgramSTTProvider::Initialize(const std::string& key) {
    api_key = key;
    return !api_key.empty();
}

void DeepgramSTTProvider::Start(TranscriptCallback callback) {
    if (running) return;
    on_transcript = callback;
    running = true;
    worker_thread = std::thread(&DeepgramSTTProvider::RunLoop, this);
}

void DeepgramSTTProvider::Stop() {
    running = false;
    if (hWebSocket) WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    if (worker_thread.joinable()) worker_thread.join();
    
    if (hWebSocket) WinHttpCloseHandle(hWebSocket);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    hWebSocket = hRequest = hConnect = hSession = NULL;
}

void DeepgramSTTProvider::RunLoop() {
    hSession = WinHttpOpen(L"HomeIndeed/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;

    hConnect = WinHttpConnect(hSession, L"api.deepgram.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) return;

    // Deepgram URL with Keyword Boosting for Bible
    std::wstring path = L"/v1/listen?model=nova-2&smart_format=true&encoding=linear16&sample_rate=16000&channels=1";
    path += L"&keywords=Genesis&keywords=Exodus&keywords=Leviticus&keywords=Numbers&keywords=Deuteronomy"; // etc...

    hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) return;

    std::wstring auth = L"Authorization: Token " + std::wstring(api_key.begin(), api_key.end());
    WinHttpAddRequestHeaders(hRequest, auth.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) return;

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return;
    if (!WinHttpReceiveResponse(hRequest, NULL)) return;

    hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, NULL);
    if (!hWebSocket) return;

    HomeInAudioHandler* audio = GetAudioHandler();
    std::vector<float> pcmf32;
    std::vector<int16_t> pcm16;

    while (running) {
        if (is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        audio->GetSamples(pcmf32, true);
        if (!pcmf32.empty()) {
            pcm16.clear();
            for (float f : pcmf32) {
                pcm16.push_back(static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, f)) * 32767.0f));
            }

            // Send binary chunk
            WinHttpWebSocketSend(hWebSocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, pcm16.data(), (DWORD)(pcm16.size() * 2));
        }

        // Non-blocking receive
        char buffer[4096];
        DWORD dwDownloaded = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        if (WinHttpWebSocketReceive(hWebSocket, buffer, sizeof(buffer), &dwDownloaded, &type) == ERROR_SUCCESS) {
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                std::string json(buffer, dwDownloaded);
                HandleResponse(json);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void DeepgramSTTProvider::HandleResponse(const std::string& json) {
    // Basic JSON parsing for transcript (in a real app, use a proper JSON lib)
    size_t pos = json.find("\"transcript\":\"");
    if (pos != std::string::npos) {
        size_t start = pos + 14;
        size_t end = json.find("\"", start);
        if (end != std::string::npos) {
            std::string text = json.substr(start, end - start);
            if (!text.empty() && on_transcript) {
                on_transcript(text, json.find("\"is_final\":true") == std::string::npos);
            }
        }
    }
}
