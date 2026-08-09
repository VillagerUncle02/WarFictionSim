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

3. 若使用扩展自带通用门禁（而非项目 `.specify/extensions/implement-loop/scripts/gates.ps1`），配置 `gates.steps` 的自定义命令会自动并入，无需手动传参；
4. **运行时自适应**：先检查项目实际使用的语言/工具（`go.mod`、`pom.xml`、`build.gradle`、`Makefile`、`package.json`、`pyproject.toml`、`Cargo.toml` 等）；通用门禁未覆盖的工具链，由你（AI）自行运行对应的测试/构建/格式命令作为门禁的一部分，并把补充命令记入审计记录；不要因为没有配置就跳过门禁；
5. 门禁失败 → 报告失败步骤与修复方向，不静默通过；
6. 门禁通过 → 简要汇报"构建/测试/格式"各项结果（含 AI 补充的命令）。