---
name: speckit-implement-loop-run
description: Run the full implementation loop (branch -> tasks -> gates -> AI review/fix
  -> commit -> CI -> PR -> AI PR review -> next feature) with human gates preserved
compatibility: Requires spec-kit project structure with .specify/ directory
metadata:
  author: github-spec-kit
  source: implement-loop:commands/run.md
---

# Implement Loop（实现循环自动化 · 通用版）

## User Input

```text
$ARGUMENTS
```

你必须考虑用户输入（非空时）。支持以下可选参数：

- `--feature <目录>`：覆盖 feature 目录（相对仓库根或绝对路径）；
- `--branch <分支名>`：覆盖分支名（默认 `<branch.prefix><feature 目录名>`）；
- `--phase <Phase 名>` / `--tasks <T001-T050>`：只执行指定阶段/任务范围（用于中断后续跑）；
- `--skip-issues`：临时跳过 issue 映射前置检查；
- `--language en`：本次交互/记录改用英文；
- `--no-chained`：本次不用链式分支。

## 目标与边界

自动化以下环节：创建/检出功能分支 → 按 agent 分配逐阶段执行任务 → 门禁 → AI 审查/修复循环 → Conventional Commits → 推送触发 GitHub Actions CI → CI 反馈/修复循环 → 创建 PR → AI PR 审查（结论回填，不批准不合并）→ 通过后自动进入下一功能。

**不在本命令范围**：任务生成（`speckit.tasks`）、issue 创建（`speckit.taskstoissues`）、agent 分配与确认（`speckit.agent-assign.assign`）、PR Approve 与 Merge——以上由用户人工完成。假设 `tasks.md` 与（如配置开启）`agent-assignments.yml` 已存在且经用户确认。

**语言**：与实现/审查 agent 的对话、提示词、审计记录、门禁输出、PR 说明、汇报**全部使用配置语言**（默认中文 `zh-CN`，配置项 `language`），方便人工审查。

## 运行时自适应（AI 补全原则）

配置与脚本只负责"能确定的事"；**凡是无法由代码确定的项目差异，由当前运行本命令的 AI 检查项目后自行补全**，并把决定写入审计记录：

- **门禁**：`<GATES_SCRIPT>` 为空时，AI 检查项目实际使用的语言/工具（如 `go.mod`、`pom.xml`、`build.gradle`、`Makefile`、`package.json`、`pyproject.toml`、`Cargo.toml` 等），参照 `<GATES_TEMPLATE>`（样板）**现场编写 `<repo>/scripts/gates.ps1`**，经用户确认后执行并提交；已有门禁脚本但未覆盖某些命令时，**AI 自行运行对应的测试/构建/格式命令**作为补充（如 `go test ./...`、`make test`、`bun test`），并记录到审计记录；
- **agent 角色**：配置的角色为空、在当前平台不存在或 spawn 失败时，AI 扫描可用 agent（`.claude/agents/`、平台内置角色）后选用最接近的角色，或降级为 `default`，并在汇报中说明；无法确定时询问用户；
- **其它项目事实**：凡配置缺失且无法自动探测的，AI 基于项目现状作出合理决定，在汇报中说明；重要且不确定的（如默认分支、CI 触发方式）先询问用户再继续。

## 第 0 步：加载配置

在仓库根目录运行：

```powershell
.specify/extensions/implement-loop/scripts/powershell/load-config.ps1 -Json
```

其中 `.specify/extensions/implement-loop/scripts/powershell/load-config.ps1 -Json` 是注册后的脚本路径（通常为 `.specify/extensions/implement-loop/scripts/powershell/load-config.ps1`，`specify extension add` 后自动改写）。若用户传了 `--feature <dir>`，改为运行 `.specify/extensions/implement-loop/scripts/powershell/load-config.ps1 -Json -Feature "<dir>"`（等价于命令内联的 `-Feature` 参数）。解析输出 JSON，得到以下关键字段（全部为绝对路径或已解析值）：

