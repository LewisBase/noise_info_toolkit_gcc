# noise_info_toolkit_gcc

C++ 实现的轻量级噪声信息计算工具包（v2.0），从 Python 项目 [noise_info_toolkit](https://github.com/LewisBase/noise_info_toolkit) 移植而来。

## 设计目标

纯噪声信息计算，提供两个简洁接口，不包含任何存储、文件解析、事件检测等功能。

## 两个核心接口

### 接口一：每秒调用 — `process_one_second(buffer_start, buffer_end)`

传入一秒音频原始数据指针（float 或 double），返回该秒所有 **81 个指标**：

```cpp
#include "noise_processor.hpp"

using namespace noise_toolkit;

NoiseProcessor processor(48000);  // sample_rate

// 传入 float 或 double 缓冲区指针
SecondMetrics m = processor.process_one_second(buffer_start, buffer_end);

// m 包含 81 个指标：
//   - 元数据: timestamp, duration_s
//   - 声级: LAeq, LCeq, LZeq, LAFmax, LZpeak, LCpeak
//   - 剂量: dose_frac_niosh/osha_pel/osha_hca/eu_iso
//   - QC: overload_flag, underrange_flag, wearing_state
//   - 峰度: kurtosis_total, kurtosis_a_weighted, kurtosis_c_weighted, beta_kurtosis
//   - 原始矩: n_samples, sum_x/s1, sum_x2/s2, sum_x3/s3, sum_x4/s4
//   - 1/3倍频程SPL: freq_63hz_spl ~ freq_16khz_spl (9个频段)
//   - 1/3倍频程矩S1-S4: 每个频段5个值 × 9个频段 = 45个字段
//     其中包括 n, s1, s2, s3, s4 (用于精确峰度合成)
```

### 接口二：每分钟调用 — `aggregate_minute_metrics(second_metrics_array, count)`

传入该分钟 60 个 `SecondMetrics`，返回聚合后的分钟指标 `MinuteMetrics`：

```cpp
std::array<SecondMetrics, 60> seconds;
for (int i = 0; i < 60; ++i) {
    seconds[i] = processor.process_one_second(...);
}

MinuteMetrics minute = processor.aggregate_minute_metrics(seconds);
```

## 构建

```bash
cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 运行测试

```bash
./test_noise_processor    # 10个单元测试（全部通过）
./noise_toolkit_example   # 示例程序
./dose_validator          # 剂量计算理论值验证
```

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

## 峰度计算（S1-S4 原始矩统计）

根据规范 4.X.3，使用原始矩统计量 S1-S4 跨时段精确合成峰度 β：

```
μ = S1 / n
m2 = S2/n - μ²
m4 = S4/n - 4μ·S3/n + 6μ²·S2/n - 3μ⁴
β = m4 / m2²
```

## 文件结构

```
noise_info_toolkit_gcc/
├── include/
│   ├── noise_metrics.hpp       # 核心数据结构（SecondMetrics, MinuteMetrics）
│   ├── noise_processor.hpp     # 主处理器（两个接口）
│   ├── dose_calculator.hpp     # 剂量计算器
│   ├── signal_utils.hpp         # 信号处理工具
│   ├── iir_filter.hpp          # IIR滤波器设计
│   └── noise_toolkit.hpp       # 主入口
├── src/
│   ├── noise_processor.cpp     # 处理器实现
│   ├── dose_calculator.cpp      # 剂量计算实现
│   ├── signal_utils.cpp         # 信号处理实现
│   └── iir_filter.cpp          # IIR滤波器实现
├── tests/
│   └── test_noise_processor.cpp # 单元测试（10个测试）
├── examples/
│   └── main.cpp                # 示例程序
├── dose_validator.cpp          # 剂量计算验证
└── CMakeLists.txt
```

## 依赖项

- C++17 编译器
- CMake 3.14+
- pthread

## 与 Python 版本对比

使用 `validation/validate_dose_calculator.py` 对比 Python 和 C++ 版本：

| 标准 | 指标 | Python | C++ | 差异 |
|------|------|--------|-----|------|
| NIOSH | Dose% | 0.046840% | 0.046840% | 0 |
| NIOSH | TWA | 51.706172 dBA | 51.706172 dBA | 0 |

✅ 所有计算结果一致

## 项目链接

- **C++ 版本（本项目）**: https://github.com/LewisBase/noise_info_toolkit_gcc
- **Python 原版**: https://github.com/LewisBase/noise_info_toolkit

## 许可证

待定 / 请参考原 Python 项目许可证
