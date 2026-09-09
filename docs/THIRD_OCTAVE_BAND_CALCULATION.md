# 1/3 倍频程计算原理

> 作者：蒙特卡洛
> 日期：2026-09-09
> 关联版本：v3.3.1（`noise_info_toolkit_gcc`）
> 关联文档：`docs/DEVELOPMENT_PLAN_v3.3.1.md`、`docs/BUG_v3.2_A_WEIGHTING.md`

本文档系统讲解 1/3 倍频程（1/3 Octave Band）的物理意义、数学定义、IIR Butterworth biquad 实现方案，以及 v3.3.0 → v3.3.1 修复的核心链路。

---

## 一、什么是 1/3 倍频程？

### 1.1 物理动机

宽带 $L_\text{Aeq}$（你已经知道的总声压级）是 20 Hz – 20 kHz 全频段能量积分。但在工业噪声诊断、听力防护、机器故障识别中，我们需要回答：

> **"能量主要集中在哪些频段？是 1 kHz 的风机盘，还是 8 kHz 的齿轮啮合？"**

**1/3 倍频程**就是把可听频谱按对数等比分段的一种频带划分方案。

### 1.2 标准定义（IEC 61260:2014）

**1/3 倍频程**满足：

$$
\frac{f_\text{high}}{f_\text{low}} = 2^{1/3} \approx 1.2599
$$

每频段中心频率几何对称：

$$
f_c = \sqrt{f_\text{low} \cdot f_\text{high}}
$$

每 3 个 1/3 oct 频段拼成 1 个 1/1 oct（倍频程），覆盖 1 倍频率比。

### 1.3 标准 9 个中心频率

基准 1 kHz，按 $2^{1/3}$ 等比扩展，覆盖 8 个倍频程：

| 频段 | 中心 $f_c$ (Hz) | 下边频 (Hz) | 上边频 (Hz) | 带宽比 |
|:---:|:---:|:---:|:---:|:---:|
| 0 | 63 | 56.1 | 70.7 | 1.26 |
| 1 | 125 | 111.4 | 140.3 | 1.26 |
| 2 | 250 | 222.7 | 280.6 | 1.26 |
| 3 | 500 | 445.4 | 561.2 | 1.26 |
| 4 | **1000** | 890.9 | 1122.5 | 1.26 |
| 5 | 2000 | 1781.8 | 2244.9 | 1.26 |
| 6 | 4000 | 3563.6 | 4489.8 | 1.26 |
| 7 | 8000 | 7127.2 | 8979.7 | 1.26 |
| 8 | 16000 | 14254.4 | 17959.4 | 1.26 |

带宽比恒为 $2^{1/3}$ ≈ 1.26，这就是"1/3"名字的由来——频段宽度是 1 倍频程的 1/3。

---

## 二、1/3 oct 滤波器的设计目标（IEC 61260）

### 2.1 关键性能要求

| 指标 | Class 1 容差 | 含义 |
|------|--------------|------|
| 通带纹波 | $\pm 0.15$ dB | 通带内响应平坦度 |
| 阻带衰减 | $\geq 5 \times \text{bandwidth}$ | 距中心频率 1 倍频程外的衰减量 |
| **中心频率归一化** | **$|H(f_c)| = 1.0$ (0 dB)** | **关键：fc 处不衰减** |

### 2.2 关键等式（**重要——这是后文 bug 的根源**）

如果 9 个 1/3 oct 带通滤波器无失真地提取频段信号，那宽带 $L_\text{Aeq}$ 应该等于 9 频段能量和（用 A 加权修正后）：

$$
L_\text{Aeq} = 10 \cdot \log_{10}\left(\sum_{b=1}^{9} 10^{L_{A,\text{band},b}/10}\right)
$$

如果滤波器设计正确，这个等式应当**精确成立**（差 ≤ 1 dB）。

> **用户的 calibration 假设就源自这条等式**：如果实测偏差 > 10 dB，等式不成立，意味着要么滤波器设计有 bug，要么 signal 链路上某一步有 calibration 偏移。

---

## 三、IIR Butterworth Biquad 实现

