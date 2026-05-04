#include "homein-resampler.hpp"
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <condition_variable>

struct obs_source;
typedef struct obs_source obs_source_t;

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
    void Clear();
    
    std::condition_variable& GetNotify() const { return audio_cv; }
    std::mutex& GetNotifyMutex() const { return audio_mtx; }
    
    /**
     * @brief Returns the last recorded Peak audio level (0.0 to 1.0).
     */
    float GetLastLevel() const { return current_level.load(); }
    
    /**
     * @brief Latches the AI listener onto a specific OBS source.
     */
    void SetCaptureSource(obs_source_t* new_source);

private:
    mutable std::mutex buffer_mutex;
    std::vector<float> pcm_buffer;
    
    std::unique_ptr<HomeInResampler> resampler;
    uint32_t last_sample_rate = 0;
    std::atomic<float> current_level{0.0f};
    std::atomic<bool> gate_open{true};
    uint32_t silent_frames = 0;

    mutable std::condition_variable audio_cv;
    mutable std::mutex audio_mtx;

    static constexpr uint32_t TARGET_SAMPLE_RATE = 16000;
    static constexpr float VAD_THRESHOLD = 0.001f;
    static constexpr uint32_t VAD_SILENCE_LIMIT = 50; // ~1 second of silence before gating

    obs_source_t* current_latch_source = nullptr;
    bool using_filter_path = false;

    friend void* homein_audio_filter_create(struct obs_data*, struct obs_source*);
    friend void homein_audio_filter_destroy(void*);
    friend void on_source_audio_capture(void*, struct obs_source*, const struct audio_data*, bool);
};

// Global accessor or singleton for the active handler
extern HomeInAudioHandler* GetAudioHandler();
