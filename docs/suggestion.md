**nRF54L15 平台噪声指标计算方法建议：逐条答复与设计方案**

根据硬件开发工程师《噪声计算法建议》整理

下面按工程师文件中的每一条建议逐条评估。总体结论是：大方向正确，但不能简单照单全收。这些建议主要从"让算法在
nRF54L15
上跑得动、编得过、内存不爆"的角度出发；而我们的设备是职业噪声剂量计/复杂噪声风险评估设备，还必须同时满足：计量可靠性、峰值捕获、A/C/Z
计权、峰度计算、长期剂量累计、可校准、可审计、可扩展。

nRF54L15 的定位可以概括为 128 MHz Arm Cortex-M33、最高 1.5 MB NVM、256
KB RAM，并带有 PDM、I2S、ADC、SPI/UART
等外设资源。因此，它适合作为低功耗智能噪声剂量计的主控和无线
SoC。工程师建议中提出的核心约束包括：256 KB SRAM、单精度
FPU、避免桌面式整秒 vector 处理、避免 double 软浮点、避免 STL
动态分配和复杂 C++ 运行库依赖，这些判断总体成立。

# 一、总体判断

我建议将工程师的建议分成三类处理：

  ----------------------------------------------------------------------------------------------------------------
  **建议**                                    **我的判断**            **处理方式**
  ------------------------------------------- ----------------------- --------------------------------------------
  double → float                              基本正确                热路径用
                                                                      float，但长期累计和校准验证保留混合精度

  24-bit → 16-bit                             不能直接接受            采集链路尽量保留 24-bit/32-bit
                                                                      slot，用流式计算解决 RAM

  vector → std::array                         正确                    但大数组不能放局部栈，应放
                                                                      static/global/class member

  滤波器系数运行期设计 → 编译期常量           正确                    固定 48 kHz 时强烈建议采用常量表

  map\<string\> → enum                        正确                    热路径必须
                                                                      enum/constexpr；字符串只在配置解析阶段使用

  禁异常/RTTI/thread_local/iostream/complex   正确                    嵌入式核心算法库应零异常、零 RTTI、零 heap

  去掉 alignas(64)                            正确                    但 DMA buffer 仍需按硬件要求做 4/8/word 对齐

  裸机无 OS 依赖                              需要澄清                推荐算法核心不依赖 OS，但整机系统可用 Zephyr
  ----------------------------------------------------------------------------------------------------------------

最重要的一点是：RAM 优化不能靠降低测量质量来解决。

对我们的产品，正确路径应是：

保留高质量采样链路 + 小块 DMA 环形缓冲 + 流式声学计算 + 每秒输出指标 +
60 秒窗口统计 + 事件触发存储。

而不是：

为了节省 RAM，把 24-bit 输入直接降为 16-bit，并继续使用整秒缓冲。

# 二、对"根因分类"的答复

工程师把问题归纳为四个根因：R1 单精度 FPU、R2 256 KB SRAM、R3 裸机/libc
受限、R4 硬件接口差异。这个框架基本正确。

## R1：单精度 FPU

### 工程师建议

nRF54L15 的 Cortex-M33 有单精度 FPU，没有双精度 FPU，因此 double
会走软浮点，性能显著劣化。建议 double → float + 混合精度累加。

### 我的答复

这个建议应采纳，但要加边界条件。

在 48 kHz 实时采样下，每秒有 48,000 个样本。如果每个样本都进行 A 计权、C
计权、平方、峰值、四阶矩、可能还有 1/3 倍频程滤波，那么每个样本路径上的
double 会严重拖慢系统。对于 Cortex-M33，这一点非常关键。

### 合理方案

实时热路径使用：

float sample_f;

float a_weighted;

float c_weighted;

float z_weighted;

float biquad_state;

float leq_accumulator_block;

但是以下部分不建议简单全部 float：

1\. 8 小时总能量累计

2\. Dose 长期累计

3\. 60 秒或全班次 kurtosis 的 S1--S4 统计合成

4\. 校准系数和参考声压换算

5\. PC 端验证算法

### 方案 A：嵌入式端 float + 补偿累加

例如使用 Kahan summation 或分块累计，减少误差累积。

### 方案 B：嵌入式端使用 64-bit 定点或 double 低频累计

double
不应该出现在每个样本循环里，但如果只是每秒更新一次总剂量，软浮点成本可以接受。也可以用
uint64_t 或定点 Q 格式累计线性能量。

