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

    wparams.language   = "en";    // Skip language detection — saves 200-400ms per chunk
    wparams.translate  = false;   // Never translate — raw English transcription only
    wparams.no_context = true;    // Prevents stale context hallucinations between chunks
    wparams.single_segment   = true;
    wparams.suppress_blank   = true;
    wparams.suppress_nst     = true;

    // Church/worship vocabulary bias
    wparams.initial_prompt =
        // Leading examples of colon-formatted refs prime Whisper's output format.
        "John 3:16. Mark 5:1. Romans 8:28. Psalms 23:1. Isaiah 53:5. "
        "Matthew 28:19. Luke 4:18. Acts 2:38. Ephesians 2:8. Revelation 21:4. "
        "Genesis, Exodus, Leviticus, Numbers, Deuteronomy, Joshua, Judges, Ruth, "
        "Samuel, Kings, Chronicles, Psalms, Proverbs, Isaiah, Jeremiah, Ezekiel, "
        "Daniel, Matthew, Mark, Luke, John, Acts, Romans, Corinthians, Galatians, "
        "Ephesians, Philippians, Colossians, Hebrews, James, Peter, Revelation. "
        "Hallelujah, amen, praise, worship, glory, grace, salvation, Holy Spirit.";

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

        // Whisper tiny.en needs minimum 1s for reliable output.
        // 3s gives enough context for full phrases like "Mark chapter 5 verse 1".
        static constexpr int kWindowSamples  = WHISPER_SAMPLE_RATE * 3;   // 48,000 = 3s
        static constexpr int kOverlapSamples = WHISPER_SAMPLE_RATE / 2;   // 8,000  = 0.5s
        static constexpr int kMinSamples     = WHISPER_SAMPLE_RATE * 1;   // 16,000 = 1s

        if ((int)pcmf32.size() < kMinSamples) continue;

        // Extract the window to process
        pcm_window = pcmf32;
        if ((int)pcm_window.size() > kWindowSamples) {
            pcm_window.erase(pcm_window.begin(), 
                pcm_window.begin() + ((int)pcm_window.size() - kWindowSamples));
        }

        // Compute RMS on the full Whisper input window.
        // Must be on pcm_window/pcmf32, NOT on latest_samples — a tiny new chunk
        // can have near-zero RMS even when the window contains clear speech.
        float sum = 0.0f;
        for (float s : pcmf32) sum += s * s;
        float rms = std::sqrtf(sum / static_cast<float>(pcmf32.size()));

        // 0.001 is much more sensitive to ensure Whisper picks up standard microphones without needing high gain.
        static constexpr float kVadThreshold = 0.001f;
        if (rms < kVadThreshold) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto t_start = std::chrono::high_resolution_clock::now();
        if (whisper_full(ctx, wparams, pcm_window.data(), (int)pcm_window.size()) != 0) {
            continue;
        }
        auto t_end = std::chrono::high_resolution_clock::now();
        
        // After processing, keep overlap for next cycle
        if ((int)pcmf32.size() > kOverlapSamples) {
            pcmf32.erase(pcmf32.begin(),
                pcmf32.begin() + ((int)pcmf32.size() - kOverlapSamples));
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
