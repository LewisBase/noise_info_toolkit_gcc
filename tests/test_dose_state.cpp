/**
 * @file test_dose_state.cpp
 * @brief Unit tests for DoseState + dose_state.hpp thin-wrapper API
 *
 * v3.1.3 — Tests cover:
 *   - accumulate_dose_frac: basic accumulation + zero-duration boundary
 *   - dose_to_pct: simple scaling
 *   - dose_to_twa: boundary conditions + 3dB (NIOSH) consistency + 5dB (OSHA) regression
 *   - dose_to_lex8h: basic + 4-standard consistency with DoseCalculator
 *
 * Numerical test values verified against DoseCalculator reference implementation.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstdlib>

#include "dose_state.hpp"
#include "dose_calculator.hpp"

using namespace noise_toolkit;

// Floating-point tolerance for log10-based computations
constexpr float kTolerance = 0.01f;

// Helper: check that two floats are approximately equal
bool approx_equal(float a, float b, float tol = kTolerance) {
    return std::fabs(a - b) <= tol;
}

// Helper: assert + report
#define ASSERT_APPROX(actual, expected, msg) \
    do { \
        float _a = (actual); \
        float _e = (expected); \
        if (!approx_equal(_a, _e)) { \
            std::cerr << "\n  ASSERT FAILED: " << msg \
                      << " (actual=" << _a << " expected=" << _e \
                      << " diff=" << std::fabs(_a - _e) << ")\n"; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(actual, expected, msg) \
    do { \
        if (!((actual) == (expected))) { \
            std::cerr << "\n  ASSERT FAILED: " << msg \
                      << " (actual=" << (actual) << " expected=" << (expected) << ")\n"; \
            return 1; \
        } \
    } while (0)

//==============================================================================
// Test 1: accumulate_dose_frac — basic accumulation
//==============================================================================
int test_accumulate_basic() {
    std::cout << "Test 1: accumulate_dose_frac basic... ";

    DoseState state = {};  // {0, 0}
    state = accumulate_dose_frac(state, 0.5f, 1.0f);  // +0.5 dose, +1s

    ASSERT_APPROX(state.cumulative_dose_frac, 0.5f, "cumulative_dose_frac");
    ASSERT_APPROX(state.elapsed_hours, 1.0f / 3600.0f, "elapsed_hours (1s in hours)");

    // Accumulate a second segment
    state = accumulate_dose_frac(state, 0.25f, 2.0f);  // +0.25, +2s

    ASSERT_APPROX(state.cumulative_dose_frac, 0.75f, "after 2nd accumulate");
    ASSERT_APPROX(state.elapsed_hours, 3.0f / 3600.0f, "elapsed_hours (3s in hours)");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 2: accumulate_dose_frac — zero duration boundary (no-op)
//==============================================================================
int test_accumulate_zero_duration() {
    std::cout << "Test 2: accumulate_dose_frac zero duration (no-op)... ";

    DoseState state = {};
    state.cumulative_dose_frac = 0.5f;
    state.elapsed_hours = 1.0f;

    // Negative duration: no-op
    DoseState out = accumulate_dose_frac(state, 0.1f, -0.5f);
    ASSERT_APPROX(out.cumulative_dose_frac, 0.5f, "neg duration: dose unchanged");
    ASSERT_APPROX(out.elapsed_hours, 1.0f, "neg duration: time unchanged");

    // Zero duration: no-op
    out = accumulate_dose_frac(state, 0.1f, 0.0f);
    ASSERT_APPROX(out.cumulative_dose_frac, 0.5f, "zero duration: dose unchanged");
    ASSERT_APPROX(out.elapsed_hours, 1.0f, "zero duration: time unchanged");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 3: dose_to_pct — simple scaling
//==============================================================================
int test_dose_to_pct_basic() {
    std::cout << "Test 3: dose_to_pct basic... ";

    DoseState state1 = {};
    state1.cumulative_dose_frac = 0.5f;
    ASSERT_APPROX(dose_to_pct(state1), 50.0f, "0.5 → 50%");

    DoseState state2 = {};
    state2.cumulative_dose_frac = 1.0f;
    ASSERT_APPROX(dose_to_pct(state2), 100.0f, "1.0 → 100%");

    DoseState state3 = {};
    state3.cumulative_dose_frac = 1.5874f;  // 4h@90dB NIOSH = 158.74%
    ASSERT_APPROX(dose_to_pct(state3), 158.74f, "1.5874 → 158.74%");

    DoseState state4 = {};
    state4.cumulative_dose_frac = 0.0f;
    ASSERT_APPROX(dose_to_pct(state4), 0.0f, "0.0 → 0%");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 4: dose_to_twa — boundary conditions
//==============================================================================
int test_twa_boundary_empty() {
    std::cout << "Test 4: dose_to_twa boundary (empty state)... ";

    // Empty state: should return 0 (no division by zero)
    DoseState state = {};
    ASSERT_APPROX(dose_to_twa(state, DoseStandard::NIOSH), 0.0f,
                  "elapsed=0, dose=0 → 0");
    ASSERT_APPROX(dose_to_twa(state, DoseStandard::OSHA_PEL), 0.0f,
                  "OSHA empty → 0");
    ASSERT_APPROX(dose_to_twa(state, DoseStandard::OSHA_HCA), 0.0f,
                  "OSHA_HCA empty → 0");
    ASSERT_APPROX(dose_to_twa(state, DoseStandard::EU_ISO), 0.0f,
                  "EU_ISO empty → 0");

    // Only dose, no time: should return 0 (elapsed=0 branch)
    DoseState state_dose_only = {};
    state_dose_only.cumulative_dose_frac = 0.5f;
    ASSERT_APPROX(dose_to_twa(state_dose_only, DoseStandard::NIOSH), 0.0f,
                  "elapsed=0, dose>0 → 0 (no time normalization possible)");

    // Only time, no dose: should return 0 (dose=0 branch)
    DoseState state_time_only = {};
    state_time_only.elapsed_hours = 4.0f;
    ASSERT_APPROX(dose_to_twa(state_time_only, DoseStandard::NIOSH), 0.0f,
                  "elapsed>0, dose=0 → 0 (no exposure)");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 5: dose_to_twa — 3dB exchange rate (NIOSH) consistency
//==============================================================================
int test_twa_3db_niosh_consistency() {
    std::cout << "Test 5: dose_to_twa 3dB (NIOSH) consistency with DoseCalculator... ";

    // Test scenario: 4h@90dB → NIOSH dose_pct = 158.74%, TWA = 87.01 dB
    DoseState state = {};
    state.cumulative_dose_frac = 1.5874f;  // 158.74%
    state.elapsed_hours = 4.0f;

    float twa_via_wrapper = dose_to_twa(state, DoseStandard::NIOSH);
    float twa_via_direct = DoseCalculator::calculate_twa(
        state.cumulative_dose_frac * 100.0f,
        DoseStandard::NIOSH);

    ASSERT_APPROX(twa_via_wrapper, 87.01f, "NIOSH TWA = 87.01 dB");
    ASSERT_APPROX(twa_via_wrapper, twa_via_direct, "wrapper == direct (NIOSH)");

    // Also test EU_ISO (3dB, same coefficient as NIOSH)
    float twa_eu = dose_to_twa(state, DoseStandard::EU_ISO);
    float twa_eu_direct = DoseCalculator::calculate_twa(
        state.cumulative_dose_frac * 100.0f,
        DoseStandard::EU_ISO);
    ASSERT_APPROX(twa_eu, twa_eu_direct, "wrapper == direct (EU_ISO)");
    ASSERT_APPROX(twa_eu, 87.01f, "EU_ISO TWA = 87.01 dB (same as NIOSH)");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 6: dose_to_twa — 5dB exchange rate (OSHA) regression
//          This is THE critical test: catches the 10.0f vs 16.61f coefficient bug
//==============================================================================
int test_twa_5db_osha_regression() {
    std::cout << "Test 6: dose_to_twa 5dB (OSHA) regression (16.61 coefficient)... ";

    // Test scenario: 4h@90dB → OSHA_PEL dose_pct = 50%, TWA = 85.0 dB
    DoseState state = {};
    state.cumulative_dose_frac = 0.5f;  // 50%
    state.elapsed_hours = 4.0f;

    float twa_osha_pel = dose_to_twa(state, DoseStandard::OSHA_PEL);
    float twa_osha_pel_direct = DoseCalculator::calculate_twa(
        state.cumulative_dose_frac * 100.0f,
        DoseStandard::OSHA_PEL);

    // 4h@90dB → OSHA_PEL TWA = 85.0 dB (5dB exchange rate with 16.61 coefficient)
    ASSERT_APPROX(twa_osha_pel, 85.0f, "OSHA_PEL TWA = 85.0 dB (4h@90dB)");
    ASSERT_APPROX(twa_osha_pel, twa_osha_pel_direct,
                  "wrapper == direct (OSHA_PEL)");

    // Sanity check: 10·log10(0.5) + 90 = 86.99 dB (WRONG answer for OSHA)
    // If this assertion fails, the coefficient regression has been broken.
    float naive_twa = 10.0f * std::log10(0.5f) + 90.0f;  // WRONG formula
    std::cout << "[naive 10·log10 TWA = " << naive_twa
              << " dB (should NOT be used for OSHA)] ";

    // Make sure we're using the CORRECT formula (16.61), not the naive 10.0
    if (approx_equal(twa_osha_pel, naive_twa)) {
        std::cerr << "\n  REGRESSION: wrapper returned 10·log10 result, "
                  << "but OSHA must use 16.61·log10!\n";
        return 1;
    }

    // Also test OSHA_HCA: 4h@90dB → HCA dose_pct = 100%, TWA = 85.0 dB
    DoseState state_hca = {};
    state_hca.cumulative_dose_frac = 1.0f;  // 100%
    state_hca.elapsed_hours = 4.0f;
    float twa_osha_hca = dose_to_twa(state_hca, DoseStandard::OSHA_HCA);
    ASSERT_APPROX(twa_osha_hca, 85.0f, "OSHA_HCA TWA = 85.0 dB (4h@90dB, dose=100%)");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 7: dose_to_lex8h — basic
//          LEX,8h is the constant SPL equivalent to 8h exposure with same dose
//==============================================================================
int test_lex8h_basic() {
    std::cout << "Test 7: dose_to_lex8h basic... ";

    // 100% dose (cumulative_dose_frac=1.0) → LEX = L_c
    DoseState state = {};
    state.cumulative_dose_frac = 1.0f;  // 100%
    state.elapsed_hours = 4.0f;  // any value (LEX doesn't depend on elapsed)

    ASSERT_APPROX(dose_to_lex8h(state, DoseStandard::NIOSH), 85.0f,
                  "NIOSH 100% dose → LEX=85.0 (L_c)");
    ASSERT_APPROX(dose_to_lex8h(state, DoseStandard::OSHA_PEL), 90.0f,
                  "OSHA_PEL 100% dose → LEX=90.0 (L_c)");
    ASSERT_APPROX(dose_to_lex8h(state, DoseStandard::OSHA_HCA), 85.0f,
                  "OSHA_HCA 100% dose → LEX=85.0 (L_c)");
    ASSERT_APPROX(dose_to_lex8h(state, DoseStandard::EU_ISO), 85.0f,
                  "EU_ISO 100% dose → LEX=85.0 (L_c)");

    // 4h@90dB NIOSH: dose_pct=158.74, LEX = 10·log10(1.5874) + 85 = 87.01
    DoseState state_4h = {};
    state_4h.cumulative_dose_frac = 1.5874f;
    state_4h.elapsed_hours = 4.0f;
    ASSERT_APPROX(dose_to_lex8h(state_4h, DoseStandard::NIOSH), 87.01f,
                  "NIOSH LEX(4h@90dB) = 87.01 dB");

    // Empty state: LEX = 0
    DoseState empty = {};
    ASSERT_APPROX(dose_to_lex8h(empty, DoseStandard::NIOSH), 0.0f,
                  "empty state → LEX=0");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 8: dose_to_lex8h — 4-standard consistency with DoseCalculator
//==============================================================================
int test_lex8h_consistency() {
    std::cout << "Test 8: dose_to_lex8h 4-standard consistency... ";

    // Random-ish but reproducible dose_frac values
    const float test_fracs[] = {0.1f, 0.5f, 1.0f, 1.5874f, 2.5f};
    const DoseStandard standards[] = {
        DoseStandard::NIOSH,
        DoseStandard::OSHA_PEL,
        DoseStandard::OSHA_HCA,
        DoseStandard::EU_ISO
    };
    const int n_fracs = sizeof(test_fracs) / sizeof(test_fracs[0]);
    const int n_standards = sizeof(standards) / sizeof(standards[0]);

    for (int i = 0; i < n_fracs; ++i) {
        DoseState state = {};
        state.cumulative_dose_frac = test_fracs[i];
        state.elapsed_hours = 4.0f;  // any non-zero value

        for (int s = 0; s < n_standards; ++s) {
            float lex_wrapper = dose_to_lex8h(state, standards[s]);
            float lex_direct = DoseCalculator::calculate_lex(
                state.cumulative_dose_frac * 100.0f,
                DoseCalculator::get_profile(standards[s]));

            ASSERT_APPROX(lex_wrapper, lex_direct,
                "LEX consistency: frac=" + std::to_string(test_fracs[i]) +
                " std=" + std::to_string(static_cast<int>(standards[s])));
        }
    }

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Test 9 (extra): numeric chain — 4h@90dB across all 4 standards
//          Verifies the whole pipeline end-to-end
//==============================================================================
int test_numeric_chain_4h_90db() {
    std::cout << "Test 9: numeric chain 4h@90dB across 4 standards... ";

    // Simulate "4 hours at 90 dB" with 4 separate DoseState variables
    // Each gets the segment dose_frac from DoseCalculator::calculate_dose_increment

    DoseState niosh_state = {};
    DoseState osha_pel_state = {};
    DoseState osha_hca_state = {};
    DoseState eu_iso_state = {};

    // Simulate 14400 segments of 1 second each (4h total)
    const int n_segments = 14400;
    const float dt_s = 1.0f;
    const float laeq = 90.0f;

    const auto& prof_n = DoseCalculator::get_profile(DoseStandard::NIOSH);
    const auto& prof_p = DoseCalculator::get_profile(DoseStandard::OSHA_PEL);
    const auto& prof_h = DoseCalculator::get_profile(DoseStandard::OSHA_HCA);
    const auto& prof_e = DoseCalculator::get_profile(DoseStandard::EU_ISO);

    float frac_n = DoseCalculator::calculate_dose_increment(laeq, dt_s, prof_n) / 100.0f;
    float frac_p = DoseCalculator::calculate_dose_increment(laeq, dt_s, prof_p) / 100.0f;
    float frac_h = DoseCalculator::calculate_dose_increment(laeq, dt_s, prof_h) / 100.0f;
    float frac_e = DoseCalculator::calculate_dose_increment(laeq, dt_s, prof_e) / 100.0f;

    for (int i = 0; i < n_segments; ++i) {
        niosh_state = accumulate_dose_frac(niosh_state, frac_n, dt_s);
        osha_pel_state = accumulate_dose_frac(osha_pel_state, frac_p, dt_s);
        osha_hca_state = accumulate_dose_frac(osha_hca_state, frac_h, dt_s);
        eu_iso_state = accumulate_dose_frac(eu_iso_state, frac_e, dt_s);
    }

    // Verify Dose%
    ASSERT_APPROX(dose_to_pct(niosh_state), 158.74f, "NIOSH Dose%(4h@90dB) = 158.74%");
    ASSERT_APPROX(dose_to_pct(osha_pel_state), 50.0f, "OSHA_PEL Dose%(4h@90dB) = 50%");
    ASSERT_APPROX(dose_to_pct(osha_hca_state), 100.0f, "OSHA_HCA Dose%(4h@90dB) = 100%");
    ASSERT_APPROX(dose_to_pct(eu_iso_state), 158.74f, "EU_ISO Dose%(4h@90dB) = 158.74%");

    // Verify TWA
    ASSERT_APPROX(dose_to_twa(niosh_state, DoseStandard::NIOSH), 87.0f, "NIOSH TWA(4h@90dB) = 87.0 dB");
    ASSERT_APPROX(dose_to_twa(osha_pel_state, DoseStandard::OSHA_PEL), 85.0f, "OSHA_PEL TWA(4h@90dB) = 85.0 dB");
    ASSERT_APPROX(dose_to_twa(osha_hca_state, DoseStandard::OSHA_HCA), 85.0f, "OSHA_HCA TWA(4h@90dB) = 85.0 dB");
    ASSERT_APPROX(dose_to_twa(eu_iso_state, DoseStandard::EU_ISO), 87.0f, "EU_ISO TWA(4h@90dB) = 87.0 dB");

    // Verify LEX,8h
    ASSERT_APPROX(dose_to_lex8h(niosh_state, DoseStandard::NIOSH), 87.0f, "NIOSH LEX(4h@90dB) = 87.0 dB");
    ASSERT_APPROX(dose_to_lex8h(osha_pel_state, DoseStandard::OSHA_PEL), 86.99f, "OSHA_PEL LEX(4h@90dB) = 86.99 dB (10·log10(0.5)+90)");
    ASSERT_APPROX(dose_to_lex8h(osha_hca_state, DoseStandard::OSHA_HCA), 85.0f, "OSHA_HCA LEX(4h@90dB) = 85.0 dB");
    ASSERT_APPROX(dose_to_lex8h(eu_iso_state, DoseStandard::EU_ISO), 87.0f, "EU_ISO LEX(4h@90dB) = 87.0 dB");

    // Verify elapsed_hours
    ASSERT_APPROX(niosh_state.elapsed_hours, 4.0f, "NIOSH elapsed = 4.0 h");
    ASSERT_APPROX(osha_pel_state.elapsed_hours, 4.0f, "OSHA_PEL elapsed = 4.0 h");

    std::cout << "PASSED\n";
    return 0;
}

//==============================================================================
// Main
//==============================================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "  DoseState Unit Tests (v3.1.3)\n";
    std::cout << "========================================\n\n";

    int failures = 0;
    failures += test_accumulate_basic();
    failures += test_accumulate_zero_duration();
    failures += test_dose_to_pct_basic();
    failures += test_twa_boundary_empty();
    failures += test_twa_3db_niosh_consistency();
    failures += test_twa_5db_osha_regression();
    failures += test_lex8h_basic();
    failures += test_lex8h_consistency();
    failures += test_numeric_chain_4h_90db();

    std::cout << "\n========================================\n";
    if (failures == 0) {
        std::cout << "  ALL " << 9 << " TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } else {
        std::cout << "  " << failures << " TEST(S) FAILED\n";
        std::cout << "========================================\n";
        return 1;
    }
}
