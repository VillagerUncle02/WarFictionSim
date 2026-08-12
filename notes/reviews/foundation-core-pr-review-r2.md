# Review feature/foundation-core - 第 2 轮（PR #106，head 33cd292 → 16f5a6e）

- 审查范围：PR #106（T008–T011），head `33cd292` → `16f5a6e`（N1 修复后）
- 结论：**PASS（无 🔴）**；第 1 轮 F1–F6 全部核对完毕；新增 N1（🟡，已修复）、
  N2/N3（💭，记录在案）。

## 第 1 轮逐项核对

| 项 | 级别 | 结论 |
|---|---|---|
| F1 RNG 双实现 | 🟡 | 已修复：`Rng` 持有 `detail::Pcg32Random`，`next()/state()/restore()/reset()` 与 vendor `pcg32_random_r/pcg32_srandom_r` 逐行等价，无第二份 PCG 常量/核心；44/44 测试通过 |
| F2 tasks.md 路径 | 💭 | 已修复（T008–T011 补 `native/` 前缀）；同源遗留见 N2 |
| F3 sync-pr-closes 注释 | 💭 | 已修复，注释与差集实现一致 |
| F4 PR 正文 | 💭 | 已修复：任务列表 T008–T011，Closes #8–#11 |
| F5/F6 历史记录/sim_core | 💭 | 记录在案 |

## 新增 Findings

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| N1 | 🟡 | .github/workflows/ci.yml:168 | Windows 下 clang-tidy 头文件诊断路径为混合分隔符（前缀反斜杠 + include 相对正斜杠），上一版 filter 仍匹配不到项目头文件 | `[regex]::Escape($repoRoot) + '[\\/]native[\\/](sim\|core_c)[\\/]'` 分隔符容忍写法 | 已修复（16f5a6e），本地实测可显示 pcg32.hpp 诊断 |
| N2 | 💭 | specs/.../tasks.md | Path Conventions 及后续任务行仍用旧顶级布局路径 | 后续 PR 全量扫一遍 tasks.md 路径并更新 Path Conventions | 记录在案（后续任务分支处理） |
| N3 | 💭 | .github/workflows/ci.yml:166 | 注释表述不准（filter 控制诊断输出而非分析范围） | 已随 N1 一并修正措辞 | 已修复 |

## CI 证据

- run `31610253729`（head 16f5a6e）success；44/44 测试通过；lint 步骤无海量
  warning 计数（重定向生效）。
- run `31608914920`（head 33cd292）success，日志干净。

## 未验证猜测（不阻塞）

1. CI 精确 clang-tidy 版本未直接确认（VS LLVM 优先于独立 LLVM），N1 结论基于
   LLVM 22.1.2 本地实证；建议合并前看一次 CI 日志确认无回归。
2. gtest 静态 triplet、跨 libm 位级一致性等第 1 轮遗留项仍在，超出本 PR 范围。
