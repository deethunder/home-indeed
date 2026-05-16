#pragma once
#include "ISTTProvider.hpp"
#include <string>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <vosk_api.h>
#endif

class HomeInVoskEngine : public ISTTProvider {
public:
    HomeInVoskEngine();
    ~HomeInVoskEngine();

    bool Initialize(const std::string& model_path) override;
    void Start(TranscriptCallback callback) override;
    void Stop() override;

    void SetPaused(bool paused) override { is_paused = paused; }
    bool IsPaused() const override { return is_paused; }
    bool IsRunning() const override { return running; }

    std::string GetName() const override { return "Vosk (Zero-Hallucination)"; }
    bool IsCloud() const override { return false; }

private:
    void RunLoop();

    VoskModel *model = nullptr;
    VoskRecognizer *recognizer = nullptr;
    
    std::thread worker_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> is_paused{false};
    TranscriptCallback on_transcript;
};