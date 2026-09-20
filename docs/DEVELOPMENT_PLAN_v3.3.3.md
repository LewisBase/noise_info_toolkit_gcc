# v3.3.3 开发计划 — 事件检测算法升级为基于 LAF/LAS

> 日期：2026-09-20
> 作者：蒙特卡洛
> 状态：**已实施（2026-09-20）** — 全量测试 9/9 通过，22 条 v3.3.3 单元测试全过
> 前置版本：v3.3.2（接口二指数时间计权 + LAF/LAS 输出）
> 范围：**仅升级事件检测算法**，不改动 A/C/Z 加权系数、不动滤波器
> 关联文档：`docs/DEVELOPMENT_PLAN_v3.3.2.md`（指数时间计权实施）

---

## 零、决策记录（2026-09-20 v3.3.3 已实施）

| # | 决策项 | 实际选项 | 说明 |
|---|--------|---------|------|
| 0 | 事件检测触发指标 | ✓ **基于 LAF**（τ=125ms） | 四维度联合判定 |
| 1 | 事件分级方法 | ✓ **多维联合 + 三档分级** | MINOR/MODERATE/SEVERE |
| 2 | 时间常数 τ | ✓ **复用 v3.3.2 的 IEC 标准值** | Fast=125ms, Slow=1s |
| 3 | 与现有 EventDetector 集成 | ✓ **新增 `check_metrics()`，旧 `check_segment()` 保留** | 向后兼容 |
| 4 | 内存增量 | ✓ **约 152 字节**（LAF 历史 128B + 背景/计数 24B） | 低于计划上限 220B |
| 5 | CSV 输出字段 | ✓ **SecondMetrics +5 字段（含 LCF/LCS/LCSmax）；MinuteMetrics +4 计数** | 调用方负责回写 |
| 6 | 与 NoiseProcessor 耦合 | ✓ **保持解耦**（EventDetector 独立接口） | 非计划中的「接口三」 |
| 7 | 设备侧接口形式 | ✓ **新增 buf/end 重载**（设备只能传 buf/end） | 仅 metrics 重载不可用 |
| 8 | C 计权是否同步 | ✓ **是（方案 C）**：补 LCF/LCS/LCSmax，删除 Z 的时间计权状态 | 删死代码保持精简 |

---

## 一、问题背景

### 1.1 当前事件检测的局限

当前 `noise_processor.cpp` 中的事件检测主要依赖 **LZPeak > 140 dB**（过载标志）和 **wearing_state**（基于 LAeq 阈值）。这些指标对以下场景不够：

| 场景 | 当前检测能力 | 问题 |
|------|------------|------|
| **工业噪声合规**（85 dB 阈值） | ❌ 无 | LAeq 受 10ms 窗口影响（v3.3.1 bug，v3.3.2 修复） |
| **冲击噪声**（撞击、爆炸） | ⚠️ 仅 LZPeak | 不区分瞬时冲击与持续噪声 |
| **噪声水平突变**（机器启停） | ⚠️ 无 | 无法快速检测 |
| **脉冲指标**（LAF-LAS 差值） | ❌ 无 | 商用标准方法，缺失 |
| **事件分级**（轻微/中度/严重） | ❌ 仅过载 2 档 | 粒度太粗 |

### 1.2 商用声级计的事件检测标准

IEC 61672 + 商用产品（科赛乐 Optimus+、爱华 AWA、B&K）的事件检测通用方法：

```cpp
// 方法 1：LAF 阈值触发（最基本）
if (LAF > threshold_high) {
    event_detected = true;
}

// 方法 2：脉冲指标（LAF vs LAS 差值）
// 商用标准: LAFmax - LASmin > 6 dB 视为脉冲事件
impulse_metric = LAF_max_last_1s - LAS_max_last_1s;
if (impulse_metric > 6.0f) {
    impulse_event = true;
}

// 方法 3：LAF 上升率（event onset 检测）
LAF_rise = LAF_now - LAF_500ms_ago;
if (LAF_rise > 10.0f) {
    event_onset = true;
}

// 方法 4：背景对比
LAF_vs_background = LAF_max_event - LAeq_last_5min;
if (LAF_vs_background > 15.0f) {
    significant_event = true;
}
```

