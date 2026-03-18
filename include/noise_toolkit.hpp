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

namespace noise_toolkit {

// Constants
constexpr double REFERENCE_PRESSURE = 20e-6;  // 20 μPa
constexpr double OVERLOAD_THRESHOLD = 140.0;   // dB
constexpr double UNDERRANGE_THRESHOLD = 30.0;  // dB

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
double db_to_pressure(double db, double p0 = REFERENCE_PRESSURE);
double pressure_to_db(double pressure, double p0 = REFERENCE_PRESSURE);
double calculate_rms(const std::vector<double>& samples);
double calculate_peak(const std::vector<double>& samples);

// A-weighting filter coefficients
extern const std::vector<double> A_WEIGHTING_FREQUENCIES;
extern const std::vector<double> A_WEIGHTING_GAINS;

// C-weighting filter coefficients
extern const std::vector<double> C_WEIGHTING_FREQUENCIES;
extern const std::vector<double> C_WEIGHTING_GAINS;

} // namespace noise_toolkit
