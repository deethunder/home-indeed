#include "homein-whisper.hpp"
#include "../audio/homein-audio.hpp"
#include <obs-module.h>
#include <chrono>
#include <cmath>

HomeInWhisperEngine::HomeInWhisperEngine() {}

HomeInWhisperEngine::~HomeInWhisperEngine() {
    Stop();
    if (ctx) whisper_free(ctx);
}

bool HomeInWhisperEngine::Initialize(const std::string& model_path) {
    if (ctx) whisper_free(ctx);
    model_file = model_path;

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    
    if (!ctx) return false;
    blog(LOG_INFO, "Whisper STT Engine initialized successfully with model: %s", model_path.c_str());
    return true;
}

void HomeInWhisperEngine::Start(TranscriptCallback callback) {
    if (running.exchange(true)) return;
    on_transcript = callback;
    worker_thread = std::thread(&HomeInWhisperEngine::RunLoop, this);
}

void HomeInWhisperEngine::Stop() {
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void HomeInWhisperEngine::RunLoop() {
    HomeInAudioHandler* audio = GetAudioHandler();
    std::vector<float> pcmf32;
    std::vector<float> pcm_window;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.translate = false;
    wparams.print_timestamps = true;
    wparams.n_threads = std::max(1, std::min(4, (int)std::thread::hardware_concurrency() - 1));
    wparams.language = "en";
    wparams.no_context = true;
    wparams.single_segment = true;
    wparams.suppress_blank = true;
    wparams.suppress_nst = true;
    wparams.no_speech_thold = 0.6f;
    wparams.entropy_thold = 2.4f;

    while (running) {
        {
            std::unique_lock<std::mutex> lock(audio->GetNotifyMutex());
            audio->GetNotify().wait_for(lock, std::chrono::milliseconds(200), [this, audio] {
                return !running || audio->GetBufferedCount() >= WHISPER_SAMPLE_RATE / 4;
            });
        }

        if (!running) break;
        if (is_paused) {
            std::vector<float> dump;
            audio->GetSamples(dump, true); 
            continue;
        }

        std::vector<float> latest_samples;
        audio->GetSamples(latest_samples, true); 
        if (latest_samples.empty() && pcmf32.empty()) continue;

        pcmf32.insert(pcmf32.end(), latest_samples.begin(), latest_samples.end());
        static constexpr int kMaxSamples = WHISPER_SAMPLE_RATE * 15;  
        if ((int)pcmf32.size() > kMaxSamples) {
            pcmf32.erase(pcmf32.begin(), pcmf32.begin() + ((int)pcmf32.size() - kMaxSamples));
        }
        pcm_window = pcmf32;

        float rms = 0.0f;
        if (!latest_samples.empty()) {
            float sum = 0.0f;
            int vad_start = std::max(0, (int)pcm_window.size() - WHISPER_SAMPLE_RATE);
            for (int i = vad_start; i < (int)pcm_window.size(); ++i) {
                sum += pcm_window[i] * pcm_window[i];
            }
            rms = std::sqrt(sum / static_cast<float>(pcm_window.size() - vad_start));
        }

        static int consecutive_silent_frames = 0;
        bool should_flush = false;
        if (rms < 0.001f) {
            consecutive_silent_frames++;
            if (consecutive_silent_frames > 6) should_flush = true;
        } else {
            consecutive_silent_frames = 0;
        }

        if (whisper_full(ctx, wparams, pcm_window.data(), (int)pcm_window.size()) != 0) continue;

        const int n_segments = whisper_full_n_segments(ctx);
        std::string full_text;
        for (int i = 0; i < n_segments; ++i) {
            full_text += whisper_full_get_segment_text(ctx, i);
        }

        if (!full_text.empty() && on_transcript) {
            full_text.erase(0, full_text.find_first_not_of(" \t\n\r"));
            auto last = full_text.find_last_not_of(" \t\n\r");
            if (last != std::string::npos) full_text.erase(last + 1);

            if (full_text.length() > 2 && full_text != last_emitted_text) {
                last_emitted_text = full_text;
                on_transcript(full_text, !should_flush); 
            }
        }

        if (should_flush) {
            pcmf32.clear();
            last_emitted_text.clear();
            consecutive_silent_frames = 0;
        }
    }
}