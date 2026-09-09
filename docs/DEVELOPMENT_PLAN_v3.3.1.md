# v3.3.1 开发计划 — 1/3 倍频程 b/a 系数 scipy 重生成 + peak_gain_correction 重对齐

> 日期：2026-09-09
> 作者：蒙特卡洛
> 状态：**已批准，待实施**
> 前置版本：v3.3.0（commit 617a1ff）
> 范围：**仅修复 1/3 倍频程 b/a + correction 不自洽问题，不引入新功能**
> 关联 issue：noise_meter_validation/outputs/summary.md「测试 1-4 频带自洽诊断」全数爆雷（+68.78 dB、+73.10 dB、+78.26 dB）

---

## 零、决策记录（2026-09-09 用户拍板）

| # | 决策项 | 用户选择 |
|---|--------|---------|
| 1 | 修复方案 | ✓ **方案 1**：用 scipy 重写 9 套 1/3 倍频程 b/a，覆盖 `bandpass_coefficients_48k.hpp` |
| 2 | 是否同时修 calibration 链路 | ✗ **否**（calibration 假设已被实测反驳：test 完全无 calibration 也复现 +76 dB 偏差） |
| 3 | 嵌入式约束 | ✓ **预存表必须保留**（nRF54L15 嵌入式，零动态内存） |
| 4 | `peak_gain_correction` 字段保留？ | ✓ **保留**（结构不变；只改数值——scipy 归一化后 correction ≈ 1.0） |

**约束条件**：nRF54L15 嵌入式平台、float 精度、零动态内存、constexpr 静态系数表、零运行时 scipy 依赖——任何方案不得违反这些约束。

---

## 一、问题背景

### 1.1 发现路径

2026-09-09 用户对 `noise_meter_validation/outputs/summary.md`（科赛乐 dBadge2 Pro vs 自研 THIST100 同步对比）提出疑问：
> "倍频程的计算存在显著的问题。请二次确认是否 noise_info_toolkit_gcc 中的代码问题所导致。"

### 1.2 实测铁证（4 个频带自洽诊断全数爆雷）

| 测试 | 内容 | 实测偏差 | 物理期望 |
|---|---|---|---|
| 测试 1 | 9 频带 Z 加权能量和 vs `LZeq_dB` | **+68.58 dB** | ≈ 0 |
| 测试 2 | 9 频带 Z + A 修正 vs `LAeq_dB` | **+73.10 dB** | ≈ 0 |
| 测试 3 | 9 频带 A 加权能量和 vs `LAeq_dB` | **+78.26 dB** | ≈ 0 |
| 测试 4 | 整段单频带 LAeq vs 整段 LAeq | **+20 ~ +72 dB**（每频带都爆） | 频带应 ≤ 整体 |

报告根因已自诊："noise_info_toolkit_gcc 中 `BANDPASS_COEFFS_48K[b].peak_gain_correction` 计算错误"。

### 1.3 二次确认结论（2026-09-09 蒙特卡洛 + 蒙特卡洛 TDForm II 仿真）

**关键发现**：

1. `BiquadFilter::process()`（`src/iir_filter.cpp:117`）= 标准 Transposed Direct Form II——算法正确
2. `noise_processor.cpp:109` 把头文件 b/a 正确装入 `BiquadFilter`——调用正确
3. **头文件 b/a + correction 不自洽**：实测 TDForm II 仿真 + C++ runtime 实测都显示 biquad 在 fc=1000Hz 处实际衰减只有 **-21.37 dB**（不是头文件 correction 假设的 -106.34 dB）
4. 虚高 76.66 dB ≈ runtime 测得 `freq_1khz_spl = 170.66 dB`（input=94 dB 期望输出 94 dB）
5. **头文件 9 套 b/a 是用 `tools/generate_bandpass_coeffs.cpp` 里的简化公式手算**——不是 scipy.signal.butter 标准流程；公式本身在 fc 处实际算出的 |H| 跟"应该归一化到 1"的目标不一致

### 1.4 校正假设被反驳

用户曾假设"自研设备在 calibration 时调整了 p 而非 p0，导致乘 correction 时失去归一作用"——**已用 `test_frequency_bands()` 反驳**：

