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

        // [Sub-Second Latency] Reduced from 1s to 500ms for ultra-fast response
        if (audio->GetBufferedCount() < WHISPER_SAMPLE_RATE / 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::vector<float> latest_samples;
        audio->GetSamples(latest_samples, true);
        
        // Append to our local processing buffer
        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());

        // [Sliding Window] Keep 3 seconds of context to ensure accuracy for 500ms chunks
        const int context_size = WHISPER_SAMPLE_RATE * 3;
        if (pcmf32.size() > context_size) {
            pcmf32.erase(pcmf32.begin(), pcmf32.begin() + (pcmf32.size() - context_size));
        }

        // [VAD] Root Mean Square (RMS) calculation to skip silence and save CPU
        float sum = 0;
        for (float s : latest_samples) sum += s * s;
        float rms = sqrtf(sum / latest_samples.size());
        
        // Skip Whisper if the audio is essentially silent (-40dB approx)
        if (rms < 0.01f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
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
            // Cleanup and emit
            full_text.erase(0, full_text.find_first_not_of(" \t\n\r"));
            full_text.erase(full_text.find_last_not_of(" \t\n\r") + 1);
            
            if (!full_text.empty()) {
                on_transcript(full_text, false);
            }
        }

        // Low sleep for low latency
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
