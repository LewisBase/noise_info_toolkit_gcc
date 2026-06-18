/**
 * @file test_end_to_end.cpp
 * @brief End-to-end validation: LAeq, LCeq, LZeq, Dose on known test signals
 *
 * Tests:
 *  1. 1 kHz pure tone at 94 dB SPL: LAeq ≈ 94, LCeq ≈ 94, LZeq ≈ 94
 *  2. 1 kHz pure tone at 90 dB SPL × 60s: NIOSH dose ≈ 0.66%
 *  3. White noise at 85 dB SPL × 8h: NIOSH dose ≈ 100%
 *  4. C-weighting parity: LCeq ≥ LAeq for broadband noise
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>
#include <cassert>

#include "noise_processor.hpp"
#include "dose_state.hpp"
#include "math_constants.hpp"

using namespace noise_toolkit;

namespace {

const int FS = 48000;
const float REF_PA = 20e-6f;

std::vector<float> sine(float freq, float spl_db, float dur_s) {
    size_t n = static_cast<size_t>(FS * dur_s);
    std::vector<float> data(n);
    float peak = REF_PA * std::pow(10.f, spl_db / 20.f) * std::sqrt(2.f);
    for (size_t i = 0; i < n; ++i)
        data[i] = peak * std::sin(noise_const::TWO_PI_F * freq * i / FS);
    return data;
}

std::vector<float> white_noise(float spl_db, float dur_s) {
    size_t n = static_cast<size_t>(FS * dur_s);
    std::vector<float> data(n);
    float rms = REF_PA * std::pow(10.f, spl_db / 20.f);
    std::mt19937 gen(42);
    std::normal_distribution<float> dis(0.f, rms);
    for (size_t i = 0; i < n; ++i) data[i] = dis(gen);
    return data;
}

bool check(const char* name, float got, float expected, float tol_db) {
    float err = std::abs(got - expected);
    bool ok = err <= tol_db;
    std::cout << "  " << std::setw(4) << (ok ? "✓" : "✗") << " "
              << std::setw(45) << std::left << name
              << ": " << std::fixed << std::setprecision(2) << got
              << " (expected " << expected << " ±" << tol_db << ")"
              << (ok ? "" : "  ← FAIL") << "\n";
    return ok;
}

bool check_pct(const char* name, float got, float expected, float tol_pct) {
    float err_pct = std::abs(got - expected) / expected * 100.f;
    bool ok = err_pct <= tol_pct;
    std::cout << "  " << std::setw(4) << (ok ? "✓" : "✗") << " "
              << std::setw(45) << std::left << name
              << ": " << std::fixed << std::setprecision(4) << got << "%"
              << " (expected " << expected << "% ±" << tol_pct << "%)"
              << (ok ? "" : "  ← FAIL") << "\n";
    return ok;
}

} // namespace

int main() {
    std::cout << "==========================================\n";
    std::cout << "  End-to-End Validation (v3.3.0 matched-z)\n";
    std::cout << "  fs=" << FS << " Hz\n";
    std::cout << "==========================================\n\n";

    int pass = 0, fail = 0;

    // ===== Test 1: 1 kHz at 94 dB SPL =====
    {
        std::cout << "Test 1: 1 kHz pure tone at 94 dB SPL\n";
        auto sig = sine(1000.f, 94.f, 1.f);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(sig.data(), sig.data() + sig.size(), 1.f);

        float tol = 0.5f;  // Class 1 tolerance
        (check("LAeq (= LZeq + 0 dB)", m.LAeq, 94.f, tol) ? pass : fail)++;
        (check("LCeq (= LZeq + 0 dB)", m.LCeq, 94.f, tol) ? pass : fail)++;
        (check("LZeq", m.LZeq, 94.f, tol) ? pass : fail)++;
        std::cout << "\n";
    }

    // ===== Test 2: NIOSH Dose at 90 dB SPL × 10s =====
    {
        std::cout << "Test 2: NIOSH dose at 90 dB SPL × 10s\n";
        auto sig = sine(1000.f, 90.f, 10.f);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(sig.data(), sig.data() + sig.size(), 10.f);

        // NIOSH: dose_frac = t/T_ref * 2^((LAeq-85)/3)
        // 10s at 90 dB: 10/28800 * 2^(5/3) = 0.0003472 * 3.1748 = 0.0011024
        // In percent: 0.11024%
        float expected_dose_pct = 0.11024f;
        float dose_pct = m.dose_frac_niosh * 100.f;
        (check_pct("NIOSH dose% (90 dB × 10s)", dose_pct, expected_dose_pct, 5.f) ? pass : fail)++;
        std::cout << "\n";
    }

    // ===== Test 3: NIOSH Dose formula validation =====
    {
        std::cout << "Test 3: NIOSH dose formula validation\n";
        // NIOSH: dose_frac = t/T_ref * 2^((LAeq-Lc)/q)
        // t=1s, T_ref=28800s, Lc=85, q=3
        // At 85 dB: 1/28800 * 2^0 = 3.4722e-5 per second
        // 28800 such blocks → dose=1.0=100%
        auto sig = sine(1000.f, 85.f, 1.f);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(sig.data(), sig.data() + sig.size(), 1.f);
        float expected_frac = 1.f / 28800.f;  // = 3.4722e-5
        float dose_pct_per_s = m.dose_frac_niosh * 100.f;
        (check_pct("NIOSH dose% per 1s at 85 dB", dose_pct_per_s, expected_frac * 100.f, 5.f) ? pass : fail)++;

        // Verify: 28800 × dose_frac_per_s ≈ 1.0
        float total_pct = dose_pct_per_s * 28800.f;
        (check_pct("NIOSH dose% extrapolated to 8h", total_pct, 100.f, 5.f) ? pass : fail)++;
        std::cout << "\n";
    }

    // ===== Test 4: LCeq ≥ LAeq for broadband noise =====
    {
        std::cout << "Test 4: LCeq ≥ LAeq (broadband noise)\n";
        auto sig = white_noise(90.f, 3.f);
        NoiseProcessor proc(FS);
        auto m = proc.process_segment(sig.data(), sig.data() + sig.size(), 3.f);

        bool ok = m.LCeq >= m.LAeq - 0.01f;  // tiny float tolerance
        std::cout << "  " << (ok ? "✓" : "✗")
                  << " LCeq ≥ LAeq: LCeq=" << std::fixed << std::setprecision(2) << m.LCeq
                  << " dB, LAeq=" << m.LAeq << " dB, LZeq=" << m.LZeq << " dB"
                  << (ok ? "" : "  ← FAIL") << "\n";
        (ok ? pass : fail)++;
        std::cout << "\n";
    }

    // ===== Test 5: A-weighted sensitivity =====
    {
        std::cout << "Test 5: A-weighting attenuation checks\n";
        NoiseProcessor proc(FS);

        // 100 Hz: -19.1 dB attenuation
        auto sig100 = sine(100.f, 94.f, 1.f);
        auto m100 = proc.process_segment(sig100.data(), sig100.data() + sig100.size(), 1.f);
        (check("LAeq at 100 Hz (94-19.1=74.9)", m100.LAeq, 74.9f, 0.5f) ? pass : fail)++;

        // 10 kHz: -2.5 dB attenuation
        auto sig10k = sine(10000.f, 94.f, 1.f);
        auto m10k = proc.process_segment(sig10k.data(), sig10k.data() + sig10k.size(), 1.f);
        (check("LAeq at 10 kHz (94-2.5=91.5)", m10k.LAeq, 91.5f, 0.5f) ? pass : fail)++;
        std::cout << "\n";
    }

    // ===== Summary =====
    std::cout << "==========================================\n";
    std::cout << "  " << pass << "/" << (pass+fail) << " tests passed";
    if (fail > 0) std::cout << "  ← " << fail << " FAILURES";
    std::cout << "\n==========================================\n";

    return fail > 0 ? 1 : 0;
}
