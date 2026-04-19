#include <gtest/gtest.h>
#include "homein-resampler.hpp"
#include <vector>
#include <cmath>

/**
 * @brief Tests the HomeInResampler for accuracy and stability.
 * v1.1 Testing Framework
 */
class ResamplerTest : public ::testing::Test {
protected:
    HomeInResampler resampler;
};

// Verify that the resampler can handle a silent buffer
TEST_F(ResamplerTest, HandlesSilence) {
    std::vector<float> input(1024, 0.0f);
    std::vector<float> output;
    
    // Resampling from 48k to 16k
    resampler.Process(input, output, 48000.0f, 16000.0f);
    
    for (float sample : output) {
        EXPECT_NEAR(sample, 0.0f, 0.0001f);
    }
}

// Verify that a constant DC offset is preserved (sanity check)
TEST_F(ResamplerTest, PreservesDCOffset) {
    std::vector<float> input(1024, 0.5f);
    std::vector<float> output;
    
    resampler.Process(input, output, 44100.0f, 16000.0f);
    
    // The first few samples might be transient, but the rest should be close to 0.5
    for (size_t i = 5; i < output.size(); ++i) {
        EXPECT_NEAR(output[i], 0.5f, 0.01f);
    }
}

// Verify output buffer sizing logic
TEST_F(ResamplerTest, CorrectOutputSize) {
    std::vector<float> input(480, 0.0f); // 10ms at 48kHz
    std::vector<float> output;
    
    resampler.Process(input, output, 48000.0f, 16000.0f);
    
    // Should be roughly 160 samples (1/3 of input)
    EXPECT_EQ(output.size(), 160);
}