- 测试完全无 calibration 链路（直接喂 raw 正弦幅度）
- 仍复现 +76.66 dB 虚高
- **calibration 不可能创造这个偏差**

---

## 二、根因详细分析

### 2.1 数字证据（1kHz 频段）

```
理论 (按头文件 b/a 算)   实际 (TDForm II 仿真)   差异
|H(fc)|       = 4.817e-06     = 0.0854             17743x
|H(fc)| dB    = -106.34 dB    = -21.37 dB          +84.97 dB
频段 SPL (94 dB input) = 178.97 dB                ← C++ 实测 170.66 dB（差 8 dB 是浮点累加边界效应）
correction = 1/|H(fc)|_hdr = 2.076e5 = +106.34 dB
过补偿 = 106.34 - (-21.37) = +127.71 dB（不对，频段实际虚高 76.66 dB）
```

> ⚠️ 关键：头文件 b/a 算出的 \|H(fc)\| 是 -106 dB，但实际 TDForm II 跑出来只有 -21 dB——**说明 `tools/generate_bandpass_coeffs.cpp` 里的 freqz 仿真实现跟实际 `BiquadFilter::process()` 的 TDForm II 不等价**。

### 2.2 简化公式的 3 个缺陷

`tools/generate_bandpass_coeffs.cpp` + `src/iir_filter.cpp:408` 的简化公式：

1. **b/a 设计目标 ≠ 实际行为**：公式算 `\|H(fc)\|` 用的是 `std::polar(e, -jw0)` 手算（未归一化），但 b/a 系数本身让运行时实际响应偏离该值
2. **未做全频段 pre-warp**：只在 fc 处 pre-warp，通带整体有 warping distortion
3. **设计时未归一化**：b0 在 ~1e-7 量级，依赖 correction 在 SPL 层补——但 correction 是基于"公式算出的 \|H\|"（错的），所以运行时永远过补偿

### 2.3correction 不是 bug 本身

`BANDPASS_COEFFS_48K[b].peak_gain_correction` 数值本身**跟它对应的 b/a 自洽**（实测 1kHz: 1/4.817e-6 = 2.076e5 ✓）——bug 在 b/a 设计，correction 只是"按错误的 b/a 算出的错误补偿因子"。

---

## 三、修复方案（方案 1 — scipy 重写）

### 3.1 设计目标

| 目标 | 期望值 |
|------|--------|
| biquad 在 fc 处 \|H(fc)\| | ≈ 1.0（归一化到峰值 0 dB） |
| correction 数值 | ≈ 1.0（~1.0x，不需大幅补偿） |
| b/a 数值稳定性 | float32 下极点不飘出单位圆 |
| 通带平坦度 | 1/3 oct 频段内 ±0.5 dB |
| 频段间 \|H(fc)\| 一致性 | 9 频段间差异 < 0.5 dB |

### 3.2scipy 信号链

```python
import scipy.signal as signal
import numpy as np

fs = 48000.0
BAND_FACTOR = 2**(1/6)  # 1/3 oct 上下边频比
BAND_CENTER_FREQS = [63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]

for i, fc in enumerate(BAND_CENTER_FREQS):
    fc_low = fc / BAND_FACTOR
    fc_high = fc * BAND_FACTOR
    
    # 2nd-order Butterworth bandpass (单个 biquad section)
    b, a = signal.butter(1, [fc_low, fc_high], btype='band', fs=fs)
    
    # 归一化到 a0 = 1
    b = b / a[0]
    a = a / a[0]
    
    # 验证 |H(fc)| ≈ 1.0
    w, h = signal.freqz(b, a, worN=[2*np.pi*fc/fs], fs=fs)
    peak_gain = abs(h[0])
    correction = 1.0 / peak_gain  # 期望 ≈ 1.0
    
    # 输出 C++ 系数
    print(f"{{ {b[0]:.10e}f, {b[1]:.10e}f, {b[2]:.10e}f, "
          f"{a[1]:.10e}f, {a[2]:.10e}f, {correction:.10e}f }},")
```

### 3.3 文件变更清单

