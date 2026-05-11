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
        blog(LOG_ERROR, "Whisper: Failed to initialize context from %s", model_path.c_str());
        return false;
    }

    blog(LOG_INFO, "Whisper: Context initialized successfully from %s", model_path.c_str());
    blog(LOG_INFO, "Whisper STT Engine initialized successfully with model: %s", model_path.c_str());
    return true;
}

void HomeInSTTEngine::Start(TranscriptCallback callback) {
    if (running.exchange(true)) return;
    on_transcript = callback;
    blog(LOG_INFO, "HomeIndeed: Whisper (Local) STT Started");
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
        "Genesis, Exodus, Leviticus, Numbers, Deuteronomy, Joshua, Judges, Ruth, Samuel, Kings, Chronicles, Ezra, Nehemiah, Esther, Job, Psalms, Proverbs, Ecclesiastes, Song of Solomon, Isaiah, Jeremiah, Lamentations, Ezekiel, Daniel, Hosea, Joel, Amos, Obadiah, Jonah, Micah, Nahum, Habakkuk, Zephaniah, Haggai, Zechariah, Malachi, Matthew, Mark, Luke, John, Acts, Romans, Corinthians, Galatians, Ephesians, Philippians, Colossians, Thessalonians, Timothy, Titus, Philemon, Hebrews, James, Peter, Jude, Revelation. "
        "John 3:16. Mark 5:1. Romans 8:28. Psalms 23:1. Hallelujah, amen, praise, worship, glory.";

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
            std::vector<float> dump;
            audio->GetSamples(dump, true); 
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // --- LOW-LATENCY CHUNKING ---
        std::vector<float> latest_samples;
        audio->GetSamples(latest_samples, true); 
        
        if (latest_samples.empty() && pcmf32.empty()) continue;

        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());

        // 15-second expanding window for full context
        static constexpr int kMaxSamples = WHISPER_SAMPLE_RATE * 15;  
        static constexpr int kMinSamples = WHISPER_SAMPLE_RATE * 1;   

        if ((int)pcmf32.size() < kMinSamples) continue;

        // Prevent buffer from growing infinitely (cap at 15s)
        if ((int)pcmf32.size() > kMaxSamples) {
            pcmf32.erase(pcmf32.begin(), pcmf32.begin() + ((int)pcmf32.size() - kMaxSamples));
        }

        pcm_window = pcmf32;

        // Stable VAD: Check for actual new audio
        float rms = 0.0f;
        
        if (latest_samples.empty()) {
            // THE FIX: If the OBS Gate is closed and sends no new audio, force silence!
            rms = 0.0f; 
        } else {
            // Otherwise, check the volume of the last 1 second of the buffer
            float sum = 0.0f;
            int vad_start = std::max(0, (int)pcm_window.size() - WHISPER_SAMPLE_RATE);
            for (int i = vad_start; i < (int)pcm_window.size(); ++i) {
                sum += pcm_window[i] * pcm_window[i];
            }
            rms = std::sqrtf(sum / static_cast<float>(pcm_window.size() - vad_start));
        }
        
        // Back to the stable develop branch threshold
        static constexpr float kVadThreshold = 0.001f; 
        static int consecutive_silent_frames = 0;
        bool should_flush = false;

        // If silent, flag for flushing, but DO NOT skip processing this frame!
        if (rms < kVadThreshold) {
            consecutive_silent_frames++;
            if (consecutive_silent_frames > 6) { // ~1.5s of silence
                should_flush = true;
            }
        } else {
            consecutive_silent_frames = 0;
        }

        auto t_start = std::chrono::high_resolution_clock::now();
        // blog(LOG_INFO, "Whisper: Processing %d samples (RMS: %f)", (int)pcm_window.size(), rms);
        
        // Strict anti-hallucination parameters
        wparams.no_speech_thold = 0.6f;
        wparams.entropy_thold = 2.4f;

        if (whisper_full(ctx, wparams, pcm_window.data(), (int)pcm_window.size()) != 0) {
            blog(LOG_ERROR, "Whisper: Full processing failed");
            continue;
        }

        // Output assembly
        const int n_segments = whisper_full_n_segments(ctx);
        std::string full_text;
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            full_text += text;
        }

        if (!full_text.empty() && on_transcript) {
            full_text.erase(0, full_text.find_first_not_of(" \t\n\r"));
            auto last = full_text.find_last_not_of(" \t\n\r");
            if (last != std::string::npos) full_text.erase(last + 1);

            if (full_text.length() > 2 && full_text != last_emitted_text) {
                last_emitted_text = full_text;
                // If it's a flush, emit as FINAL (false). Otherwise, emit as PARTIAL (true).
                on_transcript(full_text, !should_flush); 
            }
        }

        // Safely wipe the buffer AFTER Whisper has outputted the final word
        if (should_flush) {
            pcmf32.clear();
            last_emitted_text.clear();
            consecutive_silent_frames = 0;
        }
    }
}