### 1.3 v3.3.2 已打好的基础

v3.3.2 已实现指数时间计权，每秒输出：

```
LAF_dB  — A 加权 Fast (τ=125ms) 时间加权 SPL
LAS_dB  — A 加权 Slow (τ=1s)  时间加权 SPL
LAFmax, LASmax — 上一窗口最大值
```

**v3.3.3 只做"如何基于 LAF/LAS 判定事件"的算法**，不改动滤波器、加权、时间计权本身。

---

## 二、方案设计

### 2.1 核心思路

**基于 LAF 的事件检测** + **基于 LAF-LAS 差值的脉冲指标** + **基于 LAF 上升率的 onset 检测**：

```
v3.3.1 事件检测（基于 LApeak / LAeq）:
  - 仅过载检测（LApeak > 140 dB）
  - 单维度

v3.3.3 事件检测（基于 LAF / LAS）:
  - 多维度：LAF 阈值 + 脉冲指标 + 上升率
  - 多档分级：轻微 / 中度 / 严重
```

### 2.2 事件检测四维架构

| 维度 | 触发指标 | 阈值（建议） | 响应时间 | 适用场景 |
|------|---------|------------|---------|---------|
| **D1: LAF 阈值** | LAF > threshold_high | 85 / 95 / 110 dB 三档 | 125 ms | 工业噪声合规、机器启动检测 |
| **D2: 脉冲指标** | LAFmax(t) - LASmax(t) | > 6 dB | 1 s | 冲击噪声、瞬态事件 |
| **D3: LAF 上升率** | LAF(t) - LAF(t-500ms) | > 10 dB | 500 ms | 事件起点检测 |
| **D4: 背景对比** | LAFmax - LAeq(5min 背景) | > 15 dB | 5 s | 显著事件判定 |

**最终事件分级**：

```
event_type =
  if any(LAF > 110 dB OR LAF-LAS > 12 dB)        → "severe"
  elif any(LAF > 95 dB OR LAF rise > 15 dB)       → "moderate"
  elif any(LAF > 85 dB OR LAF rise > 10 dB)       → "minor"
  else                                             → "none"
```

### 2.3 关键算法

#### 2.3.1 LAF 阈值判定（D1）

```cpp
// 工业噪声合规（NIOSH 85 dB / OSHA 90 dB / 欧盟 85 dB）
struct EventThresholds {
    float laf_minor_dB   = 85.0f;   // 轻微事件
    float laf_moderate_dB = 95.0f;   // 中度事件
    float laf_severe_dB   = 110.0f;  // 严重事件（听力损伤风险）
};

EventType detect_event_laf_threshold(float laf_now,
                                      const EventThresholds& thr) noexcept {
    if (laf_now >= thr.laf_severe_dB)   return EventType::SEVERE;
    if (laf_now >= thr.laf_moderate_dB) return EventType::MODERATE;
    if (laf_now >= thr.laf_minor_dB)   return EventType::MINOR;
    return EventType::NONE;
}
```

#### 2.3.2 脉冲指标（D2）

```cpp
// 跟踪过去 1 秒内 LAF 和 LAS 的最大值
struct ImpulseTracker {
    float laf_max_1s{0.0f};
    float las_max_1s{0.0f};
    // ... update logic
};

float compute_impulse_metric(const ImpulseTracker& t) noexcept {
    return t.laf_max_1s - t.las_max_1s;   // 单位 dB
}

// IEC 61672 + 商用标准：> 6 dB 视为脉冲事件
bool detect_impulse_event(const ImpulseTracker& t) noexcept {
    return compute_impulse_metric(t) > 6.0f;
}
```

