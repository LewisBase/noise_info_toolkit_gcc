# v3.2 开发计划 — 频段滤波器归一化 + 多采样率 A/C 加权稳定化

> 日期：2026-06-04
> 作者：蒙特卡洛
> 状态：**已批准，待实施**
> 前置版本：v3.1.3（暴露 Dose%/TWA 换算接口）
> 范围：**仅修复 v3.1.2 时代遗留的两个预存 bug，不引入新功能**

---

## 零、决策记录（2026-06-04 用户拍板）

| # | 决策项 | 用户选择 |
|---|--------|---------|
| 1 | Bug A 修复方案 | ✓ **通带中心增益归一化**（标准做法）|
| 2 | Bug B 修复方案 | ✓ **方案 C：多采样率预存表**（非 48k 不再动态生成）|
| 3 | 多采样率支持范围 | ✓ **7 个采样率**（8k / 16k / 22.05k / 32k / 44.1k / 48k / 96k）|
| 4 | A/C 加权是否一起修 | ✓ **一起修**（共用回退代码，必须统一处理）|

**A+C 混合兜底方案**：表内 7 个采样率查表（零计算、最高精度）；表外采样率回退到 `a/c_weighting_design_fixed()` pre-warp 修正版（任意采样率兜底）。

---

## 一、问题背景

### 1.1 v3.1.3 报告的两个 Bug（commit f38396c 修复测试时发现）

| Bug | 表现 | 严重程度 | 影响范围 |
|-----|------|---------|---------|
| **A** | 1/3 倍频程带通滤波器未做通带中心增益归一化，9 个频段 SPL 绝对值错误，频段间大小关系反了 | 中-高 | 频段诊断失效（"找噪声源在哪个频段"） |
| **B** | 非 48k 采样率下 A 加权滤波器不稳定，LAeq = NaN | 高 | 任何非 48k 采样率设备（8k/16k/44.1k 全部失效） |

### 1.2 为什么 v3.1.3 没修

| Bug | 真正修复的影响 | v3.1.3 范围内处理 |
|-----|---------------|------------------|
| **A** | 改 `bandpass()` 函数 → 重生成 9 个频段系数 → 所有 `process_segment` 频段输出绝对值变化 | 推迟到 v3.2 |
| **B** | 改 `a_weighting_design()` 稳定性 → 影响非 48k 路径 | 推迟到 v3.2 |

两条都在 `AGENTS.md` "Known Issues / Tech Debt" 详细记录。

### 1.3 v3.2 任务定位

**只修两个 bug，不引入新功能**：

1. **修复 Bug A**：`filter_design::bandpass()` 加通带中心增益归一化，重新生成 9 个频段预存系数
2. **修复 Bug B**：`filter_design::a_weighting_design()` 和 `c_weighting_design()` 在 8k/16k/44.1k 采样率下生成稳定滤波器

**保证向后兼容**：v3.1.3 的所有接口和测试都应继续通过。

---

## 二、修复 Bug A：1/3 倍频程带通滤波器归一化

### 2.1 根因

#### 2.1.1 当前实现

`src/iir_filter.cpp:408-456`：

```cpp
double alpha = wc / Q;          // alpha = wc / Q
double b0 = alpha;              // ← b 系数与 wc 成正比
double b1 = 0.0;
double b2 = -alpha;
double a0 = 1.0 + alpha + wc*wc;
double a1 = 2.0 * (wc*wc - 1.0);
double a2 = 1.0 - alpha + wc*wc;

b0 /= a0; b1 /= a0; b2 /= a0;  // 归一化 a0=1
a1 /= a0; a2 /= a0; a0 = 1.0;
```

**只归一化了 a0=1.0，没归一化通带中心增益**。

#### 2.1.2 数学推导

`b0 = alpha = wc / Q`，Q 是常数（1/3 oct Q ≈ 4.318），`wc ∝ fc`。

**通带中心频率响应**（z = e^(jω0) 处求 H(z)，窄带近似）：

