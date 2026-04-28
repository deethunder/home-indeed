#include "homein-stt.hpp"
#include "../audio/homein-audio.hpp"
#include <obs-module.h>
#include <chrono>
#include <thread>
#include <cctype>
#include <algorithm>

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
    std::vector<float> pcm_window;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.translate        = false;
    wparams.print_special    = false;
    wparams.print_progress   = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = true;
    
    // Performance: Fast Greedy search (Beam=1) is 2-3x faster than Beam Search
    wparams.strategy = WHISPER_SAMPLING_GREEDY;
    
    // Fix 4 — Optimized Threading
    wparams.n_threads = std::max(1,
        std::min(4, (int)std::thread::hardware_concurrency() - 1));

    wparams.no_context       = false; // Keep context for overlapping accuracy
    wparams.single_segment   = true;
    wparams.suppress_blank   = true;
    wparams.suppress_nst     = true;

    // Church/worship vocabulary bias
    wparams.initial_prompt =
        "Bible verse references like John 3:16, Mark 5:1, Romans 8:28, Psalms 23:1. "
        "Genesis, Exodus, Leviticus, Numbers, Deuteronomy, Joshua, Judges, Ruth, "
        "Samuel, Kings, Chronicles, Psalms, Proverbs, Isaiah, Jeremiah, Ezekiel, Daniel, "
        "Matthew, Mark, Luke, John, Acts, Romans, Corinthians, Galatians, Ephesians, "
        "Philippians, Colossians, Thessalonians, Timothy, Hebrews, James, Peter, "
        "Revelation. Hallelujah, amen, praise, worship, glory, grace, salvation.";

    while (running) {
        // --- EFFICIENCY UPGRADE: Wait for data notification ---
        {
            std::unique_lock<std::mutex> lock(audio->GetNotifyMutex());
            audio->GetNotify().wait_for(lock, std::chrono::milliseconds(200), [this, audio] {
                return !running || audio->GetBufferedCount() >= WHISPER_SAMPLE_RATE / 4; // Wake every 250ms
            });
        }

        if (!running) break;

        if (is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // --- LOW-LATENCY CHUNKING ---
        std::vector<float> latest_samples;
        audio->GetSamples(latest_samples, true); 
        
        if (latest_samples.empty() && pcmf32.empty()) continue;

        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());

        // Use an 800ms window with 200ms overlap
        const int window_size = WHISPER_SAMPLE_RATE * 0.8;
        const int overlap_size = WHISPER_SAMPLE_RATE * 0.2;

        if ((int)pcmf32.size() < window_size) {
             // Not enough for a full window yet, but let's process anyway if we have enough
             if (pcmf32.size() < WHISPER_SAMPLE_RATE / 4) continue;
        }

        // Extract the window to process
        pcm_window = pcmf32;
        if ((int)pcm_window.size() > window_size) {
            pcm_window.erase(pcm_window.begin(), pcm_window.begin() + (pcm_window.size() - window_size));
        }

        auto t_start = std::chrono::high_resolution_clock::now();
        if (whisper_full(ctx, wparams, pcm_window.data(), (int)pcm_window.size()) != 0) {
            continue;
        }
        auto t_end = std::chrono::high_resolution_clock::now();
        
        // Trim the buffer but keep overlap for the next cycle
        if ((int)pcmf32.size() > overlap_size) {
             pcmf32.erase(pcmf32.begin(), pcmf32.begin() + (pcmf32.size() - overlap_size));
        }

        const int n_segments = whisper_full_n_segments(ctx);
        std::string full_text;
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            full_text += text;
        }

        if (!full_text.empty() && on_transcript) {
            // Trim whitespace and skips
            full_text.erase(0, full_text.find_first_not_of(" \t\n\r"));
            auto last = full_text.find_last_not_of(" \t\n\r");
            if (last != std::string::npos) full_text.erase(last + 1);

            if (full_text.length() > 2 && full_text != last_emitted_text) {
                last_emitted_text = full_text;
                on_transcript(full_text, false);
            }
        }
    }
}
