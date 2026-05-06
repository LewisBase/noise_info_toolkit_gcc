# nRF54L15 嵌入式优化开发计划

> 基于 `docs/suggestion.md` 中嵌入式工程师建议，针对当前代码库现状制定

## 一、当前代码库现状分析

### 1.1 已完成的优化

| 项目 | 状态 | 说明 |
|------|------|------|
| float精度优化 | ✅ 已完成 | 热路径使用float，Dose计算保留double |
| 流式处理架构 | ✅ 已完成 | NoiseProcessor支持逐秒处理 |

### 1.2 待解决的问题

| 问题 | 当前状态 | 目标状态 | 优先级 |
|------|----------|----------|--------|
| vector动态分配 | 大量使用std::vector | std::array/固定缓冲 | P0 |
| 滤波器系数 | 运行期动态计算 | 编译期常量表 | P0 |
| map<string>查找 | DoseProfile用map存储 | enum+constexpr表 | P0 |
| 异常处理 | 3处throw | 返回码/状态码 | P1 |
| thread_local | signal_utils中使用 | 移除或重构 | P1 |
| alignas(64) | SecondMetrics使用 | 移除或改为4字节对齐 | P2 |
| 双构建系统 | 单一构建 | PC/嵌入式分离 | P1 |
| 滤波器重复初始化 | 每次调用重新设计 | 一次性初始化 | P0 |

---

## 二、开发阶段规划

### 第一阶段：核心数据结构重构（P0）

**目标**：消除动态内存分配，为嵌入式部署奠定基础

#### 2.1 DoseCalculator 重构

**当前问题**：
- `std::map<std::string, DoseProfile> profiles_` 使用红黑树+字符串键
- `DoseProfile` 包含 `std::string name/description` 成员
- `get_profile()` 返回值拷贝，包含堆分配字符串

**重构方案**：

```cpp
// dose_calculator.hpp

enum class DoseStandard : uint8_t {
    NIOSH = 0,
    OSHA_PEL = 1,
    OSHA_HCA = 2,
    EU_ISO = 3,
    COUNT = 4
};

struct DoseProfile {
    float criterion_level;    // dBA
    float exchange_rate;      // dB
    float threshold;          // dBA
    float reference_duration; // hours
};

// 编译期常量表
constexpr std::array<DoseProfile, static_cast<size_t>(DoseStandard::COUNT)>
DOSE_PROFILES = {{
    {85.0f, 3.0f, 80.0f, 8.0f},  // NIOSH
    {90.0f, 5.0f, 90.0f, 8.0f},  // OSHA_PEL
    {85.0f, 5.0f, 80.0f, 8.0f},  // OSHA_HCA
    {87.0f, 3.0f, 80.0f, 8.0f}   // EU_ISO
}};

class DoseCalculator {
public:
    // 返回引用而非拷贝
    static const DoseProfile& get_profile(DoseStandard standard);

    // 字符串解析仅用于配置阶段
    static DoseStandard parse_standard(const char* name);

    // 计算接口保持不变
    static double calculate_dose_increment(float laeq, float duration_s,
                                          const DoseProfile& profile);
private:
    // 移除 profiles_ map成员
};
```

**修改文件**：
- `include/dose_calculator.hpp`
- `src/dose_calculator.cpp`
- `src/noise_processor.cpp` (调用处)
- `examples/main.cpp`
- `tests/test_noise_processor.cpp`

**验收标准**：
- [ ] 编译通过，无std::map/std::string使用
- [ ] 所有单元测试通过
- [ ] dose_validator验证结果一致

---

#### 2.2 信号处理容器重构

**当前问题**：
- `signal_utils.hpp` 函数参数和返回值使用 `std::vector`
- `IIRCoefficients` 使用 `std::vector<float>` 存储系数
- `apply_a_weighting()` 等函数内部创建临时vector

**重构方案**：