- `REPO_ROOT` / `EXT_DIR` / `FEATURE_DIR` / `FEATURE_NAME`；
- `SPEC_FILE` / `PLAN_FILE` / `TASKS_FILE` / `DATA_MODEL_FILE` / `CONTRACTS_DIR` / `RESEARCH_FILE` / `QUICKSTART_FILE`（可能不存在，逐个用文件系统确认）；
- `ASSIGNMENTS_FILE` / `ASSIGNMENTS_PATH`（可能不存在，见第 4 步）；
- `LANGUAGE` / `PARALLEL` / `DEVOPS_AGENT`；
- `BRANCH_PREFIX` / `BRANCH_BASE` / `CHAINED`；
- `CI_WORKFLOW_FILE` / `CI_WORKFLOW_NAME` / `CI_WAIT_TIMEOUT_SECONDS` / `CI_REQUIRE_PUSH_TRIGGER`；
- `GATES_SCRIPT`（可能为空：配置 `gates.script` > 项目 `<repo>/scripts/gates.ps1`；为空时按"运行时自适应"现场编写）与 `GATES_TEMPLATE`（样板路径）；
- `OPEN_PR_SCRIPT` / `WAIT_CI_SCRIPT`（已解析：项目自定义优先，否则扩展自带）；
- `CHECK_ISSUES_SCRIPT` / `SYNC_PR_CLOSES_SCRIPT` / `PREPARE_BRANCH_SCRIPT` / `MERGE_REBASE_SCRIPT` / `CHECK_PR_ORDER_SCRIPT`（扩展自带辅助脚本绝对路径）；
- `REVIEWS_DIR` / `REVIEWER` / `DEVOPS_OPINION` / `MAX_ROUNDS` / `CONVERGENCE_WARN_ROUNDS` / `SECOND_OPINION` / `SECOND_OPINION_TRIGGERS`；
- `REQUIRE_ISSUES` / `ISSUE_TITLE_PATTERN` / `PR_BODY_TEMPLATE`；
- `CURRENT_BRANCH`。

将上述值作为本命令全部后续步骤的唯一事实来源；不要硬编码任何路径。所有路径在引用前用 `Test-Path` 确认存在。

**自动探测字段**（未在配置/环境变量中显式设置时）：`BRANCH_BASE` 从 `origin/HEAD` 探测（探测不到则为空，由你检查仓库后确定）；`CI_WORKFLOW_FILE` / `CI_WORKFLOW_NAME` 从 `.github/workflows/` 探测（优先 `ci.yml`，名称读 `name:` 字段；探测不到则为空，由你读取 workflow 确定）；`REVIEWER` / `DEVOPS_AGENT` / `DEVOPS_OPINION` **不做代码猜测**（未配置即为空），由你在第 4 步按"运行时自适应"解析。**不要假设目标项目与作者项目使用相同的宪法、角色或流程。**

## 第 1 步：前置检查

1. `FEATURE_DIR` 存在且包含 `tasks.md`（缺失 → 停下，提示先运行 `speckit.tasks`）；
2. **issue 映射校验**（`REQUIRE_ISSUES=true` 且未传 `--skip-issues`）：运行

```powershell
pwsh -File <CHECK_ISSUES_SCRIPT> -TasksFile <TASKS_FILE> -TitlePattern "<ISSUE_TITLE_PATTERN>"
```

   存在缺失 → 停下，提示用户先运行 `speckit.taskstoissues`；网络/认证错误（退出码 2）→ 确认在可访问 gh 的环境执行；
3. 工作区干净（`git status --porcelain --untracked-files=no` 为空；未跟踪文件不阻塞但需提示）；有已跟踪改动 → 停下询问用户处理方式；
4. `git fetch origin <BRANCH_BASE>`（`BRANCH_BASE` 为空时先确定：用 `git remote show origin` 或查看现有 PR 的 base，通常为 `main`/`master`；不确定则询问用户后再继续）；
5. 确认 `gh` 已认证（`gh auth status`）。git fetch/push、gh 等网络命令如受运行环境沙箱限制，需在沙箱外执行或先向用户请求授权；
6. **CI 触发检查**（`CI_REQUIRE_PUSH_TRIGGER=true`）：`CI_WORKFLOW_FILE` 为空时先列出 `.github/workflows/`，读取各 workflow 确定本项目用哪个（参考项目宪法/README/既有 PR 的 checks）；确认该 workflow 的 `on.push` 覆盖功能分支（不是仅 main）。若 `on.push` 带 `paths` 过滤，记录下来——纯文档改动可能不触发 CI，等待脚本超时（退出码 2）时按此判断；
7. **workflow 位于默认分支检查**：`git ls-tree origin/<BRANCH_BASE> --name-only .github/workflows/`，确认 `<CI_WORKFLOW_FILE>` 是否已存在于默认分支：
   - 存在 → 第 8 步走"推送触发 CI"；
   - 不存在（新仓库首个 feature 常见，`gh workflow run` 会 404）→ 记录"workflow 未合入默认分支"，第 8 步自动降级为"先开 PR、用 pull_request 触发 CI"（见第 8 步降级路径）；