#### 2.3.3 LAF 上升率（D3）

```cpp
// 跟踪 LAF 在 500ms 前的值
struct LafRiseTracker {
    float laf_now{0.0f};
    float laf_500ms_ago{0.0f};
};

float compute_laf_rise(const LafRiseTracker& t) noexcept {
    return t.laf_now - t.laf_500ms_ago;
}

// 典型阈值：10 dB 上升视为事件起点
bool detect_event_onset(const LafRiseTracker& t) noexcept {
    return compute_laf_rise(t) > 10.0f;
}
```

#### 2.3.4 背景对比（D4）

```cpp
// 需要 5 分钟 LAeq 滑动平均
struct BackgroundTracker {
    float laeq_5min_avg{0.0f};
};

bool detect_significant_event(float laf_now, const BackgroundTracker& bg) noexcept {
    return (laf_now - bg.laeq_5min_avg) > 15.0f;
}
```

### 2.4 事件检测器数据流

```
每秒 1 Hz 处理:
  ┌─────────────────────────────────────────────────────────┐
  │ NoiseProcessor::process_segment()                         │
  │   ↓ 输出 SecondMetrics{ LAF, LAS, LAeq, ... }            │
  └─────────────────────────────────────────────────────────┘
       ↓
  ┌─────────────────────────────────────────────────────────┐
  │ EventDetector::update(second_metrics)                     │
  │   - 更新 LAF/LAS 滑窗（ImpulseTracker）                │
  │   - 更新 LAF 上升率（LafRiseTracker）                   │
  │   - 更新 LAeq 5min 背景（BackgroundTracker）            │
  │   - 计算事件类型（多维度联合判定）                       │
  │   - 输出 EventResult{ event_type, event_flag }            │
  └─────────────────────────────────────────────────────────┘
       ↓
  ┌─────────────────────────────────────────────────────────┐
  │ MinuteMetrics::event_count[] 累计 + EventDetector log    │
  └─────────────────────────────────────────────────────────┘
```

### 2.5 内存代价

| 项 | 大小 | 说明 |
|----|------|------|
| LAF/LAS 滑窗（1 秒）| 192 字节 | 仅跟踪 1 秒内的 LAF/LAS max |
| LAF 上升率状态 | 8 字节 | laf_now + laf_500ms_ago |
| LAeq 5min 背景 | 192 字节 | 5min × 1Hz = 300 样本？不需要，用指数平均 |
| 实际 LAeq 5min 指数状态 | 4 字节 | state = α_5min·state + (1-α)·laeq |
| 阈值表 | 12 字节 | 3 个 float |
| 事件类型计数器 | 12 字节 | 3 个 int（minor/moderate/severe 计数）|
| **总增量** | **< 200 字节** | 比 v3.3.2 的 24 字节略多 |

nRF54L15 RAM 256 KB 几乎无压力。

### 2.6 与现有 EventDetector 的关系

```
现有 EventDetector（v3.3.1）:
  - 输入: LApeak, LAeq
  - 输出: overload_flag, underrange_flag, wearing_state
  - 算法: 单阈值

v3.3.3 EventDetector（重构）:
  - 输入: LAF, LAS, LAeq, LApeak（保持兼容）
  - 输出: overload_flag, underrange_flag, wearing_state（保持兼容）
        + event_type (新增), event_severity (新增)
  - 算法: 多维度 LAF/LAS 判定
```

**兼容性**：保留现有 3 个 flag（过载、欠载、佩戴），新增 2 个字段（事件类型、严重程度）。下游消费者无需修改。

---

## 四、改动范围

### 4.1 修改文件