```cpp
// iir_filter.hpp

// 固定阶数的IIR系数
template<size_t ORDER>
struct FixedIIRCoefficients {
    std::array<float, ORDER + 1> b;
    std::array<float, ORDER + 1> a;
};

// Biquad系数（已有，保持不变）
struct BiquadCoefficients {
    float b0, b1, b2, a0, a1, a2;
};

// 固定级数的Biquad链
template<size_t SECTIONS>
struct BiquadChain {
    std::array<BiquadCoefficients, SECTIONS> sections;
    std::array<float, SECTIONS * 2> state;  // 每级2个状态变量
};
```

```cpp
// signal_utils.hpp

// A/C计权滤波器 - 固定3级biquad
constexpr size_t A_WEIGHTING_SECTIONS = 3;
constexpr size_t C_WEIGHTING_SECTIONS = 3;

// 预计算的48kHz系数（编译期常量）
constexpr BiquadChain<A_WEIGHTING_SECTIONS> A_WEIGHT_48K = {
    // sections: {b0, b1, b2, a0, a1, a2} x 3
    // state: 全零初始化
};

constexpr BiquadChain<C_WEIGHTING_SECTIONS> C_WEIGHT_48K = {
    // 预计算系数
};

// 流式处理接口 - 无vector
class WeightingFilter {
public:
    void reset();
    float process_sample(float input);
private:
    BiquadChain<A_WEIGHTING_SECTIONS> chain_;
};

// 批量处理接口 - 使用span或指针+长度
void apply_a_weighting(const float* input, float* output, size_t length,
                       float sample_rate = 48000.0f);
```

**修改文件**：
- `include/iir_filter.hpp`
- `include/signal_utils.hpp`
- `src/iir_filter.cpp`
- `src/signal_utils.cpp`

**验收标准**：
- [ ] 热路径无vector创建
- [ ] A/C计权系数为编译期常量
- [ ] 滤波器状态可跨调用保持

---

#### 2.3 NoiseProcessor 输入接口重构

**当前问题**：
- `process_one_second()` 接受 `std::vector<float>` 参数
- 内部进行整秒数据处理

**重构方案**：

```cpp
// noise_processor.hpp

class NoiseProcessor {
public:
    // 流式接口 - 逐块处理
    void process_block(const float* samples, size_t count);

    // 每秒结束时获取结果
    SecondMetrics get_second_metrics() const;

    // 重置当前秒的累加器
    void reset_second_accumulator();

    // 原有接口保留用于PC端验证
    SecondMetrics process_one_second(const std::vector<float>& signal);

private:
    // 运行时累加器
    struct SecondAccumulator {
        uint32_t sample_count = 0;
        float sum_a_squared = 0.0f;
        float sum_c_squared = 0.0f;
        float sum_z_squared = 0.0f;
        float peak_c = 0.0f;
        float peak_z = 0.0f;
        // ... 其他统计量
    } accum_;

    // 滤波器实例（保持状态）
    WeightingFilter a_filter_;
    WeightingFilter c_filter_;
};
```

**修改文件**：
- `include/noise_processor.hpp`
- `src/noise_processor.cpp`

**验收标准**：
- [ ] 流式接口与整秒接口结果一致（误差<0.01dB）
- [ ] 无动态内存分配

---

### 第二阶段：构建系统分离（P1）

**目标**：实现PC端和嵌入式端双构建系统

#### 2.4 CMakeLists.txt 重构

