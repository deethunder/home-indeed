#include "homein-vosk.hpp"
#include "../audio/homein-audio.hpp"
#include <obs-module.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>

HomeInVoskEngine::HomeInVoskEngine() {}

HomeInVoskEngine::~HomeInVoskEngine() {
    Stop();
    if (recognizer) vosk_recognizer_free(recognizer);
    if (model) vosk_model_free(model);
}

bool HomeInVoskEngine::Initialize(const std::string& model_path) {
    model = vosk_model_new(model_path.c_str());
    if (!model) {
        blog(LOG_ERROR, "Vosk: Failed to load model at %s", model_path.c_str());
        return false;
    }

    // 16000.0 matches our audio tap perfectly
    recognizer = vosk_recognizer_new(model, 16000.0);
    blog(LOG_INFO, "Vosk STT Engine initialized successfully");
    return true;
}

void HomeInVoskEngine::Start(TranscriptCallback callback) {
    if (running.exchange(true)) return;
    on_transcript = callback;
    worker_thread = std::thread(&HomeInVoskEngine::RunLoop, this);
}

void HomeInVoskEngine::Stop() {
    running = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void HomeInVoskEngine::RunLoop() {
    HomeInAudioHandler* audio = GetAudioHandler();
    std::vector<float> pcmf32;
    std::vector<int16_t> pcm16;

    while (running) {
        {
            std::unique_lock<std::mutex> lock(audio->GetNotifyMutex());
            audio->GetNotify().wait_for(lock, std::chrono::milliseconds(50), [this, audio] {
                return !running || audio->GetBufferedCount() > 0;
            });
        }

        if (!running) break;
        if (is_paused) {
            audio->GetSamples(pcmf32, true); 
            continue;
        }

        audio->GetSamples(pcmf32, true);
        if (pcmf32.empty()) continue;

        // Vosk strictly requires Int16 audio instead of Float32
        pcm16.clear();
        pcm16.reserve(pcmf32.size());
        for (float f : pcmf32) {
            pcm16.push_back(static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, f)) * 32767.0f));
        }

        // Feed audio directly into the recognizer
        int state = vosk_recognizer_accept_waveform(recognizer, (char*)pcm16.data(), pcm16.size() * sizeof(int16_t));
        
        std::string result_json;
        bool is_final = false;

        if (state == 1) {
            result_json = vosk_recognizer_result(recognizer);
            is_final = true;
        } else {
            result_json = vosk_recognizer_partial_result(recognizer);
        }

        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(result_json));
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString text = is_final ? obj["text"].toString() : obj["partial"].toString();
            
            if (!text.trimmed().isEmpty() && on_transcript) {
                on_transcript(text.toStdString(), !is_final);
            }
        }
    }
}