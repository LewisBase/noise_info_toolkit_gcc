/**
 * @file noise_metrics.hpp
 * @brief Core metrics data structures for noise information toolkit
 *
 * Defines all 81 per-second metrics and per-minute aggregated metrics.
 * Based on Python noise_info_toolkit TimeHistory model.
 *
 * This header is standalone and does NOT include noise_toolkit.hpp
 * (which contains removed features).
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <array>

namespace noise_toolkit {

//==============================================================================
// Constants
//==============================================================================

constexpr float REFERENCE_PRESSURE = 20e-6f;      // 20 μPa
constexpr float OVERLOAD_THRESHOLD = 140.0f;       // dB
constexpr float UNDERRANGE_THRESHOLD = 30.0f;     // dB
constexpr int THIRD_OCTAVE_BAND_COUNT = 9;         // 63Hz to 16kHz

//==============================================================================
// Frequency Band Definitions
//==============================================================================

constexpr std::array<float, THIRD_OCTAVE_BAND_COUNT> THIRD_OCTAVE_BANDS = {
    63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

constexpr std::array<const char*, THIRD_OCTAVE_BAND_COUNT> THIRD_OCTAVE_BAND_NAMES = {
    "63Hz", "125Hz", "250Hz", "500Hz", "1kHz", "2kHz", "4kHz", "8kHz", "16kHz"
};

//==============================================================================
// Raw Moment Statistics (for kurtosis aggregation, per spec 4.X.3)
//==============================================================================

/**
 * @brief Raw moment statistics for one frequency band
 * Used for precise kurtosis synthesis across time periods
 */
struct FreqBandMoments {
    int32_t n{0};        // Sample count
    float s1{0.0f};      // Σx_k
    float s2{0.0f};      // Σx_k²
    float s3{0.0f};      // Σx_k³
    float s4{0.0f};      // Σx_k⁴

    FreqBandMoments() = default;
    FreqBandMoments(int32_t n_, float s1_, float s2_, float s3_, float s4_)
        : n(n_), s1(s1_), s2(s2_), s3(s3_), s4(s4_) {}
};

//==============================================================================
// Per-Second Metrics (79 fields, 308 bytes) — v3.3.0 LAPeak added
//==============================================================================

/**
 * @brief Single second metrics - 79 fields, 308 bytes (v3.3.0: LAPeak added)
 *
 * Field count breakdown:
 *   - 2: timestamp, duration_s
 *   - 7: LAeq, LCeq, LZeq, LAFmax, LZPeak, LCPeak, LAPeak   [v3.3.0: +1 LAPeak]
 *   - 4: dose_frac_niosh/osha_pel/osha_hca/eu_iso
 *   - 3: overload_flag (LZPeak > 140 dB), underrange_flag, wearing_state
 *   - 4: kurtosis_total/a_weighted/c_weighted, beta_kurtosis
 *   - 5: n_samples, sum_x/s1, sum_x2/s2, sum_x3/s3, sum_x4/s4
 *   - 9: freq band SPLs (63Hz-16kHz)
 *   - 45: freq band raw moments S1-S4 (9 bands × 5 values)
 *   Total: 2 + 9 + 1 + 4 + 3 + 4 + 5 + 9 + 45 = 82 fields (+ padding); sizeof ≈ 324 bytes
 *   (实际 sizeof = 324, padded by compiler)
 *
 * Padding note (v3.3.2 updated): Total field bytes = 323 (68 floats×4 + 3 bools×1 + 11 int32×4),
 * sizeof = 324 (next multiple of 4 above 323). v3.3.2 新增 LAF/LAS/LASmax 3 个 float 字段。
 *
 * v3.3.2 时间计权 (per IEC 61672-1 §7):
 *   - LAF = A 加权 + Fast 时间计权 (τ=125ms), 瞬时时间加权 SPL
 *   - LAS = A 加权 + Slow 时间计权 (τ=1s), 瞬时时间加权 SPL
 *   - LAeq = LAS (τ=1s 等效于 1s 时间常数, 用于合规测量)
 *   - 算法实现：state = α·state + (1−α)·y², α = exp(−1/(τ·fs))
 *   - 与 LAeq 的差异：LAF/LAS 是"瞬时"读数, LAeq 是"平均"读数
 *   - 接口二 仍输出 LAeq (平均值) 用于合规; 接口一同步输出 LAF/LAS 用于实时显示
 *
 * overload 事件判定 (per IEC 61672-1 Class 1):
 *   - OVERLOAD = LZPeak > 140 dB (OVERLOAD_THRESHOLD constant)
 *   - IMPULSE_SUSPECT = LZeq >= 90 dB 连续 debounce_frames 帧 (EventDetector 内部状态)
 *   - 接口一/二只暴露 overload_flag/overload_count (基于 LZPeak > 140);
 *     IMPULSE_SUSPECT 判定需调用接口三 EventDetector
 *   - v3.3.0 加 LAPeak: 听力损伤评估关键指标 (与 LZPeak/LCPeak 组成加权 peak 三件套)
 *   - v3.3.2 加 LAF/LAS: 事件检测算法升级为基于 LAF/LAS (v3.3.3 计划)
 */
