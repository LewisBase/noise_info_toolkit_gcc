# explainer/ — ARCHITECTURE.html 生成器

本目录用于**生成并维护** `ARCHITECTURE.html`（架构与代码讲解文档）。生成物与本目录同处，便于统一管理。

## 为什么用生成器

`ARCHITECTURE.html` 引用源码 **2000+ 行**，手写必然与代码漂移。生成器按**行号**从真实源文件提取代码，
并在开头做 **13 项锚点校验**（例：`noise_processor.cpp:126` 必须含 `process_segment`）。
任何行号错位会**立即中止**，不会产出错位文档。

## 重建

```bash
cd explainer
./build.sh          # = python3 gen_arch.py && python3 assemble.py
```

产物：`explainer/ARCHITECTURE.html`

## 两个脚本的分工

| 脚本 | 职责 |
|---|---|
| `gen_arch.py` | 读源码 → 锚点校验 → 逐段提取代码 → 生成页面内容 → `/tmp/arch_pages.pkl` |
| `assemble.py` | 注入 CSS / SVG 架构图 / 分页 JS → 组装成最终 HTML |

## 文档结构

- **p1 架构图**：7 层 × 类/模块 × 函数，20 条有向箭头，可点击跳转
- **p2–p4**：项目全貌 / 静态库性质 / 固件集成
- **p5–p8**：四个接口逐段讲解（**左栏讲解 + 右栏对应源码**）
- **p9–p11**：滤波基础设施 / 系数组件 / 数据结构
- **p12**：⚠ 状态连续性（20 Hz 问题结论）
- **p13–p14**：signal_utils / 工具与验证链

## 修改源码后如何更新文档

1. 如果**行号变了**：更新 `gen_arch.py` 顶部 `CHECKS` 与各分段的 `(起始行, 结束行)` 元组
2. 如果**新增函数**：在对应 `P['pN']` 里加 `seg(路径, 起, 止, 标题, 讲解HTML)`
3. 跑 `./build.sh` —— 锚点校验会告诉你哪个行号失效了

### 分段写法

```python
seg('src/noise_processor.cpp', 155, 170, 'Phase 2 · A/C 计权',
    '<p>左栏讲解...</p><p class="key">关键点</p><p class="risk">风险点</p>',
    tag='03')          # tag 是段号徽标
```

讲解里可用的提示样式：

| class | 含义 |
|---|---|
| `risk` | 红色 · 坑 / 危险 |
| `key` | 绿色 · 关键点 |
| `tip` | 蓝色 · 提示 |

## 约束

- **不要手工编辑 `ARCHITECTURE.html`** —— 下次 `build.sh` 会覆盖
- 代码块一律通过 `code()` / `seg()` / `grab()` 生成，**禁止手抄源码**
- `grab(路径, anchor)` 用花括号配平自动截取整个结构体/类，适合不固定行号的声明块
