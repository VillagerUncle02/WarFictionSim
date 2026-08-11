---
description: Create the next chained feature branch after the current PR passes AI review
scripts:
  ps: scripts/powershell/load-config.ps1 -Json
---

# Next Feature（创建下一个链式分支）

## User Input

```text
$ARGUMENTS
```

支持 `--feature <目录>`、`--branch <名称>`、`--no-chained`。

## Steps

1. 运行 `{SCRIPT}` 解析配置，取得 `FEATURE_DIR`、`BRANCH_PREFIX`、`BRANCH_BASE`、`CHAINED`、`PREPARE_BRANCH_SCRIPT` 等；
2. 前置确认：当前分支的 PR 已通过 AI 审查（`<REVIEWS_DIR>/<branch>-pr-review.md` 为 PASS）且 CI 绿；若未通过 → 停下，不创建下一分支；
3. 确定下一功能：若 `--feature`/`--branch` 未指定，按用户/分配清单中的下一个 Phase 或用户故事确定；
4. 创建分支：

```powershell
pwsh -File <PREPARE_BRANCH_SCRIPT> -Branch <下一分支> -Base <BRANCH_BASE> [-Chained -Tip <当前分支 tip>]
```

5. 汇报分支名与基点，提示可运行 `speckit.implement-loop.run` 继续实现。
