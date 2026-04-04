# noise_info_toolkit_gcc

噪声剂量计算工具包的C++实现，从Python版本移植而来。

> **项目来源**: 本项目由 Python 项目 [noise_info_toolkit](https://github.com/LewisBase/noise_info_toolkit) 移植而来，用于职业噪声暴露评估。

## 功能特性

- **噪声剂量计算**: 支持 NIOSH、OSHA_PEL、OSHA_HCA、EU_ISO 四种国际标准
- **声学指标计算**: Leq、LAeq、LCeq、Peak、TWA、LEX,8h 等
- **事件检测**: 脉冲噪声事件检测（支持LEQ/Peak/Slope三种触发模式）
- **时序处理**: 每秒时间历史数据处理和存储
- **音频处理**: WAV文件读写、1/3倍频程分析、A/C计权滤波
- **数据存储**: SQLite数据库存储（与Python版本兼容的表结构）

## 快速开始

### 构建项目

```bash
cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 独立验证程序

项目包含两个独立的验证程序（单文件，不依赖库）：

```bash
# 理论值验证
./dose_validator

# TDMS文件处理对比
./process_tdms_cpp
```

### 运行完整测试

```bash
# 运行所有测试
./noise_toolkit_example

# 运行特定测试
./noise_toolkit_example dose      # 剂量计算器
./noise_toolkit_example audio     # 音频处理器
./noise_toolkit_example time      # 时序处理器
./noise_toolkit_example buffer    # 环形缓冲区
./noise_toolkit_example database  # 数据库
```

## 使用示例

### 示例1: 剂量计算

```cpp
#include "noise_toolkit.hpp"

using namespace noise_toolkit;

int main() {
    // 创建剂量计算器（使用NIOSH标准）
    DoseCalculator calculator(DoseStandard::NIOSH);
    
    // 计算剂量
    double laeq = 85.0;  // dBA
    double duration = 3600.0;  // 秒
    
    auto result = calculator.calculate_dose(laeq, duration);
    
    std::cout << "Dose%: " << result.dose_percent << std::endl;
    std::cout << "TWA: " << result.twa << " dBA" << std::endl;
    std::cout << "LEX,8h: " << result.lex_8h << " dBA" << std::endl;
    
    return 0;
}
```

### 示例2: 音频处理

```cpp
#include "noise_toolkit.hpp"

using namespace noise_toolkit;

int main() {
    // 处理WAV文件
    AudioProcessor processor;
    
    auto metrics = processor.process_wav_file("input.wav");
    
    std::cout << "LAeq: " << metrics.laeq << " dBA" << std::endl;
    std::cout << "LCeq: " << metrics.lceq << " dBC" << std::endl;
    std::cout << "Peak: " << metrics.peak << " dB" << std::endl;
    
    return 0;
}
```

### 示例3: 事件检测

```cpp
#include "noise_toolkit.hpp"

using namespace noise_toolkit;

int main() {
    // 创建事件检测器
    EventDetector detector(EventTriggerMode::LEQ, 90.0);
    
    // 设置回调函数
    detector.on_event([](const Event& event) {
        std::cout << "事件触发时间: " << event.timestamp << std::endl;
        std::cout << "峰值: " << event.peak_level << " dB" << std::endl;
    });
    
    // 处理音频数据
    detector.process(samples, sample_rate);
    
    return 0;
}
```

### 示例4: 数据库操作

```cpp
#include "noise_toolkit.hpp"

using namespace noise_toolkit;

int main() {
    // 创建数据库连接
    Database db("measurement.db");
    db.initialize_tables();
    
    // 插入时间历史数据
    TimeHistoryRecord record;
    record.timestamp = std::time(nullptr);
    record.laeq = 82.5;
    record.dose_increment = 0.01;
    
    db.insert_time_history(record);
    
    // 查询数据
    auto records = db.get_time_history(start_time, end_time);
    
    return 0;
}
```

## 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_TESTS` | ON | 是否构建测试 |
| `BUILD_SHARED_LIBS` | ON | 构建动态库(.so)或静态库(.a) |

## 依赖项

- C++17 编译器
- CMake 3.14+
- SQLite3 开发库 (`libsqlite3-dev` 或 `sqlite-devel`)
- pthread

## 标准参数

| 标准 | 准则级 (dBA) | 交换率 (dB) | 参考时长 (h) | 说明 |
|------|-------------|-------------|-------------|------|
| NIOSH | 85 | 3 | 8 | 美国NIOSH标准 |
| OSHA_PEL | 90 | 5 | 8 | OSHA允许暴露限值 |
| OSHA_HCA | 85 | 5 | 8 | OSHA听力保护修正案 |
| EU_ISO | 85 | 3 | 8 | 欧盟/ISO标准 |

## 剂量计算公式

- **允许暴露时间**: `T = Tref / 2^((L - Lc) / ER)`
- **剂量计算**: `Dose% = 100 × (dt/Tref) × 2^((L-Lc)/ER)`
- **TWA**: 
  - NIOSH/ISO: `TWA = 10 × log10(Dose%/100) + Lc`
  - OSHA: `TWA = 16.61 × log10(Dose%/100) + Lc`
- **LEX,8h**: `LEX = 10 × log10(Dose%/100) + Lc`

## Python版本对比验证

使用 `validation/validate_dose_calculator.py` 对比Python和C++版本的计算结果：

```bash
python3 validation/validate_dose_calculator.py
```

| 标准 | 指标 | Python | C++ | 差异 |
|------|------|--------|-----|------|
| NIOSH | Dose% | 0.046840% | 0.046840% | 0 |
| NIOSH | TWA | 51.706172 dBA | 51.706172 dBA | 0 |

✅ 所有计算结果一致

## 文件结构

```
noise_info_toolkit_gcc/
├── include/              # 头文件目录
│   ├── noise_toolkit.hpp # 主入口头文件
│   ├── dose_calculator.hpp
│   ├── audio_processor.hpp
│   ├── signal_utils.hpp
│   ├── time_history_processor.hpp
│   ├── event_detector.hpp
│   ├── event_processor.hpp
│   ├── ring_buffer.hpp
│   ├── database.hpp
│   ├── wav_reader.hpp
│   ├── tdms_converter.hpp
│   └── iir_filter.hpp
├── src/                  # C++源文件
├── examples/             # 示例代码
│   └── main.cpp          # 完整功能示例
├── validation/           # 验证脚本（Python）
│   ├── validate_dose_calculator.py
│   └── process_tdms_python.py
├── doc/                  # 文档
├── build_test/           # 构建目录
├── dose_validator.cpp    # 独立验证程序
└── process_tdms_cpp.cpp  # TDMS处理验证程序
```

## 项目链接

- **C++ 版本（本项目）**: https://github.com/LewisBase/noise_info_toolkit_gcc
- **Python 原版**: https://github.com/LewisBase/noise_info_toolkit

## 许可证

待定 / 请参考原Python项目许可证
