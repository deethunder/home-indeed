#pragma once
#include <string>
#include <functional>

/**
 * @class ISTTProvider
 * @brief Interface for Speech-to-Text providers (Whisper, Deepgram, etc.)
 */
class ISTTProvider {
public:
    using TranscriptCallback = std::function<void(const std::string& text, bool is_partial)>;

    virtual ~ISTTProvider() = default;
    
    virtual bool Initialize(const std::string& config) = 0;
    virtual void Start(TranscriptCallback callback) = 0;
    virtual void Stop() = 0;
    
    virtual void SetPaused(bool paused) = 0;
    virtual bool IsPaused() const = 0;
    virtual bool IsRunning() const = 0;
    
    virtual std::string GetName() const = 0;
    virtual bool IsCloud() const = 0;
};
