#include "homein-audio.hpp"
#include <obs-module.h>
#include <memory>

static HomeInAudioHandler* g_audio_handler = nullptr;

HomeInAudioHandler* GetAudioHandler() {
    return g_audio_handler;
}

// OBS Filter Callbacks
static const char* homein_audio_filter_get_name(void* unused) {
    UNUSED_PARAMETER(unused);
    return obs_module_text("Home Indeed Audio Tap");
}

static void* homein_audio_filter_create(obs_data_t* settings, obs_source_t* context) {
    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(context);
    
    // We only want one active tap for simplicity in v1.0
    if (!g_audio_handler) {
        g_audio_handler = new HomeInAudioHandler();
    }
    return g_audio_handler;
}

static void homein_audio_filter_destroy(void* data) {
    // Note: We keep the global handler alive or manage it carefully
    // For now, let's just null it out if this filter is destroyed
    if (data == g_audio_handler) {
        // In a more robust system, we'd delete here, but many instances might exist
    }
}

static struct obs_audio_data* homein_audio_filter_audio(void* data, struct obs_audio_data* audio) {
    HomeInAudioHandler* handler = static_cast<HomeInAudioHandler*>(data);
    if (handler) {
        handler->ProcessAudio(audio);
    }
    return audio;
}

void HomeInAudioHandler::Register() {
    struct obs_source_info info = {};
    info.id = "homein_audio_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = homein_audio_filter_get_name;
    info.create = homein_audio_filter_create;
    info.destroy = homein_audio_filter_destroy;
    info.filter_audio = homein_audio_filter_audio;

    obs_register_source(&info);
}

HomeInAudioHandler::HomeInAudioHandler() {
    pcm_buffer.reserve(TARGET_SAMPLE_RATE * 10); // Reserve 10 seconds
}

HomeInAudioHandler::~HomeInAudioHandler() {
}

void HomeInAudioHandler::ProcessAudio(struct obs_audio_data* audio) {
    std::lock_guard<std::mutex> lock(buffer_mutex);

    // Get current OBS audio settings
    uint32_t sample_rate = audio_output_get_sample_rate(obs_get_audio());
    
    // Initialize or recreate resampler if sample rate changes
    if (!resampler || sample_rate != last_sample_rate) {
        resampler = std::make_unique<HomeInResampler>(sample_rate, TARGET_SAMPLE_RATE);
        last_sample_rate = sample_rate;
        obs_log(LOG_INFO, "Audio Resampler initialized: %u Hz -> %u Hz", sample_rate, TARGET_SAMPLE_RATE);
    }

    float** data = reinterpret_cast<float**>(audio->data);
    size_t frames = audio->frames;
    
    // 1. Mix to Mono
    std::vector<float> mono_input;
    mono_input.reserve(frames);

    for (size_t i = 0; i < frames; ++i) {
        float mono_sample = 0.0f;
        int active_channels = 0;
        for (int c = 0; c < 8; ++c) { 
            if (data[c]) {
                mono_sample += data[c][i];
                active_channels++;
            }
        }
        if (active_channels > 0) mono_sample /= (float)active_channels;
        mono_input.push_back(mono_sample);
    }

    // 2. High-Quality Resample
    resampler->Process(mono_input, pcm_buffer);

    // 3. Buffer Management
    // Cap buffer at 30 seconds to prevent memory leaks if STT hangs
    if (pcm_buffer.size() > TARGET_SAMPLE_RATE * 30) {
        pcm_buffer.erase(pcm_buffer.begin(), pcm_buffer.begin() + (pcm_buffer.size() - TARGET_SAMPLE_RATE * 30));
    }
}

void HomeInAudioHandler::GetSamples(std::vector<float>& out_samples, bool clear) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    out_samples = pcm_buffer;
    if (clear) {
        pcm_buffer.clear();
    }
}

size_t HomeInAudioHandler::GetBufferedCount() const {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    return pcm_buffer.size();
}
