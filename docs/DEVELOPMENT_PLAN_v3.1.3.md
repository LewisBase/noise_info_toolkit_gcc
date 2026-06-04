# v3.1.3 开发计划 — 暴露 Dose% / TWA 换算接口

> 日期：2026-06-04
> 作者：蒙特卡洛
> 状态：**已审阅，待开发**
> 前置版本：v3.1.2（事件检测）

## 关键设计决策（2026-06-04 确认）

| 决策点 | 选择 | 理由 |
|-------|------|------|
| 函数签名风格 | **风格 A**：4 个标准共用 1 个函数 + `DoseStandard` 枚举入参 | 嵌入式代码体积敏感，1 个函数 vs 4 个函数 |
| DoseState 结构 | **方案 A**：保留 8 字节 POD（`cumulative_dose_frac` + `elapsed_hours`） | 强制 8 字节预防"省 4 字节导致 TWA 算错"bug |
| Dose% 是否分标准 | **不分**（伪问题，已澄清） | 业务侧每个标准各持一个 `DoseState`，状态名隐含标准信息 |
| 对 v3.1.2 接口影响 | **零**：只新增头文件，不修改任何已有标识符 | 已部署的嵌入式固件升级 0 行代码改动 |

---

## 一、需求背景

### 1.1 嵌入式工程师反馈（v3.1.2 评审）

> 「三接口只给每段 `dose_frac`，没给 Dose% / TWA。」

具体场景：nRF54L15 固件需要向上位机/日志系统汇报"今日累积剂量"和"时间加权平均声级"，但当前接口设计**只输出段级 dose 分数**，`TWA` 和累积 `Dose%` 需要嵌入式侧自行实现换算逻辑。

### 1.2 现状盘点

**算法库内部已实现**（`include/dose_calculator.hpp`）：

| 函数 | 功能 | 是否暴露 |
|------|------|---------|
| `DoseCalculator::calculate_dose_increment(laeq, duration, profile)` | 段级 dose% | ✅（已被 `process_segment()` 调用） |
| `DoseCalculator::calculate_twa(dose_pct, standard)` | 累积 dose% → TWA | ❌ |
| `DoseCalculator::calculate_lex(dose_pct, profile)` | LEX,8h | ❌ |
| `DoseCalculator::calculate_dose_from_lex(lex, profile)` | LEX,8h → dose% | ❌ |
| `DoseCalculator::calculate_allowed_time(laeq, profile)` | 允许暴露时间 | ❌ |

**关键事实**：

- ✅ `DoseCalculator` 类**已经实现完整的 dose ↔ TWA 换算能力**（含 4 个标准、含 5dB 系数分支）
- ❌ `process_segment` / `aggregate_metrics` **没有调用** `DoseCalculator::calculate_twa` 输出 TWA
- ❌ 嵌入式侧**没有标准化的"累积状态"持有方式**，要自己造轮子

**v3.1.3 任务定位**：**薄包装（thin wrapper）**——把 `DoseCalculator` 已有能力 + 业务侧 `DoseState` 状态结构，组合成 4 个 inline 纯函数对外暴露。

### 1.3 关键概念澄清

#### 1.3.1 Dose% 是不是"累加 dose_frac"？

**是的，纯线性累加。** `dose_frac_*` 字段在 `process_segment` 中实际存储的是 **dose 分数**（已 /100）：

$$D_i = \frac{t_i}{T_c} \cdot 2^{(L_i - L_c)/q}$$

累积：

$$\text{Dose\%} = 100 \cdot \sum_i D_i$$

**判定**：Dose% = 100% → 累积剂量刚好等于标准（NIOSH 85 dB/8h 或 OSHA 90 dB/8h）。

#### 1.3.2 TWA 公式

TWA 本质是**"把累积剂量压缩到准则工时后的等效声级"**：

$$\text{TWA} = L_c + \frac{q}{\log_{10} 2} \cdot \log_{10}\!\left(D_{\text{cum}} \cdot \frac{T_c}{T_{\text{meas}}}\right)$$