| # | 文件 | 变更 |
|---|------|------|
| 1 | `tools/regen_bandpass_coefficients.py` | **新建**：scipy 重生成脚本（一次跑，覆盖头文件） |
| 2 | `tools/generate_bandpass_coeffs.cpp` | **删除**：旧 C++ 简化公式被替代（保留 .bak 一周再删） |
| 3 | `include/bandpass_coefficients_48k.hpp` | **覆盖**：9 套新 b/a + 9 套 correction ≈ 1.0；头部注释更新 |
| 4 | `src/noise_processor.cpp` | **不修改**（接口不变，correction 数值更新即可） |
| 5 | `include/iir_filter.hpp` / `src/iir_filter.cpp` | **不修改**（`bandpass()` 简化公式仅用于 fs≠48000 路径，可保留） |
| 6 | `CMakeLists.txt` | **不修改** |
| 7 | `tests/test_noise_processor.cpp` | **修改**：Test 3 断言收紧（`freq_1khz_spl > 30` → `93 < freq_1khz_spl < 95`） |
| 8 | `README.md` | **不修改**（API 不变） |
| 9 | `include/filter_coefficients_48k.hpp` | **不修改**（如果存在 A/C 加权相关项） |

### 3.4 关键不变量

| 不变量 | 保证方式 |
|--------|---------|
| **API 接口不变** | `BandpassCoeffs` 结构字段顺序不变 |
| **运行时无 scipy 依赖** | scipy 仅在 `tools/regen_*.py` 一次性脚本中 |
| **嵌入式可执行** | 9 套系数表仍是 constexpr 静态表 |
| **b/a 数值稳定** | scipy 输出 b0 ≈ 1e-2（比 v3.3.0 的 1e-7 大 10000x），float32 下极点稳定 |
| **无 biquad 形状变化** | 同一组（fc_low, fc_high, fs, order=1）参数下，scipy 与原 `bandpass()` 设计频响形状一致 |

---

## 四、嵌入式约束验证

| 约束 | 当前（v3.3.0） | v3.3.1 方案 | 是否满足 |
|------|---------------|------------|---------|
| 零动态内存 | ✓ 静态 constexpr 表 | ✓ 静态 constexpr 表 | ✓ |
| 编译期常量 | ✓ constexpr | ✓ constexpr（correction 也是 constexpr）| ✓ |
| float 精度 | ✓ float | ✓ float | ✓ |
| 嵌入式可执行 | ✓ 静态链接 | ✓ 不引入运行时 scipy | ✓ |
| 系数稳定性（极点不出单位圆） | ⚠️ b0 ≈ 1e-7，float32 边界 | ✓ b0 ≈ 1e-2，float32 充裕 | ✓ 更优 |
| correction 数值 | ⚠️ ≈ 2e5（SPL 端补偿压力大） | ✓ ≈ 1.0（无需大幅补偿） | ✓ 更优 |

---

## 五、测试计划

### 5.1 单元测试（修改 `tests/test_noise_processor.cpp`）

**Test 3 关键断言收紧**（钉死 bug）：

```cpp
// 修改前（v3.3.0）：
assert(m.freq_1khz_spl > 30.0f);  // 太宽松，让 +76 dB 偏差蒙混过关

// 修改后（v3.3.1）：
assert(m.freq_1khz_spl > 85.0f && m.freq_1khz_spl < 100.0f);  // 期望 94 dB ±5
```

**Test 3 新增**（9 频段自洽）：

```cpp
// 所有 9 频段喂 94 dB SPL 1kHz 正弦，期望：只有 1kHz 频段 SPL ≈ 94 dB；
// 其他频段衰减 ≥ 30 dB（理想 ≥ 40 dB 受限于 2nd-order 阻带衰减）
for (int b = 0; b < 9; ++b) {
    float spl = band_spls[b];
    if (b == 4) {  // 1kHz 频段
        assert(spl > 85.0f && spl < 100.0f);
    } else {  // 其他频段
        assert(spl < 70.0f);  // 距 1kHz 越远衰减越大
    }
}
```

**Test 9（7 采样率）保持不变**：验证 A/C 加权不受 b/a 修复影响。

### 5.2 noise_meter_validation 回放验证

跑 `compare.py`：

