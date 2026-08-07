---
name: "speckit-implement-loop"
description: "运行自动化实现循环：开分支 → 按已确认的 agent 分配执行任务 → 门禁 → AI 审查/修复循环 → 提交 → 推送触发 GitHub Actions CI → 获取 CI 反馈并修复至无异常 → 开 PR。分配确认、PR 审批与合并保持人工。"
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

## 任务执行

按 `tasks.md` 的 Phase 顺序（Setup → Foundational → US1… → Polish）执行，对每个任务：

- 读取 `agent-assignments.yml` 中该任务的 agent；
- **命名 agent**（如 Backend Architect、Desktop App Engineer、DevOps Automator、Multi-Agent Systems Architect、Prompt Engineer、Technical Writer、UI Designer、Software Architect）：以该角色 spawn 执行，**中文提示词**包含：任务 ID、完整描述、相关契约/数据模型引用、精确文件路径、依赖上下文；
- **`default`**：在当前上下文内直接实现；
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

- **收敛检测**：连续 2 轮 🔴/🟡 数量不下降或同类问题重复 → 审计记录标注"收敛异常提醒"，建议换更高推理档位 / 缩小 diff 范围 / 人工介入；
- **轮次提醒**：超过 3 轮 → 审计记录标注"轮次过多提醒"，继续直到通过；
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
- 轮次提醒：正常 / 已超 3 轮
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

3. 修复循环（CI 反馈 + AI 审查结合）：

   - 主循环 agent 先核实 CI 失败是否由本次改动引起（对照改动范围与失败步骤，区分真实 bug 与环境问题）；
   - 把 CI 失败信息 + Code Reviewer 的 findings 一起交给对应实现 agent（同一角色）修复；
   - 实现 agent 修复 → 本地 `scripts/gates.ps1 -Quick` → 提交（Conventional Commits）→ `git push origin <branch>` → 回到第 2 步；
   - 循环直到 CI 全部通过且 AI 审查无 🔴/🟡。

4. 收敛与轮次提醒（与 AI 审查循环同一规则）：

   - 连续 2 轮 CI 失败未下降或同类问题重复 → 审计记录标注"CI 收敛异常提醒"，建议人工介入；
   - 超过 3 轮 → 审计记录标注"CI 轮次过多提醒"，继续直到通过。

5. 审计记录：每轮 CI 结果写入 `notes/reviews/<branch>-ci.md`（时间、run id、结论、失败步骤、修复 commit 列表），随逻辑组提交入库。

## 开 PR（CI 通过后）

- **前提**：本地 gates 通过 且 CI 反馈无异常；

```text
powershell -File scripts/open-pr.ps1 -Title "<feat: 说明>" -Issue "<issue 编号>"
```

- PR 标题与说明使用中文；
- PR 正文必须包含 `Closes #<issue>`（issue 编号来自前置检查的映射）。

## 停止点（人工闸门）

- 创建 PR 后**立即停止**，用中文向用户报告：分支、PR 链接、完成任务范围、门禁结果、审查轮数与结论、审计记录路径；
- **绝不自动 merge、绝不直接推送 main**（宪法第 3 条）；
- 等待用户 Approve + Merge；合并后 PR 的 `Closes #` 自动关闭对应 issue。

## 汇报模板

```text
## 实现循环完成
- 分支：<branch>
- PR：<url>
- 完成任务：Txxx–Tyyy（N 条，[X] 已标记）
- 门禁：通过（构建/测试/格式）
- CI：通过（N 轮，失败 M 次已修复）或"未触发"说明
- AI 审查：N 轮，🔴/🟡 问题 M 个（已修复 / 待确认）
- 审计记录：notes/reviews/<branch>-r*.md、<branch>-ci.md
- 下一步：等待人工 Approve + Merge
```