> 这是从累积剂量反推的公式——**嵌入式唯一可行路径**，不需存历史 $L_i$。

#### 1.3.3 ⚠️ 5dB 交换率的 OSHA 修正系数（重要）

`DoseCalculator::calculate_twa` 的实现（`dose_calculator.hpp:97-105`）：

```cpp
static float calculate_twa(float total_dose_pct, DoseStandard standard) {
    if (total_dose_pct <= 0.0f) return 0.0f;
    const auto& profile = get_profile(standard);
    bool is_osha = (standard == DoseStandard::OSHA_PEL ||
                    standard == DoseStandard::OSHA_HCA);
    float coeff = is_osha ? 16.61f : 10.0f;
    return coeff * std::log10(total_dose_pct / 100.0f) + profile.criterion_level;
}
```

| 标准 | 交换率 $q$ | log10 系数 | 系数来源 |
|------|-----------|-----------|----------|
| **NIOSH** | 3 dB | 10.0 | $q/\log_{10} 2 = 3/0.301 = 9.97 \approx 10$ |
| **EU/ISO** | 3 dB | 10.0 | 同 NIOSH |
| **OSHA PEL** | 5 dB | **16.61** | $5/\log_{10} 2 = 16.609 \approx 16.61$ |
| **OSHA HCA** | 5 dB | **16.61** | 同 OSHA PEL |

**严禁**用 10·log10 公式套 OSHA 数据 —— 会差 > 5dB。

新接口通过 `DoseStandard` 枚举**自动选系数**，嵌入式侧调用时无需关心系数差异。

---

## 二、设计目标

### 2.1 接口设计原则

延续 v3.1.2 的无状态设计：

```
状态管理：嵌入式侧持有（业务状态 = 嵌入式 RAM）
算法库职责：纯函数包装 DoseCalculator 已有能力
测试性：DoseState POD + 4 个 inline 函数，PC 端单元测试直接覆盖
向后兼容：v3.1.2 的 process_segment / aggregate_metrics 接口不动
```

### 2.2 三个设计目标

| 目标 | 度量 |
|------|------|
| 暴露 Dose% / TWA 换算 | 新增 4 个 inline 纯函数 + 1 个 POD 状态结构 |
| 薄包装 DoseCalculator | 4 个函数实现 ≤ 1 行调用 + 边界检查 |
| 保持 v3.1.2 接口稳定 | `process_segment` / `aggregate_metrics` 签名不变 |

### 2.3 不在本版本范围

- ❌ **不在 `SecondMetrics` / `MinuteMetrics` 中加 TWA 字段**
  - 理由：TWA 是累积量，段级无意义
- ❌ **不修改 `DoseCalculator` 已有方法**
  - 理由：v3.1.2 已被使用，修改是 breaking change
- ❌ **不提供"内置状态"的版本**
  - 理由：保持无状态，让嵌入式侧显式持有

---

## 三、接口设计

### 3.1 新增头文件：`include/dose_state.hpp`

