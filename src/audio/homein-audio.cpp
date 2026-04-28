#include "homein-audio.hpp"
#include <obs-module.h>
#include <obs.h>
#include <memory>
#include <mutex>

static std::unique_ptr<HomeInAudioHandler> g_audio_handler = nullptr;
static std::mutex g_handler_mutex;

static void on_source_audio_capture(void* data, obs_source_t* source, const struct audio_data* audio, bool muted) {
    UNUSED_PARAMETER(source);
    if (muted || !audio) return;
    
    HomeInAudioHandler* handler = static_cast<HomeInAudioHandler*>(data);
    if (handler->using_filter_path) return; // filter path takes priority to avoid doubling
    
    // Map 'audio_data' to 'obs_audio_data' structure for ProcessAudio
    struct obs_audio_data obs_audio;
    for (int i = 0; i < 8; i++) {
        obs_audio.data[i] = (i < MAX_AUDIO_CHANNELS) ? audio->data[i] : nullptr;
    }
    obs_audio.frames = audio->frames;
    obs_audio.timestamp = audio->timestamp;
    
    handler->ProcessAudio(&obs_audio);
}

HomeInAudioHandler* GetAudioHandler() {
    std::lock_guard<std::mutex> lock(g_handler_mutex);
    if (!g_audio_handler) {
        g_audio_handler = std::make_unique<HomeInAudioHandler>();
    }
    return g_audio_handler.get();
}

// OBS Filter Callbacks
static const char* homein_audio_filter_get_name(void* unused) {
    UNUSED_PARAMETER(unused);
    return obs_module_text("Home Indeed Audio Tap");
}

static obs_properties_t* homein_audio_filter_properties(void* data) {
    UNUSED_PARAMETER(data);
    obs_properties_t* props = obs_properties_create();
    obs_properties_add_text(props, "status", "Status: 🟢 Connected & Listening", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "info", "This filter automatically feeds audio to the Home Indeed AI Dock. No manual settings required.", OBS_TEXT_INFO);
    return props;
}

static void* homein_audio_filter_create(obs_data_t* settings, obs_source_t* context) {
    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(context);
    
    HomeInAudioHandler* handler = GetAudioHandler();
    handler->using_filter_path = true;
    return handler;
}

static void homein_audio_filter_destroy(void* data) {
    HomeInAudioHandler* handler = static_cast<HomeInAudioHandler*>(data);
    if (handler) {
        handler->using_filter_path = false;
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
    info.get_properties = homein_audio_filter_properties;

    obs_register_source(&info);
}

HomeInAudioHandler::HomeInAudioHandler() {
    pcm_buffer.reserve(TARGET_SAMPLE_RATE * 10); // Reserve 10 seconds
}

HomeInAudioHandler::~HomeInAudioHandler() {
    SetCaptureSource(nullptr); // Unlatch on destroy
}

void HomeInAudioHandler::SetCaptureSource(obs_source_t* new_source) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    
    if (current_latch_source == new_source) return;

    // Unlatch old source
    if (current_latch_source) {
        obs_source_remove_audio_capture_callback(current_latch_source, on_source_audio_capture, this);
        obs_source_release(current_latch_source);
        current_latch_source = nullptr;
    }

    // Latch new source
    if (new_source) {
        current_latch_source = new_source;
        // NOTE: We assume ownership of the reference from the caller
        obs_source_add_audio_capture_callback(current_latch_source, on_source_audio_capture, this);
        blog(LOG_INFO, "HomeIndeed: AI Latched onto source '%s'", obs_source_get_name(new_source));
    }
}

void HomeInAudioHandler::ProcessAudio(struct obs_audio_data* audio) {
    std::lock_guard<std::mutex> lock(buffer_mutex);

    // Get current OBS audio settings
    uint32_t sample_rate = audio_output_get_sample_rate(obs_get_audio());
    
    // Initialize or recreate resampler if sample rate changes
    if (!resampler || sample_rate != last_sample_rate) {
        resampler = std::make_unique<HomeInResampler>(sample_rate, TARGET_SAMPLE_RATE);
        last_sample_rate = sample_rate;
        blog(LOG_INFO, "Audio Resampler initialized: %u Hz -> %u Hz", sample_rate, TARGET_SAMPLE_RATE);
    }

    float** data = reinterpret_cast<float**>(audio->data);
    size_t frames = audio->frames;
    
    // 1. Mix to Mono
    std::vector<float> mono_input;
    mono_input.reserve(frames);

    float max_peak = 0.0f;
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
        
        float abs_sample = std::abs(mono_sample);
        if (abs_sample > max_peak) max_peak = abs_sample;
        
        mono_input.push_back(mono_sample);
    }

    // Smooth the peak value (decaying peak)
    float prev_level = current_level.load();
    if (max_peak > prev_level) {
        current_level.store(max_peak);
    } else {
        current_level.store(prev_level * 0.8f + max_peak * 0.2f);
    }

    // 2. High-Quality Resample
    if (max_peak > VAD_THRESHOLD) {
        silent_frames = 0;
        gate_open = true;
    } else {
        silent_frames++;
        if (silent_frames > VAD_SILENCE_LIMIT) {
            gate_open = false;
        }
    }

    if (gate_open) {
        resampler->Process(mono_input, pcm_buffer);
        
        // Notify STT engine that new data is ready
        {
            std::lock_guard<std::mutex> lock_notify(audio_mtx);
            audio_cv.notify_one();
        }
    }

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
