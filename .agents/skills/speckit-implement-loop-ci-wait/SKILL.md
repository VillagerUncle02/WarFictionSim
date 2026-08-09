---
name: speckit-implement-loop-ci-wait
description: Push the current branch and wait for GitHub Actions CI feedback
compatibility: Requires spec-kit project structure with .specify/ directory
metadata:
  author: github-spec-kit
  source: implement-loop:commands/ci-wait.md
---

# Push & Wait CI（推送并等待 CI 反馈）

## User Input

```text
$ARGUMENTS
```

可选参数：`--branch <name>`、`--timeout <秒>`。

## Steps

1. 运行 `.specify/extensions/implement-loop/scripts/powershell/load-config.ps1 -Json` 解析配置，取得 `BRANCH_BASE`、`CI_WORKFLOW_NAME`、`CI_WAIT_TIMEOUT_SECONDS`、`WAIT_CI_SCRIPT`；
2. 确认工作区干净（已跟踪改动先提交）；提交信息用 Conventional Commits；
3. 推送当前分支（网络命令按运行环境要求沙箱外执行）：

```powershell
git push -u origin <branch>
```

4. 等待 CI：

```powershell
pwsh -File <WAIT_CI_SCRIPT> -Branch <branch> -WorkflowName <CI_WORKFLOW_NAME> -TimeoutSeconds <timeout>
```

5. 结果处理：
   - exit 0 → 汇报通过，可继续开 PR；
   - exit 1 → 把失败日志转给实现 agent 修复，修复后重新推送并等待；
   - exit 2 → 检查 workflow push 触发条件（paths 过滤/仓库 Actions 状态），必要时 `gh workflow run <CI_WORKFLOW_FILE> --ref <branch>` 手动触发；
   - exit 3 → gh 认证/网络问题，停下向用户报告。