建议原则：

  -----------------------------------------------------------------------
  **计算位置**                        **推荐精度**
  ----------------------------------- -----------------------------------
  每个样本滤波                        float

  每个样本平方/峰值                   float 或 int32/float

  每秒 LAeq                           float

  60 秒 kurtosis                      float + 分块统计，必要时
                                      double/64-bit 合成

  8 小时 Dose/LEX,8h                  double 或 64-bit 定点

  PC 端参考算法                       double
  -----------------------------------------------------------------------

最终要做一套验证：嵌入式 float 算法 vs MATLAB/Python double
参考算法，要求 LAeq、LCpeak、Dose、kurtosis 的误差在可接受范围内。例如
LAeq 误差控制在 0.1 dB 以内，kurtosis
误差控制在工程可接受范围，并特别检查高峰度冲击噪声。

# 三、对"24-bit → 16-bit"的答复

这是整份建议中我最不同意直接采纳的一条。

## 工程师建议

为了节省 RAM，将输入采样深度从 24-bit 改为 16-bit，因为 16-bit 每样本 2
字节，24-bit I2S 通常按 4 字节对齐，每秒缓冲可减半。

## 我的答复

节约 RAM 的理由成立，但解决方法不应是牺牲输入采样深度。

对普通消费电子录音，16-bit
可能够用；但对我们设计的个人噪声剂量计，特别是要处理以下内容时，过早把采样链路降到
16-bit 是有风险的：

- 70--140 dB SPL 宽动态范围

- LCpeak

- 冲击/复杂噪声

- crest factor

- kurtosis

- 事件触发录音

- 可能的法规或科研级测量

16-bit 理论动态范围约 96 dB，但实际系统还要考虑：

1\. 麦克风自噪声

2\. 前端增益设置

3\. ADC 有效位数 ENOB

4\. 高声压下不削顶

5\. 低声压下仍有足够分辨率

6\. 冲击峰值的瞬态捕获

7\. A/C/Z 计权后再计算峰值和能量

8\. 工业现场宽动态变化

如果为了保证 140 dB SPL 不削顶而把模拟增益设低，16-bit
在低声级段的有效分辨率会下降；如果为了低声级灵敏度而提高增益，又容易在冲击噪声下削顶。这对复杂噪声和峰度尤其危险，因为峰度对极端峰值非常敏感。

## 正确解决方法

不要用"整秒 PCM 缓冲"。改成流式处理。

### 错误架构

采集 1 秒 48 kHz PCM

↓

存入 vector

↓

整秒送入算法

↓

计算 LAeq/kurtosis/peak

这种方式在 256 KB SRAM 上当然吃紧。48 kHz、24-bit、I2S 32-bit 对齐时：

48,000 samples × 4 bytes = 192,000 bytes ≈ 187.5 KB

这还只是 1 秒单通道原始 PCM，不包括
BLE、RTOS、滤波器状态、日志、显示、栈和其他缓冲。工程师指出"桌面式一秒整段进、整段出在
256 KB RAM 上跑不起来"是正确的。

### 正确架构

I2S/PDM DMA 小块采集

↓

10--20 ms 环形缓冲

↓

边采样边滤波

↓

边滤波边更新统计量

↓

每 1 秒输出 SecondMetrics

↓

每 60 秒合成 kurtosis / 风险指标

例如：

  -----------------------------------------------------------------------
  **缓冲方案**                        **RAM**
  ----------------------------------- -----------------------------------
  10 ms, 48 kHz, 32-bit, 单缓冲       480 × 4 = 1,920 B

  ping-pong 双缓冲                    3,840 B

  20 ms, 48 kHz, 32-bit, 双缓冲       7,680 B

  1 秒整段 32-bit PCM                 192,000 B
  -----------------------------------------------------------------------

所以我们完全可以保留 24-bit/32-bit slot 输入，同时把 RAM 占用控制在几 KB
到十几 KB。

## 我的建议

### 采集层

保留：

24-bit ADC 或 24-bit 数字麦克风输出

I2S 32-bit slot 接收

int32_t DMA buffer

### 算法层

进入算法时转换为：

float x = static_cast\<float\>(sample_int32) \* scale;

### 存储层

每秒只保存声学指标，不保存整秒 PCM：

LAeq,1s

LCeq,1s

LZeq,1s

LCpeak

LZpeak

Dose increment

crest factor

kurtosis components

overload flag

calibration flag

event flag

### 事件录音层

