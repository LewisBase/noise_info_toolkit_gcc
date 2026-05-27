# v3.1.2 开发计划 — 简化事件检测接口

> 日期：2026-05-27
> 作者：蒙特卡洛
> 状态：草稿，待审阅

---

## 一、需求背景

### 1.1 Python 版本事件检测回顾

在 `noise_info_toolkit` (Python) 中，事件检测包含以下组件：

| 组件 | 功能 | 内存占用 |
|------|------|----------|
| `SlidingWindowCalculator` | 125ms 滑动窗口计算 LZeq_125 | ~4800 samples |
| `EventDetector` | 三种触发检测（声级/峰值/斜率）+ 去抖动 | ~10KB |
| `RingBuffer` | 12秒环形缓冲（2s pre-trigger + 8s post-trigger） | ~576KB @ 48kHz |
| `EventProcessor` | 整合检测+缓冲+音频保存 | 整体 ~600KB |

**触发条件**：
- 声级触发：LZeq_125 ≥ 90-95 dB（125ms 窗口）
- 峰值触发：LCpeak ≥ 130 dB
- 斜率触发：ΔLZeq ≥ 10 dB/50ms
- 去抖动：0.5s 间隔

### 1.2 嵌入式约束

嵌入式平台（nRF54L15）资源受限：
- **RAM**：~256KB（整个芯片）
- **栈空间**：有限（每个任务 ~4-8KB）
- **无堆分配**：热路径不能有动态内存分配

### 1.3 嵌入式工程师建议

> "咱们是否可以先简单一点，事件检测的接口返回的可以是异常类型或者无异常，如果存在异常，说明传递的那 100ms 数据量存在问题，直接将这一帧当作起始（这个标记我来做）"

**关键要点**：
1. 简化返回值：异常类型 / 无异常
2. 嵌入工程师负责标记"起始点"
3. 接口只需要判断是否存在异常
4. 不需要保存音频、计算 SEL、记录事件日志

---

## 二、设计目标

### 2.1 接口设计原则

```
返回值类型：EventCheckResult (enum)
  - NORMAL          // 无异常，正常通过
  - OVERLOAD        // 过载（LCpeak 超限）
  - UNDERRANGE      // 信号太弱（低于阈值）
  - IMPULSE_SUSPECT // 疑似冲击噪声（需要标记起始点）
```

### 2.2 简化策略

| Python 版特性 | 嵌入式简化版 | 说明 |
|--------------|-------------|------|
| 125ms 滑动窗口 | **10ms 固定窗口** | 直接复用 `process_segment()` 的 10ms 块 |
| 三种触发模式 | **双触发模式（峰值+声级）** | 峰值触发（LCpeak ≥ 阈值）返回 OVERLOAD；声级触发（LZeq ≥ 阈值）返回 IMPULSE_SUSPECT |
| 0.5s 去抖动 | **帧计数器去抖** | `debounce_frames`（连续 N 帧）+ `cooldown_frames`（冷却 N 帧），均通过配置控制 |
| 12s 环形缓冲 | **零缓冲** | 不保存音频，只返回检测结果 |
| 事件音频保存 | **无** | 嵌入式工程师自行处理 |
| SEL/LAE 计算 | **无** | 不计算事件级指标 |

### 2.3 目标指标

- 接口调用延迟：< 1ms
- 额外 RAM 占用：< 100 bytes
- 零堆分配：所有状态为栈变量或类成员

---

## 三、接口设计

### 3.1 新增头文件：`include/event_detector.hpp`

