---
name: "speckit-implement-loop"
description: "运行自动化实现循环：开分支（链式）→ 按已确认的 agent 分配执行任务（DevOps 类任务由 DevOps Automator subagent 执行）→ 门禁 → AI 审查/修复循环 → 提交 → 推送触发 GitHub Actions CI → 获取 CI 反馈并修复至无异常 → 开 PR → AI 审查 PR（结论回填 PR Comment，不批准不合并）→ 通过后自动进入下一功能。分配确认、PR 审批与合并保持人工。"
---

# Implement Loop（实现循环自动化）

## 范围

自动化以下环节：创建/检出功能分支 → 按 `agent-assignments.yml` 逐阶段执行任务 → 门禁 → AI 审查/修复循环 → Conventional Commits → 推送触发 GitHub Actions CI → CI 反馈/修复循环（直至无异常）→ 创建 PR。

**不在本技能范围**：任务生成（`/speckit.tasks`）、issue 创建（`/speckit.taskstoissues`）、agent 分配与确认（`/speckit-agent-assign-assign`）、PR 审批与合并——以上由用户人工完成。假设 `tasks.md` 与 `agent-assignments.yml` 已存在且经用户确认。

**语言要求**：与实现/审查 agent 的对话、提示词、审计记录、门禁输出、PR 说明、汇报**全部使用中文**，方便人工审查。

## 前置检查

1. 确认 `specs/001-war-sim-command-battle/tasks.md` 与 `specs/001-war-sim-command-battle/agent-assignments.yml` 存在；
2. **issue 映射校验**：运行 `gh issue list`，校验本次范围内每个任务 ID（`T###`）都有对应 issue；存在缺失 → 停下，提示用户先运行 `/speckit.taskstoissues`；
3. 确认工作区干净（`git status`）；存在未提交改动 → 停下询问用户处理方式；
4. `git fetch origin main`；
5. 加载上下文：`plan.md`、`data-model.md`、`contracts/`、`research.md`、`quickstart.md`、`.specify/memory/constitution.md`。
6. 确认 gh 已认证（keyring）；所有 gh/git 网络命令需在沙箱外（escalated）执行；
7. 确认 `.github/workflows/ci.yml` 的 `on.push` 覆盖功能分支（仓库已配置为任意分支 push 触发，勿改回 main-only，否则 PR 前 CI 循环无法启动）。

## 分支

- 从 `origin/main` 最新提交创建/检出分支，命名 `feature/<story>`（如 `feature/us1-mvp`、`feature/us3-battalion`），或按用户指定；
- 同名分支已存在时检出并同步 `origin/main`；
- 禁止直接推送 main（宪法第 3 条）。

### 链式分支队列与合并顺序（v2）

- **链式**：下一个分支基于上一分支 tip 创建（`git checkout -b feature/<next> <current-tip>`），天然包含前序改动；
- **单活动分支**：同一时间只有 1 个分支在实现，其余为"AI 审查通过、等人工合并"的 PR；
- **合并必须按序**（PR#N 先于 PR#N+1）：CI 增加"前序 PR 未合并 → 本分支 CI 标红并提示"（scripts/check-pr-order.ps1，见 ci.yml check-pr-order job）；
- **合并后自动 rebase**：前序合并后用 scripts/merge-rebase-next.ps1 对后续分支 rebase origin/main 并重新推送 + CI；有冲突则停下中文报告人工；
- **PR diff**：GitHub 按 base=main 自动重算，前序合并后本 PR 只显示自己的提交，审查无干扰。

## 任务执行

按 `tasks.md` 的 Phase 顺序（Setup → Foundational → US1… → Polish）执行，对每个任务：

- 读取 `agent-assignments.yml` 中该任务的 agent；
- **命名 agent**（如 Backend Architect、Desktop App Engineer、DevOps Automator、Multi-Agent Systems Architect、Prompt Engineer、Technical Writer、UI Designer、Software Architect）：以该角色 spawn 执行，**中文提示词**包含：任务 ID、完整描述、相关契约/数据模型引用、精确文件路径、依赖上下文；
- **`default`**：在当前上下文内直接实现；
- **DevOps 类任务**（CI/流水线/构建/依赖锁定，如 T002/T003/T005/T006/T094）：执行交给 DevOps Automator subagent，不并入自动化流程环节；
- 测试任务先写并确认 FAIL（RED），再实现（宪法第 2 条）；
- 同文件任务串行；不同文件且标 `[P]` 的可并行；
- 完成后在 `tasks.md` 将该任务标记为 `[X]`，用中文汇报进度。