$$|H(e^{j\omega_0})| \approx \frac{2 \cdot b_0}{\omega_0} = \frac{2 \cdot \alpha}{\omega_0} = \frac{2 \cdot wc/Q}{\omega_0}$$

由于 `wc ≈ tan(π·fc/fs) ≈ π·fc/fs = ω0/2`：

$$|H(e^{j\omega_0})| \approx \frac{2 \cdot \omega_0/(2Q)}{\omega_0} = \frac{1}{Q} \approx 0.232$$

**理论值应该是 1.0，实际 ≈ 0.232**（−12.7 dB），但这个值是常数（不依赖 fc），那为什么实测有 fc 依赖？

实际数字推导（精确版本，**不**用窄带近似）：

- 1kHz 频段，fs=48k，wc = tan(π·1000/48000) ≈ 0.0654
- alpha = wc / 4.318 ≈ 0.01514
- a0 = 1 + alpha + wc² ≈ 1.0193
- b0 归一化后 = 0.01514 / 1.0193 ≈ 0.01486
- 通带中心响应 = |H(e^jω0)| = ?

实测 1kHz 正弦波输入 1kHz 频段，输出 RMS = 0.0485 Pa（输入 RMS = 0.707 Pa）→ 衰减 0.0686 = −23.3 dB

**所以实测衰减比 1/Q 严重很多**——是因为窄带近似不准，且分母也有 fc 相关性。

**总之**：b 系数与 fc 成正比是确定的；通带响应不归一化是确定的；导致 1kHz 频段响应比 8kHz 频段小 25 dB 是确定的。

### 2.2 修复方案

#### 2.2.1 在 `bandpass()` 函数中加通带中心增益归一化

修改 `src/iir_filter.cpp::bandpass()`：

```cpp
IIRCoefficients bandpass(double low_freq, double high_freq,
                         double sample_rate, int order) {
    // ... 现有计算 b0, b1, b2, a0, a1, a2 ...

    // 归一化 a0=1（现有代码）
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0; a0 = 1.0;

    // === 新增：通带中心增益归一化 ===
    // 计算几何中心频率
    double fc_geo = std::sqrt(low_freq * high_freq);
    double w0 = 2.0 * noise_const::PI_F * fc_geo / sample_rate;

    // 评估 H(z) 在 z = e^(jw0) 处的幅值
    std::complex<double> z_inv = std::polar(1.0, -w0);
    std::complex<double> num = b0 + b1 * z_inv + b2 * z_inv * z_inv;
    std::complex<double> den = 1.0 + a1 * z_inv + a2 * z_inv * z_inv;
    double peak_gain = std::abs(num / den);

    // 归一化使通带中心增益 = 1.0
    if (peak_gain > 1e-12) {
        b0 /= peak_gain;
        b1 /= peak_gain;
        b2 /= peak_gain;
    }

    return {{static_cast<float>(b0), static_cast<float>(b1), static_cast<float>(b2)},
            {1.0f, static_cast<float>(a1), static_cast<float>(a2)}};
}
```

#### 2.2.2 重新生成 48kHz 预存系数

```bash
cd tools
g++ -std=c++17 -I../include -o gen_bp ../src/iir_filter.cpp generate_bandpass_coeffs.cpp
./gen_bp > ../include/bandpass_coefficients_48k.hpp
```

#### 2.2.3 验证修复

**预期结果**（1kHz 正弦波输入）：

| 频段 | 修复前 SPL | 修复后期望 SPL | 物理意义 |
|------|-----------|--------------|---------|
| 1kHz | 61.7 dB | ≈ 91 dB（输入 SPL） | 1kHz 频段通过 |
| 8kHz | 87.1 dB | 极低（< 30 dB） | 8kHz 频段阻断 |

宽频带噪声输入下：9 个频段 SPL 应大致相同（±3 dB）。

### 2.3 风险评估

