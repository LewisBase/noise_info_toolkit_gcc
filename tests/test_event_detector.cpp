/**
 * @file test_event_detector.cpp
 * @brief Unit tests for EventDetector — v3.3.0 LZeq deprecate edition
 *
 * [v3.3.0] IMPULSE_SUSPECT trigger disabled by default (leq_threshold_db = INFINITY).
 * Only OVERLOAD (LZpeak >= 140 dB) is active as the single-trigger interface.
 * Legacy Impulse Suspect tests removed; replacement tests verify disabled behavior.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <array>
#include <cstdlib>
#include <limits>
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

//==============================================================================
// KEPT TESTS (unrelated to IMPULSE_SUSPECT)
//==============================================================================

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

void test_underrange_signal() {
    std::cout << "Test 3: Underrange signal (LZeq < 30dB)... ";

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
    std::cout << "Test 4: Reset detector state... ";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = 130.0f;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // Trigger an event (OVERLOAD should work)
    float peak_pa = 283.0f;
    auto overload_data = generate_sine(1000.0f, peak_pa, 48000, 480);
    detector.check_segment(overload_data.data(), overload_data.data() + overload_data.size());
    assert(detector.was_impulse_detected() == true);

    // Reset
    detector.reset();
    assert(detector.was_impulse_detected() == false);

    std::cout << "PASSED (reset clears flags & counters)\n";
}

void test_config_access() {
    std::cout << "Test 5: Config access... ";

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
    std::cout << "Test 6: Different sample rate (16000 Hz)... ";

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
    std::cout << "Test 7: Empty buffer... ";

    EventDetectorConfig config;
    EventDetector detector(config);

    std::vector<float> empty;
    auto result = detector.check_segment(empty.data(), empty.data());

    // Empty buffer should return NORMAL (no samples to evaluate)
    assert(result == EventCheckResult::NORMAL);

    std::cout << "PASSED (empty buffer handled)\n";
}

//==============================================================================
// [v3.3.0] REPLACEMENT TESTS — IMPULSE_SUSPECT disabled
//==============================================================================

void test_impulse_suspect_disabled() {
    std::cout << "Test 8: IMPULSE_SUSPECT disabled (leq_threshold_db=INFINITY)... ";

    // Default config: leq_threshold_db = INFINITY (no LZeq trigger)
    EventDetector detector;

    // Generate loud signal: ~100 dB LZeq
    float rms_pa = spl_to_pa(100.0f);
    float amplitude = rms_pa * std::sqrt(2.0f);

    // Feed 5 consecutive frames at high level
    for (int i = 0; i < 5; ++i) {
        auto data = generate_sine(1000.0f, amplitude, 48000, 480);
        auto result = detector.check_segment(data.data(), data.data() + data.size());
        // Must never return IMPULSE_SUSPECT (trigger is disabled)
        assert(result != EventCheckResult::IMPULSE_SUSPECT);
        // Should return NORMAL (not OVERLOAD either, since peak < 140 dB)
        assert(result == EventCheckResult::NORMAL);
    }

    // was_impulse_detected should be false (no trigger fired)
    assert(detector.was_impulse_detected() == false);

    std::cout << "PASSED (5 frames @ 100dB yield NORMAL, no IMPULSE_SUSPECT)\n";
}

void test_leq_threshold_default_infinity() {
    std::cout << "Test 9: leq_threshold_db defaults to INFINITY... ";

    EventDetectorConfig config;
    // Default should be INFINITY
    assert(std::isinf(config.leq_threshold_db));
    assert(config.leq_threshold_db > 0.0f);  // positive infinity

    // Explicit INFINITY also works
    config.leq_threshold_db = 90.0f;
    assert(config.leq_threshold_db == 90.0f);
    config.leq_threshold_db = INFINITY;
    assert(std::isinf(config.leq_threshold_db));

    std::cout << "PASSED (default is INFINITY, can toggle to 90.0f and back)\n";
}

void test_overload_still_works_with_infinity_leq() {
    std::cout << "Test 10: OVERLOAD still fires despite INFINITY leq_threshold... ";

    // Default config: leq_threshold_db = INFINITY, peak_threshold_db = 140
    EventDetectorConfig config;
    // peak_threshold_db defaults to OVERLOAD_THRESHOLD which is 140.0f
    EventDetector detector(config);

    // OVERLOAD: peak ~283 Pa (~143 dB LZpeak) — must fire even with INFINITY leq
    float peak_pa = 283.0f;
    auto data = generate_sine(1000.0f, peak_pa, 48000, 480);
    auto result = detector.check_segment(data.data(), data.data() + data.size());

    assert(result == EventCheckResult::OVERLOAD);
    assert(detector.was_impulse_detected() == true);

    // Clear and verify
    detector.clear_impulse_flag();
    assert(detector.was_impulse_detected() == false);

    std::cout << "PASSED (OVERLOAD still works with INFINITY leq_threshold)\n";
}

//==============================================================================
// Main
//==============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "  EventDetector Unit Tests  [v3.3.0 LZeq deprecate]\n";
    std::cout << "========================================\n\n";

    try {
        test_normal_signal();
        test_overload_signal();
        test_underrange_signal();
        test_reset();
        test_config_access();
        test_different_sample_rate();
        test_empty_buffer();

        test_impulse_suspect_disabled();
        test_leq_threshold_default_infinity();
        test_overload_still_works_with_infinity_leq();

        std::cout << "\n========================================\n";
        std::cout << "  ALL 10 TESTS PASSED\n";
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
