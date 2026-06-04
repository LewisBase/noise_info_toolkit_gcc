/**
 * @file dose_state.hpp
 * @brief Cumulative dose state + thin-wrapper API for Dose%/TWA/LEX,8h
 *
 * v3.1.3 — Business-side accumulator for embedded firmware.
 *
 * Design:
 *   - Algorithm library REMAINS stateless (no cross-call state held)
 *   - Business side (embedded firmware) holds a `DoseState` (8 bytes POD)
 *   - 4 inline functions act as thin wrappers around existing `DoseCalculator`
 *
 * Usage:
 *   DoseState state = {};                          // zero-init
 *   state = accumulate_dose_frac(state, frac, dt);  // update per segment
 *   float pct = dose_to_pct(state);                // read Dose% (any time)
 *   float twa = dose_to_twa(state, DoseStandard::NIOSH);  // read TWA
 *
 * Backward compatibility: This header only ADDS new identifiers to the
 * `noise_toolkit` namespace. v3.1.2 interfaces (process_segment, aggregate_metrics,
 * SecondMetrics, MinuteMetrics, EventDetector, DoseCalculator) are unchanged.
 */

#pragma once

#include "dose_calculator.hpp"
#include <cstdint>

namespace noise_toolkit {

//==============================================================================
// Dose State (business-side accumulator, 8 bytes POD)
//==============================================================================

/**
 * @brief Cumulative dose state held by the business side (embedded firmware).
 *
 * One `DoseState` instance is used per exposure standard. For example, to track
 * 4 standards in parallel, the firmware holds 4 separate `DoseState` variables.
 *
 * Memory: 8 bytes, trivially copyable, no constructor side effects.
 *
 * Field meanings:
 *   - cumulative_dose_frac: Σ D_i (dimensionless). Can exceed 1.0 if Dose% > 100%.
 *                           E.g., NIOSH 4h@90dB → 1.5874 (158.74% dose).
 *   - elapsed_hours: Σ t_i in hours. Monotonically increasing during a measurement.
 *                    Used by `dose_to_twa` for time normalization.
 *
 * Lifecycle:
 *   DoseState state = {};                              // zero-init
 *   while (recording) {
 *       SecondMetrics m = processor.process_segment(...);
 *       state = accumulate_dose_frac(state, m.dose_frac_niosh, dt);
 *       if (report_due) {
 *           log("NIOSH Dose%%=%.1f%% TWA=%.1fdB",
 *               dose_to_pct(state),
 *               dose_to_twa(state, DoseStandard::NIOSH));
 *       }
 *   }
 */
struct DoseState {
    float cumulative_dose_frac;  ///< Σ D_i (dimensionless, can exceed 1.0)
    float elapsed_hours;         ///< Σ t_i in hours (monotonically increasing)
};

//==============================================================================
// Pure-Function API (thin wrappers around DoseCalculator)
//==============================================================================

/**
 * @brief Update `DoseState` with a new segment's dose fraction.
 *
 * Pure function: returns a new `DoseState`, does not modify the input.
 * The caller assigns the result back to its state variable:
 *   `state = accumulate_dose_frac(state, frac, dt);`
 *
 * @param state Current cumulative state (input, not modified)
 * @param dose_frac Segment dose fraction (e.g., `m.dose_frac_niosh`)
 * @param duration_s Segment duration in seconds (must be > 0 to take effect)
 * @return Updated `DoseState` with the new segment included
 *
 * @note If `duration_s <= 0`, returns `state` unchanged (no-op, no state change).
 *       This handles the case where the caller passes an invalid duration.
 */
inline DoseState accumulate_dose_frac(const DoseState& state,
                                       float dose_frac,
                                       float duration_s) noexcept {
    if (duration_s <= 0.0f) return state;
    DoseState out;
    out.cumulative_dose_frac = state.cumulative_dose_frac + dose_frac;
    out.elapsed_hours = state.elapsed_hours + (duration_s / 3600.0f);
    return out;
}

/**
 * @brief Convert cumulative `DoseState` to Dose percentage.
 *
 * Pure linear scaling: Dose% = 100 × cumulative_dose_frac.
 *
 * @param state Current cumulative state
 * @return Dose percentage. 0% = no exposure; 100% = standard met; >100% = exceeded.
 *
 * @note Does NOT depend on `elapsed_hours`. The standard information is
 *       implicit in which `DoseState` is passed (the caller chose the
 *       standard when calling `accumulate_dose_frac`).
 */
inline float dose_to_pct(const DoseState& state) noexcept {
    return state.cumulative_dose_frac * 100.0f;
}

/**
 * @brief Convert cumulative `DoseState` to Time-Weighted Average (TWA).
 *
 * Thin wrapper around `DoseCalculator::calculate_twa`.
 * The 5dB / 3dB log10 coefficient is selected automatically based on
 * `DoseStandard` (see DoseCalculator::calculate_twa for the formula).
 *
 * @param state Current cumulative state
 * @param standard Noise exposure standard (NIOSH / OSHA_PEL / OSHA_HCA / EU_ISO)
 * @return TWA in dB, or 0.0f if no exposure / zero elapsed time
 *
 * @note IMPORTANT — 5dB exchange rate coefficient (OSHA only):
 *       - 3 dB ER (NIOSH/ISO): coefficient = 10.0
 *       - 5 dB ER (OSHA): coefficient = 16.61 (= 5 / log10(2))
 *       Do NOT manually compute with 10·log10 for OSHA data — the TWA will
 *       be off by more than 5 dB. This function handles the coefficient
 *       selection automatically.
 */
inline float dose_to_twa(const DoseState& state,
                          DoseStandard standard) noexcept {
    // Boundary conditions: no measurement yet, or no dose accumulated
    if (state.elapsed_hours <= 0.0f) return 0.0f;
    if (state.cumulative_dose_frac <= 0.0f) return 0.0f;

    // Thin wrapper: convert dose_frac → dose_pct, then call DoseCalculator
    return DoseCalculator::calculate_twa(
        state.cumulative_dose_frac * 100.0f,  // dose_pct = 100 × dose_frac
        standard);
}

/**
 * @brief Convert cumulative `DoseState` to Daily Noise Exposure Level (LEX,8h).
 *
 * Thin wrapper around `DoseCalculator::calculate_lex`.
 * LEX,8h is the constant SPL that, if sustained for 8 hours, would produce
 * the same dose as the current cumulative exposure.
 *
 * @param state Current cumulative state
 * @param standard Noise exposure standard
 * @return LEX,8h in dB, or 0.0f if no dose accumulated
 *
 * @note Does NOT depend on `elapsed_hours` — LEX is defined as normalization
 *       to 8 hours by construction. For TWA (which DOES depend on
 *       `elapsed_hours` for time normalization), use `dose_to_twa` instead.
 */
inline float dose_to_lex8h(const DoseState& state,
                            DoseStandard standard) noexcept {
    if (state.cumulative_dose_frac <= 0.0f) return 0.0f;

    return DoseCalculator::calculate_lex(
        state.cumulative_dose_frac * 100.0f,
        DoseCalculator::get_profile(standard));
}

} // namespace noise_toolkit
