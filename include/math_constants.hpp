/**
 * @file math_constants.hpp
 * @brief Mathematical constants for cross-platform compatibility
 *
 * M_PI is not guaranteed by C++ standard and may be missing on
 * embedded toolchains (Zephyr/picolibc/arm-zephyr-eabi).
 * These constants provide a portable alternative.
 */

#pragma once

namespace noise_toolkit {
namespace noise_const {

inline constexpr float PI_F = 3.14159265358979323846f;
inline constexpr float TWO_PI_F = 2.0f * PI_F;

} // namespace noise_const
} // namespace noise_toolkit