```cpp
#pragma once

#include "dose_calculator.hpp"
#include <cstdint>

namespace noise_toolkit {

//==============================================================================
// Dose State (business-side accumulator)
//==============================================================================

/**
 * @brief Cumulative dose state — held by business side (embedded firmware).
 *
 * Updated each segment via accumulate_dose_frac().
 * Read at any time via dose_to_pct() / dose_to_twa() to get current values.
 *
 * Memory: 8 bytes, POD, trivially copyable.
 *
 * Lifecycle example:
 *   DoseState state = {};                      // zero-init
 *   state = accumulate_dose_frac(state, m.dose_frac_niosh, duration_s);
 *   float twa = dose_to_twa(state, DoseStandard::NIOSH);
 */
struct DoseState {
    float cumulative_dose_frac;  // Σ D_i (dimensionless, can be > 1.0 for >100% dose)
    float elapsed_hours;         // Σ t_i in hours (always growing during measurement)
};

//==============================================================================
// Pure-Function API (thin wrappers around DoseCalculator)
//==============================================================================

/**
 * @brief Update DoseState with a new segment's dose_frac.
 *
 * Pure function: returns updated state, doesn't modify input.
 *
 * @param state Current cumulative state
 * @param dose_frac Segment dose fraction (e.g., m.dose_frac_niosh)
 * @param duration_s Segment duration in seconds
 * @return Updated DoseState
 *
 * @note If duration_s <= 0, returns state unchanged (no-op).
 */
inline DoseState accumulate_dose_frac(const DoseState& state,
                                       float dose_frac,
                                       float duration_s) noexcept {
    if (duration_s <= 0.0f) return state;
    DoseState out;
    out.cumulative_dose_frac = state.cumulative_dose_frac + dose_frac;
    out.elapsed_hours = state.elapsed_hours + (duration_s / 3600.0f);
    return out;
}

/**
 * @brief Convert cumulative DoseState to Dose percentage.
 *
 * @param state Current cumulative state
 * @return Dose percentage (0% = no exposure, 100% = standard met, >100% = exceeded)
 */
inline float dose_to_pct(const DoseState& state) noexcept {
    return state.cumulative_dose_frac * 100.0f;
}

/**
 * @brief Convert cumulative DoseState to Time-Weighted Average (TWA).
 *
 * Thin wrapper: calls DoseCalculator::calculate_twa().
 * The 5dB/3dB log10 coefficient is selected automatically based on DoseStandard.
 *
 * @param state Current cumulative state
 * @param standard Noise exposure standard (NIOSH / OSHA_PEL / OSHA_HCA / EU_ISO)
 * @return TWA in dB, or 0.0f if no exposure / zero elapsed time
 */
inline float dose_to_twa(const DoseState& state,
                          DoseStandard standard) noexcept {
    if (state.elapsed_hours <= 0.0f) return 0.0f;
    if (state.cumulative_dose_frac <= 0.0f) return 0.0f;
    return DoseCalculator::calculate_twa(
        state.cumulative_dose_frac * 100.0f,  // dose_frac → dose_pct
        standard);
}

/**
 * @brief Convert cumulative DoseState to Daily Noise Exposure Level (LEX,8h).
 *
 * Thin wrapper: calls DoseCalculator::calculate_lex().
 *
 * @param state Current cumulative state
 * @param standard Noise exposure standard
 * @return LEX,8h in dB, or 0.0f if no exposure
 */
inline float dose_to_lex8h(const DoseState& state,
                            DoseStandard standard) noexcept {
    if (state.cumulative_dose_frac <= 0.0f) return 0.0f;
    return DoseCalculator::calculate_lex(
        state.cumulative_dose_frac * 100.0f,
        DoseCalculator::get_profile(standard));
}

} // namespace noise_toolkit
```

### 3.2 使用示例

```cpp
#include "noise_processor.hpp"
#include "dose_state.hpp"

using namespace noise_toolkit;

NoiseProcessor processor(48000);
DoseState niosh_state = {};   // 业务侧持有
DoseState osha_state = {};

while (recording) {
    auto samples = read_audio_block();

    // 1. 段级指标（无状态、纯函数）
    SecondMetrics m = processor.process_segment(
        samples.data(), samples.data() + samples.size(), 0.01f);

    // 2. 累加剂量（业务侧持有状态，库只提供纯函数）
    niosh_state = accumulate_dose_frac(niosh_state, m.dose_frac_niosh, 0.01f);
    osha_state = accumulate_dose_frac(osha_state, m.dose_frac_osha_pel, 0.01f);

    // 3. 实时汇报（任意时刻可读，不需等结束）
    if (report_interval_elapsed) {
        log("Dose%: NIOSH=%.1f%%  OSHA=%.1f%%",
            dose_to_pct(niosh_state), dose_to_pct(osha_state));
        log("TWA:   NIOSH=%.1f dB  OSHA=%.1f dB",
            dose_to_twa(niosh_state, DoseStandard::NIOSH),
            dose_to_twa(osha_state, DoseStandard::OSHA_PEL));
    }
}
```