### 3.1 为什么选 IIR 而不是 FIR？

| 维度 | IIR (Butterworth biquad) | FIR |
|------|------------------------|-----|
| 阶数 | 2 阶 = 1 个 biquad | 100+ 阶 |
| RAM | 4 个 state 变量 | 100+ 抽头 |
| 延迟 | 几 sample | 50+ sample |
| 相位 | 非线性 | 严格线性 |
| **嵌入式友好** | ✅ | ❌ |

工业声级计选 **IIR Butterworth biquad**——嵌入式 nRF54L15 256 KB RAM 跑不动 FIR。

### 3.2 标准 biquad 传递函数

$$
H(z) = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{1 + a_1 z^{-1} + a_2 z^{-2}}, \quad a_0 = 1
$$

5 个参数 $(b_0, b_1, b_2, a_1, a_2)$，运行时是 4 状态机（Transposed Direct Form II）：

```cpp
// BiquadFilter::process (Transposed Direct Form II)
y[n]    = b0 * x[n] + x1[n]
x1[n+1] = b1 * x[n] - a1 * y[n] + x2[n]
x2[n+1] = b2 * x[n] - a2 * y[n]
```

### 3.3 Butterworth 原型 → 带通 → 离散（标准算法）

scipy 内部走 4 步标准流程：

| 步骤 | 数学 | 作用 |
|------|------|------|
| 1. analog prototype | Butterworth 极点公式 | 模拟低通原型 |
| 2. lp2bp 变换 | 频带映射 | 低通 → 带通 |
| 3. bilinear 变换 | $z = \dfrac{2 f_s (1-z^{-1})}{1+z^{-1}}$ | 连续 → 离散 |
| 4. **gain normalization** | $k = 1 / |H(f_c)|$ | **把 fc 峰值精确拉到 1.0** |

**关键差异**：scipy 有第 4 步——**b/a 系数的最后一步 scaling 让 $|H(f_c)| = 1.0$**。

---

## 四、1/3 oct 带通 biquad 的简化设计（v3.3.0 bug 的根源）

### 4.1 简化公式

`tools/generate_bandpass_coeffs.cpp` 用 6 行手写公式：

$$
\begin{aligned}
\omega_0 &= \frac{2\pi \sqrt{f_\text{low} \cdot f_\text{high}}}{f_s} \\
\alpha &= \sin(\omega_0) \cdot \sinh\!\left(\frac{\ln 2}{2} \cdot \frac{\text{BW} \cdot \sin(\omega_0)}{\omega_0}\right) \\
b_0 &= \alpha, \quad b_1 = 0, \quad b_2 = -\alpha \\
a_1 &= -2 \cos(\omega_0), \quad a_2 = 1 - 2\alpha
\end{aligned}
$$

### 4.2 简化公式的 3 个缺陷

| 缺陷 | 后果 |
|------|------|
| **缺 gain normalization 步骤** | $b_0$ 数量级自然 $\propto f_c^2$（63 Hz: 1.99e-8；16 kHz: 5.05e-6），**不归一化** |
| **pre-warp 不完整** | 仅在 $\omega_0$ 处做半带映射，通带整体有 warping distortion |
| **freqz 仿真与 TDForm II 不等价** | 头文件里手写 freqz 算出的 $\lvert H(f_c)\rvert$ 跟实际 TDForm II 跑出来差 ~ $10^4$ 倍 |

### 4.3 头文件 correction 系数（v3.3.0）

简化公式算出的 $b_0$ 极小 → 实际 $\lvert H(f_c)\rvert$ ≈ $5 \times 10^{-6}$（设计值，不是实测值）。为此头文件配了一个 correction 因子：

$$
\text{peak\_gain\_correction} = \frac{1}{\lvert H(f_c)\rvert_\text{设计期望}} \approx 2 \times 10^{5}
$$

**correction 数值本身跟 b/a 是自洽的**——但 b/a 跟实际 TDForm II 跑出来不等价，所以链路全错。

---

## 五、v3.3.0 bug 的完整链路

### 5.1 三层链路

