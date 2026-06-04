/**
 * @file main.cpp
 * @brief Example and manual verification for noise_info_toolkit (v3.1.2)
 *
 * Demonstrates the three core interfaces:
 * - Interface 1: process_segment(buffer, buffer + size, duration)
 * - Interface 2: aggregate_metrics(metrics, count, unit_duration)
 * - Interface 3: EventDetector::check_segment(buffer, buffer + size)
 *
 * This is a demo program (stdout only, no assert). For automated checks use
 * ./test_noise_processor and ./test_event_detector (prefer Debug build).
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <cmath>
#include <random>

#include "noise_processor.hpp"
#include "event_detector.hpp"
#include "dose_state.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

namespace {

const int kSampleRate = 48000;
const float kBlockDurationS = 0.01f;  // 10 ms
const size_t kBlockSamples = static_cast<size_t>(kSampleRate * kBlockDurationS);

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
        data[i] = amplitude * std::sin(noise_const::TWO_PI_F * freq_hz * t);
    }
    return data;
}

/** Constant-amplitude block (peak SPL ≈ 20*log10(amp/p0) dB) */
std::vector<float> generate_constant(float amplitude_pa, size_t n) {
    return std::vector<float>(n, amplitude_pa);
}

const char* event_result_str(EventCheckResult r) {
    switch (r) {
        case EventCheckResult::NORMAL: return "NORMAL";
        case EventCheckResult::OVERLOAD: return "OVERLOAD";
        case EventCheckResult::UNDERRANGE: return "UNDERRANGE";
        case EventCheckResult::IMPULSE_SUSPECT: return "IMPULSE_SUSPECT";
    }
    return "?";
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

void print_event_line(int frame, EventCheckResult ev, bool impulse_flag) {
    std::cout << "  frame " << std::setw(2) << frame << ": event="
              << event_result_str(ev)
              << "  impulse_flag=" << (impulse_flag ? "Y" : "N") << "\n";
}

} // namespace

void test_interface1() {
    std::cout << "\n========== Interface 1: Per-Segment Processing (1 s) ==========\n";

    NoiseProcessor processor(kSampleRate);

    auto data = generate_sine(1000.0f, 85.0f, kSampleRate, 1.0f);
    SecondMetrics m = processor.process_segment(data.data(), data.data() + data.size(), 1.0f);

    std::cout << "1 kHz tone @ 85 dB SPL, 1 s:\n";
    print_second_metrics(m);

    std::cout << "\nWhite noise @ 90 dB SPL, 1 s:\n";
    auto noise_data = generate_noise(90.0f, kSampleRate, 1.0f);
    SecondMetrics m2 = processor.process_segment(
        noise_data.data(), noise_data.data() + noise_data.size(), 1.0f);
    print_second_metrics(m2);
}

void test_interface1_10ms() {
    std::cout << "\n========== Interface 1: 10 ms Block ==========\n";

    NoiseProcessor processor(kSampleRate);
    auto data = generate_sine(1000.0f, 85.0f, kSampleRate, kBlockDurationS);
    SecondMetrics m = processor.process_segment(
        data.data(), data.data() + data.size(), kBlockDurationS);

    std::cout << "1 kHz tone @ 85 dB SPL, 10 ms:\n";
    std::cout << "  duration_s=" << m.duration_s << " n_samples=" << m.n_samples << "\n";
    print_second_metrics(m);
}

void test_interface2() {
    std::cout << "\n========== Interface 2: Aggregation (1 s units) ==========\n";

    NoiseProcessor processor(kSampleRate);
    std::array<SecondMetrics, 60> seconds;

    for (int i = 0; i < 60; ++i) {
        float spl = 70.0f + 25.0f * (i / 59.0f);
        auto data = generate_noise(spl, kSampleRate, 1.0f);
        seconds[i] = processor.process_segment(
            data.data(), data.data() + data.size(), 1.0f);
        seconds[i].timestamp = static_cast<float>(i);
    }

    MinuteMetrics minute = processor.aggregate_metrics(seconds.data(), 60, 1.0f);
    print_minute_metrics(minute);
}