### 3.3 不修改 v3.1.2 接口

**保持不变**：
- `NoiseProcessor::process_segment()`
- `NoiseProcessor::aggregate_metrics()`
- `SecondMetrics`（81 字段不动）
- `MinuteMetrics`（已有 dose_frac 累加字段）
- `EventDetector::check_segment()`

**理由**：
- v3.1.2 已被嵌入式使用，改动是 breaking change
- Dose%/TWA 是累积量，段级接口语义不对
- 新接口作为**附加**而非**替换**

---

## 四、实现计划

### 4.1 文件变更

| 操作 | 文件路径 | 说明 |
|------|----------|------|
| **新增** | `include/dose_state.hpp` | POD 状态 + 4 个 inline 包装函数 |
| **新增** | `tests/test_dose_state.cpp` | 单元测试 |
| **修改** | `examples/main.cpp` | 演示新接口用法 |
| **修改** | `README.md` | API 章节补充新接口 |
| **修改** | `docs/DEVELOPMENT_PLAN_v3.1.3.md` | 本文档 |

**不修改**：
- `include/noise_processor.hpp`（v3.1.2 接口不动）
- `include/noise_metrics.hpp`（结构体字段不动）
- `include/dose_calculator.hpp`（已有方法不动）

### 4.2 实现步骤

#### Phase 1：核心实现（半天）

1. **定义数据结构**
   - `DoseState` POD（2 个 float）

2. **实现 4 个 inline 纯函数**
   - `accumulate_dose_frac()`：1 个加法
   - `dose_to_pct()`：1 个乘法
   - `dose_to_twa()`：边界检查 + 1 行调用 `DoseCalculator::calculate_twa`
   - `dose_to_lex8h()`：边界检查 + 1 行调用 `DoseCalculator::calculate_lex`

3. **实现细节**
   - 全部函数 `noexcept`、inline
   - 全部零堆分配
   - 全部零状态（库不持有跨调用状态）

#### Phase 2：单元测试（半天）

1. **编写单元测试**（`tests/test_dose_state.cpp`）
   - `accumulate_dose_frac` 累加正确性
   - `accumulate_dose_frac` 边界（duration_s ≤ 0）
   - `dose_to_pct` 简单乘 100
   - `dose_to_twa` 边界（elapsed_hours=0, dose=0）
   - `dose_to_twa` 与 `DoseCalculator::calculate_twa` 数值一致性
   - `dose_to_lex8h` 基础计算
   - 全 4 标准回归测试

2. **回归测试**
   - `process_segment()` 行为不变
   - `aggregate_metrics()` 行为不变
   - `EventDetector::check_segment()` 行为不变

#### Phase 3：示例与文档（半天）

1. **更新 `examples/main.cpp`**
   - 现有 demo 末尾演示新接口
   - 4 个标准 Dose%/TWA 实时输出

2. **更新 `README.md`**
   - API 章节补充 `dose_state.hpp` 说明
   - "累积剂量" 章节补公式说明

3. **更新 `AGENTS.md`**
   - 记录 v3.1.3 变更

---

## 五、接口规格

### 5.1 完整 API

```cpp
// include/dose_state.hpp
namespace noise_toolkit {

// 1. 业务状态（POD, 8 字节）
struct DoseState { float cumulative_dose_frac; float elapsed_hours; };

// 2. 纯函数（4 个 inline）
DoseState accumulate_dose_frac(const DoseState& state, float dose_frac, float duration_s) noexcept;
float      dose_to_pct(const DoseState& state) noexcept;
float      dose_to_twa(const DoseState& state, DoseStandard standard) noexcept;
float      dose_to_lex8h(const DoseState& state, DoseStandard standard) noexcept;

} // namespace noise_toolkit
```

