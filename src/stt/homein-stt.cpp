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

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.translate        = false;

    // Fix 4 — Thread count thrashing (CPU cores - 1, min 1, max 4)
    wparams.n_threads = std::max(1,
        std::min(4, (int)std::thread::hardware_concurrency() - 1));

    // FIX #2a: no_context = true prevents Whisper from carrying over stale
    // context between chunks, which caused hallucination loops where the same
    // phrase repeated endlessly and the de-stutter filter then blocked everything.
    wparams.no_context       = true;

    wparams.single_segment   = true;
    wparams.suppress_blank   = true;
    wparams.suppress_nst     = true;

    // Church/worship vocabulary bias
    // Fix 7 — Enhanced initial_prompt with formatted examples
    wparams.initial_prompt =
        "Bible verse references like John 3:16, Mark 5:1, Romans 8:28, Psalms 23:1. "
        "Genesis, Exodus, Leviticus, Numbers, Deuteronomy, Joshua, Judges, Ruth, "
        "Samuel, Kings, Chronicles, Psalms, Proverbs, Isaiah, Jeremiah, Ezekiel, Daniel, "
        "Matthew, Mark, Luke, John, Acts, Romans, Corinthians, Galatians, Ephesians, "
        "Philippians, Colossians, Thessalonians, Timothy, Hebrews, James, Peter, "
        "Revelation. Hallelujah, amen, praise, worship, glory, grace, salvation.";

    while (running) {
        if (is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // Wait for at least 0.2 s of audio before processing (High-Speed Mode)
        if (audio->GetBufferedCount() < WHISPER_SAMPLE_RATE / 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::vector<float> latest_samples;
        // Fix: Use a 400ms overlap for context. Instead of clearing the whole buffer, 
        // we keep the tail end of the previous chunk to help the AI understand 
        // the transition between words.
        audio->GetSamples(latest_samples, true); 
        
        // We keep the last 6400 samples (400ms at 16kHz) for the next cycle's context
        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());

        const int context_size = WHISPER_SAMPLE_RATE * 1.5; // 1.5s total window
        if ((int)pcmf32.size() > context_size) {
            pcmf32.erase(pcmf32.begin(),
                         pcmf32.begin() + ((int)pcmf32.size() - context_size));
        }

        // Fix 6 — VAD computed on the full sliding window, not just the new chunk
        float sum = 0;
        for (float s : pcmf32) sum += s * s;
        float rms = sqrtf(sum / (float)pcmf32.size());

        if (rms < 0.008f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
    

        if (whisper_full(ctx, wparams, pcmf32.data(), (int)pcmf32.size()) != 0) {
            blog(LOG_ERROR, "Whisper failed to process audio");
            continue;
        }

        const int n_segments = whisper_full_n_segments(ctx);
        std::string full_text;
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            std::string segment = text;

            // Skip non-speech meta-tokens
            if (segment.find("[BLANK_AUDIO]") != std::string::npos ||
                segment.find("[MUSIC]")       != std::string::npos ||
                segment.find("[LAUGHTER]")    != std::string::npos ||
                segment.find("(noise)")       != std::string::npos) {
                continue;
            }
            full_text += segment;
        }

        if (!full_text.empty() && on_transcript) {
            // Trim whitespace
            full_text.erase(0, full_text.find_first_not_of(" \t\n\r"));
            auto last = full_text.find_last_not_of(" \t\n\r");
            if (last != std::string::npos) full_text.erase(last + 1);

            // Fix 2 — De-stutter kills short references (content-aware exception)
            auto looks_like_reference = [](const std::string& t) {
                for (size_t i = 1; i < t.size(); ++i)
                    if (std::isdigit((unsigned char)t[i]) &&
                        std::isalpha((unsigned char)t[i-1])) return true;
                return false;
            };

            bool is_ref = looks_like_reference(full_text);
            if (!is_ref && full_text == last_emitted_text && full_text.length() < 15) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            if (!full_text.empty()) {
                last_emitted_text = full_text;
                on_transcript(full_text, false);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