8. 加载上下文文件（见第 2 步），并向用户做一次简短中文/配置语言汇报："准备在 <branch> 上执行 <TASKS_FILE>，范围 <阶段>，等待你的确认后开始"。**如果用户尚未确认 agent 分配，先停下等待确认**。

## 第 2 步：加载上下文

读取 feature 目录下**实际存在**的制品：

- `plan.md`（技术栈、架构、目录结构）——必读；
- `tasks.md`（任务清单）——必读；
- `spec.md`（需求/用户故事）——存在则读；
- `data-model.md`、`contracts/`（全部契约文件）、`research.md`、`quickstart.md`——存在则读；
- `.specify/memory/constitution.md`（项目宪法）——存在则读，作为不可妥协约束；
- `ASSIGNMENTS_PATH`——存在则读（agent 分配）。

## 第 3 步：分支

运行：

```powershell
pwsh -File <PREPARE_BRANCH_SCRIPT> -Branch <分支名> -Base <BRANCH_BASE> [-Chained -Tip <上一分支 tip>]
```

规则：

- 分支名默认 `<BRANCH_PREFIX><FEATURE_NAME>`（如 `feature/us1-mvp`），可被 `--branch` 或用户指定覆盖；
- 链式（`CHAINED=true`，默认）：下一分支基于上一分支 tip 创建（`git checkout -b <next> <current-tip>`），天然包含前序改动；
- **单活动分支**：同一时间只有 1 个分支在实现，其余为"AI 审查通过、等人工合并"的 PR；
- 禁止直接推送 `BRANCH_BASE`（宪法类约束，默认 main）；
- 同名分支已存在 → 检出并报告与基线的差异，不自动合并/重置（避免破坏已有工作）；
- 若上一分支 PR 已合并 → 分支基点使用 `origin/<BRANCH_BASE>`；若未合并 → 使用上一分支 tip（链式）。

## 第 4 步：任务执行

按 `tasks.md` 的 Phase 顺序执行（Setup → Foundational → User Story… → Polish；若传了 `--phase` / `--tasks` 则只执行指定范围）。对每个任务：

- 读取 `ASSIGNMENTS_PATH` 中该任务的 agent（不存在分配文件 → 全部按 `default` 并在第一次时提示用户"未找到 agent 分配，全部由当前上下文直接实现"）；
- **命名 agent**（以 `ASSIGNMENTS_PATH` 中的角色名为准，不要假设固定角色清单）：以该角色 spawn 执行，**配置语言提示词**必须包含：任务 ID、完整描述、相关契约/数据模型引用、精确文件路径、依赖上下文（前一任务产物）；spawn 前确认该角色在当前平台存在；不存在或 spawn 失败时，按"运行时自适应"选择最接近的可用角色或降级 `default`，并在汇报中说明；无法确定时询问用户；
- **`default`**：在当前上下文内直接实现；
- **`DEVOPS_AGENT` 类任务**（CI/流水线/构建/依赖锁定等）：`DEVOPS_AGENT` 非空且可用时统一交给该角色 subagent 执行；为空或角色不可用时，由你（AI）选择当前平台最接近的 DevOps 角色，或按普通分配/`default` 执行，并在汇报中提示；
- **测试先行（本工作流固定规则，不可关闭）**：测试任务先写并确认 FAIL（RED），再实现。这是扩展自身的工作流要求，不依赖任何项目的宪法条款；**任何项目都按此执行**；
- 同文件任务串行；不同文件且标 `[P]` 的可并行（`PARALLEL=true`）；
- 完成后在 `tasks.md` 将该任务标记为 `[X]`，用配置语言汇报进度；
- 任务失败 → 该阶段暂停，收集错误上下文后用中文汇报，不静默跳过。