```cmake
# 顶层选项
option(BUILD_EMBEDDED "Build for nRF54L15 target" OFF)
option(BUILD_PC_TOOLS "Build PC validation tools" ON)

# 嵌入式构建配置
if(BUILD_EMBEDDED)
    # 禁用异常
    add_compile_options(-fno-exceptions -fno-rtti)

    # 禁用动态分配相关的STL
    add_compile_definitions(
        NOISE_EMBEDDED_BUILD
        NO_EXCEPTIONS
        NO_RTTI
        NO_IOSTREAM
    )

    # 嵌入式专用源文件
    set(EMBEDDED_SOURCES
        src/dose_calculator.cpp
        src/signal_utils.cpp
        src/iir_filter.cpp
        src/noise_processor.cpp
    )

    add_library(noise_core STATIC ${EMBEDDED_SOURCES})
    target_compile_features(noise_core PRIVATE cxx_std_17)

# PC工具构建
else()
    # 完整C++特性
    add_library(noise_toolkit STATIC
        src/dose_calculator.cpp
        src/signal_utils.cpp
        src/iir_filter.cpp
        src/noise_processor.cpp
    )

    # 测试和示例
    if(BUILD_TESTS)
        add_executable(test_noise_processor tests/test_noise_processor.cpp)
        target_link_libraries(test_noise_processor noise_toolkit)
    endif()

    if(BUILD_PC_TOOLS)
        add_executable(dose_validator dose_validator.cpp)
        target_link_libraries(dose_validator noise_toolkit)
    endif()
endif()
```

#### 2.5 条件编译宏定义

```cpp
// noise_toolkit.hpp

#ifdef NOISE_EMBEDDED_BUILD
    // 嵌入式构建：禁用动态特性
    #define NOISE_NOEXCEPT noexcept
    #define NOISE_CONSTEXPR constexpr
#else
    // PC构建：允许完整特性
    #define NOISE_NOEXCEPT
    #define NOISE_CONSTEXPR
#endif

// 错误处理
#ifdef NO_EXCEPTIONS
    enum class NoiseError : uint8_t {
        Ok = 0,
        InvalidParameter,
        BufferOverrun,
        FilterNotInitialized
    };
    #define NOISE_RETURN_IF_ERROR(expr) \
        do { auto _err = (expr); if (_err != NoiseError::Ok) return _err; } while(0)
#else
    #define NOISE_RETURN_IF_ERROR(expr) expr
#endif
```

**修改文件**：
- `CMakeLists.txt`
- `include/noise_toolkit.hpp`
- 所有 `.hpp` 和 `.cpp` 文件（添加条件编译）

**验收标准**：
- [ ] `cmake -DBUILD_EMBEDDED=ON` 编译通过
- [ ] 嵌入式构建无exception/RTTI/iostream依赖
- [ ] PC构建功能完整

---

### 第三阶段：C++特性裁剪（P1）

**目标**：移除嵌入式不支持的C++特性

#### 2.6 异常处理移除

**当前异常位置**：
1. `iir_filter.cpp:17` - `throw std::invalid_argument`
2. `iir_filter.cpp:20` - `throw std::invalid_argument`
3. `dose_calculator.cpp:43` - `throw std::invalid_argument`

**重构方案**：

```cpp
// iir_filter.cpp - 修改前
if (b.empty()) {
    throw std::invalid_argument("Filter coefficients cannot be empty");
}

// iir_filter.cpp - 修改后
#ifdef NO_EXCEPTIONS
    if (b.empty()) {
        return;  // 或设置错误标志
    }
#else
    if (b.empty()) {
        throw std::invalid_argument("Filter coefficients cannot be empty");
    }
#endif
```

#### 2.7 thread_local 移除

**当前问题**：`signal_utils.cpp:85` 使用 `thread_local WeightedSignalProcessor g_processor`

**重构方案**：
- 方案A：改为类成员变量（推荐）
- 方案B：改为static变量（单线程场景）

```cpp
// 方案A：将滤波器状态移入NoiseProcessor类
class NoiseProcessor {
private:
    WeightedSignalProcessor weighted_processor_;
};
```

**修改文件**：
- `src/signal_utils.cpp`
- `src/iir_filter.cpp`
- `src/dose_calculator.cpp`
- `include/noise_processor.hpp`

**验收标准**：
- [ ] 嵌入式构建无throw/try/catch
- [ ] 嵌入式构建无thread_local
- [ ] 所有测试通过

---

### 第四阶段：内存对齐优化（P2）

