/**
 * @file dose_calculator.cpp
 * @brief Implementation of noise dose calculator
 */

#include "dose_calculator.hpp"
#include <cmath>
#include <stdexcept>

namespace noise_toolkit {

DoseCalculator::DoseCalculator() {
    // Initialize predefined profiles
    profiles_["NIOSH"] = DoseProfile(
        "NIOSH", 85.0, 3.0, 0.0, 8.0,
        "NIOSH标准: 85dBA准则级, 3dB交换率, 8小时参考时长"
    );
    
    profiles_["OSHA_PEL"] = DoseProfile(
        "OSHA_PEL", 90.0, 5.0, 0.0, 8.0,
        "OSHA_PEL标准: 90dBA准则级, 5dB交换率, 8小时参考时长"
    );
    
    profiles_["OSHA_HCA"] = DoseProfile(
        "OSHA_HCA", 85.0, 5.0, 0.0, 8.0,
        "OSHA_HCA标准: 85dBA准则级, 5dB交换率, 8小时参考时长"
    );
    
    profiles_["EU_ISO"] = DoseProfile(
        "EU_ISO", 85.0, 3.0, 0.0, 8.0,
        "EU_ISO标准: 85dBA准则级, 3dB交换率, 8小时参考时长"
    );
}

DoseProfile DoseCalculator::get_profile(const std::string& standard) const {
    auto it = profiles_.find(standard);
    if (it == profiles_.end()) {
        std::string available;
        for (const auto& p : profiles_) {
            if (!available.empty()) available += ", ";
            available += p.first;
        }
        throw std::invalid_argument("Unknown standard: " + standard + 
                                   ". Available: " + available);
    }
    return it->second;
}

DoseProfile DoseCalculator::get_profile(DoseStandard standard) const {
    switch (standard) {
        case DoseStandard::NIOSH: return get_profile("NIOSH");
        case DoseStandard::OSHA_PEL: return get_profile("OSHA_PEL");
        case DoseStandard::OSHA_HCA: return get_profile("OSHA_HCA");
        case DoseStandard::EU_ISO: return get_profile("EU_ISO");
    }
    return get_profile("NIOSH");  // default
}

double DoseCalculator::calculate_allowed_time(double laeq, 
                                               const DoseProfile& profile) const {
    if (laeq < profile.threshold) {
        return std::numeric_limits<double>::infinity();
    }
    
    // T = Tref / 2^((L - Lc) / ER)
    double exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
    double allowed_time = profile.reference_duration / std::pow(2.0, exponent);
    return allowed_time;
}

double DoseCalculator::calculate_dose_increment(double laeq, double duration_s,
                                                 const DoseProfile& profile) const {
    if (laeq < profile.threshold) {
        return 0.0;
    }
    
    double duration_h = duration_s / 3600.0;
    
    // Dose% = 100 × (dt/Tref) × 2^((L-Lc)/ER)
    double exponent = (laeq - profile.criterion_level) / profile.exchange_rate;
    double dose_increment = 100.0 * (duration_h / profile.reference_duration) * 
                           std::pow(2.0, exponent);
    
    return dose_increment;
}

double DoseCalculator::calculate_total_dose(
    const std::vector<std::pair<double, double>>& measurements,
    const DoseProfile& profile) const {
    
    double total_dose = 0.0;
    for (const auto& m : measurements) {
        total_dose += calculate_dose_increment(m.first, m.second, profile);
    }
    return total_dose;
}

double DoseCalculator::calculate_twa(double total_dose_pct, 
                                      const DoseProfile& profile) const {
    if (total_dose_pct <= 0.0) {
        return 0.0;
    }
    
    double twa;
    if (profile.name.find("OSHA") == 0) {
        // OSHA uses special coefficient 16.61
        twa = 16.61 * std::log10(total_dose_pct / 100.0) + profile.criterion_level;
    } else {
        // NIOSH and ISO use coefficient 10
        twa = 10.0 * std::log10(total_dose_pct / 100.0) + profile.criterion_level;
    }
    
    return twa;
}

double DoseCalculator::calculate_lex(double total_dose_pct, 
                                      const DoseProfile& profile) const {
    if (total_dose_pct <= 0.0) {
        return 0.0;
    }
    
    double lex = 10.0 * std::log10(total_dose_pct / 100.0) + profile.criterion_level;
    return lex;
}

double DoseCalculator::calculate_dose_from_lex(double lex, 
                                                const DoseProfile& profile) const {
    double dose = 100.0 * std::pow(10.0, (lex - profile.criterion_level) / 10.0);
    return dose;
}

DoseMetrics DoseCalculator::calculate_all_metrics(double laeq, double duration_s,
                                                   const DoseProfile& profile) const {
    DoseMetrics metrics;
    metrics.dose_pct = calculate_dose_increment(laeq, duration_s, profile);
    metrics.dose_fraction = metrics.dose_pct / 100.0;
    metrics.allowed_time_h = calculate_allowed_time(laeq, profile);
    
    // For single frame, TWA and LEX equal input level only if duration >= reference
    if (duration_s >= profile.reference_duration * 3600.0) {
        metrics.twa = laeq;
        metrics.lex_8h = laeq;
    } else {
        metrics.twa = 0.0;  // Will be calculated from accumulated dose
        metrics.lex_8h = 0.0;
    }
    
    return metrics;
}

std::map<std::string, DoseMetrics> DoseCalculator::calculate_multi_standard(
    double laeq, double duration_s) const {
    
    std::map<std::string, DoseMetrics> results;
    for (const auto& p : profiles_) {
        results[p.first] = calculate_all_metrics(laeq, duration_s, p.second);
    }
    return results;
}

std::map<std::string, DoseProfile> DoseCalculator::get_all_profiles() const {
    return profiles_;
}

// Convenience functions
double calculate_noise_dose(double laeq, double duration_h, 
                           const std::string& standard) {
    DoseCalculator calc;
    DoseProfile profile = calc.get_profile(standard);
    double duration_s = duration_h * 3600.0;
    return calc.calculate_dose_increment(laeq, duration_s, profile);
}

double calculate_twa_from_dose(double total_dose_pct, const std::string& standard) {
    DoseCalculator calc;
    DoseProfile profile = calc.get_profile(standard);
    return calc.calculate_twa(total_dose_pct, profile);
}

} // namespace noise_toolkit
