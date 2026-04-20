#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include "whisper.h"

/**
 * @class HomeInSTTEngine
 * @brief Thread-safe wrapper for the Whisper.cpp AI engine.
 * 
 * This engine manages a background worker thread that continuously processes
 * 16kHz audio from the HomeInAudioHandler. It handles context management,
 * segment calculation, and provides transcribed text via asynchronous callbacks.
 */
class HomeInSTTEngine {
public:
    /** Callback signature for transcription results. */
    using TranscriptCallback = std::function<void(const std::string& text, bool is_partial)>;

    HomeInSTTEngine();
    ~HomeInSTTEngine();

    /**
     * @brief Loads the AI model and prepares the Whisper context.
     * @param model_path Absolute path to the .bin model file.
     * @return True if model loaded successfully, false otherwise.
     */
    bool Initialize(const std::string& model_path);

    /**
     * @brief Launches the background worker thread.
     * @param callback Function to be called when text segments are generated.
     */
    void Start(TranscriptCallback callback);

    /**
     * @brief Stops the STT engine.
     */
    void Stop();

    /**
     * @brief Pauses context processing (mic button).
     */
    void SetPaused(bool paused) { is_paused = paused; }
    bool IsPaused() const { return is_paused; }
    bool IsRunning() const { return running; }

private:
    /** Internal loop function executed by the worker thread. */
    void RunLoop();

    struct whisper_context* ctx = nullptr;
    std::thread worker_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> is_paused{false};
    TranscriptCallback on_transcript;

    std::string model_file;
    std::string last_emitted_text;
    
    // Performance settings
    int n_threads = 4;
};