```cpp
#pragma once

#include <cstdint>

namespace noise_toolkit {

//==============================================================================
// Event Detection Result
//==============================================================================

enum class EventCheckResult : uint8_t {
    NORMAL = 0,        // 无异常
    OVERLOAD = 1,     // 过载（峰值超标）
    UNDERRANGE = 2,   // 信号太弱
    IMPULSE_SUSPECT = 3  // 疑似冲击噪声
};

//==============================================================================
// Event Detection Configuration
//==============================================================================

struct EventDetectorConfig {
    // 触发阈值
    float leq_threshold_db{90.0f};      // LZeq 触发阈值 (dB)
    float peak_threshold_db{130.0f};     // LCpeak 触发阈值 (dB)
    float underrange_threshold_db{30.0f}; // 信号太弱阈值 (dB)

    // 去抖动配置
    uint8_t debounce_frames{3};         // 连续 N 帧异常才触发
    uint8_t cooldown_frames{5};         // 触发后 N 帧内不重复触发

    // 参考声压
    float reference_pressure{20e-6f};

    EventDetectorConfig() = default;
};

//==============================================================================
// Event Detector
//==============================================================================

class EventDetector {
public:
    /**
     * @brief Constructor
     * @param config Event detection configuration
     * @param sample_rate Sample rate (default 48000)
     */
    explicit EventDetector(const EventDetectorConfig& config = EventDetectorConfig{},
                           int sample_rate = 48000) noexcept;

    /**
     * @brief Check if current segment has anomaly
     *
     * Lightweight interface: returns anomaly type or NORMAL.
     * Zero heap allocation, all state in class members.
     *
     * @param buffer_start Pointer to start of PCM buffer
     * @param buffer_end Pointer to end of PCM buffer
     * @return EventCheckResult: NORMAL or anomaly type
     */
    EventCheckResult check_segment(const float* buffer_start,
                                    const float* buffer_end) noexcept;

    /**
     * @brief Reset detector state
     */
    void reset() noexcept;

    /**
     * @brief Check if last segment was flagged as IMPULSE_SUSPECT
     */
    bool was_impulse_detected() const noexcept { return impulse_detected_; }

    /**
     * @brief Clear impulse flag (call after marking start point)
     */
    void clear_impulse_flag() noexcept { impulse_detected_ = false; }

private:
    EventDetectorConfig config_;
    int sample_rate_;

    // 去抖动计数器
    uint8_t consecutive_anomaly_count_{0};
    uint8_t cooldown_remaining_{0};

    // 峰值跟踪
    float peak_z_max_{0.0f};
    float peak_c_max_{0.0f};

    // 标志
    bool impulse_detected_{false};

    // 计算单帧 LZeq
    float compute_leq(const float* start, const float* end) noexcept;
    float compute_peak(const float* start, const float* end, bool is_c_weighted) noexcept;
};

//==============================================================================
// Inline Implementation
//==============================================================================

inline float EventDetector::compute_leq(const float* start, const float* end) noexcept {
    float sum_sq = 0.0f;
    int count = 0;
    for (const float* p = start; p < end; ++p) {
        sum_sq += (*p) * (*p);
        ++count;
    }
    if (count == 0) return 0.0f;
    float rms = sum_sq / count;
    if (rms <= 0.0f) return 0.0f;
    float p0 = config_.reference_pressure;
    return 20.0f * (rms > 0.0f ? log10f(rms) / (p0 * p0) : 0.0f);
    // Simplified: actual implementation needs sqrt
}

// Note: Full implementation in .cpp
```

### 3.2 与 NoiseProcessor 的集成方式

两种集成方案：

**方案 A：独立接口（推荐）**
```cpp
// 独立使用
EventDetector detector(config);
EventCheckResult result = detector.check_segment(buffer_start, buffer_end);
if (result == EventCheckResult::IMPULSE_SUSPECT) {
    // 嵌入式工程师标记起始点
}
```

**方案 B：嵌入 NoiseProcessor**
```cpp
class NoiseProcessor {
public:
    // 新增接口
    EventCheckResult process_and_check(const float* buffer_start,
                                        const float* buffer_end,
                                        float duration_s = 0.01f) noexcept;

    // 同时返回 metrics 和检查结果
    struct SegmentResult {
        SecondMetrics metrics;
        EventCheckResult event_status;
    };
    SegmentResult process_segment_with_check(...);
};
```

**方案选择**：采用**方案 A（独立接口）**，已确认。

**方案 A：独立接口** ✅ 确认采用
- 嵌入式工程师独立创建 `EventDetector` 实例
- 与 `NoiseProcessor` 完全解耦，职责单一
- 灵活决定何时调用检测（可以在 `process_segment()` 之前或之后）

```cpp
// 使用示例
NoiseProcessor processor(48000);
EventDetector detector(config);  // 独立实例

while (true) {
    auto samples = get_audio_block();
    
    // 1. 先检测事件（10ms 块）
    EventCheckResult result = detector.check_segment(samples.data(), samples.data() + samples.size());
    if (result != EventCheckResult::NORMAL) {
        // 嵌入式工程师标记起始点
    }
    
    // 2. 再处理指标（10ms 块）
    SecondMetrics metrics = processor.process_segment(samples.data(), samples.data() + samples.size(), 0.01f);
}
```