如需要事件触发短录音，可以选择：

  -----------------------------------------------------------------------
  **模式**                            **建议**
  ----------------------------------- -----------------------------------
  法规/科研模式                       保存 24-bit 或 32-bit packed
                                      事件片段

  企业现场源识别模式                  可保存 16-bit 压缩片段

  普通长期日志模式                    不保存原始音频，只保存指标
  -----------------------------------------------------------------------

因此，我对这一条的正式答复是：不同意把主采样链路直接降为 16-bit。应保留
24-bit/32-bit 输入，通过流式计算、小块 DMA、每秒指标输出和外部存储来解决
RAM 问题。16-bit
可以作为事件音频压缩、低功耗简化模式或非计量级模式，但不应作为科研级/计量级主路径。

# 四、对"std::vector → std::array"的答复

## 工程师建议

把所有 std::vector 改成 std::array 或 C 风格定长数组，避免
heap/malloc，因滤波器阶数、频带数、状态长度都是固定的。

## 我的答复

这个建议应采纳。

在嵌入式实时声学系统中，尤其是剂量计这种长时间连续运行设备，动态内存分配是风险源：

1\. malloc/free 有不可预测延迟

2\. heap 可能碎片化

3\. 分配失败难以恢复

4\. 上电构造全局对象时可能出现初始化顺序问题

5\. 实时音频路径不能容忍偶发卡顿

## 但要注意一个细节

std::array 不等于一定安全。它不使用 heap，但如果把很大的 std::array
放在函数局部变量中，它会进入栈，仍可能导致 stack overflow。

例如：

void process() {

std::array\<float, 48000\> one_second_buffer; // 不推荐

}

这会占用约 192 KB 栈空间，仍然不可接受。

## 推荐写法

### 小状态量可作为类成员

struct BiquadState {

float z1;

float z2;

};

class WeightingFilter {

public:

std::array\<BiquadState, NUM_SECTIONS\> states;

std::array\<BiquadCoeff, NUM_SECTIONS\> coeffs;

};

### 大缓冲区放 static/global 或专门内存区

static int32_t dma_ping\[DMA_BLOCK_SAMPLES\];

static int32_t dma_pong\[DMA_BLOCK_SAMPLES\];

### 热路径禁止动态分配

禁止 new

禁止 malloc

禁止 std::vector 动态增长

禁止 std::map

禁止 std::string 热路径拼接

## 对我们设备的具体方案

将算法库分成两层：

noise_core_embedded/

无 vector

无 string

无 map

无 iostream

无 exception

无 RTTI

固定采样率

固定滤波器表

流式输入

noise_tools_pc/

可用 vector

可用 double

可用 Python/MATLAB 导出

可用 SQLite/TDMS/CSV

用于验证、回放、批量分析

这样既保证嵌入式端可靠，又保留 PC 端科研分析能力。

# 五、对"滤波器系数运行期设计 → 编译期常量"的答复

## 工程师建议

A 计权、C 计权、1/3 倍频程滤波器不要在运行期用
std::complex\<double\>、tan、pow、双线性变换去设计，而应在编译期使用固定系数表。

## 我的答复

这个建议强烈建议采纳。

个人噪声剂量计不是通用音频工作站。产品一旦确定采样率，例如 48 kHz，A
计权、C 计权、Z 计权、1/3
倍频程滤波器的系数就是固定的。每次开机重新设计滤波器没有意义。

## 推荐方案

### 固定采样率

我建议产品主采样率固定为：

48 kHz

原因：

1\. 与工业声学记录和音频采集兼容

2\. 足以覆盖常规职业噪声频带

3\. 便于 1 秒窗口精确对应 48,000 样本

4\. 便于事件音频回放

5\. 便于后续 AI 声源识别

### 编译期常量表

例如：

struct BiquadCoeff {

float b0;

float b1;

float b2;

float a1;

float a2;

};

constexpr std::array\<BiquadCoeff, A_WEIGHTING_SECTIONS\> A_WEIGHT_48K =
{

/\* precomputed coefficients \*/

};

constexpr std::array\<BiquadCoeff, C_WEIGHTING_SECTIONS\> C_WEIGHT_48K =
{

/\* precomputed coefficients \*/

};

### 系数来源

滤波器系数不应由工程师随意生成一次就固定。应建立正式流程：

1\. PC 端用 double 精度生成

2\. 与 IEC 频率响应目标比较

3\. 输出 48 kHz float 系数

