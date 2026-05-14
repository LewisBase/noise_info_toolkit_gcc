# noise_info_toolkit_gcc

C++ 实现的轻量级噪声信息计算工具包（v3.1.1），从 Python 项目 [noise_info_toolkit](https://github.com/LewisBase/noise_info_toolkit) 移植而来。

## 设计目标

纯噪声信息计算，提供两个简洁接口，不包含任何存储、文件解析、事件检测等功能。
针对 nRF54L15 嵌入式平台优化：零动态内存分配、编译期常量滤波器系数、constexpr 剂量标准表、流式逐样本处理架构。
全链路单精度 `float` 计算，无 `double` 软浮点依赖。

## 两个核心接口

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
./test_noise_processor           # 单元测试（12个测试）
./noise_toolkit_example          # 示例程序
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
│   ├── noise_processor.hpp            # 主处理器（两个接口）—— v3.1 流式架构
│   ├── dose_calculator.hpp            # 剂量计算器（静态方法，constexpr 表，float）
│   ├── signal_utils.hpp               # 信号处理工具（含流式 weighting 接口）
│   ├── iir_filter.hpp                 # IIR 滤波器设计（含 process_sample 流式接口）
│   ├── filter_coefficients_48k.hpp    # 预计算 A/C 计权系数（48kHz）
│   ├── bandpass_coefficients_48k.hpp  # 预计算 1/3 倍频程带通系数（48kHz）—— NEW
│   ├── math_constants.hpp             # 数学常量（PI_F, TWO_PI_F）—— NEW
│   └── noise_toolkit.hpp              # 主入口
├── src/
│   ├── noise_processor.cpp            # 处理器实现 —— v3.1 流式，零堆分配
│   ├── dose_calculator.cpp            # 剂量计算实现（PC 字符串 API）
│   ├── signal_utils.cpp               # 信号处理实现（含 inplace weighting）
│   └── iir_filter.cpp                 # IIR 滤波器实现（含 process_sample）
├── tools/
│   └── generate_bandpass_coeffs.cpp   # bandpass 系数生成工具 —— NEW
├── tests/
│   └── test_noise_processor.cpp       # 单元测试（12 个测试）
├── examples/
│   └── main.cpp                       # 示例程序
├── dose_validator.cpp                 # 剂量计算验证（独立单文件）
├── validation/
│   └── validate_dose_calculator.py    # Python 对比验证脚本
├── docs/
│   ├── suggestion.md                  # 嵌入式工程师优化建议
│   ├── development_plan.md            # 开发计划
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