struct SecondMetrics {
    //=== Metadata (2) ===
    float timestamp{0.0f};      // Unix timestamp (seconds since epoch)
    float duration_s{1.0f};     // Actual duration (typically 1.0s)

    //=== Sound Levels (13) — v3.3.3 LCF/LCS/LCSmax added ===
    float LAeq{0.0f};           // A-weighted equivalent SPL (时间平均)
    float LCeq{0.0f};           // C-weighted equivalent SPL (时间平均)
    float LZeq{0.0f};           // Z-weighted (unweighted) equivalent SPL (时间平均)
    float LAFmax{0.0f};         // [deprecate: 由 LAF 替代, 保留兼容旧消费者]
    float LASmax{0.0f};         // A 加权 + Slow 时间计权最大值 [v3.3.2 added]
    float LCSmax{0.0f};         // C 加权 + Slow 时间计权最大值 [v3.3.3 added]
    float LAF{0.0f};            // A-weighted Fast time-weighted SPL (τ=125ms) [v3.3.2 added]
    float LAS{0.0f};            // A-weighted Slow time-weighted SPL (τ=1s) [v3.3.2 added]
    float LCF{0.0f};            // C-weighted Fast time-weighted SPL (τ=125ms) [v3.3.3 added]
    float LCS{0.0f};            // C-weighted Slow time-weighted SPL (τ=1s) [v3.3.3 added]
    float LZPeak{0.0f};         // Z-weighted peak level (OVERLOAD 判定主依据)
    float LCPeak{0.0f};         // C-weighted peak level
    float LAPeak{0.0f};         // A-weighted peak level [v3.3.0 added] — 听力损伤评估关键指标

    //=== Dose Increments (4) ===
    float dose_frac_niosh{0.0f};     // NIOSH dose fraction (0-1)
    float dose_frac_osha_pel{0.0f};  // OSHA PEL dose fraction
    float dose_frac_osha_hca{0.0f};  // OSHA HCA dose fraction
    float dose_frac_eu_iso{0.0f};    // EU/ISO dose fraction

    //=== Quality Control (3) ===
    bool overload_flag{false};    // LZPeak > 140 dB (OVERLOAD_THRESHOLD); per IEC 61672-1 Class 1 过载判定
    bool underrange_flag{false};  // LAeq < 30 dB (UNDERRANGE_THRESHOLD); 传感器信号太低
    bool wearing_state{true};     // LAeq > 40 dB 表示佩戴中 (粗略检测)

    //=== 事件检测结果 (2) — v3.3.3 新增 ===
    // 由 EventDetector::check_metrics() 填入（调用方负责回写）
    //   event_type:     0=NONE, 1=MINOR, 2=MODERATE, 3=SEVERE（对应 EventType 枚举）
    //   event_severity: 0-100 严重程度评分
    uint8_t event_type{0};
    uint8_t event_severity{0};