### 5.2 入参与返回值表

| 函数 | 入参 | 返回 | 副作用 | 内部调用 |
|------|------|------|--------|----------|
| `accumulate_dose_frac` | state, dose_frac, duration_s | 新 state | 无 | 无（纯算术） |
| `dose_to_pct` | state | Dose% (float) | 无 | 无（乘法） |
| `dose_to_twa` | state, DoseStandard | TWA (dB, float) | 无 | `DoseCalculator::calculate_twa` |
| `dose_to_lex8h` | state, DoseStandard | LEX,8h (dB, float) | 无 | `DoseCalculator::calculate_lex` |

### 5.3 边界条件

| 情况 | 函数 | 行为 |
|------|------|------|
| `duration_s <= 0` | `accumulate_dose_frac` | 返回原 state（no-op） |
| `elapsed_hours == 0` | `dose_to_twa` | 返回 0.0f |
| `cumulative_dose_frac == 0` | `dose_to_twa` | 返回 0.0f |
| `cumulative_dose_frac == 0` | `dose_to_lex8h` | 返回 0.0f |
| `state.cumulative_dose_frac > 1.0`（>100% dose） | 全部 | 正常计算，结果 > 100% 或 TWA > L_c |

---

## 六、内存占用估算

| 组件 | 大小 | 说明 |
|------|------|------|
| `DoseState` | 8 bytes | 2 个 float |
| 单标准状态 | 8 bytes | 嵌入式每个监测标准持有一个 `DoseState` |
| 4 个标准状态 | 32 bytes | NIOSH + OSHA PEL + OSHA HCA + EU/ISO |
| **总计** | **~32 bytes** | 4 标准 |

**嵌入式可行性**：nRF54L15 RAM 256-512KB，32 字节占比 < 0.02%，可忽略。

---

## 七、测试用例

### 7.1 单元测试项

| 测试 | 输入 | 预期输出 | 关键点 |
|------|------|----------|--------|
| `accumulate_basic` | state={0,0}, frac=0.5, dur=1s | {0.5, 1/3600} | 累加正确 |
| `accumulate_zero_duration` | state={0.5, 1h}, frac=0.1, dur=0s | {0.5, 1h} | 边界 no-op |
| `dose_to_pct_basic` | state={0.5, 1h} | 50.0f | 简单乘 100 |
| `twa_boundary_empty` | state={0,0}, NIOSH | 0.0f | 边界 |
| `twa_consistency_3db` | 多组随机 dose_frac+duration, NIOSH | 匹配 `DoseCalculator::calculate_twa` | 回归 |
| `twa_consistency_5db` | 多组随机 dose_frac+duration, OSHA_PEL | 匹配 `DoseCalculator::calculate_twa` | **5dB 系数回归** |
| `lex8h_basic` | state={1.0, 8h}, NIOSH | 85.0 dB | 公式正确性 |
| `lex8h_consistency` | 多组随机, 4 个标准 | 匹配 `DoseCalculator::calculate_lex` | 回归 |

### 7.2 浮点容差

| 函数 | 容差 | 理由 |
|------|------|------|
| `accumulate_dose_frac` | 0（精确加法） | float 32 位累加 |
| `dose_to_pct` | 0 | float 乘法 |
| `dose_to_twa` | ±0.01 dB | log10 + 系数相乘，浮点截断 |
| `dose_to_lex8h` | ±0.01 dB | log10 |

---

## 八、风险与对策

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 嵌入式侧仍用 10·log10 算 OSHA TWA | TWA 偏差 > 5dB | 接口用 `DoseStandard` 枚举自动选系数；测试用例覆盖 |
| `DoseState` 在嵌入式端误用同一结构存 4 个标准 | 状态污染 | 文档明确"每标准一个独立 state" |
| `duration_s` 累加漂移（float 精度） | 8h 累积误差 ~ms 级 | 嵌入式通常 0.01s/段，28800 段累加误差 < 1e-5 h，忽略 |
| 5dB 系数 16.61 vs 16.609 截断 | 累积误差 < 0.01 dB | 测试容差 ±0.01 dB 覆盖 |