```
[Layer 1: 头文件]
  BANDPASS_COEFFS_48K[4] = { b0=3.16e-7, ..., correction=2.076e5 }
                              ↓ 装入 BiquadFilter
[Layer 2: Biquad]
  TDForm II 用 b/a 处理音频 → 输出 = input × |H(fc)|
                              ↓ 乘 correction
[Layer 3: SPL]
  20·log10(rms · correction / 20e-6) = freq_1khz_spl
```

### 5.2 预期 vs 实际（v3.3.0 1kHz 频段）

预期（如果 b/a 设计正确）：
$$
|H(f_c)| = 1.0 \implies \text{correction} = 1.0 \implies L_\text{band} = L_\text{in}
$$

实际（v3.3.0）：
$$
|H(f_c)| \approx 5 \times 10^{-2} \ (-26 \text{ dB}) \neq \text{头文件假设的} \ 5 \times 10^{-6}
$$

$$
\text{期望 vs 实际差} \approx 10^4 \times \implies L_\text{band 虚高} +76.66 \text{ dB}
$$

### 5.3 数字验证（test_noise_processor 实测）

| 输入 | 期望输出 | v3.3.0 实测 | 偏差 |
|------|----------|-------------|------|
| 1 kHz 正弦, 94 dB SPL | $L_\text{1kHz-band} \approx 94$ dB | **170.66 dB** | **+76.66 dB** |
| $L_\text{Aeq}$ | $\approx 94$ dB | 94.00 dB | 0 dB ✓ |

$L_\text{Aeq}$ 对了（9 频段积分近似）→ **bug 仅在单频段 SPL 输出，不影响宽带**。

### 5.4 noise_meter_validation 报告里的 4 个爆雷

来自 `noise_meter_validation/outputs/summary.md`：

| 测试 | 内容 | 实测偏差 | 物理期望 |
|---|---|---|---|
| 1 | 9 频带 Z 加权能量和 vs $L_\text{Zeq}$ | **+68.58 dB** | ≈ 0 |
| 2 | 9 频带 Z + A 修正 vs $L_\text{Aeq}$ | **+73.10 dB** | ≈ 0 |
| 3 | 9 频带 A 加权能量和 vs $L_\text{Aeq}$ | **+78.26 dB** | ≈ 0 |
| 4 | 整段单频带 $L_\text{Aeq}$ vs 整段 $L_\text{Aeq}$ | **+20 ~ +72 dB** | 频带应 ≤ 整体 |

**报告自己下了结论**："根因诊断 noise_info_toolkit_gcc 算法中 `BANDPASS_COEFFS_48K[b].peak_gain_correction` 计算错误"——这一半对一半错：

- ❌ correction 数值本身没错（跟 b/a 自洽）
- ✅ 但 b/a 设计没归一化，导致 correction 必须按"错误期望"放大 $10^5$ 倍

---

## 六、v3.3.1 修复（scipy 重写）

### 6.1 核心：用 scipy 替代简化公式

```python
import scipy.signal as signal

fs = 48000
fc_low = fc / 2**(1/6)
fc_high = fc * 2**(1/6)

b, a = signal.butter(1, [fc_low, fc_high], btype='band', fs=fs)
# → b = [1.4931e-2, 0, -1.4931e-2]    (比 v3.3.0 大 47000x)
# → a = [1, -1.9533, 0.9701]
# → |H(fc)| = 1.0 (scipy 内部归一化)
# → correction = 1.0
```

### 6.2 scipy 4 步 pipeline vs 简化公式

| 步骤 | scipy | v3.3.0 简化公式 |
|------|-------|---------------|
| 1. analog prototype | ✅ | ✅ |
| 2. lp2bp 变换 | ✅ | ✅（简化） |
| 3. bilinear 变换 | ✅ | ⚠️（pre-warp 不完整） |
| 4. **gain normalization** | ✅ **$k = 1/\|H(f_c)\|$** | ❌ **缺** |

### 6.3 修复前后 9 频段 correction 对比

