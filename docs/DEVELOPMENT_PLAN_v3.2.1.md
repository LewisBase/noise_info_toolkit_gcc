# v3.2.1 开发计划 — A/C 加权归一化修复 + 7 采样率系数重生成

> 日期：2026-06-18
> 作者：蒙特卡洛
> 状态：**已批准，待实施**
> 前置版本：v3.2（commit b867566）
> 范围：**仅修复 A/C 加权归一化 bug，不引入新功能**

---

## 零、决策记录（2026-06-18 用户拍板）

| # | 决策项 | 用户选择 |
|---|--------|---------|
| 1 | 修复方案 | ✓ **方案 A**：scipy 重生成 7 套系数 + 独立 gain factor 归一化 |
| 2 | 嵌入式约束 | ✓ **预存表必须保留**（嵌入式反馈芯片上无法使用动态生成的权重） |
| 3 | 归一化是否需要运行时动态化 | ✓ **不需要**（A 加权是 LTI 纯数学系统，与环境无关；仅 fs 与精度影响） |
| 4 | 是否同时修 v3.1.x | ✓ **本次仅修 v3.2**；若 v3.1.x 有相同 bug 由 v3.2.1 回移植（待 v3.1.3 频响测试后决定） |

**约束条件**：nRF54L15 嵌入式平台、float 精度、零动态内存、constexpr 静态系数表——任何方案不得违反这些约束。

---

## 一、问题背景

详见 `docs/BUG_v3.2_A_WEIGHTING.md`。

核心 bug：v3.2 引入的 7 采样率 A/C 加权预存表使用了错误的归一化逻辑（只调 sos[0].b），导致高频处响应严重偏离 IEC 61672 标准。

---

## 二、修复方案

### 2.1 归一化新设计：独立 gain factor

**当前（v3.2 bug）**：
```cpp
// 只调 sos[0].b，破坏滤波器形状
float norm_factor = 1.0f / current_gain;  // current_gain = |H_total(1kHz)|
sos[0].b0 *= norm_factor;
sos[0].b1 *= norm_factor;
sos[0].b2 *= norm_factor;
```

**新设计（v3.2.1）**：

把 1kHz 归一化作为**独立的 gain factor**，不修改 biquad b 系数：

```cpp
// 1. biquad b/a 系数保持纯净（不调 sos[0].b）
// 2. 单独计算 gain_normalization = 1 / |H_total(1kHz)|
float gain_normalization = 1.0f / std::abs(compute_cascade_response(sos, 1000.0f, fs));

// 3. 输出时乘一次
out *= gain_normalization;
```

### 2.2 数据结构变更

修改 `include/weighting_coefficients_multirate.hpp`：

```cpp
// v3.2.1 新结构
struct WeightingTableEntry {
    const WeightingCoeffs* a;
    int a_count;
    float a_gain;          // NEW: A 加权 1kHz 归一化系数（独立，不改 b）
    const WeightingCoeffs* c;
    int c_count;
    float c_gain;          // NEW: C 加权 1kHz 归一化系数
};
```

调用方在滤波完成后乘一次 gain：

```cpp
// process_segment 内部
filter(sos_a, x, y);
y *= table->a_gain;   // A 加权 1kHz 归一化
```

### 2.3 系数重新生成

写一次性脚本 `tools/regen_weighting_coefficients.py`：

```python
import scipy.signal as signal
import numpy as np

# IEC 61672-1 A 加权连续时间传递函数（极点频率，Hz）
f_poles_a = [(20.6, 'p'), (20.6, 'p'),    # double pole
             (107.7, 'p'), (737.9, 'p'),
             (12200.0, 'p'), (12200.0, 'p')]  # double pole
# 零点
f_zeros_a = [(0.0, 'z')] * 3   # 3 个直流零点

# 双线性变换到离散域
fs_list = [8000, 16000, 22050, 32000, 44100, 48000, 96000]
for fs in fs_list:
    z, p, k = signal.bilinear_zpk(zeros_a, poles_a, fs)
    sos = signal.zpk2sos(z, p, k)
    
    # 1kHz 增益（独立 gain factor）
    w_1k = 2 * np.pi * 1000 / fs
    _, h_1k = signal.sosfreqz(sos, worN=[w_1k])
    gain = 1.0 / abs(h_1k[0])
    
    # 输出 C++ 系数
    emit_cpp(fs, 'A', sos, gain)
```

