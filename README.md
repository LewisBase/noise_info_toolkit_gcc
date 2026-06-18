# noise_info_toolkit_gcc

C++ 实现的轻量级噪声信息计算工具包（**v3.2.1**），从 Python 项目 [noise_info_toolkit](https://github.com/LewisBase/noise_info_toolkit) 移植而来。

## 设计目标

纯噪声信息计算，提供三个简洁接口，不包含任何存储、文件解析等功能。
针对 nRF54L15 嵌入式平台优化：零动态内存分配、编译期常量滤波器系数、constexpr 剂量标准表、流式逐样本处理架构。
全链路单精度 `float` 计算，无 `double` 软浮点依赖。

## 三个核心接口

### 接口一：逐段调用 — `process_segment(buffer_start, buffer_end, duration_s)`

传入音频缓冲区指针（float）和时长（秒），返回该段所有 **81 个指标**：

```cpp
#include "noise_processor.hpp"

using namespace noise_toolkit;

NoiseProcessor processor(48000);  // sample_rate

// 传入 float 缓冲区指针和时长
SecondMetrics m = processor.process_segment(buffer_start, buffer_end, 1.0f);

// m 包含 81 个指标：
//   - 元数据: timestamp, duration_s
//   - 声级: LAeq, LCeq, LZeq, LAFmax, LZpeak, LCpeak
//   - 剂量: dose_frac_niosh/osha_pel/osha_hca/eu_iso
//   - QC: overload_flag, underrange_flag, wearing_state
//   - 峰度: kurtosis_total, kurtosis_a_weighted, kurtosis_c_weighted, beta_kurtosis
//   - 原始矩: n_samples, sum_x/s1, sum_x2/s2, sum_x3/s3, sum_x4/s4
//   - 1/3倍频程SPL: freq_63hz_spl ~ freq_16khz_spl (9个频段)
//   - 1/3倍频程矩S1-S4: 每个频段5个值 × 9个频段 = 45个字段
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
ctest                            # 运行全部测试
./test_noise_processor           # 噪声指标单元测试（12个测试）
./test_event_detector            # 事件检测单元测试（14个测试）
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

## 指标列表（81个每秒指标）

| 类别 | 数量 | 字段 |
|------|------|------|
| 元数据 | 2 | timestamp, duration_s |
| 声级 | 6 | LAeq, LCeq, LZeq, LAFmax, LZpeak, LCpeak |
| 剂量增量 | 4 | dose_frac_niosh, dose_frac_osha_pel, dose_frac_osha_hca, dose_frac_eu_iso |
| 质量控制 | 3 | overload_flag, underrange_flag, wearing_state |
| 峰度 | 4 | kurtosis_total, kurtosis_a_weighted, kurtosis_c_weighted, beta_kurtosis |
| 原始矩统计量 | 5 | n_samples, sum_x, sum_x2, sum_x3, sum_x4 |
| 1/3倍频程SPL | 9 | freq_63hz_spl ~ freq_16khz_spl |
| 1/3倍频程矩S1-S4 | 45 | 每个频段(n,s1,s2,s3,s4) × 9个频段 |
| **合计** | **81** | |

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
│   ├── filter_coefficients_48k.hpp    # 预计算 A/C 计权系数（48kHz）
│   ├── bandpass_coefficients_48k.hpp  # 预计算 1/3 倍频程带通系数（48kHz）
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
│   └── generate_bandpass_coeffs.cpp   # bandpass 系数生成工具
├── tests/
│   ├── test_noise_processor.cpp       # 噪声指标单元测试（12 个测试）
│   └── test_event_detector.cpp       # 事件检测单元测试（14 个测试）
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
