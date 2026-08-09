# speckit-implement-loop

在 [spec-kit](https://github.com/github/spec-kit) 基础之上、可跨项目复用的**自动化实现循环**扩展：链式功能分支 → 按 agent 分配执行任务 → 门禁 → AI 审查/修复循环 → 提交 → 推送触发 GitHub Actions CI → CI 反馈修复循环 → 开 PR → AI PR 审查 → 通过后自动进入下一功能。

**人工闸门始终保留**：agent 分配确认、PR Approve、PR Merge 一律由人执行，AI 只产出证据与结论，绝不自动 Approve / Merge / 推送 main。

## 设计目标

- **换项目即用**：不硬编码 feature 目录、项目名、技术栈、分支名或 agent 名单。所有项目差异通过配置（`implement-loop-config.yml`）或自动探测解决。
- **代码定不了的交给 AI**：配置与脚本只负责能确定的事；语言/工具链、门禁脚本、角色替代等无法由代码确定的项目差异，由运行时使用本扩展的 AI 检查项目后自行补全（门禁脚本缺失时按样板现场编写、经用户确认），并把决定记入审计记录（详见 [run.md](commands/run.md) 的"运行时自适应"）。
- **建立在 speckit 制品之上**：读取 `specs/<feature>/` 下的 `spec.md / plan.md / tasks.md / data-model.md / contracts/ / research.md / quickstart.md` 与 `.specify/memory/constitution.md`，不重复生成制品。
- **与 speckit 生态联动**：`speckit.tasks` 生成任务、`speckit.taskstoissues` 生成 issue、`speckit.agent-assign.*` 生成 agent 分配，本扩展负责从"已确认的分配"到"可人工合并的 PR"这一段。

## 安装

```powershell
# 从本地源码目录安装（开发模式）
specify extension add --dev ./extensions/implement-loop

# 验证
specify extension list
```

安装后：

- 命令注册到 `.claude/commands/`（Claude 类）或对应 agent 的 skills 目录（Codex 等 skills 模式，如 `.agents/skills/speckit-implement-loop-*/`）；
- 扩展文件复制到 `.specify/extensions/implement-loop/`；
- 可选 hook：`after_taskstoissues`（创建 issue 后询问是否立即开始实现循环，可在 `.specify/extensions.yml` 中禁用）。

## 命令

| 命令 | 技能（Codex skills 模式） | 作用 |
|---|---|---|
| `speckit.implement-loop.run` | `$speckit-implement-loop-run` | 全流程主命令（见下方工作流） |
| `speckit.implement-loop.gates` | `$speckit-implement-loop-gates` | 单独跑门禁（`--quick` 快速模式） |
| `speckit.implement-loop.ci-wait` | `$speckit-implement-loop-ci-wait` | 推送当前分支并等待 CI 反馈 |
| `speckit.implement-loop.pr-review` | `$speckit-implement-loop-pr-review` | 对已开 PR 做 AI 审查并回填评论 |
| `speckit.implement-loop.next` | `$speckit-implement-loop-next` | 创建下一个链式功能分支 |

## 配置

复制 `config/implement-loop.config.template.yml` 到 `.specify/extensions/implement-loop/implement-loop-config.yml` 修改；本地覆盖用同目录 `implement-loop-config.local.yml`（不提交）。环境变量 `SPECKIT_IMPLEMENT_LOOP_<KEY>` 可覆盖任意键（如 `SPECKIT_IMPLEMENT_LOOP_LANGUAGE=en`）。

常用键：

| 键 | 默认值 | 说明 |
|---|---|---|
| `language` | `zh-CN` | 交互/审计/PR 说明语言 |
| `feature.directory` | 自动 | 留空读取 `.specify/feature.json`，否则扫描 `specs/*/tasks.md` |
| `execution.assignments_file` | `agent-assignments.yml` | agent 分配文件；不存在则全部按 `default` 执行并提示 |
| `execution.devops_agent` | 空 | DevOps 类任务角色；留空由 AI 运行时解析（分配文件/可用角色），不预设 |
| `branch.prefix` / `branch.base` / `branch.chained` | `feature/` / 自动 / `true` | 分支前缀；基线从 `origin/HEAD` 探测（失败回退 `main`）；链式策略 |
| `ci.workflow_file` / `ci.workflow_name` | 自动 / 自动 | 优先 `ci.yml` 其次唯一 workflow；名称读 `name:` 字段，回退 `CI` |
| `gates.script` | 自动 | 门禁脚本：配置 > 项目 `<repo>/scripts/gates.ps1`；都没有时由 AI 参照 `templates/gates-template.ps1` 现场编写并经用户确认 |
| `notes.reviews_dir` | `notes/reviews` | 审计记录目录 |
| `review.code_reviewer` | 空 | AI 审查角色；留空由 AI 运行时解析（分配文件/可用角色），不预设 |
| `review.devops_opinion` | 空 | CI 类 PR 交叉意见角色；留空由 AI 运行时解析，不预设 |
| `github.require_issues` | `true`* | 前置检查任务 ID ↔ issue 映射（作者项目准则：任务先转 issue） |

\* 表示该默认值取自作者项目（WarFictionSim）的工作流准则。换项目时可在配置中显式覆盖；未配置的事项由 AI 在运行时解析，重要决定会询问用户并记入审计记录。

### 跨项目自动探测

以下值在未显式配置时自动探测，避免把作者项目的宪法/流程当作隐性前提：

- `branch.base`：`git symbolic-ref refs/remotes/origin/HEAD`（无网络）；探测不到则为空，由 AI 检查仓库后确定；
- `ci.workflow_file` / `ci.workflow_name`：扫描 `.github/workflows/`，优先 `ci.yml`，其次唯一 workflow；名称读 `name:` 字段；
- 角色类键不做代码猜测：未配置即为空，由 AI 在运行时解析。

## 工作流（`run` 主命令）

1. **加载配置**：`load-config.ps1` 解析 feature 目录、脚本路径、审查角色等，输出 JSON。
2. **前置检查**：feature 制品存在；`tasks.md` 存在；issue 映射完整（若开启）；工作区干净；`gh` 已认证；CI workflow 存在且 `on.push` 覆盖功能分支；`git fetch origin <base>`。
3. **加载上下文**：feature 目录下的制品 + 宪法。
4. **分支**：按配置创建/检出功能分支（链式时基于上一分支 tip）。
5. **任务执行**：按 `tasks.md` 的 Phase 顺序执行；读取 `agent-assignments.yml` 分配角色并以中文提示词 spawn；`default` 在当前上下文实现；CI/流水线类任务交给 `execution.devops_agent`（为空时 AI 选最接近角色）；**测试先行是本工作流固定规则**（测试任务先写并确认 FAIL 再实现，不依赖任何项目宪法）；`[P]` 且不同文件可并行；完成标记 `[X]`。
6. **门禁**：`<repo>/scripts/gates.ps1` 缺失时由 AI 参照样板现场编写并经用户确认；每个 Phase 结束与 PR 前跑全量门禁；AI 判断必要时跑快速门禁；未覆盖的工具链由 AI 补充命令并记入审计记录。
7. **AI 审查 ↔ 修复循环**（每个逻辑组）：Code Reviewer 审查 diff → 主循环核实（区分已确认问题与未验证猜测）→ 回传实现 agent 修复 → 快速门禁 → 重审，直到无 🔴/🟡。审计记录写入 `notes/reviews/<branch>-r<N>.md`。收敛异常（连续 4 轮不下降）与轮次过多（>5 轮）自动标注提醒。
8. **提交**：每个逻辑组 Conventional Commits，审计记录随组提交。
9. **推送与 CI**：推送分支 → `wait-ci.ps1` 轮询直至完成 → 失败则结合 CI 日志与审查 findings 修复再推，直到 CI 绿。CI 收敛异常同样标注。
10. **开 PR**：`open-pr.ps1` 生成 PR，正文含 `Closes #<issue>`，标题/正文按配置语言。
11. **AI PR 审查**：审查整个 PR diff + 与 spec/plan/tasks/宪法一致性 + 门禁与 CI 证据；结论 PASS/FAIL 回填 PR Comment；**不 Approve、不 Merge**；FAIL 则同分支修复 → CI → 重审，直到 PASS。
12. **下一功能**：本 PR AI 审查 PASS 且 CI 绿后，创建下一个链式分支回到流程开头（合并仍等人工）。

## 与任务/issue/agent 分配的联动（推荐顺序）

```text
speckit.tasks                 # 生成 tasks.md
speckit.taskstoissues         # tasks.md -> GitHub issues
speckit.agent-assign.assign   # 扫描 agents 并给出分配（人工确认）
speckit.agent-assign.validate # 校验分配（可选）
speckit.implement-loop.run    # 本扩展：实现 -> 门禁 -> CI -> PR -> PR 审查
（人工）PR Approve + Merge
```

## 人工闸门（不可逾越）

- agent 分配须经人工确认（`speckit.agent-assign.assign` 的最后一步）；
- AI 审查与 CI 只提供证据，PR 的 Approve 与 Merge 必须人工；
- 禁止直接推送 main；main 分支建议配置保护规则；
- `Rebuild`（丢弃分支重建）等方向性操作须人工确认，默认 `Fix-in-place`。

## 常见问题

- **找不到 feature 目录**：配置 `feature.directory`，或确认 `.specify/feature.json` 存在（`speckit.specify` 会自动写入）。
- **等待 CI 超时/未触发**：检查 workflow 文件名与 `ci.workflow_name`、`on.push` 的 `paths` 过滤（纯文档改动会被跳过）；必要时 `gh workflow run <file> --ref <branch>` 手动触发。
- **门禁脚本**：解析顺序——配置 `gates.script` > 项目 `<repo>/scripts/gates.ps1`；都没有时，AI 参照 `templates/gates-template.ps1` 样板现场编写，经用户确认后执行并随分支提交。
- **换了语言/工具怎么办**：不需要改扩展——门禁脚本缺失时 AI 按样板现场写；已有脚本但没覆盖的命令（Makefile、Bun、Zig 等），AI 检查项目后自行补充执行并记入审计记录。**扩展不再枚举语言/工具。**
- **skills 模式命令引用**：本扩展命令体不依赖 `__SPECKIT_COMMAND_*__` 占位符（该占位符在 Codex/ZCode 等 skills 模式下暂不解析），核心命令按中性名称描述并在正文给出对应技能名。

## 许可证

MIT
