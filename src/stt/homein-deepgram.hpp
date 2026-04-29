#pragma once
#include "ISTTProvider.hpp"
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

/**
 * @class DeepgramSTTProvider
 * @brief Cloud-based STT using Deepgram WebSocket API (WinHTTP).
 */
class DeepgramSTTProvider : public ISTTProvider {
public:
    DeepgramSTTProvider();
    ~DeepgramSTTProvider();

    bool Initialize(const std::string& api_key) override;
    void Start(TranscriptCallback callback) override;
    void Stop() override;

    void SetPaused(bool paused) override { is_paused = paused; }
    bool IsPaused() const override { return is_paused; }
    bool IsRunning() const override { return running; }

    std::string GetName() const override { return "Deepgram (Cloud)"; }
    bool IsCloud() const override { return true; }

private:
    void RunLoop();
    void HandleResponse(const std::string& json);

    std::string api_key;
    std::thread worker_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> is_paused{false};
    TranscriptCallback on_transcript;

    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    HINTERNET hWebSocket = NULL;
};