## 第 5 步：门禁

- **门禁脚本解析**：`<GATES_SCRIPT>` 非空时直接使用（配置 `gates.script` > 项目 `<repo>/scripts/gates.ps1`）；为空时按"运行时自适应"原则，检查项目实际使用的语言/工具链，参照 `<GATES_TEMPLATE>` 样板**现场编写 `<repo>/scripts/gates.ps1`**，**先给用户确认再执行**，编写结果随功能分支提交（之后即为该项目门禁）；
- **固定**：每个 Phase 结束跑全量 `<GATES_SCRIPT>`；PR 创建前跑全量 `<GATES_SCRIPT>`；
- **按需**（`GATES_QUICK_ON_DEMAND=true`）：AI 判断该跑了就跑（改动涉及测试、跨语言边界、高风险代码、审查修复后）→ `<GATES_SCRIPT> -Quick`；
- 门禁失败必须修复后再继续；无法修复时停下用配置语言汇报；
- **未知语言/工具不设限**：门禁脚本没覆盖的命令（如 Makefile、Bun、Zig 等），按"运行时自适应"原则由 AI 检查项目后自行补充执行，并把补充命令记入审计记录；不要因为没有门禁脚本就跳过门禁；
- 本地门禁只是前置，最终门禁是 GitHub Actions CI（第 8 步）。

## 第 6 步：AI 审查 ↔ 修复循环（每个逻辑组）

```text
实现 agent 完成逻辑组
   ↓
① <REVIEWER> agent 审查 diff（配置语言输出；先读 <REVIEWS_DIR>/ 本分支最近一条记录，
  带着"已修复/回归"清单进入）
   ↓
② 主循环 agent 核实：打开每条 🔴/🟡 的 file:line 对照代码，
  分成【已确认问题】与【未验证猜测】（💭 nit 不阻塞，记录即可）
   ↓
③ 回传修复：把【已确认问题清单 + 相关文件路径 + 审查记录】重新派给实现该任务的
  实现 agent（同一角色）修复——修复由实现 agent 做，审查 agent 不碰代码，主循环不代写
   ↓
④ 实现 agent 修复 → <GATES_SCRIPT> -Quick（编译 + 相关测试不回归）
   ↓
⑤ 下一轮：<REVIEWER> 重新审查（读上一轮记录，核对已修复 + 查回归/新问题）
   ↓
⑥ 循环 ③–⑤ 直到无 🔴/🟡 → 逻辑组通过 → 提交（第 7 步）
```

职责边界：

- **审查 agent**：只发现问题、给建议，不修改代码；
- **实现 agent**：只修复自己实现的问题，不扩大改动范围；
- **主循环 agent（orchestrator）**：核实 findings、调度回传、跑门禁、写审计记录，不代写修复。

若 `<REVIEWER>` 为空、在当前平台不存在或 spawn 失败：由主循环 agent 在当前上下文执行审查（降级模式），在审计记录中标注"降级审查（无专职 REVIEWER 角色）"，并提示用户配置 `review.code_reviewer`。

附加规则：

- **收敛检测**：连续 `CONVERGENCE_WARN_ROUNDS`（默认 4）轮 🔴/🟡 数量不下降或同类问题重复 → 审计记录标注"收敛异常提醒"，建议换更高推理档位 / 缩小 diff 范围 / 人工介入；
- **轮次提醒**：超过 `MAX_ROUNDS`（默认 5）轮 → 审计记录标注"轮次过多提醒"，继续直到通过；
- **第二意见**（`SECOND_OPINION=true` 且变更命中 `SECOND_OPINION_TRIGGERS` 任一关键词，如确定性核心、并发、跨语言边界、安全）：用独立只读交叉审查（如 `codex exec review --sandbox read-only`）复核，结果并入审计记录；日常变更不启用；
- **审计记录**：每轮写入 `<REVIEWS_DIR>/<branch>-r<N>.md`（配置语言，模板见下），随逻辑组一起提交入库。

### 审计记录模板

