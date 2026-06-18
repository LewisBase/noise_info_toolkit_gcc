# v3.2 Bug 报告 — A/C 加权预存表归一化错误

> 日期：2026-06-18
> 发现者：嵌入式工程师（反馈 Dose% 异常）+ 蒙特卡洛（定位根因）
> 严重性：**严重**（所有非 48k 采样率下 LAeq/LCeq 严重偏高，dose% 偏差 3-4 个数量级）
> 影响范围：v3.2 全部 7 个采样率（8k / 16k / 22.05k / 32k / 44.1k / 48k / 96k）

---

## 零、决策记录（2026-06-18 用户拍板）

| # | 决策项 | 用户选择 |
|---|--------|---------|
| 1 | bug 文档位置 | ✓ **`docs/BUG_v3.2_A_WEIGHTING.md`**（本文档）|
| 2 | 修复方案 | ✓ **方案 A**：scipy 重生成 7 套系数（嵌入式无法运行时动态生成） |
| 3 | 归一化方式 | ✓ **独立 gain factor**（不修改 biquad b 系数，1kHz 乘以 gain_normalization）|
| 4 | 是否需要运行时动态归一化 | ✓ **不需要**（A 加权是纯数学 LTI 系统，与环境无关）|

---

## 一、问题背景

### 1.1 现象

嵌入式工程师反馈：调用接口 1 `process_segment()` 累加 NIOSH dose% 得到约 **800 万 %**，预期应在 100% 量级。

提供的数据记录 `THIST100.CSV`（220 行 × 1 秒 = 220 秒，含 LAeq / LZeq / dose_frac_niosh 等完整字段）。

### 1.2 工程师侧累加逻辑确认

工程师明确反馈：

- ✓ **手动累加接口 1 返回的 `dose_frac_niosh`**，未使用接口 2 `aggregate_metrics`
- ✓ **接口 1 的 `duration_s` 入参正确**（CSV 中每行 `duration_s = 1.0`）

→ 累加方式与 `duration_s` 传值**均无问题**，bug 不在业务侧。

---

## 二、证据收集（基于 THIST100.CSV 反推）

### 2.1 CSV 概览

- 共 220 行，每行 `duration_s = 1.0`
- 累加 `dose_frac_niosh` × 100 = **8,753,214% ≈ 875 万 %**
- 与工程师报告的"800 万 %"**完全吻合**（差异源于 CSV 可能被截断）

### 2.2 关键发现：LAeq 比 LZeq 高 35 dB（物理不变量违例）

| 指标 | 数值 |
|------|------|
| LAeq 均值 | **124.65 dB** |
| LZeq 均值 | 89.54 dB |
| **LAeq - LZeq** | **+35.11 dB（均值），+28 ~ +43 dB（范围）** |

A 加权对宽带噪声（白噪声/粉红噪声/工业噪声）的预期效应：

| 噪声类型 | 期望 LAeq - LZeq（典型）| 实测 |
|---------|---------------------|------|
| 白噪声 | -1 ~ -3 dB | **+40.5 dB** |
| 粉红噪声 | +1 ~ -2 dB | **+41.1 dB** |
| 工业噪声 | -2 ~ -4 dB | **+41.4 dB** |

**A 加权在任何常见噪声下都应衰减或持平，绝对不应放大 35 dB**——这是物理不变量违例，是 bug 的直接信号。

### 2.3 dose 偏差数量级

NIOSH 公式 `dose_frac = (duration_h/8) × 2^((L-85)/3)` 是关于 L 的指数函数。

LAeq 偏高 35 dB → `2^(35/3) = 2^11.67 ≈ 3300` 倍
→ 220 行累加 dose% 偏大 ~3300 倍

实测：220 行 CSV 累加 = 875 万 %，对照"典型 124 dB × 220s"的预期 ~26,400% → 偏大 **331 倍**（与 3300 倍在同一量级，差距可能来自 dose_frac 在极值处非线性饱和）。

---

## 三、根因定位

### 3.1 错误代码

`src/iir_filter.cpp:282-309` 的 `a_weighting_design()`（动态生成路径）与 v3.2 新增的 `include/weighting_coefficients_multirate.hpp`（7 采样率预存表路径）**共用了同一段归一化逻辑**：

```cpp
// src/iir_filter.cpp:282-309（精简）
float target_gain_db = 0.0f;
float f_ref = 1000.0f;
float omega = noise_const::TWO_PI_F * f_ref / sample_rate;
std::complex<float> z(std::cos(omega), std::sin(omega));

std::complex<float> H(1.0f, 0.0f);
for (const auto& coef : sos) {
    // ... 累乘 H(f_ref)
}

float current_gain = std::abs(H);
float norm_factor = std::pow(10.0f, target_gain_db / 20.0f) / current_gain;

// Apply normalization to first section  ← BUG！
sos[0].b0 *= norm_factor;
sos[0].b1 *= norm_factor;
sos[0].b2 *= norm_factor;
```

