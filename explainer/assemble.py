#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""组装最终 ARCHITECTURE.html：精细架构图 + 14 页完整代码讲解"""
import pickle, os

P = pickle.load(open('/tmp/arch_pages.pkl','rb'))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ARCHITECTURE.html')

TABS = [
 ('p1','① 架构图'),('p2','② 项目全貌'),('p3','③ 它是什么'),('p4','④ 固件集成'),
 ('p5','⑤ 接口一 process_segment'),('p6','⑥ 接口二 aggregate'),
 ('p7','⑦ 接口三 EventDetector'),('p8','⑧ 接口四 DoseState'),
 ('p9','⑨ 滤波基础设施'),('p10','⑩ 系数与设计算法'),('p11','⑪ 数据结构'),
 ('p12','⚠ 状态连续性'),('p13','⑫ signal_utils'),('p14','⑬ 工具与验证'),
]

CSS = """
  :root{--bg:#f7f9fb;--card:#fff;--text:#16212b;--muted:#5d6d7b;--border:#dbe3ea;
    --accent:#0b6bcb;--accent-soft:#e9f2fd;--warn:#b45309;--warn-soft:#fff7e6;
    --danger:#b42318;--danger-soft:#fdeeec;--ok:#046c4e;--ok-soft:#e8f7f1;--code-bg:#f6f8fa;
    --mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace;}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);
    font:15px/1.72 -apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Hiragino Sans GB","Microsoft YaHei",sans-serif}
  header{background:linear-gradient(135deg,#083a63,#0b6bcb);color:#fff;padding:26px 24px 22px}
  header h1{margin:0 0 4px;font-size:22px}
  header p{margin:0;opacity:.9;font-size:13.5px}
  header .meta{margin-top:10px;font:12px var(--mono);opacity:.82}
  .tabs{position:sticky;top:0;z-index:60;background:rgba(247,249,251,.97);backdrop-filter:blur(8px);
    border-bottom:1px solid var(--border);padding:8px 16px;overflow-x:auto;white-space:nowrap}
  .tabs .inner{max-width:1560px;margin:0 auto;display:flex;gap:3px}
  .tabs button{font:12.5px/1 inherit;font-family:inherit;color:var(--muted);background:none;
    border:1px solid transparent;padding:7px 10px;border-radius:7px;cursor:pointer;white-space:nowrap}
  .tabs button:hover{background:var(--accent-soft);color:var(--accent)}
  .tabs button.on{background:var(--accent);color:#fff;border-color:var(--accent)}
  .page{display:none;max-width:1560px;margin:0 auto;padding:26px 20px 70px}
  .page.on{display:block}
  h2{font-size:19px;margin:0 0 6px}
  .sub{color:var(--muted);font-size:13.5px;margin:0 0 16px}
  h3{font-size:15px;margin:24px 0 8px;color:#22394d}
  h3:first-of-type{margin-top:8px}
  .diagram-wrap{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:8px 4px 2px;overflow-x:auto}
  svg.arch{display:block;width:100%;min-width:1180px;height:auto}
  .band{stroke:#e6edf3;stroke-width:1}
  .band.c1{fill:#fcfdfe}.band.c2{fill:#f7fbff}.band.c3{fill:#f6fcf8}.band.c4{fill:#faf8fe}
  .band.c5{fill:#fffdf5}.band.c6{fill:#fdf9f9}.band.c7{fill:#f8fafb}
  .cap{font:600 11.5px var(--mono);fill:#7b8b99;letter-spacing:.35px}
  .grp rect{fill:#fff;stroke:#cfdae3;stroke-width:1.4;transition:all .14s}
  .grp .gt{font:600 11.5px var(--mono);fill:#16212b}
  .grp .fn{font:9.5px var(--mono);fill:#6b7c8b}
  .grp:hover rect{stroke:var(--accent);fill:var(--accent-soft);filter:drop-shadow(0 2px 6px rgba(11,107,203,.18))}
  .grp:hover .gt{fill:var(--accent)}
  .grp.hot rect{stroke:#e79a9a;fill:#fdf3f3}.grp.hot .gt{fill:#a5292b}
  .grp.api rect{stroke:#7fb3e0;fill:#f2f9ff}.grp.api .gt{fill:#0a4f92}
  .grp.tool rect{stroke:#bccbd6;fill:#fbfcfd;stroke-dasharray:4 3}
  .arr{fill:none;stroke:#5f7c93;stroke-width:1.5;stroke-linejoin:round;stroke-linecap:round}
  .albl{font:9px var(--mono);fill:#6d8296}
  .node-link{cursor:pointer}
  pre{background:var(--code-bg);border:1px solid var(--border);border-left:3px solid var(--accent);
    border-radius:7px;padding:12px 14px;overflow-x:auto;font:12px/1.55 var(--mono);margin:8px 0}
  code{font-family:var(--mono);font-size:.93em;background:var(--code-bg);padding:1px 5px;border-radius:4px}
  pre code{background:none;padding:0}
  .csrc{display:flex;align-items:center;gap:8px;font:11px var(--mono);color:var(--muted);
    background:#eef3f7;border:1px solid var(--border);border-bottom:none;
    border-radius:7px 7px 0 0;padding:5px 11px;margin-top:10px}
  .csrc code{background:#fff;font-size:10.5px;padding:1px 5px}
  .csrc .cl{margin-left:auto;font-size:10px;color:#8b9aa8}
  .csrc + pre{margin-top:0;border-radius:0 0 7px 7px}

  /* ══ 逐段左右分栏：左讲解 / 右源码 ══ */
  .seg{display:grid;grid-template-columns:360px minmax(0,1fr);gap:16px;
    margin:16px 0;align-items:start}
  .seg .ex{background:#f8fbfd;border:1px solid #e2ecf5;border-left:3px solid #7fb3e0;
    border-radius:0 9px 9px 0;padding:11px 14px}
  .seg .ex h4{margin:0 0 8px;font-size:13.5px;color:#0a4f92;line-height:1.45}
  .seg .ex p{margin:0 0 7px;font-size:12.6px;line-height:1.72;color:#26333f}
  .seg .ex p:last-child{margin-bottom:0}
  .seg .ex ul{margin:3px 0 7px;padding-left:17px;font-size:12.6px;line-height:1.7}
  .seg .ex li{margin:2px 0}
  .seg .ex p.risk{background:#fdeeec;border-left:2px solid #b42318;
    padding:6px 9px;border-radius:0 5px 5px 0}
  .seg .ex p.key{background:#e8f7f1;border-left:2px solid #046c4e;
    padding:6px 9px;border-radius:0 5px 5px 0}
  .seg .ex p.tip{background:#e9f2fd;border-left:2px solid #0b6bcb;
    padding:6px 9px;border-radius:0 5px 5px 0}
  .seg .stag{display:inline-block;font:600 10px var(--mono);background:#0b6bcb;color:#fff;
    padding:1px 6px;border-radius:4px;margin-right:7px;vertical-align:2px}
  .seg .cd{min-width:0}
  .seg .cd pre{margin:0;font-size:11.5px}
  h4.blk{margin:22px 0 6px;font-size:13.5px;color:#22394d;
    padding-bottom:5px;border-bottom:1px dashed var(--border)}
  @media(max-width:1100px){.seg{grid-template-columns:1fr}}
  .cmt{color:#7b8b99}.kw{color:#0a5aa4;font-weight:600}.str{color:#8a5a00}
  .note,.warn,.danger,.ok{border-radius:8px;padding:11px 14px;margin:13px 0;font-size:13.5px}
  .note{background:var(--accent-soft);border-left:3px solid var(--accent)}
  .warn{background:var(--warn-soft);border-left:3px solid var(--warn)}
  .danger{background:var(--danger-soft);border-left:3px solid var(--danger)}
  .ok{background:var(--ok-soft);border-left:3px solid var(--ok)}
  .note b,.warn b,.danger b,.ok b{display:block;margin-bottom:3px}
  table{border-collapse:collapse;width:100%;margin:12px 0;font-size:13px}
  th,td{border:1px solid var(--border);padding:6px 9px;text-align:left;vertical-align:top}
  th{background:#eff4f8;font-size:12.5px}
  tbody tr:hover{background:#fafcfd}
  td.n{font-family:var(--mono);font-size:11.5px;color:var(--muted)}
  .good{color:var(--ok);font-weight:600}.bad{color:var(--danger);font-weight:600}
  .iface{display:inline-block;font:600 11px var(--mono);background:var(--accent);color:#fff;
    padding:2px 7px;border-radius:5px;margin-right:7px;vertical-align:2px}
  .pager{display:flex;justify-content:space-between;gap:12px;margin-top:34px;padding-top:16px;
    border-top:1px solid var(--border)}
  .pager button{font:13.5px inherit;font-family:inherit;background:#fff;border:1px solid var(--border);
    color:var(--text);padding:9px 16px;border-radius:8px;cursor:pointer}
  .pager button:hover{border-color:var(--accent);color:var(--accent)}
  .hint{font-size:12.5px;color:var(--muted);margin-top:10px}
  @media(max-width:760px){.page{padding:18px 12px 60px}header h1{font-size:18px}pre{font-size:11px}}
"""