    //=== Kurtosis Metrics (4) ===
    float kurtosis_total{3.0f};      // Z-weighted (raw signal) kurtosis (Pearson, normal=3)
    float kurtosis_a_weighted{3.0f};  // A-weighted kurtosis
    float kurtosis_c_weighted{3.0f};  // C-weighted kurtosis
    float beta_kurtosis{0.0f};        // Kurtosis from raw moments (per spec 4.X.3)

    //=== Raw Moment Statistics for Aggregation (5, per spec 4.X.3) ===
    int32_t n_samples{0};  // Sample count n
    float sum_x{0.0f};     // S1 = Σx_k
    float sum_x2{0.0f};    // S2 = Σx_k²
    float sum_x3{0.0f};    // S3 = Σx_k³
    float sum_x4{0.0f};    // S4 = Σx_k⁴

    //=== 1/3 Octave Band SPL (9) ===
    float freq_63hz_spl{0.0f};
    float freq_125hz_spl{0.0f};
    float freq_250hz_spl{0.0f};
    float freq_500hz_spl{0.0f};
    float freq_1khz_spl{0.0f};
    float freq_2khz_spl{0.0f};
    float freq_4khz_spl{0.0f};
    float freq_8khz_spl{0.0f};
    float freq_16khz_spl{0.0f};

    //=== 1/3 Octave Band Raw Moment Statistics S1-S4 (9 bands × 5 values = 45) ===
    // 63Hz band
    int32_t freq_63hz_n{0};
    float freq_63hz_s1{0.0f}, freq_63hz_s2{0.0f}, freq_63hz_s3{0.0f}, freq_63hz_s4{0.0f};
    // 125Hz band
    int32_t freq_125hz_n{0};
    float freq_125hz_s1{0.0f}, freq_125hz_s2{0.0f}, freq_125hz_s3{0.0f}, freq_125hz_s4{0.0f};
    // 250Hz band
    int32_t freq_250hz_n{0};
    float freq_250hz_s1{0.0f}, freq_250hz_s2{0.0f}, freq_250hz_s3{0.0f}, freq_250hz_s4{0.0f};
    // 500Hz band
    int32_t freq_500hz_n{0};
    float freq_500hz_s1{0.0f}, freq_500hz_s2{0.0f}, freq_500hz_s3{0.0f}, freq_500hz_s4{0.0f};
    // 1kHz band
    int32_t freq_1khz_n{0};
    float freq_1khz_s1{0.0f}, freq_1khz_s2{0.0f}, freq_1khz_s3{0.0f}, freq_1khz_s4{0.0f};
    // 2kHz band
    int32_t freq_2khz_n{0};
    float freq_2khz_s1{0.0f}, freq_2khz_s2{0.0f}, freq_2khz_s3{0.0f}, freq_2khz_s4{0.0f};
    // 4kHz band
    int32_t freq_4khz_n{0};
    float freq_4khz_s1{0.0f}, freq_4khz_s2{0.0f}, freq_4khz_s3{0.0f}, freq_4khz_s4{0.0f};
    // 8kHz band
    int32_t freq_8khz_n{0};
    float freq_8khz_s1{0.0f}, freq_8khz_s2{0.0f}, freq_8khz_s3{0.0f}, freq_8khz_s4{0.0f};
    // 16kHz band
    int32_t freq_16khz_n{0};
    float freq_16khz_s1{0.0f}, freq_16khz_s2{0.0f}, freq_16khz_s3{0.0f}, freq_16khz_s4{0.0f};

    SecondMetrics() = default;

    /** @brief Reset all fields to default values */
    void reset() { *this = SecondMetrics{}; }
};

//==============================================================================
// Per-Minute Aggregated Metrics
//==============================================================================

/**
 * @brief Per-minute aggregated metrics
 * Derived from 60 SecondMetrics records
 */
struct MinuteMetrics {
    float timestamp{0.0f};
    float duration_s{60.0f};

    //=== Overall Sound Levels (3) ===
    float LAeq{0.0f};
    float LCeq{0.0f};
    float LZeq{0.0f};

