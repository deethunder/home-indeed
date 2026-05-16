#pragma once
#include "ISTTProvider.hpp"
#include <memory>

// A clean enum to track all available engines
enum class STTEngineType {
    WHISPER_LOCAL,
    VOSK_LOCAL,
    DEEPGRAM_CLOUD
};

/**
 * @class HomeInSTTManager
 * @brief Factory class to instantly swap and instantiate different STT models.
 */
class HomeInSTTManager {
public:
    static std::unique_ptr<ISTTProvider> CreateEngine(STTEngineType type);
};