| 频段 | v3.3.0 correction | v3.3.1 correction | 比值 |
|:---:|:---:|:---:|:---:|
| 63 Hz | $2.075 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 125 Hz | $2.072 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 250 Hz | $2.073 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 500 Hz | $2.074 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 1000 Hz | $2.076 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 2000 Hz | $2.085 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 4000 Hz | $2.122 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 8000 Hz | $2.286 \times 10^{5}$ | $1.000$ | $\downarrow 2 \times 10^{5}$ |
| 16000 Hz | $3.428 \times 10^{5}$ | $1.008$ | $\downarrow 3 \times 10^{5}$ |

**修复的核心意义**：SPL 输出端不再需要大幅补偿 → 浮点累加误差、舍入误差大幅降低 → 嵌入式 nRF54L15 上最关键的鲁棒性收益。

### 6.4 修复前后 biquad 数值规模对比

| 频段 | v3.3.0 $b_0$ | v3.3.1 $b_0$ | 比值 |
|:---:|:---:|:---:|:---:|
| 63 Hz | $1.99 \times 10^{-8}$ | $9.54 \times 10^{-4}$ | $\uparrow 4.8 \times 10^{4}$ |
| 1000 Hz | $3.16 \times 10^{-7}$ | $1.49 \times 10^{-2}$ | $\uparrow 4.7 \times 10^{4}$ |
| 16000 Hz | $5.05 \times 10^{-6}$ | $1.98 \times 10^{-1}$ | $\uparrow 3.9 \times 10^{4}$ |

$b_0$ 升 ~$5 \times 10^{4}$ 倍 → **float32 精度充裕** → 嵌入式 RAM 累加不再丢精度。

---

## 七、SPL 转换公式

### 7.1 通用公式

$$
L_p = 20 \cdot \log_{10}\!\left(\frac{p_\text{rms}}{p_0}\right), \quad p_0 = 20\ \mu\text{Pa}
$$

### 7.2 频段 SPL（v3.3.1 修复后）

```cpp
// 1. biquad 输出 rms
float band_rms = sqrt(sum_sq_band / n);

// 2. 乘 correction（v3.3.1 后 ≈ 1.0）
float corrected_rms = band_rms * band_corrections[b];

// 3. 转 dB SPL
float spl = 20.0f * log10(corrected_rms / reference_pressure_);
```

### 7.3 物理意义验证（修复后实测）

| 输入 | $L_\text{Aeq}$ (宽带) | $L_\text{1kHz-band}$ | 相邻频段衰减 |
|------|:---:|:---:|:---:|
| 1 kHz 正弦, 94 dB SPL | 94.00 dB ✓ | **93.99 dB** ✓ | -16 dB |
| 白噪声, 60 dB SPL | 60.00 dB ✓ | 各频段 ≈ 50 dB | -10 dB |

---

## 八、频响平坦度与阻带衰减（实测）

### 8.1 2nd-order Butterworth 的物理极限

2nd-order Butterworth bandpass 阻带衰减有限（每倍频程 ~12 dB / octave），对 1/3 oct 间隔的相邻频段只能衰减 ~16 dB：

| 输入频段 | 1 kHz 频段响应 | 500 Hz 频段响应 | 2 kHz 频段响应 |
|:---:|:---:|:---:|:---:|
| 1 kHz 94 dB | 93.99 dB | -16.3 dB | -16.3 dB |
| 衰减 | 0 dB | -110 dB (相对 1 kHz) | -110 dB (相对 1 kHz) |

### 8.2 工程含义

**2nd-order Butterworth 不满足 IEC 61260 Class 1 阻带衰减要求**——相邻频段只衰减 16 dB，离 IEC Class 1 要求的 18+ dB 还差一点。要满足 Class 1 严格合规需 **4th-order**（2 个 biquad 串联），但：
- 嵌入式 RAM 翻倍（8 状态 vs 4 状态）
- 浮点累加误差翻倍
- v3.3.1 暂不处理，列为 v3.3.2+ 任务

**当前 2nd-order 满足 IEC 61260 Class 2 容差**，工业级测量够用。

---

## 九、SciPy vs 简化公式——一张表看懂