脚本一次性生成完整的 `include/weighting_coefficients_multirate.hpp` v3.2.1 版本。

---

## 三、嵌入式约束验证

| 约束 | 当前（v3.2）| v3.2.1 方案 | 是否满足 |
|------|----------|-----------|---------|
| 零动态内存 | ✓ 静态表 | ✓ 静态表 + 静态 gain | ✓ |
| 编译期常量 | ✓ constexpr | ✓ constexpr（gain 也是 constexpr）| ✓ |
| float 精度 | ✓ float | ✓ float | ✓ |
| 嵌入式可执行 | ✓ 静态链接 | ✓ 不引入运行时函数 | ✓ |
| 系数精度（量化误差）| ⚠️ 系数表人为生成 | ✓ scipy 双精度生成后 float 化 | ✓ 更优 |

**验证方法**：在 x86_64 上编译后跑频响测试，对比 v3.1.x 动态生成的频响（如可用）+ IEC 61672 参考曲线，1 kHz / 10 kHz / 20 kHz 三点偏差 < 0.1 dB。

---

## 四、测试计划

### 4.1 回归测试（新文件 `tests/test_weighting_response.cpp`）

硬性断言 7 套系数的频响：

```cpp
// tests/test_weighting_response.cpp
void test_a_weighting_1khz_is_zero_db() {
    for (int fs : {8000, 16000, 22050, 32000, 44100, 48000, 96000}) {
        float gain_db = compute_weighting_gain_db('A', 1000.0f, fs);
        assert(std::abs(gain_db) < 0.1f);  // ±0.1 dB
    }
}

void test_a_weighting_high_freq_rolloff() {
    // 10 kHz 应衰减约 -3 dB（按 IEC 61672）
    for (int fs : {8000, 16000, 22050, 32000, 44100, 48000, 96000}) {
        if (fs >= 20000) {  // 10k < fs/2 才能测
            float gain_db = compute_weighting_gain_db('A', 10000.0f, fs);
            assert(std::abs(gain_db - (-3.0f)) < 0.5f);
        }
    }
}

void test_c_weighting_flat_response() {
    // C 加权在 1 kHz ~ 20 kHz 应平坦（< ±0.5 dB）
    for (int fs : {44100, 48000, 96000}) {
        for (int f : {1000, 2000, 4000, 8000, 16000}) {
            float gain_db = compute_weighting_gain_db('C', (float)f, fs);
            assert(std::abs(gain_db) < 0.5f);
        }
    }
}
```

### 4.2 端到端验证（用 THIST100.CSV 回放）

把 CSV 喂给新库，看 LAeq - LZeq 是否回到 0 dB 附近：

- 当前：+35 dB（v3.2 bug）
- 修复后：±3 dB（正常范围）

### 4.3 dose 端到端验证

跑 `examples/test_dose_state_demo`（v3.1.3 example，60 段 × 1s @ 90dB）：

- 期望 Dose% ≈ 0.59%
- 当前：异常（v3.2 bug）

### 4.4 v3.1.x 回溯测试（可选）

如时间允许，对 v3.1.3 跑同一套频响测试，确认 v3.1.x 是否也有 bug，决定是否需要回移植修复。

---

## 五、实施步骤

### Phase 1：脚本与系数生成（半天）