| 风险 | 影响 | 缓解 |
|------|------|------|
| 频段 SPL 绝对值变化 | 任何读频段的下游代码会看到不同数字 | 文档化变更；examples/demo 同步更新 |
| 边界频段（如 63Hz/16kHz）性能差异 | 16kHz 接近 Nyquist，可能滤波器性能下降 | 测试覆盖；考虑是否需要降采样预处理 |
| 多次迭代调试系数生成 | 时间风险 | 用 `/tmp/diag.cpp` 验证后再提交 |

---

## 三、修复 Bug B：多采样率 A/C 加权稳定化

### 3.1 根因

#### 3.1.1 当前实现

`src/noise_processor.cpp:44-55`：

```cpp
if (sample_rate_ == 48000) {
    a_weight_chain_ = A_WEIGHTING_48K;  // 预存 constexpr
    c_weight_chain_ = C_WEIGHTING_48K;
} else {
    auto sos_a = filter_design::a_weighting_design(sr);  // 动态生成
    // ...
    auto sos_c = filter_design::c_weighting_design(sr);
    // ...
}
```

非 48k 采样率走 `filter_design::a_weighting_design()` 动态生成。**生成的滤波器不稳定**（极点可能出单位圆）。

#### 3.1.2 数学根因

A 加权滤波器是 6 阶（3 个 biquad 段）Butterworth 模拟原型 → 双线性变换 → 数字滤波器。

**模拟 A 加权原型**在 s 平面的极点位置取决于截止频率（约 1kHz）。

**双线性变换** `s = (2/T) · (1 - z⁻¹)/(1 + z⁻¹)` 在低采样率下会**频率扭曲**（pre-warping 不充分）：
- 高频段（>5 kHz）的极点被映射到接近 z=1
- 低采样率下 Nyquist 频率低（4 kHz @ 8k fs），**模拟原型的高频段在数字域被严重压缩**
- 极点被推到单位圆外 → 滤波器发散

**48k 采样率** Nyquist = 24 kHz，模拟 A 加权的高频段（~10 kHz）远低于 Nyquist → pre-warping 充分 → 滤波器稳定。

**8k/16k/44.1k 采样率** Nyquist 不足 → 数字滤波器不稳定。

#### 3.1.3 实测数据（A 和 C 加权同样发散）

| 采样率 (Hz) | LAeq (94 dB SPL 正弦波) | LCeq | LZeq | A 加权稳定性 | C 加权稳定性 |
|------------|------------------------|------|------|------------|------------|
| 8000 | **-nan** | **-nan** | 90.97 dB ✓ | 不稳定 | 不稳定 |
| 16000 | **-nan** | **-nan** | 90.97 dB ✓ | 不稳定 | 不稳定 |
| 44100 | **-nan** | **-nan** | 90.97 dB ✓ | 不稳定 | 不稳定 |
| 48000 | 90.97 dB ✓ | 90.97 dB ✓ | 90.97 dB ✓ | 稳定（预存）| 稳定（预存）|

**注意**：LZeq 始终稳定，因为它走 Z 加权（不加权），不经过动态生成的滤波器。

### 3.2 修复方案（用户拍板：方案 C + A 兜底混合）

#### 3.2.1 三个候选方案对比

**方案 A：A 和 C 加权函数都加 pre-warp 修正（兜底层）**

```cpp
std::vector<IIRCoefficients> a_weighting_design(float sample_rate) {
    // A 加权模拟原型的关键频率
    const float f1 = 20.598997f;   // 第一个极点
    const float f2 = 107.65265f;
    const float f3 = 737.86223f;
    const float f4 = 12194.217f;
    const float f5 = 158.48932f;   // 零点
    const float f6 = 158.48932f;
    const float A1000 = 2.0f;      // 1kHz 处的设计增益

    // 双线性变换的 pre-warp 频率
    auto prewarp = [&](float f) {
        return 2.0f * sample_rate * std::tan(M_PI * f / sample_rate);
    };

    // 计算 pre-warp 后的 s 平面极点
    // ... 详细推导 ...
}
```

**方案 B：限制最低采样率（保守，**不采用**）**