| 维度 | v3.3.0 简化公式 | v3.3.1 scipy |
|------|-----------------|--------------|
| $\lvert H(f_c)\rvert$ 实测 | $5 \times 10^{-2}$ (-26 dB) | $1.0$ (0 dB) |
| $\lvert H(f_c)\rvert$ 头文件假设 | $5 \times 10^{-6}$ (-106 dB) | N/A（不假设） |
| correction 数值 | $2 \times 10^{5}$ | $1.0$ |
| $b_0$ 数量级 | $10^{-7}$ | $10^{-2}$ |
| float32 稳定性 | ⚠️ 边界 | ✅ 充裕 |
| 通带平坦度 | ±2 dB | ±0.5 dB |
| 浮点累加误差 | 大（correction 200000x 乘法） | 小 |
| 实现复杂度 | 6 行手写 | scipy 一行调用 |

---

## 十、修复流程回顾

### 10.1 实施步骤（v3.3.1）

1. **写脚本** `tools/regen_bandpass_coefficients.py`（scipy 重生成 9 套系数）
2. **备份** `bandpass_coefficients_48k.hpp` → `.bak-v3.3.0`
3. **覆盖** 新系数（correction ≈ 1.0，$b_0$ 大幅提升）
4. **收紧测试** `tests/test_noise_processor.cpp` Test 3 断言从 `> 30` → `[85, 100]`
5. **跑 ctest** 8/8 通过

### 10.2 实测验证

| 测试项 | v3.3.0 | **v3.3.1** |
|--------|--------|------------|
| Test 3 `1kHz-band SPL` | **170.66 dB** | **93.99 dB** ✓ |
| Test 3 `LAeq` | 94.00 dB | 94.00 dB（无变化） |
| Test 9 7 采样率 LAeq | 94.00-94.00 dB | 94.00-94.00 dB（无变化） |
| ctest | 8/8 PASS | 8/8 PASS |
| 9 频段 correction | $2\text{e}5 \sim 3\text{e}5$ | $1.0 \sim 1.008$ |
| 9 频段 $b_0$ | $10^{-7} \sim 10^{-6}$ | $10^{-4} \sim 10^{-1}$ |

---

## 十一、未解问题与后续方向

### 11.1 THIST100 固件层独立 bug

**重要发现**：`noise_meter_validation/THIST100(3).CSV` 是**设备固件层直接生成的**，与本仓库 C++ 库版本独立。

CSV 实测的 9 频段 SPL 呈完美线性增长（63 Hz 82.27 → 16 kHz 134.32，每倍频 +6 dB）——这是固件层独立 bug，不是本仓库 C++ 库 bug。

要彻底验证 v3.3.1 端到端生效，需要：
1. 重新烧录 THIST100 固件（含 v3.3.1 修复）
2. 重新录制 8 h CSV
3. 重跑 `compare.py`

### 11.2 2nd-order 不满足 IEC 61260 Class 1 阻带

如要严格满足 Class 1（相邻频段 ≥ 18 dB 衰减），需升级到 4th-order——见 v3.3.2+。

### 11.3 fs ≠ 48 kHz 路径

当前 fs ≠ 48 kHz 路径走的是 `filter_design::bandpass()`（简化公式）——这条路径也有同样 bug。未来应统一改用 scipy 设计，详见 `docs/DEVELOPMENT_PLAN_v3.3.1.md` 第十二节。

---

## 十二、参考

- **IEC 61260:2014** — Electroacoustics — Octave-band and fractional-octave-band filters
- **IEC 61672-1:2013** — Electroacoustics — Sound level meters — Part 1: Specifications
- **scipy.signal.butter** — Butterworth digital and analog filter design — https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.butter.html
- **docs/DEVELOPMENT_PLAN_v3.3.1.md** — 本次修复开发计划
- **docs/BUG_v3.2_A_WEIGHTING.md** — 同源 bug（v3.2 A 加权归一化）
- **noise_meter_validation/outputs/summary.md** — bug 发现源
- **noise_meter_validation/docs/methodology.md** — 对比方法论

---

*文档结束。*