#### 2.8 移除不必要的alignas

**当前问题**：`noise_metrics.hpp:79` 中 `struct alignas(64) SecondMetrics`

**修改方案**：

```cpp
// noise_metrics.hpp - 修改前
struct alignas(64) SecondMetrics { ... };

// noise_metrics.hpp - 修改后
struct SecondMetrics { ... };  // 自然对齐

// DMA缓冲区（如果未来添加）使用硬件要求的对齐
// alignas(4) static int32_t dma_buffer[BLOCK_SIZE];
```

**修改文件**：
- `include/noise_metrics.hpp`

**验收标准**：
- [ ] sizeof(SecondMetrics) 减小（无padding）
- [ ] 功能正常

---

### 第五阶段：滤波器系数固化（P0）

**目标**：将A/C计权滤波器系数预计算为编译期常量

#### 2.9 系数生成工具

创建PC端Python脚本生成C++常量：

```python
# tools/generate_filter_coeffs.py

import numpy as np
from scipy.signal import bilinear_zpk, sosfilt

def design_a_weighting_48k():
    """设计48kHz A计权滤波器系数"""
    # IEC 61672-1 标准极点
    f1 = 20.6
    f2 = 107.7
    f3 = 737.9
    f4 = 12194.2

    # ... 设计过程 ...

    return sos  # Second-Order Sections

def generate_cpp_header(sos_a, sos_c, output_path):
    """生成C++头文件"""
    with open(output_path, 'w') as f:
        f.write("// Auto-generated filter coefficients\n")
        f.write("// Target: 48kHz sample rate\n\n")
        f.write("constexpr BiquadChain<3> A_WEIGHT_48K = {{\n")
        # ... 写入系数 ...
        f.write("}};\n")

if __name__ == "__main__":
    sos_a = design_a_weighting_48k()
    sos_c = design_c_weighting_48k()
    generate_cpp_header(sos_a, sos_c, "include/filter_coefficients_48k.hpp")
```

#### 2.10 验证流程

```bash
# 1. 生成系数
python3 tools/generate_filter_coeffs.py

# 2. 编译PC版本
mkdir build_pc && cd build_pc
cmake .. -DBUILD_PC_TOOLS=ON
make

# 3. 验证频率响应
./dose_validator --verify-filter-response

# 4. 与Python参考对比
python3 validation/validate_filter_response.py
```

**新增文件**：
- `tools/generate_filter_coeffs.py`
- `include/filter_coefficients_48k.hpp`

**修改文件**：
- `include/signal_utils.hpp`
- `src/signal_utils.cpp`
- `include/iir_filter.hpp`

**验收标准**：
- [ ] A计权频率响应符合IEC 61672-1 Class 2容差
- [ ] C计权频率响应符合标准
- [ ] 运行期无滤波器设计计算

---

### 第六阶段：60秒峰度窗口实现（P1）

**目标**：实现可合成的每秒统计量存储

#### 2.11 SecondMetrics 扩展

```cpp
// noise_metrics.hpp

struct SecondMetrics {
    // 原有字段
    float laeq_1s;
    float lceq_1s;
    float lzeq_1s;
    float lcpeak_1s;
    float lzpeak_1s;

    // 新增：峰度统计分量
    float kurtosis_s0;  // 样本数
    float kurtosis_s1;  // Σx
    float kurtosis_s2;  // Σx²
    float kurtosis_s3;  // Σx³
    float kurtosis_s4;  // Σx⁴

    // 状态标志
    uint32_t flags;
};

// 60秒环形缓冲区
template<size_t SECONDS = 60>
class MinuteRingBuffer {
public:
    void push(const SecondMetrics& metrics);
    const SecondMetrics& oldest() const;
    const SecondMetrics& newest() const;

    // 合成60秒峰度
    float compute_kurtosis_60s() const;

    // 合成60秒Leq
    float compute_leq_60s() const;

private:
    std::array<SecondMetrics, SECONDS> buffer_;
    size_t head_ = 0;
    size_t count_ = 0;
};
```

