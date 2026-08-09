---
name: speckit-implement-loop-gates
description: Run project gates (full by default, or --quick)
compatibility: Requires spec-kit project structure with .specify/ directory
metadata:
  author: github-spec-kit
  source: implement-loop:commands/gates.md
---

# Run Gates（运行门禁）

## User Input

```text
$ARGUMENTS
```

支持 `--quick`（仅构建 + 测试，跳过格式与重新配置）。

## Steps

1. 运行 `.specify/extensions/implement-loop/scripts/powershell/load-config.ps1 -Json` 解析配置，取得 `GATES_SCRIPT`；
2. 若用户传 `--quick`：

```powershell
pwsh -File <GATES_SCRIPT> -Quick
```

否则：

```powershell
pwsh -File <GATES_SCRIPT>
```

3. 门禁失败 → 报告失败步骤与修复方向，不静默通过；
4. 门禁通过 → 简要汇报"构建/测试/格式"各项结果。