    //=== Peak Levels (6) — v3.3.3 LCSmax added ===
    float LAFmax{0.0f};         // [deprecate: 由 LAF 替代, 保留兼容旧消费者]
    float LASmax{0.0f};         // [v3.3.2 added] A 加权 + Slow 时间计权最大值
    float LCSmax{0.0f};         // [v3.3.3 added] C 加权 + Slow 时间计权最大值
    float LZPeak{0.0f};        // Z-weighted peak (max across seconds in minute)
    float LCPeak{0.0f};        // C-weighted peak (max across seconds in minute)
    float LAPeak{0.0f};        // A-weighted peak (max across seconds in minute) [v3.3.0 added] — 听力损伤评估关键指标

    //=== Dose Accumulation (4) ===
    float dose_frac_niosh{0.0f};
    float dose_frac_osha_pel{0.0f};
    float dose_frac_osha_hca{0.0f};
    float dose_frac_eu_iso{0.0f};

    //=== QC Statistics (3) ===
    int32_t overload_count{0};
    int32_t underrange_count{0};
    int32_t valid_seconds{0};

    //=== 事件统计 (3) — v3.3.3 新增 ===
    int32_t event_minor_count{0};     ///< MINOR 事件秒钟数
    int32_t event_moderate_count{0};  ///< MODERATE 事件秒钟数
    int32_t event_severe_count{0};    ///< SEVERE 事件秒钟数

    //=== Kurtosis (3) ===
    float kurtosis_total{3.0f};
    float kurtosis_a_weighted{3.0f};
    float kurtosis_c_weighted{3.0f};

    //=== Raw Moments for Kurtosis (5) ===
    int32_t n_samples{0};
    float sum_x{0.0f};
    float sum_x2{0.0f};
    float sum_x3{0.0f};
    float sum_x4{0.0f};

    //=== 1/3 Octave Band SPL (9) ===
    float freq_63hz_spl{0.0f};
    float freq_125hz_spl{0.0f};
    float freq_250hz_spl{0.0f};
    float freq_500hz_spl{0.0f};
    float freq_1khz_spl{0.0f};
    float freq_2khz_spl{0.0f};
    float freq_4khz_spl{0.0f};
    float freq_8khz_spl{0.0f};
    float freq_16khz_spl{0.0f};

    //=== 1/3 Octave Band Raw Moments S1-S4 (45) ===
    int32_t freq_63hz_n{0};
    float freq_63hz_s1{0.0f}, freq_63hz_s2{0.0f}, freq_63hz_s3{0.0f}, freq_63hz_s4{0.0f};
    int32_t freq_125hz_n{0};
    float freq_125hz_s1{0.0f}, freq_125hz_s2{0.0f}, freq_125hz_s3{0.0f}, freq_125hz_s4{0.0f};
    int32_t freq_250hz_n{0};
    float freq_250hz_s1{0.0f}, freq_250hz_s2{0.0f}, freq_250hz_s3{0.0f}, freq_250hz_s4{0.0f};
    int32_t freq_500hz_n{0};
    float freq_500hz_s1{0.0f}, freq_500hz_s2{0.0f}, freq_500hz_s3{0.0f}, freq_500hz_s4{0.0f};
    int32_t freq_1khz_n{0};
    float freq_1khz_s1{0.0f}, freq_1khz_s2{0.0f}, freq_1khz_s3{0.0f}, freq_1khz_s4{0.0f};
    int32_t freq_2khz_n{0};
    float freq_2khz_s1{0.0f}, freq_2khz_s2{0.0f}, freq_2khz_s3{0.0f}, freq_2khz_s4{0.0f};
    int32_t freq_4khz_n{0};
    float freq_4khz_s1{0.0f}, freq_4khz_s2{0.0f}, freq_4khz_s3{0.0f}, freq_4khz_s4{0.0f};
    int32_t freq_8khz_n{0};
    float freq_8khz_s1{0.0f}, freq_8khz_s2{0.0f}, freq_8khz_s3{0.0f}, freq_8khz_s4{0.0f};
    int32_t freq_16khz_n{0};
    float freq_16khz_s1{0.0f}, freq_16khz_s2{0.0f}, freq_16khz_s3{0.0f}, freq_16khz_s4{0.0f};