```markdown
# Review <branch> - 第 N 轮
- 审查范围：Txxx–Tyyy（commit 范围）
- 对比上一轮：已修复 M / 回归 0 / 新增 K / 无变化
- Findings：
  | # | 级别 | file:line | 问题 | 修复方向 | 状态 |
- 未验证猜测：<清单>
- 运行时自适应：<补充的门禁命令 / 角色替代 / 其它决定>
- 整体结论：patch is correct/incorrect（置信度 0.xx）
- 收敛检测：正常 / 收敛异常提醒
- 轮次提醒：正常 / 已超 N 轮
```

## 第 7 步：提交

- 每个逻辑组用 Conventional Commits 提交（`feat:` / `fix:` / `test:` / `docs:` / `refactor:` / `chore:`），提交信息说明变更原因；
- `<REVIEWS_DIR>/` 审计记录随对应逻辑组一起提交；
- 若当前分支已有 open PR：运行 `<SYNC_PR_CLOSES_SCRIPT> -TasksFile <TASKS_FILE>`，保持 PR 正文 `Closes #` 与已完成任务同步（防止合并时漏关 issue）；
- 提交信息使用配置语言或英文均可，但必须结构清晰。

## 第 8 步：推送与 CI 反馈循环（PR 之前）

**目的**：PR 之前先推送分支触发 GitHub Actions CI，结合 CI 反馈与 AI 审查修复问题，直到 CI 无异常再开 PR。

**降级路径（workflow 未合入默认分支，由第 1.7 步判定）**：

- 跳过下面的"推送→等 push CI"，直接进入第 9 步开 PR；
- 开 PR 后立即用 `<WAIT_CI_SCRIPT>` 等待 pull_request 触发的 CI（同一脚本按"分支 + 当前 HEAD"匹配，无需指定 workflow 名称）；
- CI 失败 → 修复 → 推送（推送会更新 PR 并重新触发 PR CI）→ 回到等待；
- CI 绿 → 进入第 10 步 AI PR 审查；
- 审计记录标注"降级：workflow 未合入默认分支，改用 PR 触发 CI"。

1. 推送当前分支（网络命令按运行环境要求沙箱外执行）：

```powershell
git push -u origin <branch>
```

2. 等待并获取 CI 反馈：

```powershell
pwsh -File <WAIT_CI_SCRIPT> -Branch <branch> -WorkflowName <CI_WORKFLOW_NAME> -TimeoutSeconds <CI_WAIT_TIMEOUT_SECONDS>
```

   - `CI_WORKFLOW_NAME` 为空时不传 `-WorkflowName`（脚本按"分支 + 当前 HEAD"匹配任意 workflow）；
   - 脚本轮询该分支最新 CI run 直至完成（成功 exit 0；失败 exit 1；超时/未触发 exit 2；认证/网络 exit 3）；
   - 成功（success）→ 进入第 9 步；
   - 失败/取消 → 输出失败 job 与失败步骤日志（`gh run view <id> --log-failed`），进入修复循环；
   - 超时/未触发 → 检查 workflow 的 push 触发条件与仓库 Actions 状态；必要时 `gh workflow run <CI_WORKFLOW_FILE> --ref <branch>` 手动触发，仍异常则停下汇报；
   - 外部调用 shell 的超时时间必须 ≥ `-TimeoutSeconds + 60s`，避免外层先杀进程。

3. 修复循环（CI 反馈 + AI 审查结合）：

   - 主循环 agent 先核实 CI 失败是否由本次改动引起（对照改动范围与失败步骤，区分真实 bug 与环境问题）；
   - 把 CI 失败信息 + `<REVIEWER>` 的 findings 一起交给对应实现 agent（同一角色）修复；
   - 实现 agent 修复 → `<GATES_SCRIPT> -Quick` → 提交 → `git push origin <branch>` → 回到第 2 步；
   - 循环直到 CI 全部通过且 AI 审查无 🔴/🟡。

4. 收敛与轮次提醒（同第 6 步规则）：连续 `CONVERGENCE_WARN_ROUNDS` 轮 CI 失败未下降 → "CI 收敛异常提醒"；超过 `MAX_ROUNDS` 轮 → "CI 轮次过多提醒"。

5. 审计记录：每轮 CI 结果写入 `<REVIEWS_DIR>/<branch>-ci.md`（时间、run id、结论、失败步骤、修复 commit 列表），随逻辑组提交入库。

## 第 9 步：开 PR（CI 通过后）

- **前提**：本地门禁通过 且 CI 反馈无异常；