```cpp
NoiseProcessor(int sample_rate = 48000, ...) {
    if (sample_rate < 32000) {
        // 拒绝低于 32k 的 A 加权（精度不足）
        throw std::invalid_argument("A/C weighting requires sample_rate >= 32kHz");
    }
    // ...
}
```

**方案 C（用户拍板）：多采样率预存系数表（主路径）**

为常用采样率预存 constexpr 系数：
- 8k、16k、22.05k、32k、44.1k、48k、96k
- 每个采样率一组 A 和 C 加权系数
- 切换到 `constexpr std::array<...> WEIGHTING_TABLE[<sr>]` 查表
- 不在表内的采样率：回退到方案 A（pre-warp 修正版）

**A+C 混合最终方案**：表内 7 个采样率查表（零计算、最高精度）；表外采样率回退到方案 A（任意采样率兜底）。

#### 3.2.2 实施步骤（A+C 混合方案）

1. 实现 `a_weighting_design_fixed()` / `c_weighting_design_fixed()`（pre-warp 修正版，方案 A 兜底层）
2. 用 Python scipy 信号处理工具生成 7 个采样率 × A/C 加权稳定系数
3. 写 `include/weighting_coefficients_multirate.hpp`（7 个采样率 × 5 段 × 5 float 的 constexpr 表）
4. 改 `NoiseProcessor` 构造函数：表内 7 个采样率查表；表外采样率回退到 `*_design_fixed()`
5. 用 `/tmp/diag.cpp` 在 8k/16k/44.1k 各跑一次，验证 LAeq 和 LCeq 都不再 NaN

### 3.3 风险评估

| 风险 | 影响 | 缓解 |
|------|------|------|
| 系数精度（动态设计 → 预存） | 边界频率响应可能与 IEC 61672 标准略有差异 | 对照 IEC 61672 频响曲线表验证 |
| 嵌入式 RAM 占用增加 | 多采样率系数表可能占 ~1-2 KB | 评估；如果太大只预存项目用到的几个 |
| 测试需要 8k/16k/44.1k 音频源 | 需要生成测试信号 | 用 `/tmp/diag.cpp` 程序化生成 |
| 动态生成回退路径 | 项目仍需要"任何采样率"的回退 | 设计一个稳定的动态算法作为最后回退 |

---

## 四、测试计划

### 4.1 Bug A 验证测试

**Test A1**：1kHz 正弦波输入，1kHz 频段 SPL 应 ≈ LAeq（±3 dB）

**Test A2**：宽频带噪声输入，9 个频段 SPL 应大致相同（标准差 < 5 dB）

**Test A3**：每个频段的归一化频率响应 -3dB 带宽应 ≈ 1/3 oct

**Test A4**：1kHz 正弦波输入，63Hz/16kHz 频段 SPL 应 < 50 dB（充分抑制）

### 4.2 Bug B 验证测试（多采样率 A **和** C 加权）

**Test B1**：8k 采样率，94 dB SPL 正弦波，LAeq ∈ [85, 100] dB（不再是 NaN）

**Test B2**：16k 采样率，LAeq ∈ [85, 100] dB

**Test B3**：44.1k 采样率，LAeq ∈ [85, 100] dB

**Test B4**：A 加权频率响应 -3dB 点应符合 IEC 61672 标准

**Test B5**：A 加权在 1 kHz 处增益 = 0 dB（标准校准点）

**Test B6**：A 加权 22.05k 采样率，LAeq ∈ [85, 100] dB（验证 22.05k 也在表内）

**Test B7**：A 加权 32k 采样率，LAeq ∈ [85, 100] dB

**Test B8**：A 加权 96k 采样率，LAeq ∈ [85, 100] dB

**Test B9**：8k 采样率，LCeq ∈ [85, 100] dB（C 加权同样不再 NaN）

**Test B10**：16k 采样率，LCeq ∈ [85, 100] dB

**Test B11**：44.1k 采样率，LCeq ∈ [85, 100] dB

