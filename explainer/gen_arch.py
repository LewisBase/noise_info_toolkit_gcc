#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ARCHITECTURE.html 生成器 —— 第一遍：提取源码 + 定义分段 + 生成页面内容

设计原则
--------
1. **零手工转写**：所有代码按行号从真实源文件提取，生成前做锚点校验。
2. **逐段左右分栏**：每个逻辑段 = 左栏讲解 + 右栏对应源码，一一对应。
3. 产出 pickle 交给 assemble.py 组装成 HTML。

用法
----
    cd explainer && python3 gen_arch.py && python3 assemble.py
"""
import re, os, sys, pickle

ROOT = os.path.expanduser('~/github/noise_info_toolkit_gcc')
os.chdir(ROOT)

# ─────────────── 源码读取 ───────────────
_c = {}
def L(p):
    if p not in _c:
        _c[p] = open(p, encoding='utf-8', errors='replace').read().split('\n')
    return _c[p]

def esc(s): return s.replace('&','&amp;').replace('<','&lt;').replace('>','&gt;')
def raw(p,a,b): return '\n'.join(L(p)[a-1:b])

def hl(s):
    """在已转义文本上做轻量高亮"""
    out=[]
    for line in s.split('\n'):
        cm=''; inq=False; cut=-1
        for i,ch in enumerate(line):
            if ch=='"': inq = not inq
            elif ch=='/' and not inq and i+1<len(line) and line[i+1]=='/':
                cut=i; break
        if cut>=0: cm=line[cut:]; line=line[:cut]
        line = re.sub(r'\b(void|float|double|int|int64_t|int32_t|bool|size_t|uint8_t|uint32_t|char|'
                      r'const|constexpr|static|inline|return|if|else|for|while|do|switch|case|break|'
                      r'continue|struct|class|public|private|protected|template|typename|namespace|'
                      r'using|new|delete|noexcept|auto|true|false|nullptr|enum|explicit|operator|std)\b',
                      r'<span class="kw">\1</span>', line)
        line = re.sub(r'&quot;[^&]*?&quot;', lambda m:'<span class="str">'+m.group(0)+'</span>', line)
        if cm: line += '<span class="cmt">'+cm+'</span>'
        out.append(line)
    return '\n'.join(out)

def code(p,a,b,caption=None,compact=False):
    src = raw(p,a,b)
    body = hl(esc(src))
    cap = (f'<div class="csrc">{esc(caption) if caption else ""}'
           f'<code>{p}:{a}–{b}</code><span class="cl">{b-a+1} 行</span></div>') if not compact else ''
    return f'{cap}<pre><code>{body}</code></pre>'

def grab(p, anchor, label=None, limit=420):
    ls = L(p); start=None
    for i,ln in enumerate(ls):
        if anchor in ln: start=i; break
    if start is None: raise SystemExit(f'找不到 anchor {anchor!r} in {p}')
    depth=0; end=start
    for k in range(start, min(len(ls), start+limit)):
        depth += ls[k].count('{') - ls[k].count('}')
        if '}' in ls[k] and depth<=0: end=k; break
    return code(p, start+1, end+1, label)

def seg(p, a, b, title, explain, tag=None):
    """一段 = 左栏讲解 + 右栏源码（左右一一对应）"""
    t = f'<span class="stag">{tag}</span>' if tag else ''
    return (f'<div class="seg">'
            f'<div class="ex"><h4>{t}{title}</h4>{explain}</div>'
            f'<div class="cd">{code(p,a,b)}</div>'
            f'</div>')

def pair(title, explain, codepath, a, b):
    """无源码对应时的纯说明块（罕见）"""
    return f'<div class="seg nous"><div class="ex"><h4>{title}</h4>{explain}</div><div class="cd"></div></div>'

# ─────────────── 路径 ───────────────
NP='src/noise_processor.cpp'; SU='src/signal_utils.cpp'; II='src/iir_filter.cpp'
ED='src/event_detector.cpp';  DC='src/dose_calculator.cpp'
NPH='include/noise_processor.hpp'; NM='include/noise_metrics.hpp'
EDH='include/event_detector.hpp';  DSH='include/dose_state.hpp'
DCH='include/dose_calculator.hpp'; IIH='include/iir_filter.hpp'
FCH='include/filter_coefficients_48k.hpp'; WCH='include/weighting_coefficients_multirate.hpp'
SUH='include/signal_utils.hpp'

# ─────────────── 锚点校验 ───────────────
CHECKS = [
 (NP,126,'process_segment'), (NP,416,'aggregate_metrics'), (NP,146,'float a_buf'),
 (NP,236,'laf_sq_a_ ='), (NP,373,'band_filters_[b].reset()'),
 (SU,239,'apply_a_weighting_inplace'), (SU,263,'apply_c_weighting_inplace'),
 (II,119,'BiquadFilter::process'), (II,220,'a_weighting_design'), (II,314,'c_weighting_design'),
 (ED,79,'check_segment'), (ED,199,'check_metrics'), (DC,14,'parse_standard'),
]
for p,a,kw in CHECKS:
    if kw not in L(p)[a-1]:
        raise SystemExit(f'❌ 锚点校验失败 {p}:{a} 期望含 {kw!r}，实际 {L(p)[a-1][:70]!r}')
print(f'✅ 锚点校验通过（{len(CHECKS)} 项）', file=sys.stderr)

P = {}

# ═══════════════════════════════════════════════════════════
# p5 · 接口一 process_segment —— 14 段左右对应
# ═══════════════════════════════════════════════════════════
PS_SEG = [
 (126,139,'函数签名与入口守卫','''<p><b>签名</b>：吃的是原始 PCM 的两个指针（起止）+ 时长，不拥有缓冲区。</p>
<p><b>守卫</b>：<code>expected_n = sample_rate_ × duration_s</code> 是<b>精确</b>样本数。</p>
<p class="risk">长度不等或为 0 → <code>return SecondMetrics{};</code>，<b>静默返回全零</b>，不报错不打日志。</p>
<p>读到的 LAeq 为 0 或 <code>-inf</code> 时，第一件事查这里。</p>'''),
 (140,154,'Phase 1 · 拷贝到 scratch','''<p>声明两个 <b>VLA</b>（变长数组）<code>a_buf</code> / <code>c_buf</code>，按实际样本数在<b>栈上</b>分配。</p>
<p>为什么需要两份？A 计权和 C 计权要把<b>同一段原始信号各滤一遍</b>，不能互相覆盖。</p>
<p><b>原始 buffer 全程不被改写</b> —— Phase 3 算 LZeq / 峰值 / 峰度 / 原始矩时还要读它。所以固件传入的 buffer 在函数返回后可立即复用。</p>
<p class="tip">零堆分配：48 kHz 下 10 ms 块仅 480 样本，栈压力极小。</p>'''),
 (155,170,'Phase 2 · A/C 计权','''<p>逐样本流过两条 <b>成员</b> IIR 链：<code>a_weight_chain_</code> / <code>c_weight_chain_</code>。</p>
<p class="key">状态跨调用连续（v3.3.2 起）。</p>
<p>源码注释写明了原因：A 计权段 2 是 20.6 Hz 双极点，τ = 1/((1−|p|)·fs) ≈ 7.7 ms，约 50 ms 才收敛。若每 10 ms 段都重置，滤波器永远来不及建立 → 20/25 Hz 的 LAeq 虚高数十 dB。</p>
<p><b>1 kHz 归一化增益</b>（<code>a_weight_gain_</code>）在链<b>之后</b>单独相乘 —— v3.2.1 的修复，不能烘进 <code>b</code> 系数，否则高频响应被拉歪。</p>'''),
 (171,185,'Phase 2.5 · 计算 α 系数','''<p>只算系数，<b>实际状态更新折叠进下面的主循环</b>（避免二次遍历）。</p>
<p>α = exp(−1/(τ·fs))：Fast τ=125 ms，Slow τ=1 s，按 IEC 61672-1 §7.4。</p>
<p>48 kHz 下 Fast α ≈ 0.99983，Slow α ≈ 0.99998 —— 都非常接近 1，即「几乎不忘记历史」，这正是时间计权的物理含义。</p>'''),
 (186,199,'Phase 3 · 累加器声明','''<p>三套<b>原始矩</b> S1–S4：Z（未计权）、A、C 各一组，共 12 个变量。</p>
<p>为什么用矩而不是直接存样本？<b>峰度可以在不保留波形的前提下算出来</b>，且能跨块精确合并（接口二就靠它）。</p>
<p>另有三个能量累加器（算 Leq）与三个峰值跟踪。</p>'''),
 (200,230,'主循环 · 三种矩与能量','''<p>单遍扫描，一次循环把<b>所有</b>东西累加完 —— 没有第二趟。</p>
<p>每样本取 <code>z</code>（原始）、<code>a</code>（A 计权后）、<code>c</code>（C 计权后），各自算平方，然后：</p>
<ul><li>累加 z/a/c 的 S1–S4 矩（<code>z2*z</code> 即 z³，<code>z2*z2</code> 即 z⁴）</li>
<li>累加三个能量（<code>sum_*_sq</code>）供 Leq 使用</li></ul>
<p class="tip">全部是加法与乘法，无除法、无开方 —— MCU 上很省。</p>'''),
 (231,248,'主循环 · 指数计权与峰值','''<p class="key">四个成员状态在这四行里更新：<code>laf_sq_a_</code> / <code>las_sq_a_</code> / <code>laf_sq_c_</code> / <code>las_sq_c_</code>。</p>
<p>公式 <code>state = α·state + (1−α)·y²</code> —— 一阶指数平均。因为它们是<b>成员变量</b>，状态跨 <code>process_segment()</code> 连续。</p>
<p>同时跟踪三个峰值（取绝对值最大）。</p>
<p class="risk">若实例被重建，这四个状态归零 → LAF/LAS 读数会从 0 重新爬升，需要约 5τ（Fast 625 ms / Slow 5 s）才稳定。</p>'''),
 (250,264,'存原始矩与峰度','''<p>四个 Z 矩写进 <code>SecondMetrics</code>，供接口二聚合时使用。</p>
<p><b>峰度从矩算</b>，不读 VLA 缓冲 —— v3.3.1 的改动：避免依赖 <code>a_buf/c_buf</code> 的栈完整性（嵌入式上栈可能被破坏）。</p>
<p>三种计权各算一个峰度：<code>beta_kurtosis</code>（Z）、<code>kurtosis_a_weighted</code>、<code>kurtosis_c_weighted</code>。</p>'''),
 (265,281,'Leq 与峰值换算','''<p>一个 lambda 统一做「平方和 → RMS → dB SPL」，三处复用。</p>
<p>公式：<code>rms = sqrt(sum_sq / n)</code>，<code>Leq = 20·log10(rms / 20µPa)</code>。</p>
<p>峰值同理但用 <b>peak</b> 而非 RMS（<code>20·log10(peak / p_ref)</code>）。<code>LZPeak</code> 是 OVERLOAD 判据的主依据。</p>
<p class="tip"><code>rms &lt;= 0</code> 返回 <code>-INFINITY</code>，避免 log10(0)。</p>'''),
 (282,305,'时间计权读数与最大值','''<p>把功率状态转成 dB。关键常数：</p>
<p><code>ref_sq_db = −20·log10(20e-6) ≈ 93.9794 dB</code></p>
<p>推导：<code>10·log10(state/p_ref²) = 10·log10(state) − 20·log10(p_ref)</code>。</p>
<p>输出四个读数：<code>LAF</code>/<code>LAS</code>（A 的 Fast/Slow）、<code>LCF</code>/<code>LCS</code>（C 的 Fast/Slow）。</p>
<p class="tip"><code>LASmax</code>/<code>LCSmax</code> 直接取 Slow 值 —— Slow 本身已起平滑最大作用。<code>LAFmax</code> 是 <code>LAeq+3</code> 的近似，保留仅为兼容旧消费者。</p>'''),
 (306,323,'剂量与 QC 标志','''<p>只有 <code>LAeq &gt; 0</code> 才算剂量，避免无效值污染累计。</p>
<p>四套标准各取一次 profile，调 <code>DoseCalculator::calculate_dose_increment()</code>，<code>/100</code> 换算成比例。</p>
<p><b>QC 标志</b>：OVERLOAD 判据是 <code>LZPeak &gt; 140 dB</code>（IEC Class 1）；<code>wearing_state</code> 用 <code>LAeq &gt; 40</code> 粗判是否佩戴。</p>'''),
 (324,369,'Phase 4 · 带通增益校正','''<p class="risk">二阶带通在中心频率的峰值增益 ∝ fc² —— 低频带会比高频带低约 25 dB。必须逐带补偿。</p>
<p>48 kHz 直接用预计算值 <code>peak_gain_correction</code>（零运行时成本）；其他采样率现算 |H(e^jω)| 并取倒数。</p>
<p>这是 v3.2 的 Bug A 修复。</p>'''),
 (371,406,'逐频带滤波与 SPL','''<p>9 个频带逐个处理，每带内层再遍历全部样本。</p>
<p class="risk"><b>注意第 373 行：<code>band_filters_[b].reset();</code></b> —— 带通滤波器<b>每段都重置</b>，与 A/C 计权的「状态连续」策略不同。带通只需段内统计，重置可避免上段残留污染本段。</p>
<p>滤波后累加平方与 S1–S4 矩，乘增益校正，再转 dB。</p>
<p class="tip">最后按 <code>b</code> 用 switch 分发到 9 个具名字段（63 Hz … 16 kHz）。</p>'''),
 (408,414,'存频带矩并返回','''<p>9 组频带矩通过 <code>band_moments_ptr()</code> 写进结构体，供接口二做跨块精确聚合。</p>
<p>返回 <code>SecondMetrics</code> —— 81 项指标，一块一份。</p>'''),
]

p5 = ['<h2><span class="iface">接口一</span>process_segment() —— 逐段讲解</h2>',
      '<p class="sub">单块声学指标的主入口。<b>左栏是讲解，右栏是它对应的源码</b>，共 14 段，合计 289 行。</p>']
for i,(a,b,t,ex) in enumerate(PS_SEG,1):
    p5.append(seg(NP,a,b,t,ex,tag=f'{i:02d}'))
P['p5'] = '\n'.join(p5)

# ═══════════════════════════════════════════════════════════
# p6 · 接口二 aggregate_metrics —— 7 段
# ═══════════════════════════════════════════════════════════
AG_SEG = [
 (416,420,'签名与空输入守卫','''<p>吃一个 <code>SecondMetrics</code> 数组 + 元素个数 + 每个元素代表的时长。</p>
<p>输出 <code>MinuteMetrics</code>（40 个 float）。</p>
<p class="risk"><code>count &lt;= 0</code> 或空指针 → 返回默认构造的对象（全零/负无穷），不报错。</p>
<p class="key">本函数<b>纯数值聚合</b>，不触碰任何滤波器状态。</p>'''),
 (422,439,'累加器初始化','''<p>时间戳取第一块；<code>valid_seconds</code> 记有效块数。</p>
<p>三个功率累加器（LAeq/LCeq/LZeq）—— 注意是<b>功率</b>不是 dB。</p>
<p>六个最大值初始化为 <code>-INFINITY</code>，这样第一次 <code>std::max</code> 就能正确取到真实值。</p>
<p>矩累加器 + 9 个频带矩 + 9 个频带功率。</p>'''),
 (441,464,'主循环 · 能量 / 最大值 / 剂量 / QC','''<p class="key">核心：<code>db_to_power(m.LAeq)</code> 把每块的 dB 转成线性功率再相加。</p>
<p class="risk">绝不能直接对 dB 求平均 —— 那会低估（3 dB 差 = 功率差一倍）。</p>
<p>六个 max 用 <code>std::max</code> 滚动取最大；四套剂量直接相加；QC 用计数。</p>'''),
 (465,490,'事件计数与频带矩','''<p><b>事件分级计数</b>：按 <code>m.event_type</code>（0–3）累加 MINOR/MODERATE/SEVERE 三个计数器。<br>
注意这是<b>接口三回写</b>到 metrics 的字段 —— 接口二只是读，并不调 EventDetector。</p>
<p><b>频带矩</b>：9 个频带各累加 n 与 S1–S4，用于跨块合并峰度。</p>'''),
 (492,501,'频带能量累加','''<p>9 个频带的 SPL 也按<b>功率</b>累加，与 LAeq 同理。</p>
<p>逐行写开而不是循环，是为了避免字符串到字段的映射开销 —— 嵌入式常见的写法。</p>'''),
 (503,527,'功率平均换算','''<p>除以块数得到平均功率，再转回 dB。</p>
<p>这就是<b>能量平均</b>（equivalent level），不是算术平均。</p>
<p><b>原始矩直接相加</b>后写入结果，并用合计矩重算总峰度 —— 这样峰度在数学上与「把全部样本拼起来算」等价。</p>'''),
 (529,540,'频带矩与返回','''<p>9 组频带矩写回，返回 <code>MinuteMetrics</code>。</p>
<p class="tip">输出可继续作为下一级聚合的输入（例如 60 个 1 秒 → 1 分钟 → 1 小时），因为矩是可加的。</p>'''),
]
p6 = ['<h2><span class="iface">接口二</span>aggregate_metrics() —— 逐段讲解</h2>',
      '<p class="sub">把 N 个 <code>SecondMetrics</code> 合成一个 <code>MinuteMetrics</code>。共 7 段、125 行。</p>']
for i,(a,b,t,ex) in enumerate(AG_SEG,1):
    p6.append(seg(NP,a,b,t,ex,tag=f'{i:02d}'))
P['p6'] = '\n'.join(p6)

pickle.dump(P, open('/tmp/arch_pages.pkl','wb'))
print('✅ p5 / p6 分段生成完毕', file=sys.stderr)
for k in ('p5','p6'):
    print(f'   {k}: {len(P[k])} 字符, {P[k].count("<div class=\"seg\">")} 段', file=sys.stderr)


# ═══════════════════════════════════════════════════════════
# p7 · 接口三 EventDetector
# ═══════════════════════════════════════════════════════════
CS_SEG = [
 (79,83,'签名与空输入守卫','<p>吃原始 PCM 起止指针，输出 <code>EventCheckResult</code>（NORMAL / UNDERRANGE / OVERLOAD 等）。</p><p class="tip">起止指针逆序直接返回 NORMAL —— 防御性检查。</p>'),
 (85,86,'计算 LZeq 与 LZpeak','<p>两个内部辅助分别扫一遍缓冲：<code>compute_leq()</code> 算等效声级，<code>compute_peak()</code> 算绝对值最大。</p><p class="risk">注意这里用 <b>Z 计权</b>（未计权），不是 A 计权 —— 过载判据看的是原始峰值。</p>'),
 (88,94,'峰值触发（最高优先级）','<p class="key">峰值超阈值 → 立即 OVERLOAD，<b>绕过冷却与去抖动</b>。</p><p>同时置 <code>impulse_detected_</code>、重置冷却计数、清零异常计数。</p><p>为什么绕过？脉冲噪声的判定不能延迟 —— 一旦削波，本段数据已废。</p>'),
 (96,103,'冷却期','<p>冷却期内只抑制<b>电平</b>触发，不抑制上面的过载。</p><p>同时仍判欠量程（信号太小），返回 UNDERRANGE。</p>'),
 (105,133,'已废弃的 LZeq 触发逻辑','<p class="risk">v3.3.0 起，基于 <code>LZeq &gt; 90</code> 的 IMPULSE_SUSPECT 触发被<b>注释保留</b>（第 105–126 行整块注释，第 128 行起才是生效代码）。</p><p>接口三的单触发建议改为：<b>只有 LZpeak ≥ 140 dB → OVERLOAD</b>。</p><p>要恢复需同时取消注释并把 <code>leq_threshold_db</code> 默认值改回 90。这段是「死代码但有文档价值」的典型。</p>'),
]
p7 = ['<h2><span class="iface">接口三</span>EventDetector —— 逐段讲解</h2>',
      '<p class="sub">与 <code>NoiseProcessor</code> 完全解耦的独立状态机。先看主入口 <code>check_segment()</code>。</p>',
      '<h3>check_segment() —— 吃原始 PCM</h3>']
for i,(a,b,t,ex) in enumerate(CS_SEG,1):
    p7.append(seg(ED,a,b,t,ex,tag=f'{i:02d}'))

CM_SEG = [
 (199,206,'收集输入与时间步长','<p>把 LAF/LAS 从 metrics 取出；<code>dt</code> 用 <code>duration_s</code>（异常时兜底 1 s）。</p><p>两个 <code>isfinite</code> 标志决定后续几个判据是否可用 —— 无效值不参与比较。</p>'),
 (208,211,'D2 · 脉冲指标','<p><b>LAF − LAS</b>：Fast 比 Slow 高多少。脉冲噪声时 Fast 冲高而 Slow 滞后，差值放大。</p><p>这是区分「持续噪声」与「脉冲噪声」的核心特征量。</p>'),
 (213,219,'D3 · LAF 上升率','<p>与 <b>500 ms 前</b>的 LAF 比较。<code>lookback_laf(0.5f)</code> 查环形缓冲。</p><p>只在累计时间 &gt; 0.5 s 后才算 —— 否则没有历史可比。</p>'),
 (221,227,'D4 · 背景对比','<p>维护一个 <b>5 分钟指数平均</b>背景 <code>background_laeq_db_</code>，用当前 LAF 减背景得增量。</p><p class="key">这一步会<b>更新成员状态</b>（<code>update_background</code>），所以 check_metrics 不是纯函数。</p>'),
 (229,243,'四个判据的阈值比较','<p>把四个特征量各自与配置阈值比较，得到一组布尔量：</p><ul><li>D1 LAF 绝对阈值 → severe / moderate / minor</li><li>D2 脉冲指标 → severe / minor</li><li>D3 上升率 → moderate / minor</li><li>D4 背景增量 → 单一档</li></ul>'),
 (245,254,'分级（取最高档）','<p>按 <b>severe → moderate → minor → NONE</b> 顺序短路判断，等价于取最高严重度。</p><p class="tip">注意 D1/D2 共享 severe 档，D3/D4 只到 moderate/minor。</p>'),
 (256,265,'触发标志与过载直通','<p>四个 <code>trigger_*</code> 标志写回，便于上层知道是哪个判据触发。</p><p class="key">过载直通：<code>LZPeak ≥ 阈值</code> 时强制提升为 SEVERE —— 与 check_segment 的过载优先级一致。</p>'),
 (267,277,'严重度评分与历史更新','<p><code>grade_severity()</code> 按配置权重算出 0–100 分。</p><p class="key">最后把本次 LAF 压入环形缓冲（<code>push_laf_history</code>），供后续 lookback 使用 —— 这是 D3 判据的状态来源。</p>'),
]
p7 += ['<h3>check_metrics(const SecondMetrics&amp;) —— 用已有指标判定</h3>',
       '<p class="sub">四判据 D1–D4。注意它是<b>有状态</b>的：会更新背景与 LAF 历史。</p>']
for i,(a,b,t,ex) in enumerate(CM_SEG,1):
    p7.append(seg(ED,a,b,t,ex,tag=f'D{i}'))

p7 += ['<h3>其余入口与内部实现</h3>',
 '<h4 class="blk"><code>check_metrics(const float*, const float*)</code> —— 设备侧重载（无需 NoiseProcessor）</h4>',
 code(ED,156,193,'设备侧重载'),
 '<h4 class="blk">类声明（公开接口全貌）</h4>',
 grab(EDH,'class EventDetector','EventDetector 类'),
 '<h4 class="blk"><code>init_internal_chain()</code> —— 内置轻量 A 计权链</h4>',
 code(ED,23,66,'初始化内部 A 计权链'),
 '<h4 class="blk"><code>grade_severity()</code> —— 严重度评分</h4>',
 code(ED,328,373,'grade_severity'),
 '<h4 class="blk"><code>compute_leq()</code> / <code>compute_peak()</code></h4>',
 code(ED,375,395,'compute_leq'), code(ED,397,413,'compute_peak'),
 '<h4 class="blk"><code>reset()</code> —— 注意这里也 reset 了 a_chain_</h4>',
 code(ED,135,154,'reset()'),
 '<div class="warn"><b>第二套独立状态机</b><code>EventDetector</code> 内部有自己的 <code>a_chain_</code>、去抖动计数、LAF 环形缓冲、5 分钟背景。排查状态问题时不要只盯 <code>NoiseProcessor</code>。</div>']
P['p7'] = '\n'.join(p7)

# ═══════════════════════════════════════════════════════════
# p8 · 接口四 DoseState + DoseCalculator
# ═══════════════════════════════════════════════════════════
p8 = ['<h2><span class="iface">接口四</span>DoseState + DoseCalculator —— 逐段讲解</h2>',
 '<p class="sub">跨时间累计剂量，按四套标准换算。<code>DoseState</code> 保存累计值，<code>DoseCalculator</code> 提供纯计算。</p>',
 '<h3>DoseState —— 累计状态</h3>',
 grab(DSH,'struct DoseState','DoseState 完整定义'),
 '<h3>DoseCalculator —— 全部静态方法</h3>',
 grab(DCH,'class DoseCalculator','DoseCalculator 类'),
 '<h3>实现：标准解析与自由函数</h3>',
 code(DC,14,20,'parse_standard —— 字符串 → 枚举'),
 code(DC,22,27,'calculate_noise_dose'),
 code(DC,29,32,'calculate_twa_from_dose'),
 '<h3>四套标准参数</h3>',
 grab(DCH,'struct DoseProfile','DoseProfile 与预设档位',90),
 '<table><thead><tr><th>标准</th><th>字段</th><th>判据</th></tr></thead><tbody>'
 '<tr><td>NIOSH</td><td class="n">dose_frac_niosh</td><td>85 dBA / 3 dB 交换率</td></tr>'
 '<tr><td>OSHA PEL</td><td class="n">dose_frac_osha_pel</td><td>90 dBA / 5 dB 交换率</td></tr>'
 '<tr><td>OSHA HCA</td><td class="n">dose_frac_osha_hca</td><td>听力保护判据</td></tr>'
 '<tr><td>EU / ISO</td><td class="n">dose_frac_eu_iso</td><td>80 dB 下限</td></tr>'
 '</tbody></table>',
 '<div class="note"><b>依赖关系</b><code>DoseState</code> 的 TWA / LEX 委托 <code>DoseCalculator</code>；单块剂量增量由 <b>Phase 3</b> 直接调 <code>calculate_dose_increment()</code> 填入 <code>SecondMetrics.dose_frac_*</code>。独立校验：<code>validation/validate_dose_calculator.py</code>。</div>']
P['p8'] = '\n'.join(p8)

# ═══════════════════════════════════════════════════════════
# p9 · 滤波基础设施
# ═══════════════════════════════════════════════════════════
p9 = ['<h2>滤波基础设施 —— 逐段讲解</h2>',
 '<p class="sub">状态真正存放的地方，也是 20 Hz 问题的物理根源。</p>',
 '<h3>BiquadChain&lt;N&gt; —— 持有状态的容器</h3>',
 grab(FCH,'struct BiquadChain','BiquadChain（模板 + process 实现）'),
 seg(II,115,117,'BiquadFilter::reset()','<p>把 <code>z1_</code> / <code>z2_</code> 双状态清零。</p><p class="key">这一行就是「滤波器重新启动」的全部含义。</p>'),
 seg(II,119,126,'BiquadFilter::process()','<p>转置直接 II 型：</p><p><code>y = b0·x + z1</code><br><code>z1 = b1·x − a1·y + z2</code><br><code>z2 = b2·x − a2·y</code></p><p>每样本只做 5 次乘加，且 <code>a0</code> 预归一化为 1，无需除法。</p><p class="key">状态 <code>z1/z2</code> 就是滤波器的「记忆」—— 清零即重新启动。</p>'),
 seg(II,128,137,'BiquadFilter::process(vector)','<p>对整段信号逐样本调用上面的标量版本，返回新 vector。</p><p class="tip">离线分析用，非实时路径。</p>'),
 '<h4 class="blk">BiquadFilter 类声明</h4>',
 grab(IIH,'class BiquadFilter','BiquadFilter 类'),
 seg(II,37,64,'IIRFilter::reset() / process()','<p>通用二阶节，结构与 BiquadFilter 类似但系数为运行时传入。</p><p>用于非 48 kHz 的回落路径。</p>'),
 seg(II,66,103,'IIRFilter::process(vector) / process_sample()','<p>批量处理与就地处理两种形态。</p><p><code>process_sample(float* data, size_t count)</code> 直接改写调用者的缓冲，零额外分配。</p>'),
 '<h4 class="blk">系数结构</h4>',
 grab(IIH,'struct BiquadCoefficients','BiquadCoefficients'),
 '<div class="danger"><b>时间常数：为什么 20 Hz 特别敏感</b>A 计权在 20 Hz 衰减约 <strong>−50.4 dB</strong>，极点非常靠近单位圆，τ ≈ 7.7 ms。50 ms 窗口只有 6.5τ —— 一旦清零，整块都在瞬态里。<br>C 计权在 20 Hz 只衰减 <strong>−6.2 dB</strong>，极点远离单位圆，因此<strong>同样的清零缺陷在 C 上几乎看不出来</strong>。</div>']
P['p9'] = '\n'.join(p9)

# ═══════════════════════════════════════════════════════════
# p10 · 系数与设计算法
# ═══════════════════════════════════════════════════════════
p10 = ['<h2>系数表与设计算法 —— 逐段讲解</h2>',
 '<p class="sub">设备固定 48 kHz，运行时只查表；运行时设计仅作非 48k 回落。</p>',
 '<h3>WEIGHTING_TABLE —— 单一数据源</h3>',
 grab(WCH,'struct WeightingCoeffs','WeightingCoeffs / WeightingTableEntry / WEIGHTING_TABLE',70),
 '<h3>A 计权设计（matched-z + RBJ 峰化修正）</h3>',
 seg(II,220,262,'前段：模拟原型与极点映射','<p>先在 s 平面构造 IEC 61672-1 的 A 计权模拟传递函数，再用 <b>matched-z</b> 把极点映射到 z 平面：<code>z_p = exp(s_p·T)</code>。</p><p class="key">matched-z 能精确保留低频极点的位置 —— 这正是 20–100 Hz 精度的关键。</p>'),
 seg(II,263,310,'后段：RBJ 峰化修正与归一化','<p>matched-z 对 A 曲线的低频段拟合不足，再叠一级 <b>2 阶 RBJ 峰化</b>修正。</p><p>最后返回 SOS 数组并把 1 kHz 增益通过 <code>gain</code> 出参带出 —— <b>增益不在系数里</b>，运行时单独乘。</p>'),
 '<h3>C 计权设计（复极点最小二乘）</h3>',
 seg(II,314,345,'前段：零点锁定与极点求解','<p>零点锁定在 <code>z = 1</code>（直流处），极点自由，用最小二乘拟合 C 曲线。</p><p>C 曲线在低频平缓，因此只需 3 段。</p>'),
 seg(II,346,377,'后段：输出 SOS','<p>转成 biquad 系数并对 1 kHz 归一化，增益同样单独返回。</p>'),
 '<h3>双线性变换与 SOS 转换</h3>',
 seg(II,172,214,'bilinear_transform()','<p>经典双线性变换：<code>s → 2/T·(z−1)/(z+1)</code>。</p><p>注意预畸变（pre-warping）处理，否则高频会偏移。</p>'),
 seg(II,386,405,'tf2sos()','<p>把高阶传递函数拆成二阶节级联。<b>拆节顺序影响数值稳定性</b> —— 一般按极点靠近单位圆的程度排列。</p>'),
 '<div class="warn"><b>单一数据源原则（v3.3.4 教训）</b>原先 <code>filter_coefficients_48k.hpp</code> 里还有一份 A/C 副本，两份会漂移：C 副本在 8 kHz 同样给出 −1.09 dB（硬件实测偏高 +2.0 dB 的来源之一）；A 副本未归一化，导致 <code>signal_utils</code> 路径漏乘增益 +2.43 dB。<strong>现已删除全部副本，所有调用点统一走 WEIGHTING_TABLE。</strong></div>',
 '<table><thead><tr><th>项目</th><th>值</th></tr></thead><tbody><tr><td>保留采样率</td><td><b>仅 48 kHz</b></td></tr><tr><td>A 段数 / C 段数</td><td><b>4</b> / <b>3</b></td></tr><tr><td>精度（34 频点）</td><td>A <b>0.483 dB</b> · C <b>0.158 dB</b> → Class 1</td></tr><tr><td>验证工具</td><td><code>tests/test_class1_precision.cpp</code></td></tr></tbody></table>']
P['p10'] = '\n'.join(p10)


# ═══════════════════════════════════════════════════════════
# p2 · 项目全貌
# ═══════════════════════════════════════════════════════════
P['p2'] = """
<h2>项目全貌</h2>
<p class="sub">5 个源文件（2164 行）+ 12 个头文件 + 6 个辅助目录。</p>
<h3>顶层结构</h3>
<table><thead><tr><th>路径</th><th>文件数</th><th>体积</th><th>作用</th></tr></thead><tbody>
<tr><td class="n">src/</td><td class="n">5</td><td class="n">96 K</td><td><b>核心实现</b>，编入静态库 · 2164 行</td></tr>
<tr><td class="n">include/</td><td class="n">12（+1 .bak）</td><td class="n">100 K</td><td><b>公开 API 与数据结构</b></td></tr>
<tr><td class="n">tests/</td><td class="n">17</td><td class="n">200 K</td><td>单元 / 回归 / 精度验收</td></tr>
<tr><td class="n">tools/</td><td class="n">10</td><td class="n">204 K</td><td>系数生成器 + 频带探针</td></tr>
<tr><td class="n">docs/</td><td class="n">10</td><td class="n">184 K</td><td>开发计划与专题文档</td></tr>
<tr><td class="n">examples/</td><td class="n">1</td><td class="n">24 K</td><td>4 个接口的官方示范</td></tr>
<tr><td class="n">validation/</td><td class="n">1</td><td class="n">16 K</td><td>剂量标准独立校验</td></tr>
<tr><td class="n">results/</td><td class="n">—</td><td class="n">1.9 G</td><td>验收产物与录音</td></tr>
<tr><td class="n">build_test/</td><td class="n">20</td><td class="n">3.0 M</td><td>构建输出（可重建）</td></tr>
<tr><td class="n">explainer/</td><td class="n">4</td><td class="n">—</td><td><b>本文档的生成器</b>（见 p14）</td></tr>
</tbody></table>
<h3>src/ 五个实现文件</h3>
<table><thead><tr><th>文件</th><th>行数</th><th>体积</th><th>职责</th></tr></thead><tbody>
<tr><td class="n">noise_processor.cpp</td><td class="n">543</td><td class="n">24.9 K</td><td><b>接口一 / 二</b></td></tr>
<tr><td class="n">signal_utils.cpp</td><td class="n">634</td><td class="n">22.3 K</td><td>工具库：计权 / FFT / RMS / 峰度</td></tr>
<tr><td class="n">iir_filter.cpp</td><td class="n">534</td><td class="n">16.8 K</td><td>IIR 运行时设计 + 滤波器实现</td></tr>
<tr><td class="n">event_detector.cpp</td><td class="n">416</td><td class="n">14.7 K</td><td><b>接口三</b></td></tr>
<tr><td class="n">dose_calculator.cpp</td><td class="n">37</td><td class="n">1.2 K</td><td><b>接口四</b></td></tr>
</tbody></table>
<h3>include/ 十二个头文件</h3>
<table><thead><tr><th>头文件</th><th>行数</th><th>声明</th></tr></thead><tbody>
<tr><td class="n">noise_processor.hpp</td><td class="n">180</td><td><code>NoiseProcessor</code>（接口一/二）</td></tr>
<tr><td class="n">event_detector.hpp</td><td class="n">277</td><td><code>EventDetector</code> · <code>EventResult</code> · <code>EventType</code> · <code>EventCheckResult</code> · <code>EventDetectorConfig</code></td></tr>
<tr><td class="n">dose_calculator.hpp</td><td class="n">204</td><td><code>DoseCalculator</code> · <code>DoseProfile</code> · <code>DoseMetrics</code> · <code>DoseStandard</code></td></tr>
<tr><td class="n">dose_state.hpp</td><td class="n">165</td><td><code>DoseState</code>（接口四）</td></tr>
<tr><td class="n">noise_metrics.hpp</td><td class="n">364</td><td><code>SecondMetrics</code> · <code>MinuteMetrics</code> · <code>FreqBandMoments</code></td></tr>
<tr><td class="n">iir_filter.hpp</td><td class="n">216</td><td><code>IIRFilter</code> · <code>BiquadFilter</code> · 系数结构</td></tr>
<tr><td class="n">filter_coefficients_48k.hpp</td><td class="n">68</td><td><code>BiquadChain&lt;N&gt;</code> + 段数常量</td></tr>
<tr><td class="n">weighting_coefficients_multirate.hpp</td><td class="n">87</td><td><code>WEIGHTING_TABLE</code></td></tr>
<tr><td class="n">bandpass_coefficients_48k.hpp</td><td class="n">—</td><td><code>BANDPASS_COEFFS_48K[9]</code></td></tr>
<tr><td class="n">signal_utils.hpp</td><td class="n">206</td><td><code>Signal</code> + 24 个自由函数</td></tr>
<tr><td class="n">math_constants.hpp</td><td class="n">—</td><td>常量</td></tr>
<tr><td class="n">noise_toolkit.hpp</td><td class="n">—</td><td>umbrella header</td></tr>
</tbody></table>
"""

# p3
P['p3'] = """
<h2>它是什么：静态库，不是服务</h2>
<p class="sub">理解整个项目的前提，也是固件集成方式争议的根源。</p>
""" + code('CMakeLists.txt',28,28,'唯一的库目标') + """
<div class="warn"><b>烧进固件后不会“自己运行”</b>它是<strong>被固件代码调用的函数集合</strong>。固件必须自己创建实例、切块、调用、取结果。</div>
<table><thead><tr><th>常见误解</th><th>实际情况</th></tr></thead><tbody>
<tr><td>烧进去就常驻运行</td><td>静态库，完全被动调用</td></tr>
<tr><td>只需调用接口，无需开发</td><td>需约 30–50 行集成代码</td></tr>
<tr><td>状态由库内部管理</td><td>状态挂在<strong>调用方持有的实例</strong>上</td></tr>
<tr><td>只有一个处理器对象</td><td>需要 3 个实例：<code>NoiseProcessor</code> + <code>EventDetector</code> + <code>DoseState</code></td></tr>
</tbody></table>
"""

# p4
P['p4'] = """
<h2>固件集成</h2>
<p class="sub">覆盖 4 个接口的完整集成代码。核心只有一点：实例只创建一次。</p>
""" + code(NPH,66,68,'接口一签名') + code(NPH,85,87,'接口二签名') + """
<pre><code><span class="kw">#include</span> <span class="str">"noise_processor.hpp"</span>
<span class="kw">#include</span> <span class="str">"event_detector.hpp"</span>
<span class="kw">#include</span> <span class="str">"dose_state.hpp"</span>
<span class="kw">using namespace</span> noise_toolkit;