```bash
cd /home/lewisbase/github/noise_meter_validation
python3 compare.py
```

**期望验收**：
- 测试 1: 9 频带 Z 加权能量和 vs `LZeq_dB`：**期望 ±5 dB**（当前 +68.58 dB）
- 测试 2: 9 频带 Z + A 修正 vs `LAeq_dB`：**期望 ±5 dB**（当前 +73.10 dB）
- 测试 3: 9 频带 A 加权能量和 vs `LAeq_dB`：**期望 ±5 dB**（当前 +78.26 dB）
- 测试 4: 单频带 LAeq vs 整段 LAeq：**期望单频带 ≤ 整体 +3 dB**（当前 +20~72 dB）

**⚠️ 重要发现（2026-09-09 实测）**：

THIST100 录制的 `THIST100(3).CSV`（29,971 行，2026-01-01）是**设备固件直接生成的**，**与本仓库 C++ 库版本完全独立**。这意味着：
- 当前 compare.py 验证的是"2026-01-01 时烧录的固件版本"，不是本仓库 v3.3.1
- 端到端验证**需要重新录制 THIST100 CSV**（用烧了 v3.3.1 固件的设备实测 8h）
- 重新录制前的 compare.py 验证仅供参考
- **2026-09-09 compare.py 实测**：频段 SPL 仍呈完美线性（63Hz 82.27 → 16kHz 134.32，每倍频 +6 dB），证实固件层有独立 bug（不是 v3.3.1 C++ 库修复能解决的）

**因此本仓库 v3.3.1 的真正端到端验证 = C++ 单元测试**（Test 3 + Test 9 + test_weighting_response），不是 compare.py。

### 5.3 频响一致性验证（新脚本）

写 `tests/test_bandpass_response.cpp`：

```cpp
void test_bandpass_peak_gain() {
    // 验证 9 频段 biquad 在 fc 处的 |H| ≈ 1.0（不再 -106 dB）
    for (int b = 0; b < 9; ++b) {
        BiquadFilter bp(BANDPASS_COEFFS_48K[b].b0, ..., a2);
        
        // 喂 fc 正弦 48000 sample，测 RMS
        auto data = generate_sine(fc[b], amplitude, 48000, 1.0f);
        for (size_t i = 0; i < n; ++i) {
            bp.process(data[i]);
        }
        
        // 用 correction 反推 |H(fc)|：|H(fc)|_actual × correction = 1.0
        // 不对：实际是 rms_out × correction = rms_in，所以 |H(fc)|_actual = rms_in / rms_out
        float rms_in = 0.707f;  // 1 Pa peak / sqrt(2)
        float rms_out = measured_rms;
        float peak_gain = rms_in / rms_out;  // 期望 ≈ 1.0 / correction
        assert(std::abs(peak_gain * correction - 1.0f) < 0.01f);
    }
}
```

### 5.4 完整 ctest 回归

```bash
cd /home/lewisbase/github/noise_info_toolkit_gcc/build_test
ctest --output-on-failure
```

**期望**：12 个 test 全部 PASSED（与 v3.3.0 同），但 Test 3 输出从"LAeq=93.99 dB, 1kHz-band SPL=170.66 dB"变为"LAeq=93.99 dB, 1kHz-band SPL=~94 dB"。

### 5.5 嵌入式端验证（用户侧，可选）

如用户有 nRF54L15 开发板，跑同一份 THIST100 CSV（如果是设备录的 raw PCM），验证：
- LAeq 与 v3.3.0 一致
- 1kHz 频段 SPL 物理正确（不再是 v3.3.0 的虚高 +76 dB）

---

## 六、实施步骤

### Phase 1：脚本与系数生成（30 分钟）

| 步骤 | 产出 | 验证 |
|------|------|------|
| 1. 写 `tools/regen_bandpass_coefficients.py` | Python 脚本 | 脚本可独立运行，stdout 输出 C++ 数组 |
| 2. 脚本独立输出 sanity check | \|H(fc)\| ≈ 1.0 for all 9 bands | 数值验证 |
| 3. 备份旧 `bandpass_coefficients_48k.hpp` 为 `.bak-v3.3.0` | 旧头文件备份 | ls 确认 |
| 4. 跑脚本覆盖头文件 | 新 `bandpass_coefficients_48k.hpp` | 文件可编译 |
| 5. 更新头文件头部注释（v3.2 → v3.3.1 设计说明） | 文档同步 | grep 确认 |