4\. 固件内置版本号

5\. 用扫频信号验证 A/C 计权响应

6\. 与参考声级计/仿真结果比对

7\. 固定为产品版本的一部分

## 需要修正工程师说法的一点

工程师说移除 tan/pow/log 后可以减少 libm
依赖。这个方向对滤波器设计成立，但噪声剂量计仍然需要 log10，因为
LAeq、LCeq、LEX,8h、Dose 换算都需要 dB 计算。

不过 log10
不需要在每个样本执行，只需要每秒或显示刷新时执行，所以可以保留
log10f，或者用查表/近似算法优化。

建议：

样本级：禁止 log10 / pow / tan

每秒级：允许 log10f

显示级：允许格式化

PC 端：允许 double/log/pow

# 六、对"DoseCalculator：map\<string\> → enum + constexpr 表"的答复

## 工程师建议

4 个固定标准 NIOSH / OSHA_PEL / OSHA_HCA / EU_ISO 不应使用
map\<string\>，应改为 enum + constexpr 表，因为 map/string
会引入红黑树、动态分配和全局构造风险。

## 我的答复

这个建议应采纳。

噪声剂量计算的标准组合是固定的，不需要运行期
map。对于嵌入式系统，应采用枚举和静态表。

## 推荐结构

enum class DoseProfile : uint8_t {

NIOSH,

OSHA_PEL,

OSHA_HCA,

EU_ISO,

Custom

};

struct DoseParams {

float criterion_level_dba;

float exchange_rate_db;

float threshold_level_dba;

float reference_hours;

};

constexpr DoseParams DOSE_TABLE\[\] = {

/\* NIOSH \*/ {85.0f, 3.0f, 80.0f, 8.0f},

/\* OSHA_PEL \*/ {90.0f, 5.0f, 90.0f, 8.0f},

/\* OSHA_HCA \*/ {85.0f, 5.0f, 80.0f, 8.0f},

/\* EU_ISO \*/ {87.0f, 3.0f, 80.0f, 8.0f}

};

字符串只允许出现在配置解析阶段：

手机 App/PC 配置: \"NIOSH\"

↓

开机或配置更新时解析一次

↓

DoseProfile::NIOSH

↓

实时计算只使用 enum

## 对我们设备的扩展建议

因为我们的设备要支持复杂噪声和峰度修正，建议 DoseProfile
不只包括传统标准，还要预留：

enum class RiskModel : uint8_t {

EnergyOnly,

KurtosisAdjusted,

DualMetric,

ResearchMode

};

这样可同时输出：

1\. 传统 LAeq / Dose

2\. 峰度修正 L′Aeq

3\. LCpeak / crest factor

4\. 高峰度风险标志

5\. 研究模式下的完整统计量

# 七、对"C++ 特性裁剪"的答复

## 工程师建议

嵌入式构建应禁止异常、RTTI、thread_local、iostream、complex、动态 STL
容器等。

## 我的答复

这个建议完全正确。

对剂量计而言，最重要的是长期稳定运行。一个设备可能连续记录 8 小时、12
小时、24 小时，甚至多天。如果中途因为
heap、异常、全局构造或输出流导致死机，数据就失去了职业卫生价值。

## 推荐嵌入式 C++ 规则

### 禁止

throw / try / catch

dynamic_cast / typeid

std::vector 动态扩容

std::map

std::string 热路径操作

std::iostream

std::complex

thread_local

局部 static 非 constexpr 初始化

运行期滤波器设计

SQLite/TDMS/pthread

### 允许

constexpr

std::array

enum class

struct

template 小规模静态泛型

inline function

C 风格接口

固定大小 ring buffer

### 错误处理方式

不用 exception，而用明确返回码：

enum class NoiseStatus : uint8_t {

Ok,

Overload,

UnderRange,

InvalidCalibration,

BufferOverrun,

StorageError

};

或者每秒指标中带状态位：

struct SecondMetrics {

float laeq;

float lceq;

float lcpeak;

float kurtosis;

uint32_t flags;

};

建议 flags 包括：

BIT0: overload

BIT1: under_range

BIT2: calibration_invalid

BIT3: storage_late

BIT4: clipped_sample_detected

BIT5: high_kurtosis_event

BIT6: microphone_error

BIT7: battery_low

# 八、对"thread_local / iostream / chrono / random / complex / SQLite"等清单的答复

工程师列出的这些桌面依赖确实不应进入嵌入式构建。

## 我的建议