**方案 B：嵌入 NoiseProcessor** ❌ 不采用
- 不符合"职责单一"原则
- 增加 `NoiseProcessor` 复杂度
- 限制嵌入式工程师的灵活调用时机

**独立接口的优势**：
1. 保持 `NoiseProcessor` 纯指标计算职责
2. 嵌入式工程师可灵活决定检测与处理的调用顺序
3. 两者独立初始化、独立配置
4. 便于单独测试和替换

---

## 四、实现计划

### 4.1 文件变更

| 操作 | 文件路径 | 说明 |
|------|----------|------|
| **新增** | `include/event_detector.hpp` | 事件检测器类 |
| **新增** | `src/event_detector.cpp` | 实现 |
| **新增** | `tests/test_event_detector.cpp` | 单元测试 |
| **修改** | `include/noise_metrics.hpp` | 添加 `EventCheckResult` 枚举（如放在公共位置） |
| **修改** | `docs/DEVELOPMENT_PLAN_v3.1.2.md` | 本文档 |

### 4.2 实现步骤

#### Phase 1：核心检测逻辑（1天）

1. **定义数据结构**
   - `EventCheckResult` 枚举
   - `EventDetectorConfig` 配置结构

2. **实现 `EventDetector` 类**
   - 构造函数：初始化配置
   - `check_segment()`：计算 LZeq + LCpeak，判断异常
   - 去抖动逻辑：连续 N 帧 + 冷却期

3. **实现细节**
   ```cpp
   EventCheckResult EventDetector::check_segment(
       const float* buffer_start,
       const float* buffer_end) noexcept
   {
       // 1. 计算 LZeq (RMS -> dB)
       float lzeq = compute_segment_leq(buffer_start, buffer_end);

       // 2. 计算 LCpeak
       float lcpeak = compute_segment_peak_c(buffer_start, buffer_end);

       // 3. 冷却期检查
       if (cooldown_remaining_ > 0) {
           --cooldown_remaining_;
           return EventCheckResult::NORMAL;
       }

       // 4. 异常判断
       if (lcpeak >= config_.peak_threshold_db) {
           impulse_detected_ = true;
           cooldown_remaining_ = config_.cooldown_frames;
           return EventCheckResult::OVERLOAD;
       }

       if (lzeq >= config_.leq_threshold_db) {
           consecutive_anomaly_count_++;
           if (consecutive_anomaly_count_ >= config_.debounce_frames) {
               impulse_detected_ = true;
               cooldown_remaining_ = config_.cooldown_frames;
               consecutive_anomaly_count_ = 0;
               return EventCheckResult::IMPULSE_SUSPECT;
           }
       } else {
           consecutive_anomaly_count_ = 0;
       }

       return EventCheckResult::NORMAL;
   }
   ```

#### Phase 2：集成与测试（0.5天）

1. **编写单元测试**
   - 正常信号 → NORMAL
   - 过载信号 → OVERLOAD
   - 冲击噪声 → IMPULSE_SUSPECT
   - 去抖动验证（单帧触发不应返回 IMPULSE）

2. **与 Python 参考对比**
   - 使用 Python 版本的相同阈值测试
   - 验证检测结果一致性

#### Phase 3：文档更新（0.5天）

1. 更新 `README.md` 添加事件检测接口说明
2. 更新 `AGENTS.md` 记录 v3.1.2 变更

---

## 五、接口规格

### 5.1 `EventDetector` 类

```cpp
class EventDetector {
public:
    explicit EventDetector(const EventDetectorConfig& config = {},
                           int sample_rate = 48000) noexcept;

    // 主要接口：检测单帧是否异常
    EventCheckResult check_segment(const float* buffer_start,
                                    const float* buffer_end) noexcept;

    // 重置检测器状态
    void reset() noexcept;

    // 查询上次检测结果
    bool was_impulse_detected() const noexcept;
    void clear_impulse_flag() noexcept;

    // 配置访问（可选）
    const EventDetectorConfig& config() const { return config_; }
};
```

### 5.2 配置默认值与参数说明

```cpp
struct EventDetectorConfig {
    //=== 触发阈值（触发灵敏度控制）===
    float leq_threshold_db{90.0f};       // 声级触发阈值 (dB)
    float peak_threshold_db{130.0f};       // 峰值触发阈值 (dB)
    float underrange_threshold_db{30.0f}; // 信号太弱阈值 (dB)

    //=== 去抖动参数（防误报控制）===
    uint8_t debounce_frames{3};           // 连续 N 帧异常才触发（默认 3 帧）
    uint8_t cooldown_frames{5};            // 触发后 N 帧内不重复触发（默认 5 帧）

    //=== 参考声压 ===
    float reference_pressure{20e-6f};
};
```