| 文件 | 修改内容 |
|------|---------|
| `include/event_detector.hpp` | 新增 `EventType` 枚举、`EventResult` 结构、新阈值参数 |
| `src/event_detector.cpp` | 实现基于 LAF/LAS 的多维度判定 |
| `include/noise_metrics.hpp` | `SecondMetrics` 新增 `event_type` / `event_severity` 字段；`MinuteMetrics` 新增 `event_minor_count` / `event_moderate_count` / `event_severe_count` |
| `src/noise_processor.cpp` | 在 `aggregate_metrics()` 末尾追加事件检测聚合 |
| 单元测试 | 新增 `tests/test_event_detector_v3.3.3.cpp`（LAF 阈值 / 脉冲指标 / 上升率 4 维度） |

### 4.2 内存代价汇总

| 项 | 大小 | 备注 |
|----|------|------|
| EventDetector 状态字段 | ~200 字节 | LAF/LAS 滑窗 + 上升率 + 5min 背景 |
| SecondMetrics 新增字段 | 2 字节（enum）+ 1 字节（severity）| 兼容性：与现有字段对齐 |
| MinuteMetrics 新增计数 | 3 × int32 = 12 字节 | minor/moderate/severe |
| **总增量** | **< 220 字节** | nRF54L15 几乎无压力 |

### 4.3 CSV 输出格式兼容性

**保持原样 + 新增**：
- 现有字段名/单位不变
- 新增 `event_type` / `event_severity` 字段（不影响旧消费者）
- 每秒报告一次（与现有 1 Hz 输出一致）

---

## 五、测试与验证

### 5.1 单元测试（C++）

```cpp
TEST(EventDetector, LAFThreshold) {
    EventDetector ed;
    EventThresholds thr;  // 默认 85 / 95 / 110 dB
    
    SecondMetrics m;
    m.LAF = 87.0f;
    EXPECT_EQ(ed.update(m).event_type, EventType::MINOR);
    
    m.LAF = 100.0f;
    EXPECT_EQ(ed.update(m).event_type, EventType::MODERATE);
    
    m.LAF = 115.0f;
    EXPECT_EQ(ed.update(m).event_type, EventType::SEVERE);
}

TEST(EventDetector, ImpulseMetric) {
    EventDetector ed;
    // 模拟短促冲击：LAF 突然升高后回落
    for (int i = 0; i < 5; ++i) {
        SecondMetrics m;
        m.LAF = 70.0f;
        m.LAS = 68.0f;
        ed.update(m);
    }
    SecondMetrics impulse;
    impulse.LAF = 95.0f;  // 冲击
    impulse.LAS = 70.0f;
    EventResult r = ed.update(impulse);
    EXPECT_GT(r.impulse_metric, 6.0f);
    EXPECT_EQ(r.event_type, EventType::SEVERE);  // LAF > 85 dB
}

TEST(EventDetector, LafRiseRate) {
    EventDetector ed;
    // 模拟稳态噪声突然升高
    SecondMetrics m;
    m.LAF = 70.0f; m.LAS = 70.0f;
    for (int i = 0; i < 5; ++i) ed.update(m);  // 5秒稳态
    
    m.LAF = 90.0f;  // 突然 +20 dB
    EventResult r = ed.update(m);
    EXPECT_GT(r.laf_rise, 15.0f);
}
```

### 5.2 集成测试（用 260918 Pa-WAV）

```python
# tests/test_event_detector_v3.3.3.py
# 1. 用 260918-20hz WAV（已知是稳态 20 Hz 信号）
# 2. 跑 v3.3.3 事件检测算法
# 3. 预期: 稳态信号 → 无事件 (event_type == NONE)
# 4. 验证 LAF/LAS 输出一致性
```

### 5.3 回归测试

确保不影响：
- 现有 overload_flag / underrange_flag / wearing_state 输出
- DoseCalculator（依赖 LAeq）
- 1/3 倍频程频谱
- CSV 输出格式

### 5.4 嵌入式 RAM 验证

李工协助：
- 烧录真实固件到 nRF54L15 开发板
- 用 IDE / 调试器测 RAM 占用（预期 +220 字节）
- 实测 LAF 阈值触发、脉冲指标检测

---

