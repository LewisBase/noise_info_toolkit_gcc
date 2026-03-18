/**
 * @file dose_calculator.hpp
 * @brief Noise dose calculator supporting multiple standards (NIOSH, OSHA, EU_ISO)
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <limits>

namespace noise_toolkit {

/**
 * @brief Dose calculation standard enumeration
 */
enum class DoseStandard {
    NIOSH,      // NIOSH standard: 85dBA / 3dB / 8h
    OSHA_PEL,   // OSHA Permissible Exposure Limit: 90dBA / 5dB / 8h
    OSHA_HCA,   // OSHA Hearing Conservation Amendment: 85dBA / 5dB / 8h
    EU_ISO      // EU/ISO standard: 85dBA / 3dB / 8h
};

/**
 * @brief Dose profile configuration structure
 */
struct DoseProfile {
    std::string name;
    double criterion_level;     // Lc (dBA)
    double exchange_rate;       // ER (dB)
    double threshold;           // LT (dBA)
    double reference_duration;  // Tref (hours)
    std::string description;
    
    // 默认构造函数
    DoseProfile() : name(""), criterion_level(0), exchange_rate(0), 
                    threshold(0), reference_duration(0), description("") {}
    
    DoseProfile(const std::string& n, double cl, double er, 
                double th = 0.0, double rd = 8.0,
                const std::string& desc = "")
        : name(n), criterion_level(cl), exchange_rate(er), 
          threshold(th), reference_duration(rd), description(desc) {
        if (description.empty()) {
            description = name + ": " + std::to_string((int)criterion_level) + 
                         "dBA/" + std::to_string((int)exchange_rate) + 
                         "dB/" + std::to_string((int)reference_duration) + "h";
        }
    }
};

/**
 * @brief Dose calculation results
 */
struct DoseMetrics {
    double dose_pct;           // Dose percentage
    double dose_fraction;      // Dose as fraction (0-1)
    double twa;                // Time Weighted Average (dBA)
    double lex_8h;             // Daily noise exposure level (dBA)
    double allowed_time_h;     // Allowed exposure time (hours)
};

/**
 * @brief Noise dose calculator class
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
     * @brief Initialize with predefined profiles
     */
    DoseCalculator();
    
    /**
     * @brief Get profile by standard name
     */
    DoseProfile get_profile(const std::string& standard) const;
    
    /**
     * @brief Get profile by enum
     */
    DoseProfile get_profile(DoseStandard standard) const;
    
    /**
     * @brief Calculate allowed exposure time
     * Formula: T = Tref / 2^((L - Lc) / ER)
     */
    double calculate_allowed_time(double laeq, const DoseProfile& profile) const;
    
    /**
     * @brief Calculate dose increment for a time period
     * Formula: Dose% = 100 × (dt/Tref) × 2^((L - Lc) / ER)
     */
    double calculate_dose_increment(double laeq, double duration_s, 
                                    const DoseProfile& profile) const;
    
    /**
     * @brief Calculate total dose from multiple measurements
     */
    double calculate_total_dose(const std::vector<std::pair<double, double>>& measurements,
                                const DoseProfile& profile) const;
    
    /**
     * @brief Calculate TWA from total dose
     * NIOSH/ISO: TWA = 10 × log10(Dose%/100) + Lc
     * OSHA: TWA = 16.61 × log10(Dose%/100) + Lc
     */
    double calculate_twa(double total_dose_pct, const DoseProfile& profile) const;
    
    /**
     * @brief Calculate LEX,8h from total dose
     * Formula: LEX,8h = 10 × log10(Dose%/100) + Lc
     */
    double calculate_lex(double total_dose_pct, const DoseProfile& profile) const;
    
    /**
     * @brief Calculate dose from LEX,8h (reverse)
     * Formula: Dose% = 100 × 10^((LEX,8h - Lc) / 10)
     */
    double calculate_dose_from_lex(double lex, const DoseProfile& profile) const;
    
    /**
     * @brief Calculate all metrics for a given LAeq and duration
     */
    DoseMetrics calculate_all_metrics(double laeq, double duration_s, 
                                      const DoseProfile& profile) const;
    
    /**
     * @brief Calculate metrics for all standards
     */
    std::map<std::string, DoseMetrics> calculate_multi_standard(double laeq, 
                                                                 double duration_s) const;
    
    /**
     * @brief Get all available profiles
     */
    std::map<std::string, DoseProfile> get_all_profiles() const;

private:
    std::map<std::string, DoseProfile> profiles_;
};

// Convenience functions
double calculate_noise_dose(double laeq, double duration_h, 
                           const std::string& standard = "NIOSH");

double calculate_twa_from_dose(double total_dose_pct, 
                              const std::string& standard = "NIOSH");

} // namespace noise_toolkit