    MinuteMetrics() = default;
    void reset() { *this = MinuteMetrics{}; }
};

//==============================================================================
// Utility Functions
//==============================================================================

inline float calculate_kurtosis_from_moments(int32_t n, float s1, float s2,
                                              float s3, float s4) {
    if (n <= 0) return std::nan("");
    float mu = s1 / n;
    float m2 = s2 / n - mu * mu;
    if (m2 <= 0) return std::nan("");
    float m4 = (s4 / n
                - 4.0f * mu * (s3 / n)
                + 6.0f * (mu * mu) * (s2 / n)
                - 3.0f * (mu * mu * mu * mu));
    return m4 / (m2 * m2);
}

/**
 * @brief Get frequency band moments pointer from SecondMetrics by band index (0-8)
 */
inline FreqBandMoments* band_moments_ptr(SecondMetrics& m, int band_idx) {
    switch (band_idx) {
        case 0: return reinterpret_cast<FreqBandMoments*>(&m.freq_63hz_n);
        case 1: return reinterpret_cast<FreqBandMoments*>(&m.freq_125hz_n);
        case 2: return reinterpret_cast<FreqBandMoments*>(&m.freq_250hz_n);
        case 3: return reinterpret_cast<FreqBandMoments*>(&m.freq_500hz_n);
        case 4: return reinterpret_cast<FreqBandMoments*>(&m.freq_1khz_n);
        case 5: return reinterpret_cast<FreqBandMoments*>(&m.freq_2khz_n);
        case 6: return reinterpret_cast<FreqBandMoments*>(&m.freq_4khz_n);
        case 7: return reinterpret_cast<FreqBandMoments*>(&m.freq_8khz_n);
        case 8: return reinterpret_cast<FreqBandMoments*>(&m.freq_16khz_n);
        default: return nullptr;
    }
}

/** @brief Const version */
inline const FreqBandMoments* band_moments_ptr(const SecondMetrics& m, int band_idx) {
    return band_moments_ptr(const_cast<SecondMetrics&>(m), band_idx);
}

// Overloads for MinuteMetrics (same field layout at band moment offsets)
inline FreqBandMoments* band_moments_ptr(MinuteMetrics& m, int band_idx) {
    switch (band_idx) {
        case 0: return reinterpret_cast<FreqBandMoments*>(&m.freq_63hz_n);
        case 1: return reinterpret_cast<FreqBandMoments*>(&m.freq_125hz_n);
        case 2: return reinterpret_cast<FreqBandMoments*>(&m.freq_250hz_n);
        case 3: return reinterpret_cast<FreqBandMoments*>(&m.freq_500hz_n);
        case 4: return reinterpret_cast<FreqBandMoments*>(&m.freq_1khz_n);
        case 5: return reinterpret_cast<FreqBandMoments*>(&m.freq_2khz_n);
        case 6: return reinterpret_cast<FreqBandMoments*>(&m.freq_4khz_n);
        case 7: return reinterpret_cast<FreqBandMoments*>(&m.freq_8khz_n);
        case 8: return reinterpret_cast<FreqBandMoments*>(&m.freq_16khz_n);
        default: return nullptr;
    }
}

inline const FreqBandMoments* band_moments_ptr(const MinuteMetrics& m, int band_idx) {
    return band_moments_ptr(const_cast<MinuteMetrics&>(m), band_idx);
}

/**
 * @brief Aggregate raw moments from multiple records
 */
inline int32_t aggregate_moments(const FreqBandMoments* moments, int count,
                                  float& out_s1, float& out_s2,
                                  float& out_s3, float& out_s4) {
    int32_t total_n = 0;
    out_s1 = out_s2 = out_s3 = out_s4 = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (moments[i].n > 0) {
            out_s1 += moments[i].s1;
            out_s2 += moments[i].s2;
            out_s3 += moments[i].s3;
            out_s4 += moments[i].s4;
            total_n += moments[i].n;
        }
    }
    return total_n;
}

} // namespace noise_toolkit