# ─────────── 精细架构图（层 → 类/模块 → 函数） ───────────
SVG = """
<svg class="arch" viewBox="-8 0 1700 1345" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah" viewBox="0 0 10 10" refX="9.5" refY="5" markerWidth="7" markerHeight="7"
  orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#5f7c93"/></marker></defs>

<!-- ① 固件层 -->
<rect class="band c1" x="150" y="50" width="1500" height="98" rx="10"/>
<text class="cap" x="142" y="103" text-anchor="end">① 固件层</text>
<a class="node-link" data-goto="p4"><g class="grp"><rect x="170" y="62" width="235" height="74" rx="8"/>
<text class="gt" x="287" y="88" text-anchor="middle">ADC 采集与切块</text>
<text class="fn" x="287" y="106" text-anchor="middle">48 kHz · float PCM</text>
<text class="fn" x="287" y="121" text-anchor="middle">50 ms = 2400 samples</text></g></a>
<a class="node-link" data-goto="p12"><g class="grp hot"><rect x="420" y="62" width="245" height="74" rx="8"/>
<text class="gt" x="542" y="88" text-anchor="middle">NoiseProcessor 实例</text>
<text class="fn" x="542" y="106" text-anchor="middle">⚠ 必须只创建一次</text>
<text class="fn" x="542" y="121" text-anchor="middle">static / 类成员</text></g></a>
<a class="node-link" data-goto="p7"><g class="grp"><rect x="680" y="62" width="225" height="74" rx="8"/>
<text class="gt" x="792" y="88" text-anchor="middle">EventDetector 实例</text>
<text class="fn" x="792" y="106" text-anchor="middle">独立状态机</text>
<text class="fn" x="792" y="121" text-anchor="middle">自带 a_chain_</text></g></a>
<a class="node-link" data-goto="p8"><g class="grp"><rect x="920" y="62" width="205" height="74" rx="8"/>
<text class="gt" x="1022" y="88" text-anchor="middle">DoseState 实例</text>
<text class="fn" x="1022" y="106" text-anchor="middle">累计剂量</text>
<text class="fn" x="1022" y="121" text-anchor="middle">跨时间持存</text></g></a>
<a class="node-link" data-goto="p11"><g class="grp"><rect x="1140" y="62" width="205" height="74" rx="8"/>
<text class="gt" x="1242" y="88" text-anchor="middle">指标缓存</text>
<text class="fn" x="1242" y="106" text-anchor="middle">SecondMetrics[ ]</text>
<text class="fn" x="1242" y="121" text-anchor="middle">1 秒 = 20 块</text></g></a>
<a class="node-link" data-goto="p4"><g class="grp"><rect x="1360" y="62" width="190" height="74" rx="8"/>
<text class="gt" x="1455" y="88" text-anchor="middle">输出</text>
<text class="fn" x="1455" y="106" text-anchor="middle">显示 / 存储 / 报警</text>
<text class="fn" x="1455" y="121" text-anchor="middle">由聚合结果驱动</text></g></a>

<!-- ② 对外接口层：4 个大类 -->
<rect class="band c2" x="150" y="180" width="1500" height="164" rx="10"/>
<text class="cap" x="142" y="266" text-anchor="end">② 对外接口层</text>

<a class="node-link" data-goto="p5"><g class="grp api"><rect x="170" y="192" width="395" height="144" rx="8"/>
<text class="gt" x="182" y="212">class NoiseProcessor</text>
<text class="fn" x="182" y="232">② SecondMetrics process_segment(buf, end, dur)</text>
<text class="fn" x="182" y="250">② MinuteMetrics aggregate_metrics(arr, n, unit)</text>
<text class="fn" x="182" y="268">void reset()</text>
<text class="fn" x="182" y="290">私有: a_weight_chain_ / c_weight_chain_</text>
<text class="fn" x="182" y="308">      band_filters_[9] / laf_sq_a_ / las_sq_a_</text>
<text class="fn" x="182" y="326">      （① 接口一 ② 接口二）</text></g></a>

<a class="node-link" data-goto="p7"><g class="grp api"><rect x="585" y="192" width="395" height="144" rx="8"/>
<text class="gt" x="597" y="212">class EventDetector</text>
<text class="fn" x="597" y="232">③ EventCheckResult check_segment(buf, end)</text>
<text class="fn" x="597" y="250">EventResult check_metrics(const SecondMetrics&amp;)</text>
<text class="fn" x="597" y="268">EventResult check_metrics(buf, end)</text>
<text class="fn" x="597" y="286">void reset() / was_impulse_detected()</text>
<text class="fn" x="597" y="308">私有: a_chain_ / 去抖动 / LAF 环形缓冲</text>
<text class="fn" x="597" y="326">      5 min 指数背景（③ 接口三）</text></g></a>

<a class="node-link" data-goto="p8"><g class="grp api"><rect x="1000" y="192" width="265" height="144" rx="8"/>
<text class="gt" x="1012" y="212">DoseState</text>
<text class="fn" x="1012" y="232">④ accumulate(m, dur, std)</text>
<text class="fn" x="1012" y="250">float twa(std) const</text>
<text class="fn" x="1012" y="268">float lex(std) const</text>
<text class="fn" x="1012" y="290">字段: cumulative_dose_frac</text>
<text class="fn" x="1012" y="308">      elapsed_hours</text>
<text class="fn" x="1012" y="326">（④ 接口四）</text></g></a>

<a class="node-link" data-goto="p8"><g class="grp api"><rect x="1285" y="192" width="290" height="144" rx="8"/>
<text class="gt" x="1297" y="212">class DoseCalculator</text>
<text class="fn" x="1297" y="232">calculate_dose_increment(laeq, dur, prof)</text>
<text class="fn" x="1297" y="250">calculate_twa(dose, std)  ×2 重载</text>
<text class="fn" x="1297" y="268">calculate_lex(dose, prof)</text>
<text class="fn" x="1297" y="286">calculate_allowed_time(laeq, prof)</text>
<text class="fn" x="1297" y="304">get_profile() / parse_standard()</text>
<text class="fn" x="1297" y="322">全静态，共 11 个方法</text></g></a>

<!-- ③ 核心处理层：process_segment 内部函数级调用链 -->
<rect class="band c3" x="150" y="384" width="1500" height="232" rx="10"/>
<text class="cap" x="142" y="504" text-anchor="end">③ 核心处理层</text>

<a class="node-link" data-goto="p5"><g class="grp"><rect x="170" y="396" width="190" height="208" rx="8"/>
<text class="gt" x="265" y="418" text-anchor="middle">① 入口守卫</text>
<text class="fn" x="265" y="440" text-anchor="middle">n = end − start</text>
<text class="fn" x="265" y="456" text-anchor="middle">expected_n =</text>
<text class="fn" x="265" y="472" text-anchor="middle">　sample_rate × dur</text>
<text class="fn" x="265" y="494" text-anchor="middle">n ≠ expected_n</text>
<text class="fn" x="265" y="510" text-anchor="middle">→ return {}</text>
<text class="fn" x="265" y="532" text-anchor="middle">静默，不打日志</text></g></a>

<a class="node-link" data-goto="p5"><g class="grp"><rect x="380" y="396" width="185" height="208" rx="8"/>
<text class="gt" x="472" y="418" text-anchor="middle">② P1 拷贝</text>
<text class="fn" x="472" y="440" text-anchor="middle">for i in 0..n</text>
<text class="fn" x="472" y="456" text-anchor="middle">　a_buf[i] = src[i]</text>
<text class="fn" x="472" y="472" text-anchor="middle">　c_buf[i] = src[i]</text>
<text class="fn" x="472" y="494" text-anchor="middle">src 不被改写</text>
<text class="fn" x="472" y="510" text-anchor="middle">零堆分配</text></g></a>

<a class="node-link" data-goto="p5"><g class="grp"><rect x="585" y="396" width="200" height="208" rx="8"/>
<text class="gt" x="685" y="418" text-anchor="middle">③ P2 A/C 计权</text>
<text class="fn" x="685" y="440" text-anchor="middle">a_weight_chain_</text>
<text class="fn" x="685" y="456" text-anchor="middle">　.process(a_buf[i])</text>
<text class="fn" x="685" y="472" text-anchor="middle">　× a_weight_gain_</text>
<text class="fn" x="685" y="494" text-anchor="middle">c_weight_chain_</text>
<text class="fn" x="685" y="510" text-anchor="middle">　.process(c_buf[i])</text>
<text class="fn" x="685" y="532" text-anchor="middle">状态跨调用连续</text></g></a>

<a class="node-link" data-goto="p5"><g class="grp"><rect x="805" y="396" width="190" height="208" rx="8"/>
<text class="gt" x="900" y="418" text-anchor="middle">④ P2.5 时间计权</text>
<text class="fn" x="900" y="440" text-anchor="middle">α = exp(−1/τfs)</text>
<text class="fn" x="900" y="456" text-anchor="middle">laf_sq_a_ =</text>
<text class="fn" x="900" y="472" text-anchor="middle">　α·laf + (1−α)x²</text>
<text class="fn" x="900" y="494" text-anchor="middle">las_sq_a_ 同理</text>
<text class="fn" x="900" y="510" text-anchor="middle">Fast 125ms</text>
<text class="fn" x="900" y="526" text-anchor="middle">Slow 1s</text></g></a>

<a class="node-link" data-goto="p6"><g class="grp"><rect x="1015" y="396" width="205" height="208" rx="8"/>
<text class="gt" x="1117" y="418" text-anchor="middle">⑤ P3 指标计算</text>
<text class="fn" x="1117" y="440" text-anchor="middle">单遍 for 循环</text>
<text class="fn" x="1117" y="456" text-anchor="middle">Σx² → LAeq/LCeq/LZeq</text>
<text class="fn" x="1117" y="472" text-anchor="middle">峰值 / 峰度 / 矩 S1–S4</text>
<text class="fn" x="1117" y="494" text-anchor="middle">DoseCalculator::</text>
<text class="fn" x="1117" y="510" text-anchor="middle">　calculate_dose_increment</text>
<text class="fn" x="1117" y="532" text-anchor="middle">（4 套标准各一次）</text></g></a>

<a class="node-link" data-goto="p6"><g class="grp"><rect x="1240" y="396" width="200" height="208" rx="8"/>
<text class="gt" x="1340" y="418" text-anchor="middle">⑥ P4 倍频程</text>
<text class="fn" x="1340" y="440" text-anchor="middle">for b in 0..8</text>
<text class="fn" x="1340" y="456" text-anchor="middle">　band_filters_[b]</text>
<text class="fn" x="1340" y="472" text-anchor="middle">　　.process(x)</text>
<text class="fn" x="1340" y="494" text-anchor="middle">逐样本过带通</text>
<text class="fn" x="1340" y="510" text-anchor="middle">累加平方 → SPL</text>
<text class="fn" x="1340" y="532" text-anchor="middle">63 Hz … 8 kHz</text></g></a>

<!-- ④ 滤波基础设施 -->
<rect class="band c4" x="150" y="654" width="1500" height="156" rx="10"/>
<text class="cap" x="142" y="736" text-anchor="end">④ 滤波基础设施</text>
<a class="node-link" data-goto="p9"><g class="grp"><rect x="585" y="666" width="200" height="132" rx="8"/>
<text class="gt" x="685" y="688" text-anchor="middle">BiquadChain&lt;N&gt;::process()</text>
<text class="fn" x="685" y="708" text-anchor="middle">sections[N]</text>
<text class="fn" x="685" y="724" text-anchor="middle">state[N × 2]</text>
<text class="fn" x="685" y="740" text-anchor="middle">转置直接 II 型</text>
<text class="fn" x="685" y="762" text-anchor="middle">reset() 清零 state</text>
<text class="fn" x="685" y="782" text-anchor="middle">逐节级联</text></g></a>
<a class="node-link" data-goto="p9"><g class="grp"><rect x="805" y="666" width="190" height="132" rx="8"/>
<text class="gt" x="900" y="688" text-anchor="middle">BiquadFilter::process()</text>
<text class="fn" x="900" y="708" text-anchor="middle">b0_…a2_ 系数</text>
<text class="fn" x="900" y="724" text-anchor="middle">z1_, z2_ 状态</text>
<text class="fn" x="900" y="740" text-anchor="middle">每样本一次乘加</text>
<text class="fn" x="900" y="762" text-anchor="middle">reset() 清零 z</text></g></a>
<a class="node-link" data-goto="p9"><g class="grp"><rect x="1015" y="666" width="190" height="132" rx="8"/>
<text class="gt" x="1110" y="688" text-anchor="middle">IIRFilter::process()</text>
<text class="fn" x="1110" y="708" text-anchor="middle">通用二阶节</text>
<text class="fn" x="1110" y="724" text-anchor="middle">process_sample()</text>
<text class="fn" x="1110" y="740" text-anchor="middle">批量 process(vector)</text>
<text class="fn" x="1110" y="762" text-anchor="middle">非 48k 路径</text></g></a>
<a class="node-link" data-goto="p10"><g class="grp"><rect x="1225" y="666" width="235" height="132" rx="8"/>
<text class="gt" x="1342" y="688" text-anchor="middle">filter_design::</text>
<text class="fn" x="1342" y="708" text-anchor="middle">a_weighting_design()</text>
<text class="fn" x="1342" y="724" text-anchor="middle">c_weighting_design()</text>
<text class="fn" x="1342" y="740" text-anchor="middle">bilinear_transform()</text>
<text class="fn" x="1342" y="762" text-anchor="middle">tf2sos() / butterworth()</text>
<text class="fn" x="1342" y="782" text-anchor="middle">仅运行时回落使用</text></g></a>

<!-- ⑤ 系数源 -->
<rect class="band c5" x="150" y="844" width="1500" height="126" rx="10"/>
<text class="cap" x="142" y="911" text-anchor="end">⑤ 系数源</text>
<a class="node-link" data-goto="p13"><g class="grp"><rect x="585" y="856" width="200" height="102" rx="8"/>
<text class="gt" x="685" y="878" text-anchor="middle">load_48k_chain()</text>
<text class="fn" x="685" y="898" text-anchor="middle">signal_utils.cpp:49/65</text>
<text class="fn" x="685" y="914" text-anchor="middle">从表装入 BiquadChain</text>
<text class="fn" x="685" y="936" text-anchor="middle">gain_48k_a() / _c()</text></g></a>
<a class="node-link" data-goto="p10"><g class="grp"><rect x="805" y="856" width="190" height="102" rx="8"/>
<text class="gt" x="900" y="878" text-anchor="middle">WEIGHTING_TABLE</text>
<text class="fn" x="900" y="898" text-anchor="middle">A_48000[4] / C_48000[3]</text>
<text class="fn" x="900" y="914" text-anchor="middle">+ a_gain / c_gain</text>
<text class="fn" x="900" y="936" text-anchor="middle">48 kHz only</text></g></a>
<a class="node-link" data-goto="p10"><g class="grp"><rect x="1015" y="856" width="190" height="102" rx="8"/>
<text class="gt" x="1110" y="878" text-anchor="middle">段数常量</text>
<text class="fn" x="1110" y="898" text-anchor="middle">A_WEIGHTING_SECTIONS=4</text>
<text class="fn" x="1110" y="914" text-anchor="middle">C_WEIGHTING_SECTIONS=3</text>
<text class="fn" x="1110" y="936" text-anchor="middle">超出的段被静默丢弃</text></g></a>
<a class="node-link" data-goto="p10"><g class="grp"><rect x="1225" y="856" width="235" height="102" rx="8"/>
<text class="gt" x="1342" y="878" text-anchor="middle">BANDPASS_COEFFS_48K[9]</text>
<text class="fn" x="1342" y="898" text-anchor="middle">bandpass_coefficients_48k</text>
<text class="fn" x="1342" y="914" text-anchor="middle">63 Hz … 8 kHz</text>
<text class="fn" x="1342" y="936" text-anchor="middle">9 个 1/3 倍频程</text></g></a>

<!-- ⑥ 数据结构 -->
<rect class="band c6" x="150" y="1004" width="1500" height="126" rx="10"/>
<text class="cap" x="142" y="1071" text-anchor="end">⑥ 数据结构</text>
<a class="node-link" data-goto="p11"><g class="grp"><rect x="170" y="1016" width="225" height="102" rx="8"/>
<text class="gt" x="282" y="1038" text-anchor="middle">SecondMetrics</text>
<text class="fn" x="282" y="1058" text-anchor="middle">45 float / 81 项指标</text>
<text class="fn" x="282" y="1074" text-anchor="middle">声级13·剂量4·倍频程9</text>
<text class="fn" x="282" y="1096" text-anchor="middle">原始矩 S1–S4</text></g></a>
<a class="node-link" data-goto="p11"><g class="grp"><rect x="415" y="1016" width="190" height="102" rx="8"/>
<text class="gt" x="510" y="1038" text-anchor="middle">MinuteMetrics</text>
<text class="fn" x="510" y="1058" text-anchor="middle">40 个 float</text>
<text class="fn" x="510" y="1074" text-anchor="middle">接口二输出</text>
<text class="fn" x="510" y="1096" text-anchor="middle">聚合结果</text></g></a>
<a class="node-link" data-goto="p11"><g class="grp"><rect x="625" y="1016" width="190" height="102" rx="8"/>
<text class="gt" x="720" y="1038" text-anchor="middle">EventResult</text>
<text class="fn" x="720" y="1058" text-anchor="middle">type / severity</text>
<text class="fn" x="720" y="1074" text-anchor="middle">EventType 枚举</text>
<text class="fn" x="720" y="1096" text-anchor="middle">接口三输出</text></g></a>
<a class="node-link" data-goto="p11"><g class="grp"><rect x="835" y="1016" width="185" height="102" rx="8"/>
<text class="gt" x="927" y="1038" text-anchor="middle">DoseMetrics</text>
<text class="fn" x="927" y="1058" text-anchor="middle">DoseProfile</text>
<text class="fn" x="927" y="1074" text-anchor="middle">DoseStandard 枚举</text>
<text class="fn" x="927" y="1096" text-anchor="middle">接口四输出</text></g></a>
<a class="node-link" data-goto="p11"><g class="grp"><rect x="1040" y="1016" width="205" height="102" rx="8"/>
<text class="gt" x="1142" y="1038" text-anchor="middle">FreqBandMoments</text>
<text class="fn" x="1142" y="1058" text-anchor="middle">9 频带原始矩</text>
<text class="fn" x="1142" y="1074" text-anchor="middle">供聚合精确合并</text></g></a>
<a class="node-link" data-goto="p13"><g class="grp"><rect x="1265" y="1016" width="175" height="102" rx="8"/>
<text class="gt" x="1352" y="1038" text-anchor="middle">Signal</text>
<text class="fn" x="1352" y="1058" text-anchor="middle">data / sample_rate</text>
<text class="fn" x="1352" y="1074" text-anchor="middle">channels</text>
<text class="fn" x="1352" y="1096" text-anchor="middle">工具库输入</text></g></a>

<!-- ⑦ 工具与验证 -->
<rect class="band c7" x="150" y="1164" width="1500" height="126" rx="10"/>
<text class="cap" x="142" y="1231" text-anchor="end">⑦ 工具与验证</text>
<a class="node-link" data-goto="p14"><g class="grp tool"><rect x="170" y="1176" width="220" height="102" rx="8"/>
<text class="gt" x="280" y="1198" text-anchor="middle">tools/ (10)</text>
<text class="fn" x="280" y="1218" text-anchor="middle">regen_weighting_coefficients.py</text>
<text class="fn" x="280" y="1234" text-anchor="middle">regen_bandpass_coefficients.py</text>
<text class="fn" x="280" y="1256" text-anchor="middle">probe_*.py / probe_*.cpp</text></g></a>
<a class="node-link" data-goto="p14"><g class="grp tool"><rect x="410" y="1176" width="220" height="102" rx="8"/>
<text class="gt" x="520" y="1198" text-anchor="middle">tests/ (17)</text>
<text class="fn" x="520" y="1218" text-anchor="middle">test_class1_precision.cpp</text>
<text class="fn" x="520" y="1234" text-anchor="middle">test_end_to_end.cpp</text>
<text class="fn" x="520" y="1256" text-anchor="middle">test_state_persistence_matrix</text></g></a>
<a class="node-link" data-goto="p14"><g class="grp tool"><rect x="650" y="1176" width="195" height="102" rx="8"/>
<text class="gt" x="747" y="1198" text-anchor="middle">validation/</text>
<text class="fn" x="747" y="1218" text-anchor="middle">validate_dose_calculator.py</text>
<text class="fn" x="747" y="1240" text-anchor="middle">剂量标准独立校验</text></g></a>
<a class="node-link" data-goto="p14"><g class="grp tool"><rect x="865" y="1176" width="195" height="102" rx="8"/>
<text class="gt" x="962" y="1198" text-anchor="middle">examples/</text>
<text class="fn" x="962" y="1218" text-anchor="middle">main.cpp</text>
<text class="fn" x="962" y="1240" text-anchor="middle">4 个接口官方示范</text></g></a>
<a class="node-link" data-goto="p14"><g class="grp tool"><rect x="1080" y="1176" width="195" height="102" rx="8"/>
<text class="gt" x="1177" y="1198" text-anchor="middle">docs/ (10)</text>
<text class="fn" x="1177" y="1218" text-anchor="middle">DEVELOPMENT_PLAN_v*.md</text>
<text class="fn" x="1177" y="1240" text-anchor="middle">BUG_v3.2_A_WEIGHTING.md</text></g></a>
<a class="node-link" data-goto="p14"><g class="grp tool"><rect x="1295" y="1176" width="195" height="102" rx="8"/>
<text class="gt" x="1392" y="1198" text-anchor="middle">results/</text>
<text class="fn" x="1392" y="1218" text-anchor="middle">验收报告 / 验证数据</text>
<text class="fn" x="1392" y="1240" text-anchor="middle">测试程序</text></g></a>

<!-- ══ 调用链箭头 ══ -->
<path class="arr" marker-end="url(#ah)" d="M 405 99 L 416 99"/>
<path class="arr" marker-end="url(#ah)" d="M 542 136 L 542 188"/>
<text class="albl" x="548" y="166">调用</text>
<path class="arr" marker-end="url(#ah)" d="M 792 136 L 780 188"/>
<path class="arr" marker-end="url(#ah)" d="M 1022 136 L 1132 190"/>
<path class="arr" marker-end="url(#ah)" d="M 1242 136 L 1242 188"/>

<path class="arr" marker-end="url(#ah)" d="M 300 336 L 300 368 L 265 368 L 265 392"/>
<text class="albl" x="308" y="362">process_segment()</text>

<path class="arr" marker-end="url(#ah)" d="M 360 500 L 374 500"/>
<path class="arr" marker-end="url(#ah)" d="M 565 500 L 579 500"/>
<path class="arr" marker-end="url(#ah)" d="M 785 500 L 799 500"/>
<path class="arr" marker-end="url(#ah)" d="M 995 500 L 1009 500"/>
<path class="arr" marker-end="url(#ah)" d="M 1220 500 L 1234 500"/>

<path class="arr" marker-end="url(#ah)" d="M 685 604 L 685 662"/>
<text class="albl" x="691" y="638">process()</text>
<path class="arr" marker-end="url(#ah)" d="M 1340 604 L 1340 662"/>
<path class="arr" marker-end="url(#ah)" d="M 1117 604 L 1117 1012"/>
<text class="albl" x="1123" y="900">填充字段</text>

<path class="arr" marker-end="url(#ah)" d="M 785 732 L 799 732"/>
<path class="arr" marker-end="url(#ah)" d="M 900 798 L 900 852"/>
<text class="albl" x="906" y="830">系数</text>
<path class="arr" marker-end="url(#ah)" d="M 805 907 L 789 907"/>
<path class="arr" marker-end="url(#ah)" d="M 900 856 L 700 812"/>
<text class="albl" x="742" y="880">装入</text>
<path class="arr" marker-end="url(#ah)" d="M 1340 856 L 1340 800"/>
<text class="albl" x="1346" y="830">系数</text>
<path class="arr" marker-end="url(#ah)" d="M 415 1067 L 429 1067"/>
<text class="albl" x="330" y="1114">聚合</text>
</svg>
"""

