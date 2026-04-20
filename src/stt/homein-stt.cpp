#include "homein-stt.hpp"
#include "../audio/homein-audio.hpp"
#include <obs-module.h>
#include <chrono>

HomeInSTTEngine::HomeInSTTEngine() {
}

HomeInSTTEngine::~HomeInSTTEngine() {
    Stop();
    if (ctx) {
        whisper_free(ctx);
    }
}

bool HomeInSTTEngine::Initialize(const std::string& model_path) {
    if (ctx) {
        whisper_free(ctx);
        ctx = nullptr;
    }

    model_file = model_path;
    
    whisper_context_params cparams = whisper_context_default_params();
    // Use GPU if available (experimental in some builds, but we'll try default)
    cparams.use_gpu = true;

    ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!ctx) {
        blog(LOG_ERROR, "Failed to initialize Whisper context from %s", model_path.c_str());
        return false;
    }

    blog(LOG_INFO, "Whisper STT Engine initialized successfully with model: %s", model_path.c_str());
    return true;
}

void HomeInSTTEngine::Start(TranscriptCallback callback) {
    if (running) return;
    
    on_transcript = callback;
    running = true;
    worker_thread = std::thread(&HomeInSTTEngine::RunLoop, this);
}

void HomeInSTTEngine::Stop() {
    running = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void HomeInSTTEngine::RunLoop() {
    HomeInAudioHandler* audio = GetAudioHandler();
    if (!audio) {
        blog(LOG_ERROR, "STT Engine cannot find Audio Handler!");
        running = false;
        return;
    }

    std::vector<float> pcmf32;
    
    // Whisper parameters
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.n_threads = n_threads;
    wparams.print_realtime = false;
    wparams.print_progress = false;
    wparams.language = "en";
    wparams.translate = false;
    wparams.no_context = false; // Keep context for better continuity
    wparams.single_segment = true; // Better for real-time live captioning

    while (running) {
        if (is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // Collect samples from the buffer
        if (audio->GetBufferedCount() < WHISPER_SAMPLE_RATE * 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::vector<float> latest_samples;
        audio->GetSamples(latest_samples, true);
        
        // Append to our local processing buffer
        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());

        // Limit local buffer to 30 seconds
        if (pcmf32.size() > WHISPER_SAMPLE_RATE * 30) {
            pcmf32.erase(pcmf32.begin(), pcmf32.begin() + (pcmf32.size() - WHISPER_SAMPLE_RATE * 30));
        }

        if (whisper_full(ctx, wparams, pcmf32.data(), (int)pcmf32.size()) != 0) {
            blog(LOG_ERROR, "Whisper failed to process audio");
            continue;
        }

        const int n_segments = whisper_full_n_segments(ctx);
        std::string full_text = "";
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            full_text += text;
        }

        if (!full_text.empty() && on_transcript) {
            on_transcript(full_text, false);
        }

        // Sleep briefly to avoid 100% CPU pinning between chunks
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