<span class="cmt">// ① 三个实例，全部只创建一次</span>
<span class="kw">static</span> NoiseProcessor g_proc(48000);   <span class="cmt">// 接口一 + 二</span>
<span class="kw">static</span> EventDetector  g_evt;            <span class="cmt">// 接口三</span>
<span class="kw">static</span> DoseState      g_dose;           <span class="cmt">// 接口四</span>

<span class="kw">static</span> SecondMetrics cache[20];
<span class="kw">static</span> <span class="kw">int</span> idx = 0;

<span class="kw">void</span> on_adc_block(<span class="kw">float</span>* pcm, uint32_t n) {
    <span class="kw">auto</span> m = g_proc.process_segment(pcm, pcm + n, 0.05f);      <span class="cmt">// 接口一</span>

    <span class="kw">auto</span> ev = g_evt.check_segment(pcm, pcm + n);              <span class="cmt">// 接口三</span>
    m.event_type     = ev.type;
    m.event_severity = ev.severity;

    cache[idx++] = m;

    <span class="kw">if</span> (idx == 20) {                                          <span class="cmt">// 满 1 秒</span>
        <span class="kw">auto</span> minute = g_proc.aggregate_metrics(cache, 20, 0.05f); <span class="cmt">// 接口二</span>
        g_dose = g_dose.accumulate(minute, 1.0f, DoseStandard::NIOSH); <span class="cmt">// 接口四</span>
        ui_update(minute);
        idx = 0;
    }
}</code></pre>
<div class="warn"><b>三个实例都要长期持有</b><code>NoiseProcessor</code> 持有滤波器状态；<code>EventDetector</code> 持有去抖动 / LAF 环形缓冲 / 5 分钟背景；<code>DoseState</code> 持有累计剂量。任一实例被重建，对应状态就清零。</div>
<h3>五种错误写法</h3>
<pre><code><span class="cmt">// ❌ 1. 局部变量写在块处理函数内</span>
<span class="kw">void</span> on_adc_block(<span class="kw">float</span>* b, uint32_t n) { NoiseProcessor proc(48000); }