## 六、风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| 阈值不适用于所有应用场景 | 工业 vs 民用阈值差异大 | 阈值作为可配置参数（默认 85/95/110 dB，可改） |
| LAF-LAS 差值在低频段仍有 3 dB 偏差 | 低频冲击事件可能漏检 | 接受偏差，依赖 LAF 阈值 + 上升率兜底 |
| 事件检测增加 CPU 开销 | 影响 48 kHz 实时处理 | EventDetector 计算 < 50 cycles/秒，可忽略 |
| 5min LAeq 背景需要 5 分钟启动时间 | 启动后 5min 内背景不准 | 用初始默认值（70 dB）兜底，逐步替换 |

---

## 七、实施步骤

### Step 1: Python 算法验证
- `tests/test_event_detector_v3.3.3.py`
- 用 260918 Pa-WAV + scipy 模拟事件检测
- 输出对比表

### Step 2: 头文件扩展
- 改 `include/event_detector.hpp`：新增 EventType、EventResult、EventThresholds
- 改 `include/noise_metrics.hpp`：新增 event_type / event_severity 字段

### Step 3: EventDetector 实现
- 改 `src/event_detector.cpp`：实现 4 维度判定（LAF 阈值 + 脉冲 + 上升率 + 背景对比）
- 新增单元测试 `tests/test_event_detector_v3.3.3.cpp`

### Step 4: NoiseProcessor 集成
- 改 `src/noise_processor.cpp`：在 `aggregate_metrics()` 末尾追加 EventDetector 调用
- 跑全量 zjocclab 容器测试

### Step 5: 文档 + git tag
- 更新 `CHANGELOG.md`
- `git tag v3.3.3`

### Step 6: 嵌入式烧录
- 李工协助，烧录到 nRF54L15 开发板
- 实测 4 维度事件检测触发

---

## 八、待路易斯碱决策

| # | 问题 | 我的建议 |
|---|------|---------|
| 1 | 默认阈值（85/95/110 dB） | 是（NIOSH/OSHA 标准） |
| 2 | 是否输出 event_type 枚举值 | 是（便于 CSV 解析） |
| 3 | 是否替换现有 overload_flag 单一标志 | **不替换**（保留兼容，仅新增分级） |
| 4 | 是否需要事件时间戳（event_timestamp） | 是（v3.4.0 计划） |
| 5 | 是否要先 zjocclab 容器验证再上嵌入式 | 是（Step 4 充分测试） |

---

## 九、附录：事件类型定义

```cpp
enum class EventType : uint8_t {
    NONE     = 0,  // 无事件
    MINOR    = 1,  // 轻微事件（LAF > 85 dB）
    MODERATE = 2,  // 中度事件（LAF > 95 dB 或上升 > 15 dB）
    SEVERE   = 3,  // 严重事件（LAF > 110 dB 或脉冲 > 12 dB）
};

struct EventResult {
    EventType event_type{EventType::NONE};
    uint8_t event_severity{0};       // 0-100 严重程度评分
    float laf_threshold_dB{0.0f};    // LAF 触发值
    float impulse_metric_dB{0.0f};   // LAF-LAS 脉冲指标
    float laf_rise_dB{0.0f};         // LAF 上升率
    bool is_overload{false};         // 过载标志（保持兼容）
};
```

---

## 十、参考

- IEC 61672-1:2013 §5.4 (Time-average sound level)
- IEC 61672-1:2013 §7 (Time-weighting Fast/Slow/Impulse)
- noise_info_toolkit_gcc/docs/DEVELOPMENT_PLAN_v3.3.2.md（前置版本）
- noise_info_toolkit_gcc/results/v3.3.2/WAV_VERIFICATION_v3.3.1.md
- noise_info_toolkit_gcc/src/event_detector.cpp（现有实现参考）

---

*文档创建时间: 2026-09-20*
*前置版本: v3.3.2（指数时间计权 + LAF/LAS 输出）*
*维护者: 蒙特卡洛*