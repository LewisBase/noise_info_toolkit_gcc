/**
 * @file noise_toolkit.hpp
 * @brief Main header for noise_info_toolkit C++ library
 *
 * Includes core headers and declares utility functions.
 */

#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

#include "noise_metrics.hpp"

namespace noise_toolkit {

// Utility functions
float db_to_pressure(float db, float p0 = REFERENCE_PRESSURE);
float pressure_to_db(float pressure, float p0 = REFERENCE_PRESSURE);
float calculate_rms(const std::vector<float>& samples);
float calculate_peak(const std::vector<float>& samples);

} // namespace noise_toolkit
