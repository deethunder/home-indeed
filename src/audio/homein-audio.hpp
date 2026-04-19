#include "homein-resampler.hpp"
#include <memory>

/**
 * @class HomeInAudioHandler
 * @brief Singleton handler for OBS audio source filtering.
 * 
 * This class is registered as an OBS source filter. It intercepts PCM audio data,
 * mixes multiple channels down to Mono, and resamples the stream to 16kHz
 * using high-quality linear interpolation for optimal AI transcription.
 */
class HomeInAudioHandler {
public:
    HomeInAudioHandler();
    ~HomeInAudioHandler();

    /**
     * @brief Registers the 'Home Indeed Audio Tap' source filter in OBS.
     * Must be called during obs_module_load.
     */
    static void Register();

    /**
     * @brief Processes incoming raw PCM data from the OBS audio thread.
     * @param audio The raw audio buffer provided by OBS.
     */
    void ProcessAudio(struct obs_audio_data *audio);

    /**
     * @brief Retrieves processed 16kHz mono samples from the internal buffer.
     * @param out_samples Vector to be populated with PCM float32 samples.
     * @param clear If true (default), the internal buffer is emptied after retrieval.
     */
    void GetSamples(std::vector<float>& out_samples, bool clear = true);

    /**
     * @brief Returns the current number of samples stored in the buffer.
     * @return Number of float samples ready for processing.
     */
    size_t GetBufferedCount() const;

private:
    mutable std::mutex buffer_mutex;
    std::vector<float> pcm_buffer;
    
    std::unique_ptr<HomeInResampler> resampler;
    uint32_t last_sample_rate = 0;

    // Whisper standard is 16000Hz mono
    static constexpr uint32_t TARGET_SAMPLE_RATE = 16000;
};

// Global accessor or singleton for the active handler
extern HomeInAudioHandler* GetAudioHandler();
