---
description: AI-review an open PR against spec/plan/tasks/constitution and backfill the review comment
scripts:
  ps: scripts/powershell/load-config.ps1 -Json
---

# AI PR Review（AI 审查 PR，不批准不合并）

## User Input

```text
$ARGUMENTS
```

支持 `--pr <编号>`（缺省自动查找当前分支的 open PR）。

## Steps

1. 运行 `{SCRIPT}` 解析配置，取得 `REVIEWER`、`DEVOPS_OPINION`、`REVIEWS_DIR`、`LANGUAGE`、`FEATURE_DIR` 等；
2. 确定 PR：`gh pr view <pr> --repo <owner/repo>`；缺省时 `gh pr list --head <branch> --state open` 查找；
3. 审查对象：整个 PR diff（`gh pr diff <pr>`）+ 与 `FEATURE_DIR` 下 spec/plan/tasks 一致性 + 宪法合规（`.specify/memory/constitution.md`）+ 门禁与 CI 证据（`gh pr checks <pr>`）+ PR 说明完整性；
4. 执行者：`<REVIEWER>` agent 输出 PASS/FAIL + findings（级别、file:line、修复方向），使用配置语言；CI/流水线类 PR 可请 `<DEVOPS_OPINION>` 出具交叉意见（仅意见）；
5. 写审计文件：`<REVIEWS_DIR>/<branch>-pr-review.md`（改动范围、关键决策、门禁/CI 证据、审查结论、遗留 TODO）；
6. 回填 PR Comment：

```powershell
gh pr comment <pr> --repo <owner/repo> --body-file <REVIEWS_DIR>/<branch>-pr-review.md
```

7. **AI 绝不点 Approve、绝不 merge**；FAIL 的 findings 转给实现 agent 修复（同分支）后重审，直到 PASS。