## 门禁

- **固定**：每个阶段结束跑全量 `scripts/gates.ps1`；PR 创建前跑全量 `scripts/gates.ps1`；
- **按需**：AI 判断该跑了就跑（改动涉及测试、跨语言边界、高风险代码、审查修复后）→ `scripts/gates.ps1 -Quick`；
- 门禁失败必须修复后再继续；无法修复时停下用中文汇报。
- **远程门禁**：本地 gates 是前置；PR 前最终门禁是 GitHub Actions CI（见下方"推送与 CI 反馈循环"），本地全绿不代表 CI 通过。

## AI 审查 ↔ 修复循环（每个逻辑组）

```
实现 agent 完成逻辑组
   ↓
① Code Reviewer agent 审查 diff（中文输出；先读 notes/reviews/ 本分支最近一条，
  带着"已修复/回归"清单进入）
   ↓
② 主循环 agent 核实：打开每条 🔴/🟡 的 file:line 对照代码，
  分成【已确认问题】与【未验证猜测】（💭 nit 不阻塞，记录即可）
   ↓
③ 回传修复：把【已确认问题清单 + 相关文件路径 + 审查记录】重新派给
  实现该任务的实现 agent（同一角色）修复——修复由实现 agent 做，
  审查 agent 不碰代码，主循环不代写
   ↓
④ 实现 agent 修复 → scripts/gates.ps1 -Quick（编译 + 相关测试不回归）
   ↓
⑤ 下一轮：Code Reviewer 重新审查（读上一轮记录，核对已修复 + 查回归/新问题）
   ↓
⑥ 循环 ③–⑤ 直到无 🔴/🟡 → 逻辑组通过 → 提交
```

职责边界：

- **审查 agent**：只发现问题、给建议，不修改代码；
- **实现 agent**：只修复自己实现的问题，不扩大改动范围；
- **主循环 agent（orchestrator）**：核实 findings、调度回传、跑门禁、写审计记录，不代写修复。

附加规则：

- **收敛检测**：连续 4 轮 🔴/🟡 数量不下降或同类问题重复 → 审计记录标注"收敛异常提醒"，建议换更高推理档位 / 缩小 diff 范围 / 人工介入；
- **轮次提醒**：超过 5 轮 → 审计记录标注"轮次过多提醒"，继续直到通过；
- **第二意见（可选增强）**：变更涉及确定性核心、并发、跨语言边界或安全时，用 `codex exec review` 只读模式（`--sandbox read-only`，独立模型）交叉审查，结果并入审计记录；日常变更不启用；
- **审计记录**：每轮写入 `notes/reviews/<branch>-r<N>.md`（中文，格式见下），并随逻辑组一起提交入库。

### 审计记录模板

```markdown
# Review <branch> - 第 N 轮
- 审查范围：Txxx–Tyyy（commit 范围）
- 对比上一轮：已修复 M / 回归 0 / 新增 K / 无变化
- Findings：
  | # | 级别 | file:line | 问题 | 修复方向 | 状态 |
- 未验证猜测：<清单>
- 整体结论：patch is correct/incorrect（置信度 0.xx）
- 收敛检测：正常 / 收敛异常提醒
- 轮次提醒：正常 / 已超 5 轮
```

## 提交

- 每个逻辑组用 Conventional Commits 提交（`feat:` / `fix:` / `test:` / `docs:` / `refactor:` / `chore:`），提交信息说明变更原因；
- `notes/` 审计记录随对应逻辑组一起提交；
- 提交信息可使用中文或英文，但必须结构清晰。

## 推送与 CI 反馈循环（PR 之前）

**目的**：PR 之前先推送分支触发 GitHub Actions CI，结合 CI 反馈与 AI 审查修复问题，直到 CI 无异常再开 PR（用户要求：PR 前必须拿到 CI 反馈）。

1. 推送当前分支（沙箱外执行，需 keyring 认证）：

```text
git push -u origin <branch>
```

2. 等待并获取 CI 反馈：

```text
powershell -File scripts/wait-ci.ps1 -Branch <branch> [-TimeoutSeconds 1800]
```

   - 脚本轮询该分支最新 CI run 直至完成（成功 exit 0；失败 exit 1；超时/未触发 exit 2）；
   - 成功（success）→ 进入"开 PR"；
   - 失败/取消 → 输出失败 job 与失败步骤日志（`gh run view <id> --log-failed`），进入修复循环；
   - 超时/未触发 → 检查 ci.yml 的 push 触发条件与仓库 Actions 状态；必要时 `gh workflow run ci.yml --ref <branch>` 手动触发，仍异常则停下用中文汇报。
   - wait-ci 故障排查行为：gh 未认证/网络失败会立即报错退出（exit 3，不再静默空转）；120 秒内未出现与当前提交匹配的 run（例如纯文档改动被 ci.yml 的 paths 过滤跳过）会输出原因并 exit 2；需要更长等待时用 `-RunAppearWaitSeconds` / `-TimeoutSeconds` 调大。

