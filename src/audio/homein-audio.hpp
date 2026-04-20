#include "homein-resampler.hpp"
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

/**
 * @class HomeInAudioHandler
 * @brief Singleton handler for OBS audio source filtering.
 */
class HomeInAudioHandler {
public:
    HomeInAudioHandler();
    ~HomeInAudioHandler();

    static void Register();
    void ProcessAudio(struct obs_audio_data *audio);
    void GetSamples(std::vector<float>& out_samples, bool clear = true);
    size_t GetBufferedCount() const;
    
    /**
     * @brief Returns the last recorded Peak audio level (0.0 to 1.0).
     */
    float GetLastLevel() const { return current_level.load(); }

private:
    mutable std::mutex buffer_mutex;
    std::vector<float> pcm_buffer;
    
    std::unique_ptr<HomeInResampler> resampler;
    uint32_t last_sample_rate = 0;
    std::atomic<float> current_level{0.0f};

    static constexpr uint32_t TARGET_SAMPLE_RATE = 16000;
};

// Global accessor or singleton for the active handler
extern HomeInAudioHandler* GetAudioHandler();