**修改文件**：
- `include/noise_metrics.hpp`
- `src/noise_processor.cpp`

**验收标准**：
- [ ] 60秒峰度与整分钟计算结果一致
- [ ] 内存占用 < 1KB

---

## 三、测试计划

### 3.1 单元测试扩展

```cpp
// tests/test_embedded.cpp

TEST_CASE("DoseCalculator enum lookup") {
    const auto& profile = DoseCalculator::get_profile(DoseStandard::NIOSH);
    REQUIRE(profile.criterion_level == 85.0f);
}

TEST_CASE("BiquadChain compile-time constants") {
    // 验证系数非零
    REQUIRE(A_WEIGHT_48K.sections[0].b0 != 0.0f);
}

TEST_CASE("Streaming vs batch consistency") {
    // 对比流式和批量处理结果
    std::vector<float> signal(48000, 0.5f);

    NoiseProcessor batch_processor(48000);
    auto batch_result = batch_processor.process_one_second(signal);

    NoiseProcessor stream_processor(48000);
    for (size_t i = 0; i < 48000; i += 480) {
        stream_processor.process_block(&signal[i], 480);
    }
    auto stream_result = stream_processor.get_second_metrics();

    REQUIRE(std::abs(batch_result.laeq_1s - stream_result.laeq_1s) < 0.01f);
}
```

### 3.2 集成测试

```bash
# 运行完整验证套件
./build_test/test_noise_processor

# 对比Python参考实现
python3 validation/validate_all.py --tolerance 0.1
```

### 3.3 内存测试

```bash
# Valgrind内存检查
valgrind --leak-check=full ./build_test/test_noise_processor

# 嵌入式构建静态分析
cmake -DBUILD_EMBEDDED=ON -DCMAKE_CXX_CLANG_TIDY=clang-tidy ..
make
```

---

## 四、时间线

| 阶段 | 任务 | 预计工时 | 依赖 |
|------|------|----------|------|
| **P0-1** | DoseCalculator重构 | 2天 | - |
| **P0-2** | 信号处理容器重构 | 3天 | - |
| **P0-3** | 滤波器系数固化 | 2天 | P0-2 |
| **P0-4** | NoiseProcessor流式接口 | 2天 | P0-2 |
| **P1-1** | CMake双构建系统 | 1天 | P0-1~4 |
| **P1-2** | 异常/thread_local移除 | 1天 | P1-1 |
| **P1-3** | 60秒峰度窗口 | 1天 | P0-4 |
| **P2-1** | alignas优化 | 0.5天 | - |
| **P2-2** | 完整测试验证 | 1天 | 全部 |

**总计**：约13.5天

---

## 五、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 滤波器系数精度不足 | A/C计权响应偏差 | 与Python参考对比，误差<0.1dB |
| 流式处理累积误差 | 长时间运行结果漂移 | 使用Kahan累加或double中间值 |
| 嵌入式构建链接失败 | 无法部署到nRF54L15 | 提前在Cortex-M33工具链测试 |
| 现有测试覆盖不足 | 重构引入bug | 先补充测试再重构 |

---

## 六、验收标准汇总

### 功能验收
- [ ] 所有现有测试通过
- [ ] dose_validator结果与Python版本一致（误差<0.01%）
- [ ] A/C计权频率响应符合IEC 61672-1

### 性能验收
- [ ] 嵌入式构建无动态内存分配（heap usage = 0）
- [ ] 单秒处理延迟 < 10ms（Cortex-M33 @128MHz）
- [ ] 算法核心RAM占用 < 10KB

### 代码质量验收
- [ ] 嵌入式构建无exception/RTTI/iostream依赖
- [ ] 静态分析无警告
- [ ] 代码覆盖率 > 80%

---

*文档版本：1.0*
*创建日期：2026-05-06*
*基于：docs/suggestion.md + 代码库现状分析*
