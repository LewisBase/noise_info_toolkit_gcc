/**
 * @file dose_calculator.hpp
 * @brief Noise dose calculator supporting multiple standards (NIOSH, OSHA, EU_ISO)
 *
 * Embedded-optimized: no dynamic allocation, constexpr profile table, enum-based lookup.
 * All calculations use single-precision float.
 * String-based API available only in PC builds (#ifndef NOISE_EMBEDDED_BUILD).
 */

#pragma once

#include <array>
#include <cstdint>
#include <cmath>
#include <limits>

namespace noise_toolkit {

/**
 * @brief Dose calculation standard enumeration
 */
enum class DoseStandard : uint8_t {
    NIOSH = 0,      // NIOSH standard: 85dBA / 3dB / 8h
    OSHA_PEL = 1,   // OSHA Permissible Exposure Limit: 90dBA / 5dB / 8h
    OSHA_HCA = 2,   // OSHA Hearing Conservation Amendment: 85dBA / 5dB / 8h
    EU_ISO = 3,     // EU/ISO standard: 85dBA / 3dB / 8h
    COUNT = 4
};

/**
 * @brief Dose profile configuration structure (POD, no heap allocation)
 */
struct DoseProfile {
    float criterion_level;     // Lc (dBA)
    float exchange_rate;       // ER (dB)
    float threshold;           // LT (dBA)
    float reference_duration;  // Tref (hours)
};

/**
 * @brief Compile-time dose profile table indexed by DoseStandard
 */
constexpr std::array<DoseProfile, static_cast<size_t>(DoseStandard::COUNT)> DOSE_PROFILES = {{
    {85.0f, 3.0f, 0.0f, 8.0f},   // NIOSH
    {90.0f, 5.0f, 0.0f, 8.0f},   // OSHA_PEL
    {85.0f, 5.0f, 0.0f, 8.0f},   // OSHA_HCA
    {85.0f, 3.0f, 0.0f, 8.0f}    // EU_ISO
}};

/**
 * @brief Dose calculation results (all float)
 */
struct DoseMetrics {
    float dose_pct;           // Dose percentage
    float dose_fraction;      // Dose as fraction (0-1)
    float twa;                // Time Weighted Average (dBA)
    float lex_8h;             // Daily noise exposure level (dBA)
    float allowed_time_h;     // Allowed exposure time (hours)
};

/**
 * @brief Noise dose calculator class (all static, no state)
 *
 * Supports multiple occupational noise exposure standards:
 * - NIOSH (85 dBA / 3 dB / 8h)
 * - OSHA_PEL (90 dBA / 5 dB / 8h)
 * - OSHA_HCA (85 dBA / 5 dB / 8h)
 * - EU_ISO (85 dBA / 3 dB / 8h)
 */
class DoseCalculator {
public:
    /**
     * @brief Get profile by enum (returns reference to constexpr table, zero cost)
     */
    static const DoseProfile& get_profile(DoseStandard standard) {
        return DOSE_PROFILES[static_cast<size_t>(standard)];
    }

    /**
     * @brief Calculate allowed exposure time
     * Formula: T = Tref / 2^((L - Lc) / ER)
     */
    static float calculate_allowed_time(float laeq, const DoseProfile& profile) {
        if (laeq < profile.threshold) {
            return std::numeric_limits<float>::infinity();
        }
        float exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
        return profile.reference_duration / std::pow(2.0f, exponent);
    }

    /**
     * @brief Calculate dose increment for a time period
     * Formula: Dose% = 100 × (dt/Tref) × 2^((L - Lc) / ER)
     */
    static float calculate_dose_increment(float laeq, float duration_s,
                                           const DoseProfile& profile) {
        if (laeq < profile.threshold) {
            return 0.0f;
        }
        float duration_h = duration_s / 3600.0f;
        float exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
        return 100.0f * (duration_h / profile.reference_duration) * std::pow(2.0f, exponent);
    }

