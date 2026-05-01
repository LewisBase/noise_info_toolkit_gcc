/**
 * @file noise_toolkit.hpp
 * @brief Main header for noise_info_toolkit C++ library
 * @author Monte Carlo (based on Python implementation by Liu Hengjiang)
 * @date 2026-03-14
 * 
 * C++ implementation of noise signal processing toolkit
 * Ported from Python noise_info_toolkit project
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include <cstdint>

#include "noise_metrics.hpp"

namespace noise_toolkit {

// Constants are defined in noise_metrics.hpp

// Forward declarations
class AudioProcessor;
class DoseCalculator;
class EventDetector;
class EventProcessor;
class TDMSConverter;
class TimeHistoryProcessor;
class RingBuffer;
class Database;

// Utility functions
float db_to_pressure(float db, float p0 = REFERENCE_PRESSURE);
float pressure_to_db(float pressure, float p0 = REFERENCE_PRESSURE);
float calculate_rms(const std::vector<float>& samples);
float calculate_peak(const std::vector<float>& samples);

// A-weighting filter coefficients
extern const std::vector<float> A_WEIGHTING_FREQUENCIES;
extern const std::vector<float> A_WEIGHTING_GAINS;

// C-weighting filter coefficients
extern const std::vector<float> C_WEIGHTING_FREQUENCIES;
extern const std::vector<float> C_WEIGHTING_GAINS;

} // namespace noise_toolkit