assert 'p1' not in P or P['p1'] is None
P['p1'] = ('<h2>架构图</h2>\n'
 '<p class="sub">三层级联：<b>层次 → 类 / 模块 → 函数</b>。箭头标出关键调用链，'
 '点击任一模块跳转到对应讲解页（页内附完整原始代码）。</p>\n'
 f'<div class="diagram-wrap">{SVG}</div>\n'
 '<h3>关键调用链时序（process_segment 一行块内发生的事）</h3>\n'
 '<table><thead><tr><th>#</th><th>调用</th><th>位置</th><th>状态</th></tr></thead><tbody>'
 '<tr><td class="n">1</td><td><code>expected_n</code> 校验</td><td class="n">noise_processor.cpp:126</td><td>无</td></tr>'
 '<tr><td class="n">2</td><td><code>a_buf[i] / c_buf[i] = buffer_start[i]</code></td><td class="n">:141</td><td>无</td></tr>'
 '<tr><td class="n">3</td><td><code>a_weight_chain_.process(x) × a_weight_gain_</code></td><td class="n">:167</td><td class="good">成员状态，跨调用连续</td></tr>'
 '<tr><td class="n">4</td><td><code>c_weight_chain_.process(x) × c_weight_gain_</code></td><td class="n">:168</td><td class="good">成员状态，跨调用连续</td></tr>'
 '<tr><td class="n">5</td><td><code>laf_sq_a_ / las_sq_a_</code> 指数更新</td><td class="n">:172–185</td><td class="good">成员状态</td></tr>'
 '<tr><td class="n">6</td><td>Σx² / 峰值 / 峰度 / 矩 S1–S4</td><td class="n">:186–323</td><td>局部累加</td></tr>'
 '<tr><td class="n">7</td><td><code>DoseCalculator::calculate_dose_increment()</code> ×4</td><td class="n">:188–191</td><td>纯函数</td></tr>'
 '<tr><td class="n">8</td><td><code>band_filters_[b].process(x)</code> × 9 段</td><td class="n">:324–414</td><td class="good">成员状态，跨调用连续</td></tr>'
 '</tbody></table>\n'
 '<div class="warn"><b>红框是本次问题的关键</b>'
 '「NoiseProcessor 实例」必须只创建一次 —— 所有绿色状态的连续性都取决于它。'
 '详见 <a href="#" data-goto="p12">⚠ 状态连续性</a>。</div>\n')

