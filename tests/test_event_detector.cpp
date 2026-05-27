/**
 * @file test_event_detector.cpp
 * @brief Unit tests for EventDetector
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <array>
#include <cstdlib>
#include "event_detector.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

// Helper: generate sine wave
std::vector<float> generate_sine(float freq_hz, float amplitude,
                                   int sample_rate, int n_samples) {
    std::vector<float> data(n_samples);
    for (int i = 0; i < n_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        data[i] = amplitude * std::sin(noise_const::TWO_PI_F * freq_hz * t);
    }
    return data;
}

// Helper: generate silence
std::vector<float> generate_silence(int n_samples) {
    return std::vector<float>(n_samples, 0.0f);
}

// Helper: generate impulse (high amplitude burst)
std::vector<float> generate_impulse(float amplitude, int n_samples) {
    std::vector<float> data(n_samples, 0.0f);
    // Add impulse in the middle
    for (int i = n_samples / 4; i < n_samples / 2; ++i) {
        data[i] = amplitude;
    }
    return data;
}

// Helper: SPL to Pa
float spl_to_pa(float spl_db) {
    return 20e-6f * std::pow(10.0f, spl_db / 20.0f);
}

void test_normal_signal() {
    std::cout << "Test 1: Normal signal (LZeq ~ 70dB)... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Generate normal level signal: ~70 dB = 0.02 Pa RMS
    float rms_pa = spl_to_pa(70.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);
    auto data = generate_sine(1000.0f, amplitude, 48000, 480); // 10ms @ 48kHz

    EventCheckResult result = detector.check_segment(data.data(), data.data() + data.size());

    assert(result == EventCheckResult::NORMAL);

    std::cout << "PASSED (result=NORMAL)\n";
}

void test_overload_signal() {
    std::cout << "Test 2: Overload signal (peak > 140dB)... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f; // 130 dB threshold
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Generate overload: peak > 140 dB
    // 140 dB = 200 Pa RMS -> peak = 200 * sqrt(2) ≈ 283 Pa
    float peak_pa = 283.0f;
    auto data = generate_sine(1000.0f, peak_pa, 48000, 480);

    EventCheckResult result = detector.check_segment(data.data(), data.data() + data.size());

    assert(result == EventCheckResult::OVERLOAD);

    std::cout << "PASSED (result=OVERLOAD)\n";
}

void test_impulse_signal() {
    std::cout << "Test 3: Impulse signal (LZeq >= 90dB)... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 3; // Need 3 consecutive frames
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Generate signal with LZeq >= 90 dB
    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    // Feed 3 consecutive frames above threshold; 3rd frame triggers
    for (int i = 0; i < 3; ++i) {
        auto data = generate_sine(1000.0f, amplitude, 48000, 480);
        auto result = detector.check_segment(data.data(), data.data() + data.size());
        if (i < 2) {
            assert(result == EventCheckResult::NORMAL);
        } else {
            assert(result == EventCheckResult::IMPULSE_SUSPECT);
        }
    }

    std::cout << "PASSED (result=IMPULSE_SUSPECT after 3 frames)\n";
}

void test_single_frame_noise() {
    std::cout << "Test 4: Single frame above threshold (debounce)... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 3; // Need 3 consecutive frames
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Only 1 frame above threshold
    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    auto data = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result = detector.check_segment(data.data(), data.data() + data.size());

    // Should not trigger (only 1 frame, debounce requires 3)
    assert(result == EventCheckResult::NORMAL);

    std::cout << "PASSED (single frame does not trigger)\n";
}

void test_cooldown_period() {
    std::cout << "Test 5: Cooldown period after trigger... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 1; // Immediate trigger
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Trigger first frame
    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    auto data = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result = detector.check_segment(data.data(), data.data() + data.size());
    assert(result == EventCheckResult::IMPULSE_SUSPECT);

    // Next 5 frames should return NORMAL (cooldown)
    for (int i = 0; i < 5; ++i) {
        auto data2 = generate_sine(1000.0f, amplitude, 48000, 480);
        auto result2 = detector.check_segment(data2.data(), data2.data() + data2.size());
        assert(result2 == EventCheckResult::NORMAL);
    }

    // After cooldown, can trigger again
    auto data3 = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result3 = detector.check_segment(data3.data(), data3.data() + data3.size());
    assert(result3 == EventCheckResult::IMPULSE_SUSPECT);

    std::cout << "PASSED (cooldown prevents immediate re-trigger)\n";
}

void test_underrange_signal() {
    std::cout << "Test 6: Underrange signal (LZeq < 30dB)... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.underrange_threshold_db = 30.0f;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Generate very quiet signal: < 30 dB
    float rms_pa = spl_to_pa(20.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    auto data = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result = detector.check_segment(data.data(), data.data() + data.size());

    assert(result == EventCheckResult::UNDERRANGE);

    std::cout << "PASSED (result=UNDERRANGE)\n";
}

void test_reset() {
    std::cout << "Test 7: Reset detector state... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Trigger an event
    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    for (int i = 0; i < 3; ++i) {
        auto data = generate_sine(1000.0f, amplitude, 48000, 480);
        detector.check_segment(data.data(), data.data() + data.size());
    }

    // Reset
    detector.reset();

    // After reset, first frame should not trigger immediately (debounce)
    auto data = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result = detector.check_segment(data.data(), data.data() + data.size());
    assert(result == EventCheckResult::NORMAL);

    std::cout << "PASSED (reset clears counters)\n";
}

void test_impulse_flag() {
    std::cout << "Test 8: was_impulse_detected() and clear_impulse_flag()... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 1; // Immediate trigger
    config.cooldown_frames = 5;

    EventDetector detector(config);

    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    // Trigger
    auto data = generate_sine(1000.0f, amplitude, 48000, 480);
    detector.check_segment(data.data(), data.data() + data.size());

    assert(detector.was_impulse_detected() == true);

    detector.clear_impulse_flag();
    assert(detector.was_impulse_detected() == false);

    std::cout << "PASSED (impulse flag works correctly)\n";
}

void test_config_access() {
    std::cout << "Test 9: Config access... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 95.0f;
    config.peak_threshold_db = 135.0f;
    config.debounce_frames = 4;
    config.cooldown_frames = 6;

    EventDetector detector(config);

    const auto& retrieved_config = detector.config();
    assert(retrieved_config.leq_threshold_db == 95.0f);
    assert(retrieved_config.peak_threshold_db == 135.0f);
    assert(retrieved_config.debounce_frames == 4);
    assert(retrieved_config.cooldown_frames == 6);

    std::cout << "PASSED (config accessible)\n";
}

void test_different_sample_rate() {
    std::cout << "Test 10: Different sample rate (16000 Hz)... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // 10ms @ 16kHz = 160 samples
    float rms_pa = spl_to_pa(70.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    auto data = generate_sine(1000.0f, amplitude, 16000, 160);
    auto result = detector.check_segment(data.data(), data.data() + data.size());

    assert(result == EventCheckResult::NORMAL);

    std::cout << "PASSED (works at 16kHz)\n";
}

void test_empty_buffer() {
    std::cout << "Test 11: Empty buffer... ";

    EventDetectorConfig config;
    EventDetector detector(config);

    std::vector<float> empty;
    auto result = detector.check_segment(empty.data(), empty.data());

    // Empty buffer should return NORMAL (no samples to evaluate)
    assert(result == EventCheckResult::NORMAL);

    std::cout << "PASSED (empty buffer handled)\n";
}

void test_back_to_back_triggers() {
    std::cout << "Test 12: Back-to-back triggers with longer cooldown... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 1;
    config.cooldown_frames = 10;

    EventDetector detector(config);

    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    // Trigger first event
    auto data1 = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result1 = detector.check_segment(data1.data(), data1.data() + data1.size());
    assert(result1 == EventCheckResult::IMPULSE_SUSPECT);

    // Feed quiet signal during cooldown
    for (int i = 0; i < 10; ++i) {
        auto quiet = generate_sine(1000.0f, 0.001f, 48000, 480);
        auto result = detector.check_segment(quiet.data(), quiet.data() + quiet.size());
        assert(result == EventCheckResult::NORMAL);
    }

    // After cooldown, can trigger again
    auto data2 = generate_sine(1000.0f, amplitude, 48000, 480);
    auto result2 = detector.check_segment(data2.data(), data2.data() + data2.size());
    assert(result2 == EventCheckResult::IMPULSE_SUSPECT);

    std::cout << "PASSED (back-to-back triggers with cooldown)\n";
}

void test_overload_during_cooldown() {
    std::cout << "Test 13: Overload during cooldown is not suppressed... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 1;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);
    auto data = generate_sine(1000.0f, amplitude, 48000, 480);

    assert(detector.check_segment(data.data(), data.data() + data.size()) ==
           EventCheckResult::IMPULSE_SUSPECT);

    // Level trigger suppressed during cooldown
    assert(detector.check_segment(data.data(), data.data() + data.size()) ==
           EventCheckResult::NORMAL);

    // Peak overload must still fire
    float peak_pa = 283.0f;
    auto overload = generate_sine(1000.0f, peak_pa, 48000, 480);
    assert(detector.check_segment(overload.data(), overload.data() + overload.size()) ==
           EventCheckResult::OVERLOAD);

    std::cout << "PASSED\n";
}

void test_impulse_flag_persists() {
    std::cout << "Test 14: was_impulse_detected persists until cleared... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 1;
    config.cooldown_frames = 3;

    EventDetector detector(config);

    float rms_pa = spl_to_pa(95.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);
    auto data = generate_sine(1000.0f, amplitude, 48000, 480);

    detector.check_segment(data.data(), data.data() + data.size());
    assert(detector.was_impulse_detected());

    for (int i = 0; i < 3; ++i) {
        detector.check_segment(data.data(), data.data() + data.size());
        assert(detector.was_impulse_detected());
    }

    detector.clear_impulse_flag();
    assert(!detector.was_impulse_detected());

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  EventDetector Unit Tests\n";
    std::cout << "========================================\n\n";

    try {
        test_normal_signal();
        test_overload_signal();
        test_impulse_signal();
        test_single_frame_noise();
        test_cooldown_period();
        test_underrange_signal();
        test_reset();
        test_impulse_flag();
        test_config_access();
        test_different_sample_rate();
        test_empty_buffer();
        test_back_to_back_triggers();
        test_overload_during_cooldown();
        test_impulse_flag_persists();

        std::cout << "\n========================================\n";
        std::cout << "  ALL TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "\nTEST FAILED: Unknown error\n";
        return 1;
    }
}