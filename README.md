# noise_info_toolkit_gcc

C++ 实现的轻量级噪声信息计算工具包（**v3.3.3** — 基于 LAF/LAS 的多维度事件检测 + 指数时间计权，IEC Class 1 实验级精度），从 Python 项目 [noise_info_toolkit](https://github.com/LewisBase/noise_info_toolkit) 移植而来。

## 设计目标

纯噪声信息计算，提供三个简洁接口，不包含任何存储、文件解析等功能。
针对 nRF54L15 嵌入式平台优化：零动态内存分配、编译期常量滤波器系数、constexpr 剂量标准表、流式逐样本处理架构。
全链路单精度 `float` 计算，无 `double` 软浮点依赖。

## 三个核心接口

### 接口一：逐段调用 — `process_segment(buffer_start, buffer_end, duration_s)`

传入音频缓冲区指针（float）和时长（秒），返回该段所有 **82 个指标**：

```cpp
#include "noise_processor.hpp"

using namespace noise_toolkit;

NoiseProcessor processor(48000);  // sample_rate

// 传入 float 缓冲区指针和时长
SecondMetrics m = processor.process_segment(buffer_start, buffer_end, 1.0f);

// m 包含 82 个指标：
//   - 元数据: timestamp, duration_s
//   - 声级: LAeq, LCeq, LZeq, LAFmax, LASmax, LCSmax, LAF, LAS, LCF, LCS, LZpeak, LCpeak, LAPeak
//           [v3.3.3+LCF/LCS/LCSmax，v3.3.2+LAF/LAS/LASmax 指数时间计权，v3.3.0+LAPeak]
//   - 事件: event_type (0-3), event_severity (0-100)   [v3.3.3]
//   - 剂量: dose_frac_niosh/osha_pel/osha_hca/eu_iso
//   - QC: overload_flag, underrange_flag, wearing_state
//   - 峰度: kurtosis_total, kurtosis_a_weighted, kurtosis_c_weighted, beta_kurtosis
//   - 原始矩: n_samples, sum_x/s1, sum_x2/s2, sum_x3/s3, sum_x4/s4
//   - 1/3倍频程SPL: freq_63hz_spl ~ freq_16khz_spl (9个频段)
//   - 1/3倍频程矩S1-S4: 每个频段5个值 × 9个频段 = 45个字段
//
// 时间计权说明（v3.3.2 起，IEC 61672-1 §7）：
//   - LAF = A 加权 + Fast 时间计权 (τ=125ms) 瞬时读数
//   - LAS = A 加权 + Slow 时间计权 (τ=1s) 瞬时读数
//   - LCF = C 加权 + Fast 时间计权 (τ=125ms) 瞬时读数  [v3.3.3]
//   - LCS = C 加权 + Slow 时间计权 (τ=1s) 瞬时读数    [v3.3.3]
//   - LAeq / LCeq = 等效连续声级（τ=1s 时间常数，用于合规测量）
//   - 注：v3.3.3 方案 C 删除了 Z 的时间计权（无加权，实用价值低）
```

支持灵活时长：1秒、10ms 或任意 `sample_rate * duration_s` 个采样点。

### 接口二：聚合调用 — `aggregate_metrics(metrics_array, count, unit_duration_s)`

传入多个 `SecondMetrics`，返回聚合后的分钟指标 `MinuteMetrics`：

```cpp
std::array<SecondMetrics, 60> seconds;
for (int i = 0; i < 60; ++i) {
    seconds[i] = processor.process_segment(data, data + 48000, 1.0f);
}

MinuteMetrics minute = processor.aggregate_metrics(seconds, 60, 1.0f);
```

### 接口三：逐段检测 — `check_segment(buffer_start, buffer_end)`

传入 Z 加权 PCM 缓冲区指针（float，单位 Pa），返回本段异常类型 `EventCheckResult`。独立于 `NoiseProcessor`，零堆分配（实例约 28 bytes）：

```cpp
#include "event_detector.hpp"

using namespace noise_toolkit;

EventDetectorConfig config;  // 可选，均有默认值
config.leq_threshold_db = 90.0f;
config.peak_threshold_db = 140.0f;   // 同 OVERLOAD_THRESHOLD
config.debounce_frames = 3;
config.cooldown_frames = 5;

EventDetector detector(config);

EventCheckResult r = detector.check_segment(buffer_start, buffer_end);

// r 取值：
//   - NORMAL:          无异常
//   - OVERLOAD:        LZpeak 过载（不受 cooldown 抑制）
//   - UNDERRANGE:      LZeq 低于 underrange_threshold_db（默认 30 dB）
//   - IMPULSE_SUSPECT: LZeq 连续 debounce_frames 帧超 leq_threshold_db（默认 90 dB）
//
// 触发后可用 was_impulse_detected() / clear_impulse_flag() 标记起始点
```

典型用法：与 `process_segment()` 相同块长（如 10 ms @ 48 kHz = 480 samples），可在指标计算前后任意调用。

### 接口四：剂量累积与 Dose% / TWA / LEX,8h 换算（v3.1.3 新增）

`DoseState` POD（8 bytes）由业务侧持有，4 个 inline 纯函数作为 `DoseCalculator` 的薄包装，提供 Dose%/TWA/LEX,8h 实时换算。算法库本身保持无状态，零堆分配。

```cpp
#include "noise_processor.hpp"
#include "dose_state.hpp"

using namespace noise_toolkit;

NoiseProcessor processor(48000);

// 业务侧持有 4 个 DoseState（每个标准一个）
DoseState niosh_state = {};
DoseState osha_state  = {};

while (recording) {
    auto samples = read_audio_block();
    SecondMetrics m = processor.process_segment(samples.data(), samples.data() + samples.size(), 0.01f);

    // 1. 累加剂量（库只提供纯函数，状态由业务侧持有）
    niosh_state = accumulate_dose_frac(niosh_state, m.dose_frac_niosh,    0.01f);
    osha_state  = accumulate_dose_frac(osha_state,  m.dose_frac_osha_pel, 0.01f);

    // 2. 任意时刻读出累积量（不需等结束录制）
    if (report_due) {
        log("NIOSH  Dose%%=%.1f%%  TWA=%.1f dB  LEX,8h=%.1f dB",
            dose_to_pct(niosh_state),
            dose_to_twa(niosh_state, DoseStandard::NIOSH),
            dose_to_lex8h(niosh_state, DoseStandard::NIOSH));
        log("OSHA   Dose%%=%.1f%%  TWA=%.1f dB  LEX,8h=%.1f dB",
            dose_to_pct(osha_state),
            dose_to_twa(osha_state, DoseStandard::OSHA_PEL),
            dose_to_lex8h(osha_state, DoseStandard::OSHA_PEL));
    }
}
```

**支持的 4 个标准**（通过 `DoseStandard` 枚举选择）：

| 枚举值 | 标准 | 交换率 $q$ | 准则声级 $L_c$ | log10 系数 |
|--------|------|-----------|---------------|-----------|
| `NIOSH` | NIOSH 1998 | 3 dB | 85 dBA | 10.0 |
| `OSHA_PEL` | OSHA 29 CFR 1910.95(b) | 5 dB | 90 dBA | **16.61** |
| `OSHA_HCA` | OSHA Hearing Conservation | 5 dB | 85 dBA | **16.61** |
| `EU_ISO` | EU Directive 2003/10/EC | 3 dB | 85 dBA | 10.0 |

⚠️ **5dB 交换率修正系数 16.61**：OSHA 标准强制使用此系数（= 5/log10(2)），自动由 `DoseStandard` 枚举在 `dose_to_twa()` 内部选择，**不要手动用 10·log10 算 OSHA**。

**内存开销**：

| 组件 | 大小 |
|------|------|
| `DoseState` POD | 8 bytes |
| 4 个标准状态 | 32 bytes |
| nRF54L15 256-512 KB RAM 占比 | < 0.02% |

详见 [`docs/DEVELOPMENT_PLAN_v3.1.3.md`](docs/DEVELOPMENT_PLAN_v3.1.3.md)。

## 构建

```bash
cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

构建选项：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_TESTS` | ON | 构建测试 |
| `BUILD_EXAMPLES` | ON | 构建示例 |

库始终为静态库 `libnoise_toolkit.a`。SQLite3 在配置时检测但不链接。

## 运行测试

```bash
cd build_test
ctest                            # 运行全部 7 个测试套件
./test_noise_processor           # 噪声指标单元测试（12个测试）
./test_event_detector            # 事件检测单元测试（14个测试）
./test_class1_precision          # Class 1 A/C 加权精度测试（v3.3.0）
./test_end_to_end                # 端到端验证：合成信号 LAeq/LCeq/Dose
./noise_toolkit_example          # 三接口演示与手动验证（含 EventDetector 联调）
./dose_validator                 # 剂量计算理论值验证
```

## 精度说明

全链路使用单精度 `float`：

| 模块 | 精度 | 说明 |
|------|------|------|
| 音频处理（滤波、Leq、Peak） | float | 热路径，每个样本 |
| 剂量计算（Dose、TWA、LEX,8h） | float | 每秒更新 |
| 峰度计算（kurtosis、S1-S4） | float | 每秒统计 |
| 滤波器系数（A/C计权） | float | 编译期常量 |
| 标准参数（准则级、交换率等） | float | constexpr 表 |

对于整数可精确表示的标准参数（85、90、3、5、8 等），`float` 与 `double` 结果完全一致。

## 精度等级 (IEC 61672-1:2013)

A/C 加权滤波器的频率响应精度按照 **IEC 61672-1** 国际标准分两级：

| 等级 | 容差 | 典型用途 |
|------|------|----------|
| **Class 1（精密级）** | ±0.7 dB | 实验室级研究、听力防护标准验证、声校准器校准 |
| **Class 2（一般级）** | ±1.5 dB | 工业现场噪声评估、职业暴露监测、法规合规 |

> ⚠️ **重要**：两种等级使用的是**同一条 A/C 加权曲线**（1 kHz = 0 dB、100 Hz = -19.1 dB、10 kHz = -2.5 dB），区别仅在于测量设备对理想曲线的**复现精度**。

**本项目提供两套滤波系数**，分别满足不同精度需求：

| 版本 | 变换方法 | A-weighting 最大误差 | C-weighting 最大误差 | 等级 | biquad 段数 |
|------|----------|---------------------|---------------------|------|-------------|
| v3.2.1 | plain bilinear | 1.3 dB | >2 dB | Class 2 | 3 / 2 |
| v3.3.0 | matched-z + peaking EQ | **0.49 dB** | **0.18 dB** | **Class 1** ✅ | 4 / 3 |

**v3.3.0 的实验级精度实现**：
- 采用**极点-零点匹配变换 (matched-z)**：每个模拟极点按 `z_p = exp(s_p · T)` 直接映射，频率位置**精确无畸变**（无 bilinear warping）
- 增益形状由 **2nd-order peaking EQ** 修正（differential evolution 全局优化，代价函数为 33 个 IEC 标准频率的最大绝对误差）
- 1 kHz 归一化 gain factor 独立于 biquad 系数（与 v3.2.1 相同的 post-chain 架构）
- 详见 `include/weighting_coefficients_matched_z_48k.hpp` 和 `tools/regen_weighting_matched_z.py`

**选型建议**：
- 工业噪声监测 / 职业暴露合规 → **v3.2.1 bilinear**（3 段 biquad，性能稳定，Class 2 合规）
- 实验室级精密研究 / 听力防护标准验证 → **v3.3.0 matched-z**（4 段 biquad，**Class 1 实验级精度**）

## 指标列表（87个每秒指标 — v3.3.3+LCF/LCS/事件）

| 类别 | 数量 | 字段 |
|------|------|------|
| 元数据 | 2 | timestamp, duration_s |
| 声级 | 13 | LAeq, LCeq, LZeq, LAFmax, **LASmax** [v3.3.2], **LCSmax** [v3.3.3], **LAF** [v3.3.2], **LAS** [v3.3.2], **LCF** [v3.3.3], **LCS** [v3.3.3], LZpeak, LCpeak, LAPeak [v3.3.0] |
| 剂量增量 | 4 | dose_frac_niosh, dose_frac_osha_pel, dose_frac_osha_hca, dose_frac_eu_iso |
| 质量控制 | 3 | overload_flag (LZPeak>140 dB, IEC 61672-1 Class 1), underrange_flag, wearing_state |
| **事件检测** | **2** | **event_type** [v3.3.3] (0=NONE..3=SEVERE), **event_severity** [v3.3.3] (0-100) |
| 峰度 | 4 | kurtosis_total, kurtosis_a_weighted, kurtosis_c_weighted, beta_kurtosis |
| 原始矩统计量 | 5 | n_samples, sum_x, sum_x2, sum_x3, sum_x4 |
| 1/3倍频程SPL | 9 | freq_63hz_spl ~ freq_16khz_spl |
| 1/3倍频程矩S1-S4 | 45 | 每个频段(n,s1,s2,s3,s4) × 9个频段 |
| **合计** | **87** | v3.3.3 在 v3.3.2 基础上新增 LCF/LCS/LCSmax + event_type/event_severity |

## 标准参数

| 标准 | 准则级 (dBA) | 交换率 (dB) | 参考时长 (h) |
|------|-------------|-------------|-------------|
| NIOSH | 85 | 3 | 8 |
| OSHA_PEL | 90 | 5 | 8 |
| OSHA_HCA | 85 | 5 | 8 |
| EU_ISO | 85 | 3 | 8 |

标准参数以 `constexpr` 编译期常量表存储，通过 `DoseStandard` 枚举索引，零运行时开销。

## 峰度计算（S1-S4 原始矩统计）

根据规范 4.X.3，使用原始矩统计量 S1-S4 跨时段精确合成峰度 β：

```
μ = S1 / n
m2 = S2/n - μ²
m4 = S4/n - 4μ·S3/n + 6μ²·S2/n - 3μ⁴
β = m4 / m2²
```

## 与 Python 版本对比

使用 `validation/validate_dose_calculator.py` 对比 Python 和 C++（float）版本，覆盖全部 4 种标准 × 6 种场景：

| 标准 | 指标 | Python (double) | C++ (float) | 差异 |
|------|------|-----------------|-------------|------|
| NIOSH | Dose% | 100.000000 | 100.000000 | 0 |
| NIOSH | TWA | 85.000000 dBA | 85.000000 dBA | 0 |
| OSHA_PEL | Dose% | 100.000000 | 100.000000 | 0 |
| OSHA_PEL | TWA | 90.000000 dBA | 90.000000 dBA | 0 |
| OSHA_HCA | Dose% | 200.000000 | 200.000000 | 0 |
| EU_ISO | Dose% | 317.480210 | 317.480210 | 0 |

全部 24 组测试用例通过，**Diff=0.00e+00**（因为标准参数均为整数，float 与 double 表示完全相同）。

## 嵌入式优化特性

本项目针对 nRF54L15 (Cortex-M33) 平台进行了以下优化：

| 优化项 | 说明 |
|--------|------|
| 全 float 精度 | 剂量计算、TWA、LEX,8h 全部使用 float，无 double 软浮点 |
| DoseCalculator 重构 | `std::map<string>` → `constexpr` 数组 + 枚举索引，零堆分配 |
| 滤波器系数固化 | A/C 计权系数预计算为 `constexpr BiquadChain`（48kHz） |
| bandpass 系数固化 | 1/3 倍频程滤波器系数预计算为 `constexpr BandpassCoeffs`（48kHz） |
| alignas 移除 | `SecondMetrics` 去除 64 字节对齐，减少内存浪费 |
| 异常处理 | 嵌入式构建通过 `NOISE_EMBEDDED_BUILD` 宏禁用 throw/try/catch |
| thread_local 移除 | 改为普通 `static` 变量 |
| M_PI 移除 | 自定义 `noise_const::PI_F` / `TWO_PI_F` 常量，兼容所有嵌入式工具链 |
| 流式 A/C 计权 | 逐样本处理，`process_sample()` 接口，零堆分配 |
| 流式倍频带分析 | 9 个 persistent BiquadFilter，逐样本处理，零堆分配 |
| `NoiseProcessor` 流式架构 | `process_segment()` 热路径零 vector/new/malloc |
| `IIRFilter::process_sample()` | 新增流式接口，支持逐样本 in-place 处理 |

PC 构建保留完整 C++ 特性（异常、iostream、string 等）用于验证和调试。

## 文件结构

```
noise_info_toolkit_gcc/
├── include/
│   ├── noise_metrics.hpp              # 核心数据结构（SecondMetrics, MinuteMetrics）
│   ├── noise_processor.hpp            # 主处理器（接口一、二）—— v3.1 流式架构
│   ├── dose_calculator.hpp            # 剂量计算器（静态方法，constexpr 表，float）
│   ├── signal_utils.hpp               # 信号处理工具（含流式 weighting 接口）
│   ├── iir_filter.hpp                 # IIR 滤波器设计（含 process_sample 流式接口）
│   ├── filter_coefficients_48k.hpp    # 预计算 A/C 计权系数（48kHz，v3.2.1 + v3.3.0 两套）
│   ├── bandpass_coefficients_48k.hpp  # 预计算 1/3 倍频程带通系数（48kHz）
│   ├── weighting_coefficients_multirate.hpp    # 7 采样率 A/C 加权预存表（v3.2.1，Class 2）
│   ├── weighting_coefficients_matched_z_48k.hpp # Matched-z A/C 加权（v3.3.0，Class 1）🆕
│   ├── math_constants.hpp             # 数学常量（PI_F, TWO_PI_F）
│   ├── event_detector.hpp             # 事件检测器（接口三）
│   └── noise_toolkit.hpp              # 主入口
├── src/
│   ├── noise_processor.cpp            # 处理器实现 —— v3.1 流式，零堆分配
│   ├── dose_calculator.cpp            # 剂量计算实现（PC 字符串 API）
│   ├── signal_utils.cpp              # 信号处理实现（含 inplace weighting）
│   ├── iir_filter.cpp                # IIR 滤波器实现（含 process_sample）
│   └── event_detector.cpp            # 事件检测实现
├── tools/
│   ├── generate_bandpass_coeffs.cpp   # bandpass 系数生成工具（v3.3.0 旧版，已弃用）
│   ├── regen_bandpass_coefficients.py    # v3.3.1 scipy 重生成 1/3 oct 带通系数 🆕
│   ├── regen_weighting_coefficients.py    # v3.2.1 bilinear 7 采样率系数重生成脚本
│   └── regen_weighting_matched_z.py       # v3.3.0 matched-z 系数设计脚本
├── tests/
│   ├── test_noise_processor.cpp       # 噪声指标单元测试（12 个测试）
│   ├── test_event_detector.cpp       # 事件检测单元测试（14 个测试）
│   ├── test_class1_precision.cpp     # Class 1 A/C 加权精度测试 🆕
│   └── test_end_to_end.cpp            # 端到端验证：合成信号 LAeq/LCeq/Dose 🆕
├── examples/
│   └── main.cpp                       # 示例程序
├── dose_validator.cpp                 # 剂量理论值验证源码（构建为 dose_validator，勿提交二进制）
├── validation/
│   └── validate_dose_calculator.py    # Python 对比验证脚本
├── docs/
│   ├── suggestion.md                  # 嵌入式工程师优化建议
│   ├── development_plan.md           # 开发计划（v3.1）
│   ├── DEVELOPMENT_PLAN_v3.1.2.md     # v3.1.2 开发计划
│   └── 噪声算法嵌入式验证_关键问题与解决方案.md
└── CMakeLists.txt
```

## 依赖项

- C++17 编译器
- CMake 3.14+
- pthread

## 项目链接

- **C++ 版本（本项目）**: https://github.com/LewisBase/noise_info_toolkit_gcc
- **Python 原版**: https://github.com/LewisBase/noise_info_toolkit

## 许可证

待定 / 请参考原 Python 项目许可证

## 变更记录

### v3.3.3 (2026-09-20) — 基于 LAF/LAS 的多维度事件检测

**升级**：事件检测从单一峰值阈值升级为**四维度 LAF/LAS 联合判定** + **三档分级**。

#### 背景

`EventDetector`（接口三）原本只做单个判断：`LZPeak >= 140 dB → OVERLOAD`。
v3.3.0 起 `LZeq >= 90 dB → IMPULSE_SUSPECT` 被禁用（屏蔽块保留在源码中）。
导致以下场景无法检测：工业噪声合规阈值、冲击噪声、噪声水平突变、脉冲指标。

#### 四维度判定架构

| 维度 | 触发指标 | 阈值 | 响应时间 | 适用场景 |
|------|---------|------|---------|---------|
| **D1** LAF 阈值 | `LAF >= threshold` | 85 / 95 / 110 dB 三档 | 125 ms | 工业噪声合规、机器启动 |
| **D2** 脉冲指标 | `LAF − LAS` | \> 6 / 12 dB | 1 s | 冲击噪声、瞬态事件 |
| **D3** LAF 上升率 | `LAF(t) − LAF(t−500ms)` | \> 10 / 15 dB | 500 ms | 事件起点检测 |
| **D4** 背景对比 | `LAF − LAeq(5min 背景)` | \> 15 dB | 5 min 收敛 | 显著事件判定 |

#### 事件分级

```cpp
enum class EventType : uint8_t {
    NONE     = 0,  // 无事件
    MINOR    = 1,  // LAF > 85 dB 或 上升 > 10 dB 或 脉冲 > 6 dB
    MODERATE = 2,  // LAF > 95 dB 或 上升 > 15 dB
    SEVERE   = 3,  // LAF > 110 dB 或 脉冲 > 12 dB 或 LZPeak ≥ 140 dB
};
```

#### 新增 API（两个重载）
```cpp
// ─── 设备侧（只需 buf/end，无需 NoiseProcessor）───
// 内部自带轻量 A 计权 4 段 biquad + Fast/Slow 时间计权，逐样本算出
// LAF / LAS / LAeq / LZPeak，再走四维度判定。
EventResult r = detector.check_metrics(buffer_start, buffer_end);

// ─── 主机侧（已有 SecondMetrics，避免重复滤波）───
SecondMetrics m = proc.process_segment(buf, end, 1.0f);  // 先算指标
EventResult r2 = detector.check_metrics(m);               // 再判定

// r.event_type     — 分级（NONE/MINOR/MODERATE/SEVERE）
// r.severity       — 严重程度评分 0-100
// r.laf_dB / r.las_dB / r.impulse_metric_dB
// r.laf_rise_dB / r.background_delta_dB        — 各维度原始值
// r.trigger_*      — 四维度触发标志
```

> **两个重载结果完全一致**（同一 A 计权链 + 同一时间计权 + 同一判定逻辑），
> 已用 20 Hz 实录 WAV 逐秒对比验证：**35/35 秒事件分级一致**。
> 设备侧选哪个取决于手上有什么：只有原始缓冲 → 用 buf/end 重载；
> 已跑过 `process_segment()` → 用 metrics 重载（省一遍滤波）。

- 旧接口 `check_segment(buffer)` **完全保留**（向后兼容，仅过载/欠量程判定）
- 新增 `SecondMetrics::event_type` / `event_severity` 字段（供 CSV 输出）
- 新增 `MinuteMetrics::event_minor_count` / `event_moderate_count` / `event_severe_count`

#### 设计要点

- **与 NoiseProcessor 解耦**：调用方负责把 `EventResult` 回写到 `SecondMetrics.event_type`
- **阈值可配置**：全部位于 `EventDetectorConfig`，运行时/编译期均可改
- **零堆分配**：全部状态在内（LAF 历史环形缓冲 16 项 + 背景指数平均）
- **内存增量**：LAF 历史 16×8 = 128 字节 + 背景/计数 ~24 字节 ≈ **152 字节**
- **过载直通**：`LZPeak ≥ 140 dB` 仍直接判 SEVERE（兼容旧行为）

#### C 计权同步修复（v3.3.3 方案 C）

C 计权与 A 计权共用同一个 20.6 Hz 双极点，同样有滤波器建立时间问题（但 C 在 20 Hz
只衰减 −6.22 dB，故影响远小于 A 的 −50.50 dB）。

**修复内容**：
- 滤波器状态持久（v3.3.2 已覆盖 C 链）——LCeq 自动受益
- **新增 C 的时间计权输出**：`LCF` / `LCS` / `LCSmax`
- **删除 Z 的时间计权状态**（`laf_sq_z_`/`las_sq_z_`）——Z 无加权，瞬时时间计权实用价值低，
  每样本省 4 组乘加

**C 计权实测（纯音 @ 94 dB SPL）**：

| 频率 | LCeq 修复前（每段重置）| LCeq 修复后（连续）| IEC 理论 | 改善 |
|------|---------------------|------------------|---------|------|
| 20 Hz | 87.11（偏差 −0.67）| **87.73（偏差 −0.05）** | 87.78 | +0.62 dB |
| 25 Hz | 88.89（−0.67）| **89.51（−0.05）** | 89.56 | +0.62 dB |
| 50 Hz | 92.05（−0.65）| **92.65（−0.05）** | 92.70 | +0.60 dB |
| 100 Hz | 93.07（−0.63）| **93.66（−0.04）** | 93.70 | +0.59 dB |

**LCF/LCS 输出验证**：

| 频率 | LAF | LAS | LCF | LCS | LCSmax | 理论 (A / C) |
|------|-----|-----|-----|-----|--------|-------------|
| 20 Hz | 43.56 | 43.61 | 87.75 | 87.74 | 87.74 | 43.5 / 87.8 ✅ |
| 100 Hz | 74.88 | 74.86 | 93.64 | 93.65 | 93.65 | 74.9 / 93.7 ✅ |
| 1 kHz | 94.00 | 93.99 | 94.00 | 93.99 | 93.99 | 94.0 / 94.0 ✅ |

**结构体尺寸**：`SecondMetrics` 324 → **336 B**；`MinuteMetrics` 328 → **332 B**。

#### 验证

```
test_event_detector_v3.3.3:  22 passed, 0 failed
   D1 阈值分级 4/4  ✅
   D2 脉冲指标 4/4  ✅
   D3 上升率   3/3  ✅
   D4 背景对比 2/2  ✅
   边界/兼容/reset 9/9 ✅

全量测试: 9/9 通过（含新增 test_event_detector_v3.3.3）
```

#### 文件变更

- `include/event_detector.hpp`: `EventType` 枚举 + `EventResult` 结构 + 配置阈值 + `check_metrics()`
- `src/event_detector.cpp`: 四维度判定实现 + LAF 历史环形缓冲 + 背景指数平均 + 评分
- `include/noise_metrics.hpp`: `SecondMetrics` event_type/event_severity；`MinuteMetrics` 事件计数
- `src/noise_processor.cpp`: `aggregate_metrics()` 追加事件分级计数
- `tests/test_event_detector_v3.3.3.cpp`: 22 条单元测试
- `docs/DEVELOPMENT_PLAN_v3.3.3.md`: 开发计划

### v3.3.2 (2026-09-20) — 指数时间计权（IEC 61672-1 §7）+ LAF/LAS 输出

**Bug 修复**：PE-04 频段 20/25 Hz 低频 A 计权偏差（+25 dB）。

**根因**：
- A 计权段 2（20.6 Hz 双极点，|p|=0.9973）建立时间约 **7.7 s**
- 固件每 10 ms 报一个 LAeq 子累计再做能量平均 → 等效 **10 ms 积分窗口**
- 滤波器在 10 ms 内完全来不及进入稳态，RMS 捕获的是未衰减的瞬态信号
- 现象：20 Hz @ 94 dB SPL 时 LAeq 虚高 +25 dB（67.5 dBA vs IEC 42.4 dBA）
- 频率越高极点距单位圆越远，收敛越快 → 1 kHz 以上无此问题

**修复 1：接口二改用指数时间计权**（IEC 61672-1 §7）

```cpp
// 每样本更新
state = α · state + (1 − α) · y²
α = exp(−1 / (τ · fs))
//   Fast (τ=125ms) → LAF
//   Slow (τ=1s)    → LAS
```

- 新增 `SecondMetrics`: **LAF / LAS / LASmax**
- 新增 `MinuteMetrics`: **LASmax**
- 内存代价：6 × float32 = **24 字节**（比滑动窗口方案 192 KB 小 8000 倍）

**修复 2：滤波器状态跨调用持续保留**（v3.3.1 残留 bug）

- 删除 `process_segment()` 中每段的 `a_weight_chain_.reset()` / `c_weight_chain_.reset()`
- 新增 `NoiseProcessor::reset()` 供手动重新开始测量使用
- 原因：每段重置导致滤波器永远只有 1 s 建立时间，对 20 Hz（需 7.7 s）远远不够
- 与商用 Class 1 声级计行为一致（开机后滤波器状态连续运行）

**验证结果**：

| 测试项 | v3.3.1 | v3.3.2 | IEC 61672 理论 | 判定 |
|--------|--------|--------|----------------|------|
| 1 kHz 纯音 94 dB SPL | 94.00 | **94.00 dBA** | 94.00 | ✅ −0.00 dB |
| 20 Hz 纯音 94 dB SPL | ~67.7 | **43.72 dBA** | 43.50 | ✅ +0.22 dB |
| 25 Hz 纯音 94 dB SPL | — | **49.23 dBA** | 48.10 | ✅ +1.13 dB |
| 4 kHz 纯音 94 dB SPL | — | **94.92 dBA** | 94.90 | ✅ +0.02 dB |
| 260918 Pa-WAV 实录 | 67.49 dBA | **55.14 dBA** | — | 改善 12.35 dB |

**时间计权选型说明**：
- τ = 125 ms / 1 s 是 IEC 61672-1 §7.4 规定的标准值（容差 ±20%）
- 建议使用标准值以保证 Class 1 合规路径开放、与商用产品对齐
- 非合规应用可自定义 τ（需在文档中明确标注）

**验证方法学备注**：
- 评估低频 A 计权应使用**纯音合成信号**（单段长信号），不能用含宽带成分的实录 WAV
- 实录 WAV 的 `LAeq − LZeq` 比值取决于信号成分，不是滤波器性能的直接度量

**文件变更**：
- `include/noise_metrics.hpp`: LAF/LAS/LASmax 字段（82 个每秒指标）
- `include/noise_processor.hpp`: 6 个时间计权状态 + `reset()`
- `src/noise_processor.cpp`: 指数时间计权实现 + 滤波器状态连续性
- `tests/test_v3.3.2_laeq_validation.cpp`: 验证脚本
- `docs/DEVELOPMENT_PLAN_v3.3.2.md`: 开发计划（含 v1/v2/v3 三方案对比）

### v3.3.1 (2026-09-09) — 1/3 倍频程 b/a 系数 scipy 重生成

**Bug 修复**：v3.3.0 的 1/3 倍频程带通滤波器 b/a 系数用 `tools/generate_bandpass_coeffs.cpp` 的 6 行简化公式手算，**未做 gain normalization**——`peak_gain_correction` 数值跟 b/a 自洽，但跟实际 `BiquadFilter::process()` TDForm II 跑出来的真实响应差 ~$10^4$ 倍。

**现象**：
- 1 kHz 正弦 94 dB SPL 输入 → `freq_1khz_spl = 170.66 dB`（虚高 +76.66 dB）
- noise_meter_validation 报告 4 个频带自洽诊断全数爆雷（+68.78、+73.10、+78.26、+20~72 dB）
- `tests/test_noise_processor.cpp::test_frequency_bands` 断言只 `> 30 dB`，让 bug 蒙混过关

**修复**：
- 新增 `tools/regen_bandpass_coefficients.py`（8109 字节）：用 `scipy.signal.butter(1, [fc_low, fc_high], btype='band', fs=48000)` 完整 4 步 pipeline (zpk → lp2bp → bilinear → gain normalization)
- 覆盖 `include/bandpass_coefficients_48k.hpp`（9 套新系数）
- 保留旧头文件为 `bandpass_coefficients_48k.hpp.bak-v3.3.0`（1 周后清理）
- `tests/test_noise_processor.cpp` Test 3 收紧断言：`freq_1khz_spl > 30` → `[85, 100]` dB；新增相邻频段 ≥ 15 dB 衰减断言（2nd-order Butterworth 物理极限）

**实测验证**：

| 项 | v3.3.0 | v3.3.1 |
|----|--------|--------|
| `freq_1khz_spl` (94 dB 输入) | 170.66 dB | **93.99 dB** ✓ |
| `LAeq` (宽带) | 93.9987 dB | 93.9987 dB（不变）|
| 9 频段 `b0` 数量级 | $10^{-7}$ | $10^{-2}$（↑47000x）|
| 9 频段 `correction` | $2\text{e}5$ | $1.0$（↓200000x）|
| biquad 极点 \|p\| | ≈ 1.0（部分越界）| 0.78~1.00（稳定）|
| ctest 8/8 | PASSED | PASSED |

**API / ABI 兼容**：
- `BandpassCoeffs` 结构体字段顺序不变
- `noise_processor.cpp` 调用方代码不变（仅 correction 数值更新）
- 嵌入式 nRF54L15 可直接 drop-in 替换 v3.3.0

**端到端验证状态**：
⚠️ THIST100 设备 CSV 是固件层生成，与本仓库 C++ 库独立。当前 `compare.py` 验证继续显示频段 bug 是因为固件没升级——需要重新烧录固件并重录 CSV 才能确认 bug 在设备层也已修复。

详见 `docs/DEVELOPMENT_PLAN_v3.3.1.md` + `docs/THIRD_OCTAVE_BAND_CALCULATION.md`。

### v3.3.0 LZeq deprecate (2026-07-06)

- **LZeq>90 单帧触发阈值默认 INFINITY（禁用）**：`leq_threshold_db` 默认值从 90.0f 改为 INFINITY
- **接口三单触发推荐**：仅 LZpeak ≥ 140 dB → OVERLOAD（IMULSE_SUSPECT 不再由 LZeq 触发）
- **LOGIC 代码保留**：`src/event_detector.cpp` 中 LZeq 触发 if-block 以 `/* */` 注释块保留，未来可恢复
- **ABI 兼容**：`leq_threshold_db` 字段保留，`EventDetectorConfig` 结构体大小不变
- **恢复方法**：① 将 `include/event_detector.hpp` 中 `leq_threshold_db{INFINITY}` 改回 `leq_threshold_db{90.0f}`；② 取消 `src/event_detector.cpp` 中 LZeq 触发块的注释
- **测试更新**：IMPULSE_SUSPECT 相关 7 个测试替换为 3 个新测试（验证禁用行为、INFINITY 默认值、OVERLOAD 仍工作）
- **示例更新**：`examples/main.cpp` 中 leq_threshold/debounce/cooldown 注释标注为 "v3.3.0: no effect"

### v3.3.0 (2026-06-18) — Matched-z A/C 加权，IEC 61672-1 Class 1 实验级精度

**目标**：将 A/C 加权精度从 Class 2（±1.5 dB）提升至 Class 1（±0.7 dB），适用于实验室级精密测量和听力防护标准验证。

**技术路线**：matched-z 变换 + 2nd-order peaking EQ 修正
- `include/weighting_coefficients_matched_z_48k.hpp`：新 header，4 段 A 加权 biquad + 3 段 C 加权 biquad
  - Stage 1: matched-z 变换（`z_p = exp(s_p·T)`，极点频率精确无 warping）
  - Stage 2: 2nd-order RBJ peaking EQ（differential evolution 全局优化，代价函数 = 33 个 IEC 标准频率的最大绝对误差）
- `tools/regen_weighting_matched_z.py`：Python 设计脚本，输出 C++ constexpr 系数 + 全 33 频率验证表
- A-weighting max error = **0.49 dB** (Class 1 容差 ±0.7)；C-weighting max error = **0.18 dB**
- v3.2.1 bilinear 预存表同时保留（Class 2 工业级，3 段 biquad），用户按需选择

**新测试套件**：
- `tests/test_class1_precision.cpp`：扫 33 个 IEC 1/3 倍频程频率，对比 A/C 权加权误差（验证 Class 1 ±0.7 dB）
- `tests/test_end_to_end.cpp`：9 个端到端验证，合成已知信号检查 LAeq / LCeq / LZeq / Dose 是否物理正确
  - 1 kHz 94 dB 纯音 → LAeq=LCeq=LZeq=94.00 dB ✓
  - NIOSH dose 90 dB × 10s → 0.1102%（与公式严格自洽）✓
  - A 衰减检查：100 Hz -19.1 dB，10 kHz -2.5 dB ✓
  - LCeq ≥ LAeq（宽带噪声物理不变量）✓

**现场验证（THIST100.CSV）**：
嵌入式设备采集的 220 行 1 秒数据（v3.2 bug 固件）证实 bug：LAeq 系统性偏高 LZeq ~35 dB。v3.3.0 修正后预估 LAeq ≈ LZeq - 0.5~3 dB（典型工业宽带噪声 A 权惩罚）。

⚠️ **ABI break**：`BiquadChain<N>` 模板参数变化（A: 3→4，C: 2→3），v3.2.1 固件镜像不可复用，**需重新烧录**。系数选择在编译期完成，零运行时开销。

**嵌入式使用**：在 `noise_processor.cpp` 构造函数中切换 `#include` header 即可（系数均为 constexpr，零运行时开销）。

### v3.3.0 fix — LAPeak / LCPeak 补齐（接口一、二）

**Bug**：接口一 `process_segment()` 返回的 `SecondMetrics` 与接口二 `aggregate_metrics()` 返回的 `MinuteMetrics` 遗漏了 A/C 加权 peak level（LAPeak / 接口二 LCPeak）。

**实际现状（修复前）**：
- 接口一声级 6 个：`LAeq, LCeq, LZeq, LAFmax, LZPeak, LCPeak` — 缺 `LAPeak`
- 接口二 peak 2 个：`LAFmax, LZPeak` — 缺 `LAPeak` 与 `LCPeak`

**根因**：`process_segment()` 的 main loop 只 track 了 `peak_z` 与 `peak_c` 两个 peak；`aggregate_metrics()` 只 max 了 `LAFmax` 与 `LZPeak`。**未计算 LAPeak，也未在 MinuteMetrics 暴露 LCPeak**。

**影响**：
- 听损评估: 仅 `LAeq`（8h 等效连续声级）不足以描述峰值冲击；
- ISO 1999 / NIOSH 1998 标准推荐使用 `LAPeak` 作为计权峰值 (A-weighted Peak) 与 `LCPeak` 作为 C 加权峰值；
- 集成商/嵌入式使用者需要自己手算 (`LAPeak = 20·log10(max(a_buf)/ref_pressure)`)。

**修复 (v3.3.0 fix)**：
1. `SecondMetrics` 新增 `float LAPeak`（位置: 紧跟 `LCPeak` 后），字段计数 81 → 82。
2. `MinuteMetrics` 新增 `float LCPeak` + `float LAPeak`（peak 字段数 2 → 4），两者按 `max` 聚合。
3. `process_segment()` 在 main loop 加 `peak_a` 累加器，输出 `m.LAPeak = 20·log10(peak_a/ref_pressure)`。
4. `aggregate_metrics()` 在循环中 `result.LCPeak = max(...)` 与 `result.LAPeak = max(...)`。

**overload 事件判定说明**（本修复**不修改** overload 阈值，但增加注释）：
- 接口一/二的 `overload_flag`（即 `SecondMetrics.overload_flag` 和 `MinuteMetrics.overload_count`）以 `LZPeak > 140 dB` (`OVERLOAD_THRESHOLD`) 作为唯一判定依据。
- 这与 `EventDetector`（接口三）一致：`EventCheckResult::OVERLOAD = LZpeak >= peak_threshold_db (默认 140 dB)`。
- **`LZeq >= 90 dB` 不是 overload**，而是 `IMPULSE_SUSPECT` 的触发条件（需 `debounce_frames` 连续帧确认），由接口三内部状态维护，不暴露在接口一/二的输出中（避免重复判定逻辑）。
- 如使用者需要 `IMPULSE_SUSPECT` 标识，请调用接口三 (`EventDetector::check_segment()`) 而非接口一/二。

**测试**：
- 现有 `tests/test_noise_processor.cpp` 12 个测试不依赖 LAPeak/LCPeak，通过。
- 现有 `tests/test_event_detector.cpp` 14 个测试不受影响。
- `tests/test_end_to_end.cpp`：合成 1kHz 94dB 纯音 → LAPeak 应 = 94.00 dB（与 LAeq 相等），LCPeak = 94.00 dB。

**ABI 兼容**：
- 仅**新增字段**（末位追加，C++ POD 兼容布局），下游若按字段名访问则**向后兼容**。
- 若下游按 `sizeof(SecondMetrics)` 做内存布局（如 C 序列化、跨语言 FFI）会受影响，需重新计算偏移。
- 嵌入式工程师（nRF54L15）若需复用固件镜像请告知，固件版本同步升 v3.3.1。

### v3.2.1 (2026-06-18) — A/C 加权预存表归一化 bug 修复

**Bug 修复**：v3.2 引入的 7 采样率 A/C 加权预存表（`include/weighting_coefficients_multirate.hpp`）在归一化 1kHz 增益时错误地把 1/H_total(1kHz) 只乘到第一节 biquad 的 `b` 系数上，**破坏了滤波器响应形状**——10 kHz 处应衰减 ~82 dB，实际反而**放大 16-45 dB**，导致 LAeq 比 LZeq 系统性偏高 ~35 dB，dose% 偏差 3-4 个数量级。

**修复**：1kHz 归一化改为独立 `a_gain` / `c_gain` 因子（在 `WeightingTableEntry` 结构体里），在 biquad 链输出后单独乘一次，**不修改 biquad b/a 系数**。
- 新增 `tools/regen_weighting_coefficients.py`（scipy.signal.bilinear 重新生成 7 套系数）
- 动态生成路径 (`iir_filter.cpp::a/c_weighting_design`) 同样改为 out-param gain
- 新增 `tests/test_weighting_response.cpp`（5 个回归测试）
- `noise_processor.cpp` 在 biquad 链 process() 后乘 `a_weight_gain_` / `c_weight_gain_`

**验证**：
- 1 kHz 正弦波：LAeq = 91 dB（精确 0 dB 增益）across 7 采样率
- 100 Hz 正弦波：LAeq = 91 - 19.1 = 71.9 dB（IEC 61672 -19.1 dB 衰减）
- 白噪声端到端：LAeq - LZeq = -3.6 dB（A 加权正确衰减高频），不再是 v3.2 的 +35 dB
- NIOSH 90 dB × 1s：dose_frac = 1.10e-4（精确等于公式 (1/3600/8) × 2^(5/3)）

**已知限制**：bilinear transform 在 3 段 biquad 上对 fs/4 以上频率有 1-2 dB 误差，在 fs/2 附近可达 5-15 dB。fs=48000 时主要频段（100 Hz - 10 kHz）误差 < 1.3 dB，满足 IEC 61672 Class 2 容差；fs=22050 等低采样率 10 kHz 附近误差较大。如需 Class 1 严格合规，需高阶设计或 matched-z transform（不在 v3.2.1 范围内）。

### v3.2 (2026-06-04) — 频段 SPL 归一化 + 7 采样率 A/C 加权稳定化

**Bug A 修复：1/3 倍频程带通 SPL 归一化**
- `noise_processor.cpp` Phase 4：频段 RMS 乘以 `peak_gain_correction`（预存于 `bandpass_coefficients_48k.hpp::peak_gain_correction`）后再转 dB
- 1kHz 正弦波 → 1kHz 频段 SPL ≈ 94 dB，不再低 25 dB（修复前 61.7 dB）
- `tools/generate_bandpass_coeffs.cpp` 重写：计算每个频段的 `|H(fc)|`，生成 `peak_gain_correction = 1/|H(fc)|` 表
- 48kHz 修正系数范围：`2.07e5`（63Hz/1kHz）至 `3.43e5`（16kHz，接近 Nyquist 性能略降）

**Bug B 修复：7 个采样率 A/C 加权预存表（方案 C + A 兜底）**
- 新增 `include/weighting_coefficients_multirate.hpp`：8k/16k/22.05k/32k/44.1k/48k/96kHz × A 加权（3 段 biquad）× C 加权（2 段 biquad）预存系数
- `tools/gen_weighting_header.py`：使用 `scipy.signal.bilinear + tf2sos` 生成，**所有极点 |z| < 1.0**（已验证），1kHz 增益归一化为 0 dB（IEC 61672）
- `noise_processor.cpp`：构造函数改用 `find_weighting_entry()` 查表；表外采样率回退到 `filter_design::a/c_weighting_design()`（方案 A 兜底）
- 实测 7 采样率 1kHz 正弦波 @ 94 dB：LAeq 和 LCeq 均在 94 dB ± 0.2 dB 内，偏差来自 scipy bilin 舍入

**Phase 3 配套变更**
- `tests/test_noise_processor.cpp`：`test_sample_rates` 扩展为 7 采样率（8k/16k/22.05k/32k/44.1k/48k/96kHz），验证 A/C 加权非 NaN + LAeq 范围 + 采样率间偏差 < 1.5 dB
- README.md / AGENTS.md 更新（移除两条 known issues）

**向后兼容**：所有接口签名不变，`SecondMetrics` / `MinuteMetrics` 字段名不变（频段 SPL 值在 Bug A 修复后物理意义正确）

### v3.1.3 (2026-06-04) — 暴露 Dose% / TWA 换算接口

- 新增 `include/dose_state.hpp`，提供 `DoseState` POD（8 bytes）+ 4 个 inline 纯函数（`accumulate_dose_frac` / `dose_to_pct` / `dose_to_twa` / `dose_to_lex8h`）
- 4 个函数均为 `DoseCalculator` 已有方法的薄包装（`calculate_twa` / `calculate_lex`），算法层 0 改动
- 业务侧持有一个或多个 `DoseState`（每标准一个），库不持有跨调用状态
- `DoseStandard` 枚举自动选择 3dB / 5dB 交换率的 log10 系数（10.0 / 16.61），避免手算错误
- 新增 `tests/test_dose_state.cpp`（9 个测试，含 5dB 系数回归测试），CMake 子项目接入
- `examples/main.cpp` 新增 v3.1.3 demo（模拟 1 分钟 @ 90 dB 暴露，输出 4 标准 Dose%/TWA/LEX,8h）
- 向后兼容：v3.1.2 的 `process_segment` / `aggregate_metrics` / `EventDetector` / `DoseCalculator` 签名与行为均不变
- 不修改 `SecondMetrics` / `MinuteMetrics` 字段（TWA 是累积量，段级无意义）
- 详见 [`docs/DEVELOPMENT_PLAN_v3.1.3.md`](docs/DEVELOPMENT_PLAN_v3.1.3.md)

### v3.1.2 (2026-05-27) — 简化事件检测接口

- 新增接口三 `EventDetector::check_segment()`，返回 `EventCheckResult`（NORMAL / OVERLOAD / UNDERRANGE / IMPULSE_SUSPECT）
- 独立于 `NoiseProcessor`，零堆分配，实例约 28 bytes；输入为 Z 加权 Pa 样本
- 双触发：LZpeak ≥ `peak_threshold_db`（默认 140 dB）→ OVERLOAD；LZeq 连续 `debounce_frames` 帧超阈 → IMPULSE_SUSPECT
- 帧计数器去抖：`debounce_frames` + `cooldown_frames`（仅抑制声级重复触发，过载不受 cooldown 影响）
- 参数经 `EventDetectorConfig` 配置，默认值与 `noise_metrics.hpp` 中过载/欠量程阈值对齐
- 新增 `include/event_detector.hpp`、`src/event_detector.cpp`、`tests/test_event_detector.cpp`（14 个测试）
- 新增 `docs/DEVELOPMENT_PLAN_v3.1.2.md`；不保存音频、不计算 SEL、不做环形缓冲


### v3.1.1 (2026-05-14) — VLA 动态栈分配，修复嵌入式栈溢出

- 将 `process_segment()` 内硬编码的 `float a_buf_stack[48000]` / `float c_buf_stack[48000]` 替换为 C99 VLA（GCC extension）：`float a_buf[n]` / `float c_buf[n]`
- 栈 buffer 大小改为按实际传入的样本数 `n` 动态调整，零堆分配
- 嵌入式场景典型 10 ms block @ 48 kHz：栈使用从 ~375 KiB 降至 ~4 KiB
- 消除 `process_segment()` 在主线程上的栈溢出问题（嵌入式反馈已复现）
- GCC `-Wvla` 警告已知晓，嵌入式工具链可通过 `-Wno-vla` 抑制

### v3.1.0 (2026-05-11) — 流式架构 + 嵌入式编译兼容

**Phase 0: M_PI 替换**
- 新增 `include/math_constants.hpp`，定义 `noise_const::PI_F` / `TWO_PI_F`
- 替换所有源码中 9 处 `M_PI` 引用，兼容 Zephyr/picolibc/arm-zephyr-eabi 工具链

**Phase 1: 热路径零堆分配**
- `process_segment()` 内 A/C 计权改为 `BiquadChain::process()` 逐样本处理
- 新增 `IIRFilter::process_sample(float* data, size_t count)` 流式接口
- 新增 `apply_a_weighting_inplace()` / `apply_c_weighting_inplace()` 零堆分配接口
- 倍频带分析改为逐样本 `BiquadFilter::process()` + inline 累积 moments
- 消除 `process_segment()` 中全部 vector/new/malloc（从 ~26 次降至 0 次）

**Phase 2: 倍频带滤波器持久化**
- 新增 `include/bandpass_coefficients_48k.hpp`，9 个 1/3 倍频程带通系数预计算为 constexpr
- 新增 `tools/generate_bandpass_coeffs.cpp` 系数生成工具
- `NoiseProcessor` 构造时初始化 9 个 persistent `BiquadFilter`，不再每次调用重新设计
- `NoiseProcessor` 类大小: 528 bytes (0.5 KB)

### v3.0.0 — 初始 C++ 移植

- 从 Python noise_info_toolkit 移植为 C++17
- 两个核心接口: `process_segment()` + `aggregate_metrics()`
- 81 个每秒指标 + 聚合分钟指标
- `DoseCalculator` constexpr 表 + 枚举索引
- 预计算 A/C 计权 `BiquadChain`（48kHz）