void test_interface2_10ms() {
    std::cout << "\n========== Interface 2: Aggregation (10 ms units) ==========\n";

    NoiseProcessor processor(kSampleRate);
    std::array<SecondMetrics, 6000> segments;

    for (int i = 0; i < 6000; ++i) {
        float spl = 70.0f + 25.0f * (i / 5999.0f);
        auto data = generate_noise(spl, kSampleRate, kBlockDurationS);
        segments[i] = processor.process_segment(
            data.data(), data.data() + data.size(), kBlockDurationS);
        segments[i].timestamp = static_cast<float>(i) * kBlockDurationS;
    }

    MinuteMetrics minute = processor.aggregate_metrics(segments.data(), 6000, kBlockDurationS);
    std::cout << "Aggregated 6000 x 10 ms = " << minute.duration_s << " s\n";
    print_minute_metrics(minute);
}

void test_frequency_bands() {
    std::cout << "\n========== Frequency Band Analysis ==========\n";

    NoiseProcessor processor(kSampleRate);

    std::vector<std::pair<float, float>> tones = {
        {63.0f, 85.0f},
        {1000.0f, 85.0f},
        {8000.0f, 85.0f},
    };

    for (const auto& [freq, spl] : tones) {
        auto data = generate_sine(freq, spl, kSampleRate, 1.0f);
        SecondMetrics m = processor.process_segment(
            data.data(), data.data() + data.size(), 1.0f);

        std::cout << std::fixed << std::setprecision(1);
        std::cout << freq << " Hz: band_63Hz=" << m.freq_63hz_spl
                  << " band_1kHz=" << m.freq_1khz_spl
                  << " band_8kHz=" << m.freq_8khz_spl << "\n";
    }
}

void test_qc_flags() {
    std::cout << "\n========== NoiseProcessor QC Flags ==========\n";

    NoiseProcessor processor(kSampleRate);

    auto quiet = generate_noise(20.0f, kSampleRate, 1.0f);
    SecondMetrics m_quiet = processor.process_segment(
        quiet.data(), quiet.data() + quiet.size(), 1.0f);
    std::cout << "Quiet (20 dB): underrange=" << m_quiet.underrange_flag << "\n";

    auto normal = generate_noise(85.0f, kSampleRate, 1.0f);
    SecondMetrics m_normal = processor.process_segment(
        normal.data(), normal.data() + normal.size(), 1.0f);
    std::cout << "Normal (85 dB): underrange=" << m_normal.underrange_flag
              << " wearing=" << m_normal.wearing_state << "\n";
}

void test_sample_rates() {
    std::cout << "\n========== Sample Rate Variations ==========\n";

    std::vector<int> rates = {8000, 16000, 44100, 48000};

    for (int sr : rates) {
        NoiseProcessor processor(sr);
        auto data = generate_sine(1000.0f, 85.0f, sr, 1.0f);
        SecondMetrics m = processor.process_segment(
            data.data(), data.data() + data.size(), 1.0f);

        std::cout << std::fixed << std::setprecision(1);
        std::cout << sr << " Hz: n_samples=" << m.n_samples
                  << " LAeq=" << m.LAeq << " dB\n";
    }
}

void test_interface3_event_detector() {
    std::cout << "\n========== Interface 3: EventDetector (standalone) ==========\n";
    std::cout << "10 ms blocks @ 48 kHz, Z-weighted Pa input\n\n";

    EventDetectorConfig config;
    config.leq_threshold_db = 90.0f;
    config.peak_threshold_db = OVERLOAD_THRESHOLD;
    config.underrange_threshold_db = UNDERRANGE_THRESHOLD;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);

    // --- normal ---
    std::cout << "[1] Normal ~70 dB sine\n";
    auto normal = generate_sine(1000.0f, 70.0f, kSampleRate, kBlockDurationS);
    {
        EventCheckResult ev = detector.check_segment(normal.data(), normal.data() + normal.size());
        print_event_line(0, ev, detector.was_impulse_detected());
    }

    // --- debounce: 3 frames @ 95 dB ---
    std::cout << "\n[2] Loud 95 dB sine x 4 frames (debounce=3)\n";
    auto loud = generate_sine(1000.0f, 95.0f, kSampleRate, kBlockDurationS);
    for (int i = 0; i < 4; ++i) {
        EventCheckResult ev = detector.check_segment(loud.data(), loud.data() + loud.size());
        print_event_line(i, ev, detector.was_impulse_detected());
    }

    detector.reset();

    // --- single frame above threshold ---
    std::cout << "\n[3] Single loud frame (should stay NORMAL)\n";
    {
        EventCheckResult ev = detector.check_segment(loud.data(), loud.data() + loud.size());
        print_event_line(0, ev, detector.was_impulse_detected());
    }

    // --- underrange ---
    std::cout << "\n[4] Quiet 20 dB sine\n";
    auto quiet = generate_sine(1000.0f, 20.0f, kSampleRate, kBlockDurationS);
    {
        EventCheckResult ev = detector.check_segment(quiet.data(), quiet.data() + quiet.size());
        print_event_line(0, ev, detector.was_impulse_detected());
    }

    // --- overload (peak ~143 dB) ---
    std::cout << "\n[5] Overload constant ~283 Pa peak\n";
    auto overload = generate_constant(283.0f, kBlockSamples);
    EventCheckResult ov = detector.check_segment(overload.data(), overload.data() + overload.size());
    print_event_line(0, ov, detector.was_impulse_detected());
    detector.clear_impulse_flag();
    std::cout << "  after clear_impulse_flag: impulse_flag="
              << (detector.was_impulse_detected() ? "Y" : "N") << "\n";

    // --- empty buffer ---
    std::cout << "\n[6] Empty buffer\n";
    std::vector<float> empty;
    {
        EventCheckResult ev = detector.check_segment(empty.data(), empty.data());
        print_event_line(0, ev, detector.was_impulse_detected());
    }
}