**Test B12**：C 加权 22.05k / 32k / 96k 采样率，LCeq ∈ [85, 100] dB

**Test B13**：C 加权在 1 kHz 处增益 = 0 dB（标准校准点）

**Test B14**：C 加权频率响应 -3dB 点应符合 IEC 61672 标准

**Test B15**：A+C 加权 7 个采样率全覆盖（自动化参数化测试）

**Test B16**：表外采样率（如 11025 Hz）走方案 A 兜底，LAeq/LCeq ∈ [60, 110] dB

### 4.3 回归测试

| 测试 | 状态 |
|------|------|
| test_noise_processor 12/12 | 保持通过（重写 test_frequency_bands 用归一化后的输出） |
| test_event_detector 14/14 | 保持通过 |
| test_dose_state 9/9 | 保持通过 |
| dose_validator | 保持通过 |
| examples/main.cpp demo | 重写 demo 用归一化后频段输出 |

---

## 五、实施计划

### 5.1 文件变更

| 操作 | 文件 | 说明 |
|------|------|------|
| **修改** | `src/iir_filter.cpp` | `bandpass()` 加通带中心增益归一化（10 行）|
| **修改** | `src/signal_utils.cpp` | `a_weighting_design()` / `c_weighting_design()` 加 pre-warp 修正（方案 A 兜底层）|
| **新增** | `include/weighting_coefficients_multirate.hpp` | 7 个采样率 × A/C 加权预存表（~800 字节）|
| **修改** | `src/noise_processor.cpp` | 构造函数支持多采样率查表（48k 用旧表，新加 6 个采样率）|
| **修改** | `include/bandpass_coefficients_48k.hpp` | 重新生成（自动）|
| **修改** | `tests/test_noise_processor.cpp` | Test 3 用归一化后的频段输出做断言；Test 9 扩展到 7 个采样率 + A/C 加权 |
| **修改** | `examples/main.cpp` | demo 频段输出改用归一化后数字 + 多采样率 demo |
| **修改** | `README.md` | 频段指标章节加"已归一化"说明；A/C 加权章节加"7 个采样率支持"说明 |
| **修改** | `AGENTS.md` | 移除两条 known issues（频段归一化 + 多采样率支持）|
| **修改** | `docs/DEVELOPMENT_PLAN_v3.2.md` | 本文档状态从"草稿"更新为"已批准" |

### 5.2 实施步骤

#### Phase 1：Bug A 修复（1 天）

1. 在 `bandpass()` 函数加通带中心增益归一化
2. 跑 `/tmp/diag.cpp` 验证 1kHz/8kHz 频段响应归一化
3. 重新生成 `bandpass_coefficients_48k.hpp`
4. 编译，跑 `test_noise_processor::test_frequency_bands` 验证 Test 3 通过
5. 跑全部 ctest 确认无回归

#### Phase 2：Bug B 修复（1.5 天，A+C 混合方案）

1. 实现 `a_weighting_design_fixed()` / `c_weighting_design_fixed()`（pre-warp 修正版，方案 A 兜底层）
2. 用 Python scipy 信号处理工具生成 7 个采样率 × A/C 加权稳定系数
3. 写 `include/weighting_coefficients_multirate.hpp`（7 个采样率 × 5 段 × 5 float 的 constexpr 表）
4. 改 `NoiseProcessor` 构造函数：表内 7 个采样率查表；表外采样率回退到 `*_design_fixed()`
5. 跑 `/tmp/diag.cpp` 验证 8k/16k/44.1k 采样率下 LAeq 和 LCeq 都不再 NaN
6. 跑 `test_sample_rates` 扩展到 7 个采样率 + A/C 加权
7. 跑全部 ctest 确认无回归

#### Phase 3：测试与文档（0.5 天）

1. 写频段归一化测试（A1-A4）
2. 写多采样率 A/C 加权测试（B1-B16）
3. 更新 `examples/main.cpp` demo（增加 8k 采样率 demo）
4. 更新 README.md / AGENTS.md
5. 更新 `docs/DEVELOPMENT_PLAN_v3.2.md` 状态为"已实施"，记录实施结果