采用"双构建系统"：

PC reference build

\- double

\- vector

\- complex

\- CSV/SQLite/TDMS

\- unit test

\- waveform replay

\- algorithm validation

Embedded build

\- float

\- array

\- no heap

\- no exception

\- fixed coefficients

\- streaming calculation

这非常适合我们的项目，因为我们既需要科研级 PC
分析，又需要嵌入式产品落地。

## 关键原则

同一套算法不要写成两个完全不同版本，而应共享核心公式：

共同部分:

\- LAeq 公式

\- Dose 公式

\- kurtosis 公式

\- peak 公式

\- S1--S4 合成逻辑

\- profile 参数

不同部分:

\- PC 端输入 vector\<double\>

\- 嵌入式端输入 streaming float/int32

这样可避免 PC 算法和设备固件结果不一致。

# 九、对"去掉 alignas(64)"的答复

## 工程师建议

M33 无数据 cache，alignas(64) 对 SecondMetrics 没有性能收益，反而造成
padding 和 RAM 浪费。

## 我的答复

这个建议应采纳。

alignas(64) 通常是为了 cache line 对齐。在 Cortex-M33 这类 MCU
上，如果没有 D-cache，64 字节对齐对普通结构体没有意义。对于 60 秒 ring
buffer、分钟级聚合数组、多 profile 指标表，padding 会浪费宝贵 SRAM。

## 但要保留必要对齐

不是所有对齐都应取消。DMA buffer 仍应按硬件要求对齐，例如：

alignas(4) static int32_t i2s_dma_ping\[DMA_BLOCK_SAMPLES\];

alignas(4) static int32_t i2s_dma_pong\[DMA_BLOCK_SAMPLES\];

或者根据 Nordic EasyDMA/I2S 驱动要求设定。原则是：

SecondMetrics：不需要 64 字节对齐

DMA buffer：需要满足外设访问对齐

滤波器状态：自然对齐即可

日志结构体：紧凑布局优先

# 十、建议的最终算法架构

我建议把嵌入式算法重构为下面这种模式。

## 1. 采样输入层

I2S/PDM

↓

DMA ping-pong buffer

↓

int32_t raw_sample

↓

float normalized_sample

建议：

采样率：48 kHz

输入 slot：24-bit in 32-bit container

处理块：10 ms 或 20 ms

主声学计算：float

## 2. 实时滤波层

每个样本依次进入：

Z path: 原始/校准后信号

A path: A-weighting biquad cascade

C path: C-weighting biquad cascade

Optional: 1/3 octave filter bank

输出用于：

LAeq

LCeq

LZeq

LCpeak

LZpeak

A-weighted kurtosis 或 C/Z-weighted kurtosis

crest factor

## 3. 每秒统计层

每秒累计：

struct RunningSecondAccumulator {

uint32_t n;

float sum_a2;

float sum_c2;

float sum_z2;

float peak_c_abs;

float peak_z_abs;

float sum_x;

float sum_x2;

float sum_x3;

float sum_x4;

uint32_t clipped_count;

uint32_t flags;

};

每 48,000 个样本结束后生成：

struct SecondMetrics {

float laeq_1s;

float lceq_1s;

float lzeq_1s;

float lcpeak_1s;

float lzpeak_1s;

float crest_factor;

float kurtosis_1s;

float dose_increment_niosh;

float dose_increment_osha;

uint32_t flags;

};

## 4. 60 秒峰度窗口

不要保存 60 秒波形。只保存 60 个每秒统计块，或者保存可合成的 S1--S4。

推荐：

per-second S0, S1, S2, S3, S4

↓

60-second rolling synthesis

↓

kurtosis_60s

↓

geometric/arithmetic mean beta

↓

kurtosis-adjusted exposure

这与我们之前讨论的"每秒保存 S1--S4 以精确合成 60 秒窗口峰度"的方向一致。

## 5. 长期剂量层

每秒更新：

LAeq,8h

LEX,8h

Dose%

TWA

CNE-like cumulative exposure

kurtosis-adjusted L′Aeq

peak exceedance count

high-kurtosis event count

长期累计建议用：

64-bit 定点 或 double 低频更新

而不是每样本 double。

# 十一、建议的 RAM 预算