```powershell
pwsh -File <OPEN_PR_SCRIPT> -Title "<feat: 说明>" -TasksFile <TASKS_FILE> -Base <BRANCH_BASE> -Language <LANGUAGE>
```

- PR 标题与说明使用配置语言（默认中文）；
- **Closes 自动收集**：调用时传 `-TasksFile <TASKS_FILE>`，脚本自动收集 tasks.md 中所有 `[X]` 任务、按 issue 标题映射真实 issue 号，生成完整 `Closes #` 列表（不需要手传 `-Issues`）；映射不到的任务会警告列出（不阻塞，可人工补）；自定义正文可用 `-BodyFile` 或配置 `PR_BODY_TEMPLATE`；
- 若当前分支已存在 open PR（例如降级路径已先开 PR）：跳过创建，改用 `<SYNC_PR_CLOSES_SCRIPT> -TasksFile <TASKS_FILE>` 更新正文即可；
- 若仓库启用了链式顺序检查，可在 CI 中调用 `check-pr-order.ps1` 保证合并顺序（见扩展 README）。

## 第 10 步：AI PR 审查（开 PR 后，不批准合并）

- **审查对象**：整个 PR diff + 与 spec/plan/tasks 一致性 + 宪法合规（确定性/AI 边界/数据驱动/无头可测/代码风格等，按项目宪法）+ 门禁与 CI 证据 + PR 说明完整性；
- **审查前**：先运行 `<SYNC_PR_CLOSES_SCRIPT> -TasksFile <TASKS_FILE>`，确保正文 `Closes #` 覆盖全部已完成任务；
- **执行者**：`<REVIEWER>`（CI/流水线类 PR 可请 `<DEVOPS_OPINION>` 出具交叉意见，仅意见不并入流程）；
- **结论**：PASS / FAIL + findings（级别、file:line、修复方向）；
- **审计文件**：`<REVIEWS_DIR>/<branch>-pr-review.md`（改动范围、关键决策、门禁/CI 证据、审查结论、遗留 TODO）；
- **PR Comment 回填**：`gh pr comment <pr> --body-file <REVIEWS_DIR>/<branch>-pr-review.md`（可选对 findings 用行内 review comment 定位 file:line），人工远程打开 GitHub 即可审查；**AI 绝不点 Approve、绝不 merge**；
- **FAIL** → findings 交回实现 agent，同分支新增提交修复 → 推送 → CI → 重新 PR 审查，直到 PASS（沿用收敛/轮次提醒）；
- **"回退"语义**：默认 Fix-in-place（同分支继续修）；仅方向性错误才 Rebuild（丢弃分支、从稳定基点重建），Rebuild 需人工确认。

## 第 11 步：停止点与下一功能（人工闸门）

- **每个 PR 仍等待人工 Approve + Merge**；AI 审查与 CI 只提供证据，不替代人工；
- **绝不自动 merge、绝不直接推送 `BRANCH_BASE`**；
- **AI PR 审查 PASS 且 CI 绿后**：记录审查（`<REVIEWS_DIR>/<branch>-pr-review.md` + PR Comment），然后**自动开始下一功能**：
  - 确定下一功能（用户/分配清单中下一个未开始的 Phase 或用户故事；若没有明确顺序则向用户确认）；
  - 创建链式分支（`CHAINED=true` 时基于当前分支 tip）并回到第 0 步；
- 前序 PR 被人工合并后，对后续分支运行：

```powershell
pwsh -File <MERGE_REBASE_SCRIPT> -Branch <下一分支>
```

## 汇报模板

```text
## 实现循环完成
- 分支：<branch>
- PR：<url>
- 完成任务：Txxx–Tyyy（N 条，[X] 已标记）
- 门禁：通过（构建/测试/格式）
- CI：通过（N 轮，失败 M 次已修复）或"未触发"说明
- AI 审查：N 轮，🔴/🟡 问题 M 个（已修复 / 待确认）
- PR Closes 同步：已覆盖 N 个任务 issue（最新）
- PR 审查：PASS/FAIL（N 轮），PR Comment：<url>
- 审计记录：<REVIEWS_DIR>/<branch>-r*.md、<branch>-ci.md、<branch>-pr-review.md
- 下一步：等待人工 Approve + Merge（AI 审查已通过，可继续下一功能）
```