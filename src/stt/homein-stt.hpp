#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include "ISTTProvider.hpp"
#include "whisper.h"

/**
 * @class HomeInSTTEngine
 * @brief Whisper.cpp implementation of ISTTProvider.
 */
class HomeInSTTEngine : public ISTTProvider {
public:
    HomeInSTTEngine();
    ~HomeInSTTEngine();

    bool Initialize(const std::string& model_path) override;
    void Start(TranscriptCallback callback) override;
    void Stop() override;

    void SetPaused(bool paused) override { is_paused = paused; }
    bool IsPaused() const override { return is_paused; }
    bool IsRunning() const override { return running; }

    std::string GetName() const override { return "Whisper (Local)"; }
    bool IsCloud() const override { return false; }

private:
    void RunLoop();

    struct whisper_context* ctx = nullptr;
    std::thread worker_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> is_paused{false};
    TranscriptCallback on_transcript;

    std::string model_file;
    std::string last_emitted_text;
    int n_threads = 4;
};