下面是比较合理的 RAM 思路：

  -----------------------------------------------------------------------
  **模块**                            **推荐占用**
  ----------------------------------- -----------------------------------
  I2S DMA ping-pong, 20 ms, 32-bit    约 7.7 KB

  A/C/Z 滤波器状态                    \< 2 KB

  1/3 倍频程状态，若启用              数 KB 到十几 KB

  60 秒 SecondMetrics ring            约 4--10 KB

  60 秒 S1--S4 ring                   约 2--4 KB

  当前累加器                          \< 1 KB

  BLE/Zephyr/stack                    需预留较大空间

  文件系统/外部存储缓冲               4--16 KB

  显示/按键/报警                      小
  -----------------------------------------------------------------------

这样比"1 秒整段 PCM vector"安全得多。

最终目标应是：

算法核心 RAM \< 40 KB

音频 DMA + filter + metrics \< 60 KB

其余 RAM 留给 BLE、RTOS、栈、存储和系统安全余量

# 十二、对工程师建议的逐条结论

## 1. double → float

结论：采纳，但采用混合精度。

实时样本级计算全部 float；长期累计、PC 端验证、校准参考保留 double 或
64-bit 定点。

## 2. 24-bit → 16-bit

结论：不建议作为主方案。

应保留 24-bit/32-bit 输入。RAM 问题通过 DMA
小块、流式计算和外部存储解决。16-bit
可作为事件音频压缩或低成本版本选项，但不应作为科研级/计量级主路径。

## 3. vector → array

结论：采纳。

嵌入式端禁止动态容器。使用 std::array、固定 ring buffer、static
buffer。但避免大数组放局部栈。

## 4. 滤波器系数编译期常量

结论：采纳。

固定 48 kHz 后，A/C/Z 和 1/3 倍频程滤波器用预计算系数表。PC
端生成，嵌入式端只查表。

## 5. DoseCalculator map → enum

结论：采纳。

标准配置固定，用 enum + constexpr
表。字符串只在配置阶段解析，不进入实时计算。

## 6. 禁异常/RTTI

结论：采纳。

嵌入式算法库应无 exception、无 RTTI、无 heap。错误用状态码和 flags
表示。

## 7. 禁 iostream/complex/random/chrono/SQLite/TDMS

结论：采纳。

这些保留在 PC 工具链，不能进固件核心。

## 8. 去 alignas(64)

结论：采纳。

SecondMetrics 不需要 64 字节对齐。DMA buffer 保留必要硬件对齐。

## 9. 裸机/Zephyr 问题

结论：算法核心应 OS-independent，但整机系统可使用 Zephyr。

BLE、OTA、文件系统、低功耗管理、驱动生态都使 Zephyr
有吸引力。正确做法是让 noise_core 不依赖 Zephyr，而系统层可以用 Zephyr
调用它。

# 十三、我建议给硬件/固件工程师的正式回复要点

可以这样定技术路线：

1\. 同意将实时算法从桌面 vector 模式改为流式 streaming 模式。

2\. 同意嵌入式热路径使用 float，不使用 double。

3\. 不同意主采样链路直接降为 16-bit；应保留 24-bit/32-bit 输入能力。

4\. 同意取消整秒 PCM 缓冲，使用 10--20 ms DMA ping-pong buffer。

5\. 同意滤波器系数预计算并固化为 48 kHz 常量表。

6\. 同意所有 Dose profile 使用 enum + constexpr table。

7\. 同意嵌入式核心禁止异常、RTTI、iostream、complex、map、vector
动态分配。

8\. 同意去掉 SecondMetrics 的 64-byte alignment。

9\. 要求保留 PC double 参考算法，用于验证嵌入式 float 结果。

10\. 要求建立指标误差验收标准：LAeq、LCpeak、Dose、kurtosis、L′Aeq
均需与参考算法比较。

# 十四、最终推荐方案

我建议我们的个人噪声剂量计采用如下最终方案：

nRF54L15 主控

48 kHz 采样

24-bit ADC / digital microphone input

I2S 32-bit slot 接收

10--20 ms DMA ping-pong buffer

float 实时滤波与声学计算

constexpr A/C/Z/1/3 octave filter coefficients

每秒输出 SecondMetrics

60 秒 rolling kurtosis

多标准 Dose profile

kurtosis-adjusted exposure

外部 Flash / microSD 保存日志和事件音频

BLE/App 同步摘要数据

PC 端 double 算法作为参考验证

这条路线既尊重了工程师提出的 nRF54L15
资源限制，也没有牺牲我们产品最核心的优势：复杂噪声与峰度风险评估、计量可靠性、事件捕获能力和科研级可验证性。
