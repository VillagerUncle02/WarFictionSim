# Review feature/foundation-core - 重构后第 1 轮（PR #106）

- 审查范围：PR #106（T008–T011），head `a3a7e2c`（base main `1d5815c`）
- 背景：仓库完成 native/app 布局重组 + CMake/Ninja/clang-cl/vcpkg 工具链切换后，
  本 PR 已 rebase；旧审查记录（head `1737817`）为历史结论，本轮为全新审查。
- 审查方式：Code Reviewer agent（只读）；本地与远程 diff 一致（24 文件 / +1566 / −40）；
  CI 复核：pull_request + push 两个 run 均 success，44/44 测试通过；PR MERGEABLE。
- 结论：**PASS（无 🔴）**，含 1 个 🟡 已修复 + 5 个 💭（记录，其中 2 个已顺手处理）。

## Findings

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| F1 | 🟡 | native/sim/src/rng.cpp:46 vs native/sim/include/wfs/sim/detail/pcg32.hpp:47 | PCG-XSH-RR 核心与常量在 `Rng::next()` 中复制一份，与 vendor 头文件双实现，确定性核心存在静默漂移风险 | `Rng` 直接持有 `detail::Pcg32Random` 并调用唯一实现 | 已修复（本分支新增提交） |
| F2 | 💭 | specs/.../tasks.md:48-51 | T008–T011 行仍写旧布局路径 | 补 `native/` 前缀 | 已修复（同提交） |
| F3 | 💭 | scripts/sync-pr-closes.ps1:8 | 头注释仍写“收集所有 [X]”，实际已是差集逻辑 | 更新头注释 | 已修复（同提交） |
| F4 | 💭 | PR #106 正文 | “任务”列表含 T001–T007（Closes 块 #8–#11 正确） | 正文任务列表同步为 T008–T011 | 待人工/脚本更新正文 |
| F5 | 💭 | notes/reviews/foundation-core-pr-review.md | 旧记录固定旧 head 与旧 CI run | 本记录已注明历史结论，合并前保留即可 | 记录在案 |
| F6 | 💭 | native/sim/CMakeLists.txt | main 的 sim_core 条件块在 T012 合入后自动恢复（c_api.cpp 出现即激活） | 无需本 PR 处理 | 记录在案 |

## 未验证猜测（不阻塞）

1. gtest 静态 triplet 分支未实测（CI 覆盖默认 x64-windows 动态库路径）。
2. 数学库位级黄金值仅验证 windows-2022 + clang-cl + MSVC CRT；跨 libm 位级一致
   性不在契约内（math.h 已声明）。
3. CTest PATH 加固仅 Windows 分支生效（目标平台即 Windows）。

## 验证

- RNG 修复后：44/44 CTest 通过；PCG32 黄金序列（seed/stream 多种组合 + 上游
  参考 seed42/seq54 首值）与上游 pcg-basic 一致。
- 运行时自适应：本分支使用项目 `scripts/gates.ps1`（已适配 native/app 布局）。

## 整体结论

重构后本 PR 为纯路径迁移 + 既有内容再接线；F1 修复后无 🔴/🟡，可进入第 2 轮复审。
