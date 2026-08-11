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

1. 运行 `.specify/extensions/implement-loop/scripts/powershell/load-config.ps1 -Json` 解析配置，取得 `GATES_SCRIPT`、`GATES_TEMPLATE`；
2. **门禁脚本解析**：
   - `GATES_SCRIPT` 非空 → 直接使用；
   - 为空 → 检查项目实际使用的语言/工具链（`CMakeLists.txt`、`*.sln`、`Cargo.toml`、`package.json`、`pyproject.toml`、`go.mod`、`pom.xml`、`build.gradle(.kts)`、`Makefile` 等），参照 `GATES_TEMPLATE` 样板**现场编写 `<repo>/scripts/gates.ps1`**，**先给用户确认**再继续（编写结果随功能分支提交）；
3. 若用户传 `--quick`：

```powershell
pwsh -File <GATES_SCRIPT> -Quick
```

否则：

```powershell
pwsh -File <GATES_SCRIPT>
```

4. **运行时自适应**：门禁脚本没覆盖的命令（如 Makefile、Bun、Zig 等），由你（AI）检查项目后自行运行对应的测试/构建/格式命令作为门禁的一部分，并把补充命令记入审计记录；不要因为没有门禁脚本就跳过门禁；
5. 门禁失败 → 报告失败步骤与修复方向，不静默通过；
6. 门禁通过 → 简要汇报"构建/测试/格式"各项结果（含 AI 补充的命令）。