    /**
     * @brief Calculate TWA from total dose (enum-based, no string lookup)
     * NIOSH/ISO: TWA = 10 × log10(Dose%/100) + Lc
     * OSHA: TWA = 16.61 × log10(Dose%/100) + Lc
     */
    static float calculate_twa(float total_dose_pct, DoseStandard standard) {
        if (total_dose_pct <= 0.0f) return 0.0f;
        const auto& profile = get_profile(standard);
        bool is_osha = (standard == DoseStandard::OSHA_PEL ||
                        standard == DoseStandard::OSHA_HCA);
        float coeff = is_osha ? 16.61f : 10.0f;
        return coeff * std::log10(total_dose_pct / 100.0f) + profile.criterion_level;
    }

    /**
     * @brief Calculate TWA from total dose (profile-based)
     * NIOSH/ISO: TWA = 10 × log10(Dose%/100) + Lc
     * OSHA: TWA = 16.61 × log10(Dose%/100) + Lc
     */
    static float calculate_twa(float total_dose_pct, const DoseProfile& profile,
                                DoseStandard standard) {
        if (total_dose_pct <= 0.0f) return 0.0f;
        bool is_osha = (standard == DoseStandard::OSHA_PEL ||
                        standard == DoseStandard::OSHA_HCA);
        float coeff = is_osha ? 16.61f : 10.0f;
        return coeff * std::log10(total_dose_pct / 100.0f) + profile.criterion_level;
    }

    /**
     * @brief Calculate LEX,8h from total dose
     * Formula: LEX,8h = 10 × log10(Dose%/100) + Lc
     */
    static float calculate_lex(float total_dose_pct, const DoseProfile& profile) {
        if (total_dose_pct <= 0.0f) return 0.0f;
        return 10.0f * std::log10(total_dose_pct / 100.0f) + profile.criterion_level;
    }

    /**
     * @brief Calculate dose from LEX,8h (reverse)
     * Formula: Dose% = 100 × 10^((LEX,8h - Lc) / 10)
     */
    static float calculate_dose_from_lex(float lex, const DoseProfile& profile) {
        return 100.0f * std::pow(10.0f, (lex - profile.criterion_level) / 10.0f);
    }

    /**
     * @brief Calculate all metrics for a given LAeq and duration
     */
    static DoseMetrics calculate_all_metrics(float laeq, float duration_s,
                                              const DoseProfile& profile,
                                              DoseStandard standard) {
        DoseMetrics metrics;
        metrics.dose_pct = calculate_dose_increment(laeq, duration_s, profile);
        metrics.dose_fraction = metrics.dose_pct / 100.0f;
        metrics.allowed_time_h = calculate_allowed_time(laeq, profile);

        if (duration_s >= profile.reference_duration * 3600.0f) {
            metrics.twa = laeq;
            metrics.lex_8h = laeq;
        } else {
            metrics.twa = calculate_twa(metrics.dose_pct, profile, standard);
            metrics.lex_8h = calculate_lex(metrics.dose_pct, profile);
        }
        return metrics;
    }

    // ==================== PC-only string-based API ====================
#ifndef NOISE_EMBEDDED_BUILD
    /**
     * @brief Parse standard name string to enum (config-time only)
     */
    static DoseStandard parse_standard(const char* name);

    /**
     * @brief Get profile by standard name (PC convenience)
     */
    static DoseProfile get_profile(const char* standard) {
        return get_profile(parse_standard(standard));
    }

    /**
     * @brief Calculate TWA from total dose (string-based, PC convenience)
     */
    static float calculate_twa(float total_dose_pct, const char* standard) {
        return calculate_twa(total_dose_pct, parse_standard(standard));
    }
#endif
};

#ifndef NOISE_EMBEDDED_BUILD
// Convenience functions (PC only)
float calculate_noise_dose(float laeq, float duration_h,
                           const char* standard = "NIOSH");

float calculate_twa_from_dose(float total_dose_pct,
                               const char* standard = "NIOSH");
#endif

} // namespace noise_toolkit