**参数分类说明**：

| 类别 | 参数 | 作用 | 典型调整场景 |
|------|------|------|-------------|
| **触发阈值** | `leq_threshold_db` | 控制声级触发灵敏度 | 环境嘈杂时调高，环境安静时调低 |
| **触发阈值** | `peak_threshold_db` | 控制峰值触发灵敏度 | 检测高强度冲击噪声时调低 |
| **防误报** | `debounce_frames` | 连续 N 帧异常才算真事件 | 偶发噪声多时调大，稳定环境调小 |
| **防误报** | `cooldown_frames` | 触发后冷却 N 帧 | 事件密集时调大，避免重复触发 |

**配置示例**：
```cpp
// 高噪声环境（工厂车间）
EventDetectorConfig config;
config.leq_threshold_db = 95.0f;    // 提高阈值，减少误报
config.debounce_frames = 5;          // 增加连续帧数要求
config.cooldown_frames = 10;          // 延长冷却时间

// 低噪声环境（安静办公室）
config.leq_threshold_db = 80.0f;    // 降低阈值，捕捉轻微异常
config.debounce_frames = 2;          // 减少连续帧数要求
config.cooldown_frames = 3;          // 缩短冷却时间
```

**设计一致性**：所有检测行为参数（阈值 + 去抖计数器）均通过 `EventDetectorConfig` 统一配置，嵌入式工程师可在编译时或运行时根据实际场景调整，无需修改核心代码。

### 5.3 返回值含义

| 返回值 | 含义 | 嵌入式工程师操作 |
|--------|------|-----------------|
| `NORMAL` | 无异常，正常通过 | 无需操作 |
| `OVERLOAD` | 过载（峰值超标） | 标记帧起始点 |
| `UNDERRANGE` | 信号太弱（低于阈值） | 可能需要检查传感器 |
| `IMPULSE_SUSPECT` | 疑似冲击噪声 | 标记帧起始点 |

---

## 六、内存占用估算

| 组件 | 大小 | 说明 |
|------|------|------|
| `EventDetectorConfig` | ~16 bytes | 配置参数 |
| `EventDetector` 成员 | ~24 bytes | 计数器 + 峰值 + 标志 |
| **总计** | **~40 bytes** | 远低于嵌入式限制 |

---

## 七、测试用例

### 7.1 单元测试项

| 测试 | 输入 | 预期输出 |
|------|------|----------|
| 正常信号 | 48kHz 正弦波，LZeq=70dB | `NORMAL` |
| 过载信号 | 峰值 > 140dB | `OVERLOAD` |
| 冲击噪声 | LZeq=95dB 持续 100ms | `IMPULSE_SUSPECT` |
| 单帧噪声 | 仅1帧超阈值 | `NORMAL`（去抖动） |
| 连续触发 | 5帧连续超阈值 | 第一帧后返回 `IMPULSE_SUSPECT` |
| 冷却期 | 触发后紧接着的帧 | `NORMAL` |

### 7.2 对比验证

使用 Python 版本的相同音频数据，对比检测结果一致性。

---

## 八、风险与对策

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 简化触发漏检 | 冲击噪声未检测到 | 提供可配置阈值，嵌入式工程师可根据实际调整 |
| 去抖动参数不当 | 误报或漏报 | 默认 3 帧 debounce，可通过配置调整 |
| 精度损失 | float 计算误差 | 关键计算使用双精度内部变量，最终转 float |

---

## 九、验收标准

- [ ] `EventDetector` 类完整实现，零堆分配
- [ ] `check_segment()` 接口延迟 < 1ms
- [ ] 所有单元测试通过
- [ ] 默认配置下检测结果与 Python 版本一致
- [ ] 文档更新完成

---

## 十、后续扩展方向（v3.2+）

如需更完整的事件检测功能，可扩展：
1. **多级触发**：支持声级+峰值+斜率三种模式
2. **事件统计**：累计检测到的事件数量
3. **持续时间估算**：标记起始点后的持续时间
4. **环形缓冲集成**：与嵌入式工程师协作，添加音频缓冲功能

---

*文档版本：1.0 (草稿)*
*待审阅后更新为正式版本*