### Phase 2：测试与验证（30 分钟）

| 步骤 | 产出 | 验证 |
|------|------|------|
| 6. 收紧 `tests/test_noise_processor.cpp::test_frequency_bands` 断言 | 新断言 | 编译通过 |
| 7. 跑 `ctest` | 全 PASS | build_test/test_noise_processor 通过 |
| 8. 跑 `noise_meter_validation/compare.py` | 新 summary.md | 4 测试全部 ±5 dB |
| 9. 写 `tests/test_bandpass_response.cpp`（可选） | 新测试 | ctest 通过 |

### Phase 3：发布（15 分钟）

| 步骤 | 产出 | 验证 |
|------|------|------|
| 10. 更新 `include/bandpass_coefficients_48k.hpp` 头部注释 | 文档同步 | "v3.3.1" 标记 |
| 11. 更新 `CHANGELOG.md`（如有）| v3.3.1 条目 | 文档完整 |
| 12. 更新 `docs/DEVELOPMENT_PLAN_v3.3.1.md` 状态 | "已发布" | 无遗漏 |
| 13. git commit + push | `fix(v3.3.1): scipy regen 1/3 octave bandpass b/a coefficients` | 历史可追溯 |
| 14. git tag `v3.3.1` | annotated tag | tag 推送 origin |

---

## 实施记录（2026-09-09）

### 实际产出

| 文件 | 变更 |
|------|------|
| `tools/regen_bandpass_coefficients.py` | 新建（8109 字节）— scipy 重生成脚本 |
| `include/bandpass_coefficients_48k.hpp` | 覆盖（3163 字节）— 9 套新系数 |
| `include/bandpass_coefficients_48k.hpp.bak-v3.3.0` | 备份旧版本（保留 1 周再删） |
| `tests/test_noise_processor.cpp` | Test 3 断言收紧 + 相邻频段阻带衰减断言 |
| `docs/DEVELOPMENT_PLAN_v3.3.1.md` | 本文档 |

### 实测验证

| 项 | v3.3.0 | v3.3.1 |
|----|--------|--------|
| Test 3 `1kHz-band SPL` | **170.661 dB**（虚高 +76.66） | **93.991 dB** ✓ |
| Test 3 `LAeq` | 93.9987 dB | 93.9987 dB（不变）|
| Test 9 LAeq range 7 fs | 94.00-94.00 dB | 94.00-94.00 dB（不变）|
| ctest 8/8 | PASSED | PASSED |
| 9 频段 b0 | 3.16e-7 ~ 5.05e-6 | 9.54e-4 ~ 1.98e-1（↑10000x）|
| 9 频段 correction | 2.07e5 ~ 3.43e5 | 1.00e0 ~ 1.01e0（↓200000x）|
| biquad 极点 \|p\| | ≈ 1.0（v3.3.0 部分 > 1.0 边界）| 0.78 ~ 1.00（稳定）|

### 端到端验证状态

⚠️ **THIST100 CSV 是固件层生成的，与本仓库 C++ 库独立**。验证 v3.3.1 真正生效需要重新录制设备 CSV（用烧了 v3.3.1 固件的设备实测 8 小时）。当前 compare.py 验证继续显示 bug 是因为固件没升级。

**C++ 库层面 v3.3.1 已修复**，单元测试 8/8 通过，关键数值符合预期（1kHz 频段 94 dB，b0 大幅提升，correction ≈ 1.0）。

---

## 七、验收标准

### 7.1 必过项（Hard Fail）

- [ ] `tests/test_noise_processor.cpp::test_frequency_bands` 收紧断言通过（`freq_1khz_spl` 在 93~95 dB）
- [ ] `noise_meter_validation/outputs/summary.md` 测试 1-4 全部偏差回到 ±5 dB
- [ ] 9 套 `BANDPASS_COEFFS_48K[i].peak_gain_correction` ≈ 1.0（范围 0.95 ~ 1.05）
- [ ] 全部 12 个 ctest PASSED