---

## 九、验收标准

- [ ] `include/dose_state.hpp` 实现完成，零堆分配
- [ ] `DoseState` POD 结构（8 字节）
- [ ] 4 个 inline 函数（`accumulate_dose_frac` / `dose_to_pct` / `dose_to_twa` / `dose_to_lex8h`）
- [ ] `dose_to_twa` / `dose_to_lex8h` 内部调用 `DoseCalculator` 已有方法（薄包装）
- [ ] 8 个单元测试全部通过（含 5dB 系数回归测试）
- [ ] 与 `DoseCalculator::calculate_twa` / `calculate_lex` 数值一致性 < 0.01 dB
- [ ] `process_segment` / `aggregate_metrics` 行为不变（回归测试通过）
- [ ] `examples/main.cpp` 添加新接口演示
- [ ] `README.md` 更新
- [ ] `AGENTS.md` 记录 v3.1.3 变更

---

## 十、嵌入式工程师可能追问的预备回答

**Q1：为什么不让 `process_segment` 直接返回累积 Dose%？**

A：`process_segment` 是**无状态**接口（v3.1.2 已定原则），每段独立计算；累积是**业务状态**，应由调用方持有。如果在算法库内部维护累积状态，会破坏"零堆分配 + 易测试"两个核心优势。

**Q2：为什么不加 `SecondMetrics.twa_*` 字段？**

A：TWA 是累积量，段级无定义；强行加会让字段语义混乱（"段级 TWA" 还是"截至当前的累积 TWA"？）。`dose_to_twa(state, standard)` 实时算即可。

**Q3：能否提供"内置状态"的 `DoseCalculator` 类？**

A：可以（v3.1.2 之前的 PR 里有），但当前嵌入式环境 RAM 紧张，让业务侧显式持有 8 字节 POD 状态比类成员（虚函数、动态分配）更省。

**Q4：5dB 系数 16.61 哪里来的？**

A：`5 / log10(2) = 5 / 0.30103 = 16.6096`，取两位小数 = 16.61。物理意义：5dB 交换率意味着"每 +5dB 允许时间减半"，用 $2^x$ 表达，log10 化后系数 = $q/\log_{10}2$。OSHA 标准强制使用此系数修正。

**Q5：DoseState 的 elapsed_hours 在哪用？**

A：仅在 `dose_to_twa` 用作分母，做"压缩到准则工时"的时间归一。`dose_to_pct` / `dose_to_lex8h` 不用它。

---

## 十一、后续扩展方向（v3.2+）

1. **剂量预警回调**：当 Dose% > 50% 时通过回调通知嵌入式（避免轮询）
2. **多标准并行报告**：一次性输出 4 个标准的 Dose%/TWA（结构体打包）
3. **剂量复位/分段记录**：嵌入式侧在午休/换班时复位 DoseState，生成"工时剂量"报告
4. **事件窗内剂量**：结合 v3.1.2 的事件检测，统计"事件期间"贡献的剂量

---

## 十二、变更日志摘要

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1 (草稿) | 2026-06-04 | 初版，等待审阅 |
| 0.2 (草稿) | 2026-06-04 | 简化设计：删除 `DoseCriterion`、改用 `DoseStandard` 枚举；接口明确为 `DoseCalculator` 薄包装；删除会议答辩稿和数字用例；精简公式章节 |
| 0.3 (已审阅) | 2026-06-04 | 用户确认两个关键决策：4 标准共用 1 个函数 + 保留 8 字节 POD；更新文档状态为"已审阅，待开发"；正式记录决策点 |

---

*文档版本：0.3 (已审阅，待开发)*
*开发启动条件：用户确认开始实现*