3. 修复循环（CI 反馈 + AI 审查结合）：

   - 主循环 agent 先核实 CI 失败是否由本次改动引起（对照改动范围与失败步骤，区分真实 bug 与环境问题）；
   - 把 CI 失败信息 + Code Reviewer 的 findings 一起交给对应实现 agent（同一角色）修复；
   - 实现 agent 修复 → 本地 `scripts/gates.ps1 -Quick` → 提交（Conventional Commits）→ `git push origin <branch>` → 回到第 2 步；
   - 循环直到 CI 全部通过且 AI 审查无 🔴/🟡。

4. 收敛与轮次提醒（与 AI 审查循环同一规则）：

   - 连续 4 轮 CI 失败未下降或同类问题重复 → 审计记录标注"CI 收敛异常提醒"，建议人工介入；
   - 超过 5 轮 → 审计记录标注"CI 轮次过多提醒"，继续直到通过。

5. 审计记录：每轮 CI 结果写入 `notes/reviews/<branch>-ci.md`（时间、run id、结论、失败步骤、修复 commit 列表），随逻辑组提交入库。

## 开 PR（CI 通过后）

- **前提**：本地 gates 通过 且 CI 反馈无异常；

```text
powershell -File scripts/open-pr.ps1 -Title "<feat: 说明>" -Issue "<issue 编号>"
```

- PR 标题与说明使用中文；
- PR 正文必须包含 `Closes #<issue>`（issue 编号来自前置检查的映射）。

## AI PR 审查（开 PR 后，不批准合并）

- **审查对象**：整个 PR diff + 与 spec/plan/tasks 一致性 + 宪法合规（确定性/AI 边界/数据驱动/无头可测/代码风格）+ 门禁与 CI 证据 + PR 说明完整性；
- **执行者**：Code Reviewer（CI/流水线类 PR 可请 DevOps Automator 出具交叉意见，仅意见不并入流程）；
- **结论**：PASS / FAIL + findings（级别、file:line、修复方向）；
- **审计文件**：notes/reviews/<branch>-pr-review.md（改动范围、关键决策、门禁/CI 证据、审查结论、遗留 TODO）；
- **PR Comment 回填**：`gh pr comment <pr> --body-file notes/reviews/<branch>-pr-review.md`（可选对 findings 用行内 review comment 定位 file:line），人工远程打开 GitHub 即可审查；**AI 绝不点 Approve、绝不 merge**；
- **FAIL** → findings 交回实现 agent，同分支新增提交修复 → 推送 → CI → 重新 PR 审查，直到 PASS（沿用收敛/轮次提醒）；
- **"回退"语义**：默认 Fix-in-place（同分支继续修）；仅方向性错误才 Rebuild（丢弃分支、从稳定基点重建），Rebuild 需人工确认。

## 停止点（人工闸门）

- **每个 PR 仍等待人工 Approve + Merge**；AI 审查与 CI 只提供证据，不替代人工；
- **绝不自动 merge、绝不直接推送 main**（宪法第 3 条）；
- **AI PR 审查 PASS 且 CI 绿后**：记录审查（notes/reviews/<branch>-pr-review.md + PR Comment），然后**自动开始下一功能**（按链式分支队列创建 feature/<next>，回到流程开头）；
- 合并后 PR 的 `Closes #` 自动关闭对应 issue；随后对后续分支执行 scripts/merge-rebase-next.ps1。

## 汇报模板

```text
## 实现循环完成
- 分支：<branch>
- PR：<url>
- 完成任务：Txxx–Tyyy（N 条，[X] 已标记）
- 门禁：通过（构建/测试/格式）
- CI：通过（N 轮，失败 M 次已修复）或"未触发"说明
- AI 审查：N 轮，🔴/🟡 问题 M 个（已修复 / 待确认）
- PR 审查：PASS/FAIL（N 轮），PR Comment：<url>
- 审计记录：notes/reviews/<branch>-r*.md、<branch>-ci.md、<branch>-pr-review.md
- 下一步：等待人工 Approve + Merge（AI 审查已通过，可继续下一功能）
```