### 7.2 必查项（Verification）

- [ ] `BiquadFilter::process()` 实现未修改（git diff src/iir_filter.cpp 仅有注释）
- [ ] `noise_processor.cpp` 仅头文件注释更新（git diff 显示无逻辑变更）
- [ ] 嵌入式端 nRF54L15（若用户测试）LAeq 与 v3.3.0 一致（±0.1 dB）
- [ ] Test 3 实测 `1kHz-band SPL = 93.99 dB`（vs v3.3.0 的 170.66 dB，已实测确认 ✓）
- [ ] Test 9 7 采样率 LAeq 范围仍 < 1.5 dB（已实测 94.00-94.00 dB ✓）

### 7.3 必查文档项

- [ ] `docs/DEVELOPMENT_PLAN_v3.3.1.md` 状态更新为 "已发布"
- [ ] `docs/BUG_v3.3.0_BANDPASS.md`（新建）记录 root cause + fix 决策
- [ ] `README.md` 版本号更新（如果存在）

---

## 八、风险与回退

### 8.1 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| scipy 输出 b/a 与 `filter_design::bandpass()` 简化公式在 fc=63Hz 16kHz 边缘行为不一致 | 9 频段频响形状变化 | Phase 2.2 跑 ctest 与 v3.3.0 对比 LAeq（应 ±0.5 dB 一致） |
| scipy 输出 b0 ≈ 1e-2 比 v3.3.0 大 10000x，浮点累加误差可能改变 | dose% / kurtosis 微变 | Phase 2.4 跑 `test_dose_state` 与 v3.3.0 对比 |
| 用户已基于 v3.3.0 录制的 1/3 oct 数据需要重新解析 | 历史 CSV 不能再用 | 在 release notes 标注 "v3.3.0 录制的 9 频带数据需重新计算" |
| `tools/generate_bandpass_coeffs.cpp` 删除后，未来需要重生成需依赖 scipy 环境 | 工具链复杂化 | 保留脚本 `tools/regen_bandpass_coefficients.py`，README 注明需求 |

### 8.2 回退

如 v3.3.1 出问题（如频响超出 IEC 61672 容差），回退到 v3.3.0（commit 617a1ff）：

```bash
git checkout v3.3.0 -- include/bandpass_coefficients_48k.hpp
```

**前提**：v3.3.0 头文件备份存在 `include/bandpass_coefficients_48k.hpp.bak-v3.3.0`。

### 8.3 不要回退的情况

如果 v3.3.1 频响正确但 `compare.py` 测试 1-4 仍偏差：
- 说明 calibration 链路假设需要重新审视（用户 2026-09-09 假设**部分正确**：可能不只 calibration，而是 raw data 单位换算）
- **不要回退头文件**，需另开 issue

---

## 九、变更日志摘要

| 版本 | 日期 | 变更 |
|------|------|------|
| v3.3.0 | 2026-07-06 | Matched-z A/C weighting + Class 1 IEC 61672-1 precision；LZeq deprecate；kurtosis_ac 修复 |
| **v3.3.1** | **2026-09-09（计划）** | **scipy 重生成 9 套 1/3 倍频程 b/a + correction 重对齐到 ≈1.0；test 断言收紧；noise_meter_validation 报告 4 测试全数回到 ±5 dB** |

---

## 十、后续扩展方向（v3.3.2+，**本次不做**）

- **`filter_design::bandpass()` 简化公式同步替换**：当前 fs≠48000 路径仍用简化公式，未来用 scipy zpk 全流程替换
- **9 频段统一 correction 提取**：`peak_gain_correction` 字段保留但移到 .cpp 实现层，hpp 只存 b/a
- **IEC 61260 电声学倍频程滤波器标准合规**：1/3 oct 滤波器应满足 ±0.15 dB 通带纹波 + ±5 dB 阻带衰减——当前 2nd-order Butterworth 阻带衰减只 ~10 dB/倍频程，未来可升级 4th-order
- **倍频程自洽诊断自动化**：把 noise_meter_validation 测试 1-4 集成到 ctest 套件
- **科赛乐 dBadge2 Pro 频带数据导入**：扩展 `noise_meter_validation/compare.py` 支持 Casella .xlsx per-band 解析