BODY = []
for i,(pid,_) in enumerate(TABS):
    prev_id = TABS[i-1][0] if i>0 else None
    next_id = TABS[i+1][0] if i<len(TABS)-1 else None
    nav = '<div class="pager">'
    nav += f'<button data-goto="{prev_id}">← 上一页</button>' if prev_id else '<button disabled>← 上一页</button>'
    nav += f'<button data-goto="{next_id}">下一页 →</button>' if next_id else '<button data-goto="p1">返回架构图 →</button>'
    nav += '</div>'
    BODY.append(f'<div class="page" id="{pid}">{P[pid]}\n{nav}</div>')

HTML = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>noise_info_toolkit_gcc — 架构与代码讲解</title>
<style>{CSS}</style>
</head>
<body>
<header>
  <h1>noise_info_toolkit_gcc — 架构与代码讲解</h1>
  <p>4 个对外接口 · 7 层结构 · 2164 行源码逐段讲解 · <b>讲解与源码左右一一对应</b></p>
  <div class="meta">v3.3.4 · HEAD 2c859f2 · 48 kHz only · 静态库 libnoise_toolkit.a · 5 源 + 12 头</div>
</header>
<div class="tabs"><div class="inner" id="tabs"></div></div>
{''.join(BODY)}
<script>
(function(){{
  const PAGES = {[list(t) for t in TABS]!r};
  const tabs = document.getElementById('tabs');
  PAGES.forEach(function(p){{
    var b=document.createElement('button');
    b.textContent=p[1]; b.dataset.goto=p[0]; tabs.appendChild(b);
  }});
  function show(id, push){{
    if(!document.getElementById(id)) id='p1';
    document.querySelectorAll('.page').forEach(function(p){{p.classList.toggle('on', p.id===id);}});
    tabs.querySelectorAll('button').forEach(function(b){{b.classList.toggle('on', b.dataset.goto===id);}});
    if(push) history.replaceState(null,'','#'+id);
    requestAnimationFrame(function(){{window.scrollTo(0,0);}});
  }}
  document.addEventListener('click', function(e){{
    var t=e.target.closest('[data-goto]'); if(!t) return;
    e.preventDefault(); show(t.dataset.goto, true);
  }});
  window.addEventListener('hashchange', function(){{ show(location.hash.slice(1), false); }});
  show(location.hash.slice(1) || 'p1', false);
  window.addEventListener('load', function(){{window.scrollTo(0,0);}});
}})();
</script>
</body>
</html>
"""

open(OUT,'w',encoding='utf-8').write(HTML)
print(f'✅ 已写入 {OUT}')
print(f'   大小 {len(HTML.encode())} 字节 / {HTML.count(chr(10))+1} 行')
print(f'   页面 {len(TABS)} 个')
