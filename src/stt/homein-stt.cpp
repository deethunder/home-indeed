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
    wparams.n_threads        = n_threads;
    wparams.print_realtime   = false;
    wparams.print_progress   = false;
    wparams.language         = "en";
    wparams.translate        = false;

    // FIX #2a: no_context = true prevents Whisper from carrying over stale
    // context between chunks, which caused hallucination loops where the same
    // phrase repeated endlessly and the de-stutter filter then blocked everything.
    wparams.no_context       = true;

    wparams.single_segment   = true;
    wparams.suppress_blank   = true;
    wparams.suppress_nst     = true;

    // Church/worship vocabulary bias
    wparams.initial_prompt =
        "Bible scripture, worship, church service, "
        "Genesis, Exodus, Leviticus, Numbers, Deuteronomy, Joshua, Judges, Ruth, "
        "Samuel, Kings, Chronicles, Psalms, Proverbs, Isaiah, Jeremiah, Ezekiel, Daniel, "
        "Matthew, Mark, Luke, John, Acts, Romans, Corinthians, Galatians, Ephesians, "
        "Philippians, Colossians, Thessalonians, Timothy, Hebrews, James, Peter, "
        "Revelation, hallelujah, amen, praise, worship, glory, grace, salvation";

    while (running) {
        if (is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // Wait for at least 0.5 s of audio before processing
        if (audio->GetBufferedCount() < WHISPER_SAMPLE_RATE / 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::vector<float> latest_samples;
        audio->GetSamples(latest_samples, true);

        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());

        // FIX #4: Reduced sliding window from 3 s to 2 s.
        // Processing 3 s every cycle with tiny.en took 600-900 ms on CPU,
        // causing the perceived "very long before it transcribes" problem.
        const int context_size = WHISPER_SAMPLE_RATE * 2;
        if ((int)pcmf32.size() > context_size) {
            pcmf32.erase(pcmf32.begin(),
                         pcmf32.begin() + ((int)pcmf32.size() - context_size));
        }

        // FIX #2b: VAD threshold lowered from 0.025 to 0.008.
        // 0.025 was rejecting normal speaking volume captured through a mixer
        // or capture card at moderate gain — audio that the level meter showed
        // as active but Whisper never saw.
        float sum = 0;
        for (float s : latest_samples) sum += s * s;
        float rms = sqrtf(sum / (float)latest_samples.size());

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

            // FIX #2c: De-stutter threshold raised from 15 to 40 chars.
            // "John 3:16" is only 10 chars — the old threshold of 15 silently
            // dropped every short Bible reference after the first occurrence.
            if (full_text == last_emitted_text && full_text.length() < 40) {
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
