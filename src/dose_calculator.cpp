/**
 * @file dose_calculator.cpp
 * @brief Implementation of noise dose calculator (PC-only string API)
 */

#include "dose_calculator.hpp"

#ifndef NOISE_EMBEDDED_BUILD

#include <cstring>

namespace noise_toolkit {

DoseStandard DoseCalculator::parse_standard(const char* name) {
    if (std::strcmp(name, "NIOSH") == 0) return DoseStandard::NIOSH;
    if (std::strcmp(name, "OSHA_PEL") == 0) return DoseStandard::OSHA_PEL;
    if (std::strcmp(name, "OSHA_HCA") == 0) return DoseStandard::OSHA_HCA;
    if (std::strcmp(name, "EU_ISO") == 0) return DoseStandard::EU_ISO;
    return DoseStandard::NIOSH;  // default fallback
}

float calculate_noise_dose(float laeq, float duration_h, const char* standard) {
    auto std = DoseCalculator::parse_standard(standard);
    const auto& profile = DoseCalculator::get_profile(std);
    float duration_s = duration_h * 3600.0f;
    return DoseCalculator::calculate_dose_increment(laeq, duration_s, profile);
}

float calculate_twa_from_dose(float total_dose_pct, const char* standard) {
    auto std = DoseCalculator::parse_standard(standard);
    return DoseCalculator::calculate_twa(total_dose_pct, std);
}

} // namespace noise_toolkit

#endif // NOISE_EMBEDDED_BUILD
