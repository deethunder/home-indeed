#include "homein-stt.hpp"
#include "homein-whisper.hpp"
#include "homein-vosk.hpp"
#include "homein-deepgram.hpp"

std::unique_ptr<ISTTProvider> HomeInSTTManager::CreateEngine(STTEngineType type) {
    switch (type) {
        case STTEngineType::WHISPER_LOCAL:
            return std::make_unique<HomeInWhisperEngine>();
        case STTEngineType::VOSK_LOCAL:
            return std::make_unique<HomeInVoskEngine>();
        case STTEngineType::DEEPGRAM_CLOUD:
            return std::make_unique<DeepgramSTTProvider>();
        default:
            return nullptr;
    }
}