| 步骤 | 产出 | 验证 |
|------|------|------|
| 1. 写 `tools/regen_weighting_coefficients.py` | Python 脚本 | 脚本可独立运行，输出 C++ 代码 |
| 2. 用脚本生成新 `weighting_coefficients_multirate.hpp` | 新系数表 | 文件可编译 |
| 3. 修改数据结构（加 `a_gain` / `c_gain` 字段）| 新 struct | 编译通过 |
| 4. 修改调用方（filter 输出乘 gain）| 新代码 | 编译通过 |

### Phase 2：测试与验证（半天）

| 步骤 | 产出 | 验证 |
|------|------|------|
| 5. 写 `tests/test_weighting_response.cpp` | 频响断言测试 | `ctest` 通过 |
| 6. THIST100.CSV 回放验证 | 诊断脚本 | LAeq-LZeq 回到 ±3 dB |
| 7. `examples/test_dose_state_demo` 跑通 | 验证 dose% | 90 dB × 60s ≈ 0.59% |
| 8. v3.1.x 回溯测试（可选）| 报告 | 决定是否回移植 |

### Phase 3：发布（半天）

| 步骤 | 产出 | 验证 |
|------|------|------|
| 9. 更新 `CHANGELOG.md` | v3.2.1 条目 | 文档完整 |
| 10. tag `v3.2.1` | git tag | 推送 tag |
| 11. 更新 `README.md` / `AGENTS.md` | 文档同步 | 无遗漏 |
| 12. 通知嵌入式工程师 | 飞书消息 | 升级指引 |

---

## 六、验收标准

- [ ] `test_weighting_response` 7 采样率 × 3 测试点（1kHz / 10kHz / 20kHz）全部 ±0.5 dB 通过
- [ ] THIST100.CSV 回放 LAeq-LZeq 在 ±3 dB 内
- [ ] `test_dose_state` 9 个测试全部通过
- [ ] `dose_validator` 输出与 v3.2.1 一致（无回归）
- [ ] `examples/main.cpp` 全部 4 个 demo 跑通
- [ ] 嵌入式工程师升级 v3.2.1 后 dose% 正常

---

## 七、风险与回退

### 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| scipy 生成的系数与 v3.1.x 动态生成的不一致 | 可能引入新 bug | Phase 2 的 4.4 回溯测试必须做 |
| 嵌入式平台 float 精度问题 | nRF54L15 应无影响 | 在嵌入式 target 实测 |
| gain factor 改变调用方 API | 破坏 v3.2 兼容性 | 主版本号升 v3.2.1，强制告知嵌入式工程师 |

### 回退

如 v3.2.1 出问题，回退到 v3.1.3（**前提**：v3.1.3 无相同 bug；若 v3.1.3 也有 bug，需专门修 v3.1.x）。

---

## 八、变更日志摘要

| 版本 | 日期 | 变更 |
|------|------|------|
| v3.2 | 2026-06-04 | 引入 7 采样率 A/C 加权预存表，**但归一化 bug** |
| v3.2.1 | 2026-06-XX（计划）| 归一化改独立 gain factor + scipy 重生成 + 频响回归测试 |

---

## 九、后续扩展方向（v3.3+，**本次不做**）

- IEC 61672 1 级/2 级容差带验证（用脚本绘制容差带 vs 实测频响）
- A 加权 Z 加权自动切换（按用户配置）
- 1/1 倍频程 + 1/6 倍频程支持
- 频段时变事件检测
- 自适应 A 阈值（防爆音误触发）

---

## 十、参考

- IEC 61672-1:2013 — Electroacoustics — Sound level meters — Part 1: Specifications
  - §5.4 A 加权频率响应允差（1 级 / 2 级）
- ANSI S1.4:2014 — 同上
- `docs/BUG_v3.2_A_WEIGHTING.md` — 本次 bug 详细报告
- `docs/DEVELOPMENT_PLAN_v3.2.md` — v3.2 原始开发计划
- scipy.signal.bilinear — 双线性变换 API