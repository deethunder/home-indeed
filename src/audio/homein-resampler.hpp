#pragma once

#include <vector>
#include <cstdint>

/**
 * @brief High-quality Linear Interpolation Resampler.
 * Converts audio from a source sample rate (e.g., 44.1kHz or 48kHz) 
 * to the target rate required by Whisper (16kHz).
 */
class HomeInResampler {
public:
    HomeInResampler(uint32_t input_rate, uint32_t target_rate = 16000)
        : input_rate(input_rate), target_rate(target_rate) {
        ratio = (double)input_rate / (double)target_rate;
    }

    /**
     * @brief Resamples a chunk of mono audio.
     * @param input Raw input samples.
     * @param output Vector to hold the resampled output.
     */
    void Process(const std::vector<float>& input, std::vector<float>& output) {
        if (input.empty()) return;

        // Calculate number of output samples
        size_t out_samples = (size_t)(input.size() / ratio);
        output.reserve(output.size() + out_samples);

        for (size_t i = 0; i < out_samples; ++i) {
            double virtual_idx = (double)i * ratio + last_fraction;
            size_t idx = (size_t)virtual_idx;
            double fraction = virtual_idx - (double)idx;

            if (idx + 1 < input.size()) {
                // Linear Interpolation
                float sample = input[idx] * (float)(1.0 - fraction) + input[idx + 1] * (float)fraction;
                output.push_back(sample);
            }
        }

        // Handle carry-over fraction for the next chunk to prevent clicking/jitter
        double total_virtual = (double)out_samples * ratio + last_fraction;
        last_fraction = total_virtual - (double)input.size();
        if (last_fraction < 0) last_fraction = 0;
    }

    void Reset() {
        last_fraction = 0.0;
    }

private:
    uint32_t input_rate;
    uint32_t target_rate;
    double ratio;
    double last_fraction = 0.0;
};