### 5.3 时间表

| 阶段 | 预计时间 | 累计 |
|------|---------|------|
| Phase 1 | 1 天 | 1 天 |
| Phase 2 | 1.5 天 | 2.5 天 |
| Phase 3 | 0.5 天 | 3 天 |

**总工作量**：3 天（1 人）。

---

## 六、与 v3.1.3 的兼容性

| 接口/结构 | v3.1.3 状态 | v3.2 后状态 |
|----------|------------|------------|
| `process_segment()` 签名 | 不变 | 不变 |
| `aggregate_metrics()` 签名 | 不变 | 不变 |
| `SecondMetrics` 字段 | 不变 | **字段值变化**（频段 SPL 绝对值会变） |
| `MinuteMetrics` 字段 | 不变 | **字段值变化**（频段 SPL 累加值会变） |
| `DoseState` / 4 个 dose 函数 | 不变 | 不变 |
| `EventDetector::check_segment()` | 不变 | 不变 |

**接口签名 0 改动**，但 `SecondMetrics` / `MinuteMetrics` 的 `freq_*hz_spl` 字段值会变（这是修复目的）。下游读取频段绝对值的代码**预期会看到不同数字**（但更正确）。

---

## 七、验收标准

- [ ] `bandpass()` 系数归一化后，1kHz 正弦波输入下 1kHz 频段 SPL ≈ LAeq（±3 dB）
- [ ] 宽频带噪声输入下，9 个频段 SPL 标准差 < 5 dB
- [ ] 8k/16k/44.1k 采样率下，LAeq 不再为 NaN（在合理范围 [60, 110] dB）
- [ ] A 加权在 1 kHz 处增益 = 0 dB（±0.5 dB，符合 IEC 61672）
- [ ] C 加权在 1 kHz 处增益 = 0 dB（±0.5 dB）
- [ ] test_noise_processor 12/12 全部通过（含 1/3 oct 多频段测试）
- [ ] test_event_detector 14/14 全部通过
- [ ] test_dose_state 9/9 全部通过
- [ ] dose_validator 通过
- [ ] examples/main.cpp demo 频段输出有意义（1kHz 频段最大）
- [ ] AGENTS.md 移除两条 known issues（v3.2 已修复）

---

## 八、后续扩展方向（v3.3+）

1. **IEC 61672 1 级合规验证**：A/C 加权频率响应表全表对照
2. **1/1 oct / 1/6 oct 支持**：扩展频段分辨率
3. **频段时变事件检测**：在特定频段做事件检测（区分高低频噪声源）
4. **自适应 A 加权阈值**：根据环境噪声自动调整 A 加权参数

---

## 九、变更日志摘要

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1 (草稿) | 2026-06-04 | 初版，等待审阅 |
| 0.2 (草稿) | 2026-06-04 | 修复疏漏：Bug B 范围说明明确包含 C 加权；§3.1.1 标注 A 和 C 加权共用回退路径；§3.1.3 实测表加 LCeq 列；§3.2.1 方案描述明确 A 和 C 都加 pre-warp；§3.2.2 实施步骤明确 A 和 C 一起处理；§4.2 加 C 加权测试 B6-B10；§5.1 文件清单加 C 加权系数 |
| **1.0 (已批准)** | **2026-06-04** | **用户拍板决策**（§零）：Bug A 同意通带中心增益归一化；Bug B 走方案 C 多采样率预存表支持 7 个采样率（8k/16k/22.05k/32k/44.1k/48k/96k）；A+C 一起修确认；§4.2 扩展到 B1-B16（含 C 加权 B9-B14 和表外采样率 B16）；§5.1 文件清单加入 `weighting_coefficients_multirate.hpp` 新文件；状态从"草稿"升级为"已批准，待实施" |

---

*文档版本：1.0 (已批准)*
*下一步：实施 Phase 1（Bug A 修复）*