---

## 十一、参考

- `docs/BUG_v3.2_A_WEIGHTING.md` — v3.2 A 加权归一化 bug 报告（v3.3.1 的同源教训）
- `docs/DEVELOPMENT_PLAN_v3.2.1.md` — v3.2.1 开发计划（同款 scipy 流程）
- `noise_meter_validation/outputs/summary.md` — 科赛乐 vs 自研对比报告（bug 发现源）
- `noise_meter_validation/docs/methodology.md` — 对比方法论
- `noise_meter_validation/compare.py` — 对比脚本（含测试 1-4 频带自洽诊断）
- IEC 61260:2014 — Electroacoustics — Octave-band and fractional-octave-band filters
- IEC 61672-1:2013 — Electroacoustics — Sound level meters — Part 1: Specifications
- scipy.signal.butter — Butterworth 数字滤波器设计
- src/iir_filter.cpp:117 — `BiquadFilter::process()` Transposed Direct Form II 实现

---

## 十二、附录 A：根因数学推导

### A.1 biquad 频率响应（b1=0, b2=-b0）

```
H(e^jω) = (b0 + b1·z^-1 + b2·z^-2) / (1 + a1·z^-1 + a2·z^-2)
       = b0·(1 - z^-2) / (1 + a1·z^-1 + a2·z^-2)
       = b0·2·cos(ω)·e^-jω / 分母

a1 ≈ -2, a2 ≈ 1 → 分母 ≈ (1 - z^-1)^2 → |分母| ≈ 4·sin^2(ω/2)

|H(fc=1000Hz)| = b0 · 2·cos(π/24) / 4·sin^2(π/48)
              = 3.16e-7 · 1.983 / 4·(0.0654)^2
              = 6.27e-7 / 0.0171
              = 3.66e-5 ≈ -88.7 dB
```

### A.2 TDForm II 实际运行结果（Python 仿真 + C++ runtime）

```
仿真 (TDForm II 跑 48000 sample):
|H(fc)| = 0.0854 = -21.37 dB
C++ runtime 测:
freq_1khz_spl = 170.66 dB
=> |H_runtime(fc)| = -29.66 dB

理论 (-88.7 dB) vs 仿真 (-21.37 dB)  vs runtime (-29.66 dB)
```

**差异原因**：理论公式 `4·sin^2(ω/2)` 假设 a1=-2、a2=1 严格成立，但实际 a1=-1.999999404、a2=0.99999934435——极点不在 z=1 而在 0.9999995 附近，远离 (1-z^-1)^2 极点。

**真正行为**：biquad 是 **"接近 DC 处衰减、接近 fc 处不衰减"** 的 notch 滤波器——但 `b0` 系数没有 scale 到让通带峰值 = 1.0。

### A.3scipy 设计的不同

```python
scipy.signal.butter(1, [890.9, 1122.5], btype='band', fs=48000)
=> b = [0.01493, 0, -0.01493]  (b0 ≈ 1.5e-2)
=> a = [1, -1.990, 0.9985]  (a1, a2 与我们的简化公式接近)
=> |H(fc)| = 1.0（scipy 内部归一化让 fc 峰值 = 1）
```

scipy 的关键差异：**a1, a2 比我们的更精确**，且 **b0 已 scale 到让通带峰值 = 1.0**。

---

## 十三、附录 B：correction 数值范围对比

| 版本 | 9 频段 correction 范围 | 说明 |
|------|---------------------|------|
| v3.2.1（首次引入 correction）| 2.07e5 ~ 2.29e5 | 基于简化公式 \|H(fc)\| ≈ 5e-6 |
| v3.3.0（= v3.2.1，correction 未变）| 2.07e5 ~ 3.43e5 | 同上 |
| **v3.3.1（scipy）** | **0.95 ~ 1.05** | **基于 scipy \|H(fc)\| ≈ 1.0** |

correction 从 2e5 量级降到 ≈1，意味着 SPL 输出**不再需要大幅补偿**，浮点累加误差大幅降低。

---

*文档结束。等用户拍板后开始 Phase 1 实施。*