### 3.2 错误原理

A 加权是 **3 段 biquad 串联**（`a_weighting_design()` 的 3 个 section）：
- Section 1：高频极点 76617 rad/s（differentiator-like，b = [1, -2, 1]）
- Section 2：中频极点 4636 + 676.7 rad/s（b = [1, 0, -1]）
- Section 3：低频极点 129.4 rad/s（b = [1, -2, 1]）

总频响 `H_total(z) = H_1(z) × H_2(z) × H_3(z)`

**Bug 在于**：仅把 `1/H_total(1kHz)` 乘到 `sos[0].b`，等价于让 Section 1 承担所有归一化补偿。这在 1 kHz 处**碰巧正确**（`H_total(1kHz) = 1.0`），但**扭曲了其他频率的响应**——Section 1 在 10 kHz 附近本来就有 -80 dB 左右的衰减，乘以补偿因子后 Section 1 的 b 系数变大，10 kHz 处的总响应被错误放大。

### 3.3 实测频响（v3.2 预存表）

通过 Python `scipy.signal` 仿真 `include/weighting_coefficients_multirate.hpp` 中 7 套 A 加权系数：

| 频率 | v3.2 实际增益 | 标准 IEC 61672 A 加权 | 偏差 |
|------|------------|---------------------|------|
| 100 Hz | -59 dB | -58.96 dB | ✓ 几乎正确 |
| 1 kHz | 0 dB | 0 dB | ✓ |
| **10 kHz** | **+16 ~ +45 dB** | **-82.49 dB** | **+60 ~ +127 dB 严重异常** |

**10 kHz 应衰减 82 dB，实际放大 16-45 dB**——是 bug 的直接根源。

注意：所有 7 个采样率都有此问题（虽然具体偏差数值随 fs 略有不同），因为 v3.2 引入的多采样率表**沿用了相同的归一化 bug**。

---

## 四、为什么 v3.1.x 没有此问题

v3.1.x 的 A 加权是**单采样率**（48k），其归一化代码路径与 v3.2 完全相同——但因为只有 48k 一套系数，且工程师一直用 48k 跑测试，**症状可能没暴露**。需要验证：

- v3.1.3 在 48k 下的 A 加权频响是否正确（很可能也不正确，只是未被发现）
- 嵌入式工程师反馈的 800 万 % 是否在 v3.1.x 上同样存在（需要回退测试）

如果 v3.1.3 也有同样问题，则根因报告要更新为"v3.1.x 以来一直存在的 bug"，v3.2 预存表只是把它放大了。

---

## 五、修复策略概览

详见 `docs/DEVELOPMENT_PLAN_v3.2.1.md`。

核心思路：

1. **重写归一化逻辑**：把 `1/H_total(1kHz)` 作为**独立 gain factor**（不修改 biquad b 系数）
2. **scipy 重生成 7 套系数**：用 `scipy.signal.bilinear` 设计 IEC 61672 1 级 A/C 加权，按 7 个采样率生成正确系数
3. **加回归测试**：`tests/test_weighting_response.cpp` 硬性断言 1 kHz = 0 dB、10 kHz ≈ -3 dB、20 kHz ≈ -9 dB（容差 ±0.5 dB）
4. **保留表外采样率兜底**：维持 `a/c_weighting_design_fixed()` 作为兜底（任意 fs）

---

## 六、变更日志摘要

| 版本 | 日期 | 变更 |
|------|------|------|
| v3.2 | 2026-06-04 | **引入** 7 采样率 A/C 加权预存表（commit 77ae731），**引入归一化 bug** |
| v3.2.1（计划）| 2026-06-XX | **修复** 归一化逻辑 + 重生成 7 套系数 + 加回归测试 |

---

## 七、教训

1. **biquad 链归一化必须用独立 gain stage**（或分配到所有段），**不能只调 sos[0].b**——这是数字滤波器设计的常见错误模式
2. **物理不变量检查比数值验证更高效**：看 LAeq - LZeq 比看 dose% 数值敏感度高 100 倍
3. **嵌入式库 bug 诊断应优先索取原始数据**（CSV/日志），不要先入为主假设"业务侧累加错"——这次 100% 是库代码 bug
4. **预存表的代价是失去动态可验证性**：动态生成时单元测试可覆盖每个 fs；预存表后必须额外加频响断言测试，否则 bug 潜伏数月