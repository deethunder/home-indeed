#define NOMINMAX
#include "homein-deepgram.hpp"
#include "../audio/homein-audio.hpp"
#include <obs-module.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QApplication>
#include <QMetaObject>

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
    if (running.exchange(true)) return;
    on_transcript = callback;
    worker_thread = std::thread(&DeepgramSTTProvider::RunLoop, this);
}

void DeepgramSTTProvider::Stop() {
    running = false;
    if (hWebSocket) WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    
    if (worker_thread.joinable()) worker_thread.join();
    if (receive_thread.joinable()) receive_thread.join();
    
    if (hWebSocket) WinHttpCloseHandle(hWebSocket);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    hWebSocket = hRequest = hConnect = hSession = NULL;
}

void DeepgramSTTProvider::RunLoop() {
    hSession = WinHttpOpen(L"HomeIndeed/1.0", 
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, // Changed from AUTOMATIC_PROXY
                           WINHTTP_NO_PROXY_NAME, 
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;

    DWORD timeout = 10000; // 10 seconds
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    hConnect = WinHttpConnect(hSession, L"api.deepgram.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { blog(LOG_ERROR, "Deepgram: WinHttpConnect failed"); return; }

    // Deepgram URL with all optimizations from Rhema research
   std::wstring path =
        L"/v1/listen"
        L"?model=nova-2"           // Use the stable model
        L"&smart_format=true"
        L"&dictation=true"         // Forces "Zechariah 5:1" instead of garbled text
        L"&interim_results=true"
        L"&endpointing=300"
        L"&encoding=linear16"
        L"&sample_rate=16000"
        L"&channels=1";
    hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) return;

    // Robust Authorization header (using wide string conversion like HttpClient)
    wchar_t wKey[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, api_key.c_str(), -1, wKey, 256);
    std::wstring authHeader = L"Authorization: Token " + std::wstring(wKey);
    WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        blog(LOG_ERROR, "Deepgram: WinHttpSetOption(UPGRADE) failed. Error: %lu", GetLastError());
        return;
    }
    
    // 1. Actually attempt to send the connection request (this was missing!)
    bool bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    // 2. If the send was successful, wait for the response
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    // 3. IF EITHER OF THOSE FAILED (No internet, DNS failure, etc.), SHOW THE POPUP!
    if (!bResults) {
        blog(LOG_ERROR, "Deepgram: Connection failed. Check internet. Error: %lu", GetLastError());
        
        // --- PROFESSIONAL ERROR POPUP ---
        QMetaObject::invokeMethod(qApp, []() {
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setWindowTitle("Connection Error");
            msgBox.setText("Deepgram Cloud connection failed.");
            msgBox.setInformativeText("Please check your internet connection, or switch to the Local (Whisper) engine for offline mode.");
            msgBox.setStandardButtons(QMessageBox::Ok);
            
            // Forces the popup to stay on top of OBS
            msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint); 
            msgBox.exec();
        }, Qt::QueuedConnection);
        // --------------------------------
        
        return; // Stop running Deepgram since we have no connection
    }

    hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, NULL);
    if (!hWebSocket) { 
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        blog(LOG_ERROR, "Deepgram: WebSocket upgrade failed. HTTP Status: %lu", statusCode);
        if (statusCode == 401) blog(LOG_ERROR, "Deepgram: Unauthorized. Check your API Key.");
        else if (statusCode == 400) blog(LOG_ERROR, "Deepgram: Bad Request. Check parameters.");
        return; 
    }
    
    blog(LOG_INFO, "Deepgram: WebSocket Connected successfully");

    // Start receiving thread
    receive_thread = std::thread(&DeepgramSTTProvider::ReceiveLoop, this);

    HomeInAudioHandler* audio = GetAudioHandler();
    std::vector<float> pcmf32;
    std::vector<int16_t> pcm16;

    auto last_activity = std::chrono::steady_clock::now();
    static constexpr int kKeepAliveMs = 5000;

    while (running) {
        if (is_paused) {
            std::vector<float> dump;
            audio->GetSamples(dump, true);

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
            if (WinHttpWebSocketSend(hWebSocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, pcm16.data(), (DWORD)(pcm16.size() * 2)) == ERROR_SUCCESS) {
                // blog(LOG_DEBUG, "Deepgram: Sent %d bytes", (int)(pcm16.size() * 2));
            } else {
                blog(LOG_ERROR, "Deepgram: Failed to send binary audio chunk");
            }
            last_activity = std::chrono::steady_clock::now();
        }

        // Send KeepAlive if no audio sent in 5 seconds
        auto idle_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_activity).count();
        if (idle_ms > kKeepAliveMs) {
            const char* ping = R"({"type":"KeepAlive"})";
            WinHttpWebSocketSend(hWebSocket,
                WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                (void*)ping, (DWORD)strlen(ping));
            last_activity = std::chrono::steady_clock::now();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // High frequency for low latency
    }
}

void DeepgramSTTProvider::ReceiveLoop() {
    char buffer[16384]; 
    while (running) {
        DWORD dwDownloaded = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        
        DWORD dwError = WinHttpWebSocketReceive(hWebSocket, buffer, sizeof(buffer), &dwDownloaded, &type);
        if (dwError != ERROR_SUCCESS) {
            if (running) blog(LOG_ERROR, "Deepgram Receive Error: %lu", dwError);
            break;
        }

        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
            std::string json(buffer, dwDownloaded);
            // blog(LOG_DEBUG, "Deepgram Received: %s", json.c_str());
            HandleResponse(json);
        } else if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            break;
        }
    }
}

void DeepgramSTTProvider::HandleResponse(const std::string& json_str) {
    // Deepgram sends {"is_final":true} on utterance completion.
    // Partial results have is_final absent (not false).
    bool is_final = (json_str.find("\"is_final\":true") != std::string::npos);

    // Parse transcript text using QJsonDocument for robustness
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json_str));
    if (doc.isNull() || !doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonObject channel = root["channel"].toObject();
    QJsonArray alternatives = channel["alternatives"].toArray();
    if (alternatives.isEmpty()) return;

    QString text = alternatives[0].toObject()["transcript"].toString().trimmed();
    if (text.isEmpty()) return;

    // Emit partial results for live display; emit finals for detection pipeline
    if (on_transcript) {
        on_transcript(text.toStdString(), !is_final); // is_partial = NOT is_final
    }
}
