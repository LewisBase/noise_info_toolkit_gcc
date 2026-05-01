/**
 * @file main.cpp
 * @brief Example usage of noise_info_toolkit C++ library (v3.0)
 *
 * This example demonstrates the simplified two-interface design:
 * - Interface 1: process_one_second(buffer, buffer + size, duration) - per-segment audio processing
 * - Interface 2: aggregate_minute_metrics(metrics, count, unit_duration) - aggregation
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <cmath>
#include <random>
#include <chrono>

#include "noise_processor.hpp"

using namespace noise_toolkit;

std::vector<float> generate_noise(float spl_db, int sample_rate, float duration_s) {
    size_t n = static_cast<size_t>(sample_rate * duration_s);
    std::vector<float> data(n);

    float rms = REFERENCE_PRESSURE * std::pow(10.0f, spl_db / 20.0f);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> dis(0.0f, rms);

    for (size_t i = 0; i < n; ++i) {
        data[i] = static_cast<float>(dis(gen));
    }
    return data;
}

std::vector<float> generate_sine(float freq_hz, float spl_db,
                                   int sample_rate, float duration_s) {
    size_t n = static_cast<size_t>(sample_rate * duration_s);
    std::vector<float> data(n);

    float rms = REFERENCE_PRESSURE * std::pow(10.0f, spl_db / 20.0f);
    float amplitude = rms * std::sqrt(2.0f);

    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        data[i] = amplitude * std::sin(2.0f * M_PI * freq_hz * t);
    }
    return data;
}

void print_second_metrics(const SecondMetrics& m) {
    std::cout << "  LAeq=" << std::fixed << std::setprecision(1) << m.LAeq << " dB  "
              << "LCeq=" << m.LCeq << " dB  "
              << "LZeq=" << m.LZeq << " dB\n";
    std::cout << "  LAFmax=" << m.LAFmax << " dB  "
              << "LZPeak=" << m.LZPeak << " dB  "
              << "LCPeak=" << m.LCPeak << " dB\n";
    std::cout << "  kurtosis=" << std::setprecision(2) << m.kurtosis_total
              << "  beta=" << m.beta_kurtosis << "\n";
    std::cout << "  dose: niosh=" << std::setprecision(4) << m.dose_frac_niosh
              << "  osha_pel=" << m.dose_frac_osha_pel
              << "  osha_hca=" << m.dose_frac_osha_hca
              << "  eu_iso=" << m.dose_frac_eu_iso << "\n";
    std::cout << "  QC: overload=" << (m.overload_flag ? "Y" : "N")
              << "  underrange=" << (m.underrange_flag ? "Y" : "N")
              << "  wearing=" << (m.wearing_state ? "Y" : "N") << "\n";
    std::cout << "  1/3 octave SPLs (dB): "
              << "63Hz=" << m.freq_63hz_spl << " "
              << "500Hz=" << m.freq_500hz_spl << " "
              << "1kHz=" << m.freq_1khz_spl << " "
              << "4kHz=" << m.freq_4khz_spl << " "
              << "8kHz=" << m.freq_8khz_spl << "\n";
    std::cout << "  raw_moments: n=" << m.n_samples
              << "  S1=" << std::setprecision(6) << m.sum_x
              << "  S2=" << m.sum_x2 << "\n";
}

void print_minute_metrics(const MinuteMetrics& m) {
    std::cout << "\n=== Aggregated Metrics ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  LAeq=" << m.LAeq << " dB  "
              << "LCeq=" << m.LCeq << " dB  "
              << "LZeq=" << m.LZeq << " dB\n";
    std::cout << "  LAFmax=" << m.LAFmax << " dB  "
              << "LZPeak=" << m.LZPeak << " dB\n";
    std::cout << "  dose: niosh=" << std::setprecision(4) << m.dose_frac_niosh
              << "  osha_pel=" << m.dose_frac_osha_pel << "\n";
    std::cout << "  QC: overload_count=" << m.overload_count
              << "  underrange_count=" << m.underrange_count
              << "  valid_seconds=" << m.valid_seconds << "\n";
    std::cout << "  kurtosis=" << std::setprecision(2) << m.kurtosis_total
              << "  n_samples=" << m.n_samples << "\n";
    std::cout << "  1/3 octave SPLs: "
              << "63Hz=" << m.freq_63hz_spl << " "
              << "1kHz=" << m.freq_1khz_spl << " "
              << "4kHz=" << m.freq_4khz_spl << "\n";
}

void test_interface1() {
    std::cout << "\n========== Test Interface 1: Per-Segment Processing ==========\n";

    NoiseProcessor processor(48000);

    auto data = generate_sine(1000.0f, 85.0f, 48000, 1.0f);

    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

    std::cout << "Processed 1 second of 1kHz tone at 85 dB SPL:\n";
    print_second_metrics(m);

    std::cout << "\nProcessed 1 second of white noise at 90 dB SPL:\n";
    auto noise_data = generate_noise(90.0f, 48000, 1.0f);
    SecondMetrics m2 = processor.process_one_second(noise_data.data(),
                                                     noise_data.data() + noise_data.size(), 1.0f);
    print_second_metrics(m2);
}

void test_interface1_10ms() {
    std::cout << "\n========== Test Interface 1: 10ms Processing ==========\n";

    NoiseProcessor processor(48000);

    auto data = generate_sine(1000.0f, 85.0f, 48000, 0.01f);

    SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 0.01f);

    std::cout << "Processed 10ms of 1kHz tone at 85 dB SPL:\n";
    std::cout << "  duration_s=" << m.duration_s << " n_samples=" << m.n_samples << "\n";
    print_second_metrics(m);
}

void test_interface2() {
    std::cout << "\n========== Test Interface 2: Aggregation ==========\n";

    NoiseProcessor processor(48000);

    std::array<SecondMetrics, 60> seconds;

    for (int i = 0; i < 60; ++i) {
        float spl = 70.0f + 25.0f * (i / 59.0f);
        auto data = generate_noise(spl, 48000, 1.0f);
        seconds[i] = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);
        seconds[i].timestamp = static_cast<float>(i);
    }

    MinuteMetrics minute = processor.aggregate_minute_metrics(seconds.data(), 60, 1.0f);
    print_minute_metrics(minute);

    MinuteMetrics minute2 = processor.aggregate_minute_metrics(seconds.data(), 60, 1.0f);
    (void)minute2;
}

void test_interface2_10ms() {
    std::cout << "\n========== Test Interface 2: Aggregation (10ms units) ==========\n";

    NoiseProcessor processor(48000);

    std::array<SecondMetrics, 6000> segments;

    for (int i = 0; i < 6000; ++i) {
        float spl = 70.0f + 25.0f * (i / 5999.0f);
        auto data = generate_noise(spl, 48000, 0.01f);
        segments[i] = processor.process_one_second(data.data(), data.data() + data.size(), 0.01f);
        segments[i].timestamp = static_cast<float>(i) * 0.01f;
    }

    MinuteMetrics minute = processor.aggregate_minute_metrics(segments.data(), 6000, 0.01f);
    std::cout << "Aggregated 6000 x 10ms segments = " << minute.duration_s << " seconds\n";
    print_minute_metrics(minute);
}

void test_frequency_bands() {
    std::cout << "\n========== Test Frequency Band Analysis ==========\n";

    NoiseProcessor processor(48000);

    std::vector<std::pair<float, float>> tones = {
        {63.0f, 85.0f},
        {1000.0f, 85.0f},
        {8000.0f, 85.0f},
    };

    for (const auto& [freq, spl] : tones) {
        auto data = generate_sine(freq, spl, 48000, 1.0f);
        SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

        std::cout << std::fixed << std::setprecision(1);
        std::cout << freq << " Hz tone: ";
        std::cout << "band_63Hz=" << m.freq_63hz_spl << " "
                  << "band_1kHz=" << m.freq_1khz_spl << " "
                  << "band_8kHz=" << m.freq_8khz_spl << "\n";
    }
}

void test_qc_flags() {
    std::cout << "\n========== Test QC Flags ==========\n";

    NoiseProcessor processor(48000);

    auto quiet = generate_noise(20.0f, 48000, 1.0f);
    SecondMetrics m_quiet = processor.process_one_second(quiet.data(), quiet.data() + quiet.size(), 1.0f);
    std::cout << "Quiet signal (20 dB): underrange=" << m_quiet.underrange_flag << "\n";

    auto normal = generate_noise(85.0f, 48000, 1.0f);
    SecondMetrics m_normal = processor.process_one_second(normal.data(), normal.data() + normal.size(), 1.0f);
    std::cout << "Normal signal (85 dB): underrange=" << m_normal.underrange_flag
              << " wearing=" << m_normal.wearing_state << "\n";
}

void test_sample_rates() {
    std::cout << "\n========== Test Sample Rate Variations ==========\n";

    std::vector<int> rates = {8000, 16000, 44100, 48000};

    for (int sr : rates) {
        NoiseProcessor processor(sr);
        auto data = generate_sine(1000.0f, 85.0f, sr, 1.0f);
        SecondMetrics m = processor.process_one_second(data.data(), data.data() + data.size(), 1.0f);

        std::cout << std::fixed << std::setprecision(1);
        std::cout << sr << " Hz: n_samples=" << m.n_samples
                  << " LAeq=" << m.LAeq << " dB\n";
    }
}

int main() {
    std::cout << "================================================\n";
    std::cout << "  Noise Info Toolkit C++ v3.0\n";
    std::cout << "  Flexible Duration Interface Demo\n";
    std::cout << "================================================\n";

    test_interface1();
    test_interface1_10ms();
    test_interface2();
    test_interface2_10ms();
    test_frequency_bands();
    test_qc_flags();
    test_sample_rates();

    std::cout << "\n================================================\n";
    std::cout << "  All tests completed\n";
    std::cout << "================================================\n";

    return 0;
}