<span class="cmt">// ❌ 2. 每次测量周期重建</span>
<span class="kw">void</span> do_measurement() { NoiseProcessor proc(48000); }

<span class="cmt">// ❌ 3. new 放在循环 / 中断里</span>
<span class="kw">auto</span>* p = <span class="kw">new</span> NoiseProcessor(48000);
p-&gt;process_segment(...); <span class="kw">delete</span> p;

<span class="cmt">// ❌ 4. C 封装层每次 create / destroy</span>
<span class="kw">void</span>* h = noise_create(48000); noise_process(h, buf, n, 0.05f); noise_destroy(h);

<span class="cmt">// ❌ 5. 复用实例但每次测量前 reset()（最隐蔽）</span>
g_proc.reset();</code></pre>
"""

# p11 · 数据结构
P['p11'] = """
<h2>数据结构 —— 完整定义</h2>
<p class="sub">对外结果的载体。直接给出结构体原文。</p>
<h3>SecondMetrics —— 45 个 float / 81 项指标</h3>
""" + grab(NM,'struct SecondMetrics','SecondMetrics 完整定义') + """
<h3>MinuteMetrics —— 40 个 float</h3>
""" + grab(NM,'struct MinuteMetrics','MinuteMetrics 完整定义') + """
<h3>FreqBandMoments</h3>
""" + grab(NM,'struct FreqBandMoments','FreqBandMoments') + """
<h3>事件相关类型</h3>
""" + grab(EDH,'enum class EventType','EventType') + grab(EDH,'struct EventResult','EventResult') + grab(EDH,'struct EventDetectorConfig','EventDetectorConfig') + """
<div class="note"><b>字段命名陷阱</b>峰值字段是 <code>LCPeak</code>（大写 P），不是 <code>LCpeak</code>。<code>LAFmax</code> 已标 deprecated，新代码用 <code>LAF</code>。</div>
"""

# p12 · 状态连续性
P['p12'] = """
<h2>⚠ 状态连续性 —— 20 Hz 问题的核心</h2>
<p class="sub">本次排查的结论汇总，也是给固件方的排查依据。</p>
<h3>状态到底存在哪</h3>
""" + code(NPH,118,128,'NoiseProcessor 的私有状态成员') + """
<p><strong>实例活多久，状态就连续多久。</strong></p>
<h3>实测：三种集成方式在 20 Hz / 50 ms 下的偏差</h3>
<table><thead><tr><th>方式</th><th>A 偏差</th><th>C 偏差</th></tr></thead><tbody>
<tr><td><b>M1 单一实例，连续调用</b>（正确）</td><td class="good">+0.10 dB ✅</td><td class="good">+0.02 dB ✅</td></tr>
<tr><td>M2 每块新建 NoiseProcessor</td><td class="bad">+5.56 dB ✗</td><td class="bad">−1.14 dB ✗</td></tr>
<tr><td>M3 每块调 apply_*_weighting_inplace()</td><td class="bad">+5.56 dB ✗</td><td class="bad">−1.14 dB ✗</td></tr>
</tbody></table>
<h3>误差随窗口长度衰减（IIR 启动瞬态）</h3>
<table><thead><tr><th>分块</th><th>20 Hz 周期数</th><th>M2 / M3 的 A 偏差</th></tr></thead><tbody>
<tr><td class="n">50 ms</td><td class="n">1</td><td class="bad">+5.56 dB</td></tr>
<tr><td class="n">100 ms</td><td class="n">2</td><td>+3.61 dB</td></tr>
<tr><td class="n">1000 ms</td><td class="n">20</td><td>+0.53 dB</td></tr>
</tbody></table>
<div class="warn"><b>为什么 v3.3.4 没发现</b>v3.3.4 验收跑 10 / 100 / 1000 ms 且用连续实例。1 秒窗口有 20 个周期，同样的清零缺陷只剩 0.53 dB —— 刚好落在 Class 1 容差边缘被吞掉。<strong>50 ms 这一档从未被验证过。</strong></div>
<div class="danger"><b>更隐蔽的情形</b>正确路径下，<strong>第一个块（从零启动）本身就偏 +5.56 dB</strong>，之后才收敛。即「每块重置」的稳态误差 = 「正确路径」的首块误差。若设备每次测量开始重置、或读数取在刚启动 / 刚切档之后，即使代码正确也会看到 4~5 dB。</div>
<h3>排查清单</h3>
<ol>
<li>构造函数调用几次？<b>NoiseProcessor / EventDetector / DoseState 各应为 1</b></li>
<li>是否调用了 <code>reset()</code>？（三个类都有）</li>
<li>是否绕过接口直接调了 <code>apply_*_weighting_inplace()</code>？</li>
<li>固件链接的库是哪个 tag？</li>
<li><code>duration_s</code> 与 buffer 长度是否精确匹配？</li>
<li>读数取自连续运行的稳态，还是刚启动之后？</li>
</ol>
<div class="ok"><b>回归门禁已就位</b><code>tests/test_state_persistence_matrix.cpp</code> —— 三种集成方式 × 三个窗口。<code>tests/test_weighting_state_persistence.cpp</code> —— 两路径对照，<strong>不一致时返回非零退出码</strong>。</div>
"""

# p13 · signal_utils
P['p13'] = ['<h2>signal_utils 工具库 —— 逐段讲解</h2>',
 '<p class="sub">24 个自由函数。<b>不参与 process_segment 主链路</b>，用于离线分析与辅助计算。</p>',
 '<h3>48 kHz 系数装载</h3>',
 seg(SU,49,82,'load_48k_chain / gain_48k_*','<p>从 <code>WEIGHTING_TABLE</code> 逐条装入 biquad 系数，并单独返回 1 kHz 增益。</p><p class="key">单一数据源：所有路径共用同一张表，避免副本漂移。</p>'),
 '<h3>⚠ 两个 inplace 函数 —— 每次调用都 reset</h3>',
 seg(SU,239,260,'apply_a_weighting_inplace()','<p class="risk">§ 第 244 行 <code>chain.reset();</code> —— <b>每次调用都清零</b>。</p><p>注释写着 "Streaming A-weighting"，但这一行让它变成「每块从零启动」，流式语义名存实亡。</p><p>实测：与「每块新建 processor」数值完全一致（20 Hz 偏 <b>+5.56 dB</b>）。</p><p class="tip">它本意是给「离线处理整段录音」用的。</p>'),
 seg(SU,263,283,'apply_c_weighting_inplace()','<p>同样在第 268 行 <code>chain.reset();</code>。</p><p>但 C 计权在 20 Hz 只衰减 −6.2 dB，极点远离单位圆，瞬态小得多 → 同样缺陷只偏 <b>−1.14 dB</b>，不容易被发现。</p>'),
 '<h3>批量版本（整段一次性处理，无此问题）</h3>',
 code(SU,227,236,'apply_a_weighting / apply_c_weighting'),
 '<h3>时域统计</h3>',
 code(SU,286,301,'calculate_rms / calculate_leq'),
 code(SU,304,313,'calculate_lpeak'),
 seg(SU,394,421,'calculate_kurtosis()','<p>支持 Pearson（正态=3）与 excess（正态=0）两种口径。</p><p>用 S1–S4 矩计算，不需要保留波形。</p>'),
 '<h3>时间计权（离线版）</h3>',
 code(SU,316,338,'time_average'),
 code(SU,341,355,'fast_time_weighting'),
 '<h3>FFT / IFFT</h3>',
 code(SU,487,533,'fft'),
 code(SU,536,552,'ifft'),
 '<h3>滤波器封装</h3>',
 code(SU,556,594,'apply_iir_filter / bandpass / highpass / lowpass'),
 '<h3>频谱与功率谱密度</h3>',
 code(SU,597,616,'calculate_spectrum'),
 code(SU,619,632,'calculate_psd'),
 '<div class="note"><b>测试盲区</b><code>tests/</code> 与 <code>examples/</code> 中<strong>零调用</strong>那两个 inplace 函数 —— 16 个测试文件全部只走 <code>process_segment()</code>。这是该缺陷长期未被发现的原因。</div>']
P['p13'] = '\n'.join(P['p13'])

# p14 · 工具与验证链
P['p14'] = """
<h2>工具与验证链</h2>
<p class="sub">不参与运行时，但决定系数正确性与精度可复现性。</p>
<h3>tools/ (10)</h3>
<table><thead><tr><th>文件</th><th>作用</th></tr></thead><tbody>
<tr><td class="n">regen_weighting_coefficients.py</td><td><b>A/C 计权系数生成器</b> → WEIGHTING_TABLE</td></tr>
<tr><td class="n">regen_bandpass_coefficients.py</td><td>9 个 1/3 倍频程系数生成器</td></tr>
<tr><td class="n">generate_bandpass_coeffs.cpp</td><td>带通系数 C++ 生成</td></tr>
<tr><td class="n">probe_bands_v3.3.1.cpp</td><td>频带响应探针</td></tr>
<tr><td class="n">probe_bandsum_v3.3.1.cpp</td><td>频带求和校验</td></tr>
<tr><td class="n">probe_band_enbw_v3.3.1.py</td><td>等效噪声带宽</td></tr>
<tr><td class="n">probe_coverage_deficit_v3.3.1.py</td><td>覆盖缺口分析</td></tr>
<tr><td class="n">probe_true_octave_gap_v3.3.1.py</td><td>真实倍频程缺口（含 part2）</td></tr>
<tr><td class="n">gen_bp</td><td>已编译带通生成器</td></tr>
</tbody></table>
<h3>tests/ (17) 按主题</h3>
<table><thead><tr><th>主题</th><th>文件</th></tr></thead><tbody>
<tr><td><b>计权精度</b></td><td class="n">test_class1_precision.cpp · test_weighting_response.cpp · test_a_weighting_v3.3.1.py · test_c_weighting_v3.3.3.cpp</td></tr>
<tr><td><b>状态持久性</b></td><td class="n">test_state_persistence_matrix.cpp · test_weighting_state_persistence.cpp</td></tr>
<tr><td><b>处理器</b></td><td class="n">test_noise_processor.cpp · test_end_to_end.cpp · test_v3.3.2_laeq_validation.cpp</td></tr>
<tr><td><b>事件检测</b></td><td class="n">test_event_detector.cpp · test_event_detector_v3.3.3.cpp · test_event_buf_api_v3.3.3.cpp · test_event_longrun_v3.3.3.cpp · test_event_on_wav_v3.3.3.cpp</td></tr>
<tr><td><b>剂量</b></td><td class="n">test_dose_state.cpp</td></tr>
<tr><td><b>其他</b></td><td class="n">test_kurtosis_wav.cpp · test_full_suite_v3.3.1.py</td></tr>
</tbody></table>
<h3>explainer/ —— 本文档的生成器</h3>
<table><thead><tr><th>文件</th><th>作用</th></tr></thead><tbody>
<tr><td class="n">gen_arch.py</td><td>按行号提取源码 + 定义分段 + 生成页面内容 → <code>/tmp/arch_pages.pkl</code></td></tr>
<tr><td class="n">assemble.py</td><td>注入 CSS / SVG 架构图，组装成 <code>ARCHITECTURE.html</code></td></tr>
<tr><td class="n">build.sh</td><td>一键重建</td></tr>
<tr><td class="n">README.md</td><td>使用说明与维护约定</td></tr>
</tbody></table>
<div class="ok"><b>重建方式</b><code>cd explainer &amp;&amp; ./build.sh</code></div>
<div class="note"><b>为什么用生成器而不是手写</b>本文件 <b>200 KB+</b>、引用源码 <b>2000+ 行</b>。手写必然出现漂移；生成器在开头做 <b>13 项锚点校验</b>（如 <code>noise_processor.cpp:126</code> 必须含 <code>process_segment</code>），任何行号错位立即中止，保证文档与源码永远一致。</div>
"""

# ── 最终导出（必须在所有页面定义之后）──
pickle.dump(P, open('/tmp/arch_pages.pkl','wb'))
print('✅ 全部页面生成完毕', file=sys.stderr)
for k in sorted(P, key=lambda s: int(s[1:])):
    v = P[k]
    cnt = v.count('<div class="seg">')
    if v: print(f'   {k}: {len(v):6d} 字符, {cnt} 段', file=sys.stderr)