void test_interface3_with_noise_processor() {
    std::cout << "\n========== Interface 3 + NoiseProcessor (same 10 ms buffer) ==========\n";
    std::cout << "EventDetector on raw Z(Pa); NoiseProcessor applies A/C weighting internally\n\n";

    EventDetectorConfig config;
    config.debounce_frames = 3;
    config.cooldown_frames = 5;

    EventDetector detector(config);
    NoiseProcessor processor(kSampleRate);

    struct Scenario {
        const char* label;
        std::vector<float> data;
    };

    std::vector<Scenario> scenarios;
    scenarios.push_back({"70 dB sine (normal)",
                         generate_sine(1000.0f, 70.0f, kSampleRate, kBlockDurationS)});
    scenarios.push_back({"95 dB sine #1 (impulse debounce)",
                         generate_sine(1000.0f, 95.0f, kSampleRate, kBlockDurationS)});
    scenarios.push_back({"95 dB sine #2",
                         generate_sine(1000.0f, 95.0f, kSampleRate, kBlockDurationS)});
    scenarios.push_back({"95 dB sine #3 (expect IMPULSE on event)",
                         generate_sine(1000.0f, 95.0f, kSampleRate, kBlockDurationS)});
    scenarios.push_back({"overload ~283 Pa",
                         generate_constant(283.0f, kBlockSamples)});

    int frame = 0;
    for (const auto& sc : scenarios) {
        const float* start = sc.data.data();
        const float* end = start + sc.data.size();

        EventCheckResult ev = detector.check_segment(start, end);
        SecondMetrics m = processor.process_segment(start, end, kBlockDurationS);

        std::cout << "--- " << sc.label << " (frame " << frame << ") ---\n";
        std::cout << "  EventDetector: " << event_result_str(ev)
                  << "  impulse_flag=" << (detector.was_impulse_detected() ? "Y" : "N") << "\n";
        std::cout << "  NoiseProcessor: LZeq=" << std::fixed << std::setprecision(1) << m.LZeq
                  << " dB  LZPeak=" << m.LZPeak << " dB\n";
        std::cout << "  QC flags: overload=" << (m.overload_flag ? "Y" : "N")
                  << "  underrange(LAeq)=" << (m.underrange_flag ? "Y" : "N") << "\n";

        bool overload_match =
            (ev == EventCheckResult::OVERLOAD) == (m.overload_flag != 0);
        std::cout << "  overload consistent (event OVERLOAD vs metrics flag): "
                  << (overload_match ? "yes" : "no") << "\n\n";
        ++frame;
    }
}

//==============================================================================
// v3.1.3 — Dose State (Dose%/TWA/LEX,8h) thin-wrapper demo
//==============================================================================
void test_dose_state_demo() {
    std::cout << "\n========== Interface 4: DoseState (v3.1.3) ==========\n";
    std::cout << "Demonstrates cumulative dose accumulation + Dose%/TWA/LEX,8h readout.\n";
    std::cout << "Simulates 1 minute of 90 dB exposure (60 x 1s segments) for fast demo, reports all 4 standards.\n\n";

    NoiseProcessor processor(kSampleRate);

    // 4 dose states, one per standard (business-side held)
    DoseState niosh_state = {};
    DoseState osha_pel_state = {};
    DoseState osha_hca_state = {};
    DoseState eu_iso_state = {};

    const int n_seconds = 60;             // demo: 1 minute of 90 dB (60 x 1s segments)
    const float dt_s = 1.0f;              // 1 second segments
    const float laeq = 90.0f;             // 90 dB constant

    std::cout << "Simulating " << n_seconds << " x 1s segments @ LAeq=" << laeq << " dB (= " << n_seconds/60.0f << " minute total)...\n";

    int report_every = n_seconds / 4;     // report 4 times during sim
    for (int i = 0; i < n_seconds; ++i) {
        auto data = generate_noise(laeq, kSampleRate, dt_s);
        SecondMetrics m = processor.process_segment(
            data.data(), data.data() + data.size(), dt_s);

        // Update all 4 dose states with their respective dose_frac
        niosh_state    = accumulate_dose_frac(niosh_state,    m.dose_frac_niosh,    dt_s);
        osha_pel_state = accumulate_dose_frac(osha_pel_state, m.dose_frac_osha_pel, dt_s);
        osha_hca_state = accumulate_dose_frac(osha_hca_state, m.dose_frac_osha_hca, dt_s);
        eu_iso_state   = accumulate_dose_frac(eu_iso_state,   m.dose_frac_eu_iso,   dt_s);

        // Report at quarters
        if ((i + 1) % report_every == 0) {
            float hours_elapsed = (i + 1) * dt_s / 3600.0f;
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "  [" << hours_elapsed << "h] "
                      << "NIOSH Dose%=" << dose_to_pct(niosh_state) << "% TWA="
                      << dose_to_twa(niosh_state, DoseStandard::NIOSH) << " dB  | "
                      << "OSHA_PEL Dose%=" << dose_to_pct(osha_pel_state) << "% TWA="
                      << dose_to_twa(osha_pel_state, DoseStandard::OSHA_PEL) << " dB\n";
        }
    }

    // Final report
    std::cout << "\n=== Final (after 1 minute @ 90 dB) ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  elapsed_hours = " << niosh_state.elapsed_hours << " h\n\n";

    std::cout << "  Standard   | Dose%   | TWA (dB) | LEX,8h (dB)\n";
    std::cout << "  -----------|---------|----------|----------\n";
    std::cout << "  NIOSH      | " << std::setw(6) << dose_to_pct(niosh_state) << "% | "
              << std::setw(7) << dose_to_twa(niosh_state, DoseStandard::NIOSH) << " | "
              << std::setw(7) << dose_to_lex8h(niosh_state, DoseStandard::NIOSH) << "\n";
    std::cout << "  OSHA_PEL   | " << std::setw(6) << dose_to_pct(osha_pel_state) << "% | "
              << std::setw(7) << dose_to_twa(osha_pel_state, DoseStandard::OSHA_PEL) << " | "
              << std::setw(7) << dose_to_lex8h(osha_pel_state, DoseStandard::OSHA_PEL) << "\n";
    std::cout << "  OSHA_HCA   | " << std::setw(6) << dose_to_pct(osha_hca_state) << "% | "
              << std::setw(7) << dose_to_twa(osha_hca_state, DoseStandard::OSHA_HCA) << " | "
              << std::setw(7) << dose_to_lex8h(osha_hca_state, DoseStandard::OSHA_HCA) << "\n";
    std::cout << "  EU_ISO     | " << std::setw(6) << dose_to_pct(eu_iso_state) << "% | "
              << std::setw(7) << dose_to_twa(eu_iso_state, DoseStandard::EU_ISO) << " | "
              << std::setw(7) << dose_to_lex8h(eu_iso_state, DoseStandard::EU_ISO) << "\n";
}

int main() {
    std::cout << "================================================\n";
    std::cout << "  Noise Info Toolkit C++ v3.1.3\n";
    std::cout << "  Example / Manual Verification Demo\n";
    std::cout << "================================================\n";

    test_interface1();
    test_interface1_10ms();
    test_interface2();
    test_interface2_10ms();
    test_frequency_bands();
    test_qc_flags();
    test_sample_rates();
    test_interface3_event_detector();
    test_interface3_with_noise_processor();
    test_dose_state_demo();  // v3.1.3

    std::cout << "\n================================================\n";
    std::cout << "  All demos completed\n";
    std::cout << "  Automated tests: ./test_event_detector ./test_noise_processor ./test_dose_state\n";
    std::cout << "================================================\n";

    return 0;
}
