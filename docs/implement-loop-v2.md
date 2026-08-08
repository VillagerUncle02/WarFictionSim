# 自动化实现循环 v2 方案

## 0. 目标

在 `speckit-implement-loop` 基础上闭环：AI 审查 PR（不批准、不合并）→ 审查通过后自动进入下一功能；
解决"多个分支等待合并"的冲突风险。人工保留：Approve + Merge、重大方向决策、自动化解不开的冲突。

## 1. 整体流程

```text
main ──► PR#N（当前活动分支）
  ① 分支：从上一分支 tip 创建（链式）
  ② 实现：按 agent-assignments.yml 执行任务
     ├─ CI/流水线类任务（T002/T003/T005/T006/T094…）→ DevOps Automator subagent 执行
     └─ 其余任务 → 对应角色 agent / 直接执行
  ③ 门禁：本地全量 gates.ps1
  ④ AI 审查↔修复循环（提交前，Code Reviewer）
  ⑤ 提交（Conventional Commits）
  ⑥ 推送 → wait-ci 等 CI 绿（带硬超时）
  ⑦ 开 PR
  ⑧ AI PR 审查（开 PR 后，Code Reviewer；CI 类 PR 可交叉 DevOps Automator 意见）
     ├─ FAIL → 同分支修复 → 推送 → CI → 回到 ⑧
     └─ PASS → 记录审查 + 回填 GitHub PR Comment（人工远程可见）
  ⑨ 自动创建下一分支（PR#N+1），回到 ①
人工（异步）：按序 Approve + Merge PR#1 → PR#2 → …
```

## 2. 分支策略：链式单队列

| 规则 | 说明 |
|---|---|
| 分支命名 | `feature/<stage>`（如 `feature/setup-foundation`、`feature/foundation-core`、`feature/us1-mvp`） |
| 创建方式 | 下一分支基于上一分支 tip（`git checkout -b feature/next <current-tip>`） |
| 活动分支 | 同一时间只有 1 个活动分支；其余为"已审查通过、等人工合并"的 PR |
| PR diff | GitHub 按 base=main 自动重算：前序合并后，本 PR 只显示自己的提交 |
| 合并顺序 | 必须按序（PR#N 先于 PR#N+1）。两道闸：CI 顺序检查（check-pr-order.ps1）+ 分支保护要求必绿 |
| 合并后处理 | 前序合并后自动对后续分支 rebase origin/main 并重新推送 + CI；冲突则停下报告人工 |
| 乱序防护 | 乱序合并会导致前序 PR 变空/冲突；CI 顺序闸进一步阻止 |

## 3. 任务执行分工

- 按 `agent-assignments.yml` 执行；命名角色用对应 subagent（投递失效时兜底：主循环直接执行并记录）；
- **DevOps 类任务（CI/流水线/构建/依赖锁定）执行交给 DevOps Automator subagent**——执行分工，不并入自动化流程环节；
- 测试先写并确认 FAIL（RED）；同文件串行；标 `[P]` 且不同文件可并行。

## 4. 门禁

- 本地：每阶段结束、PR 前跑全量 `scripts/gates.ps1`；审查修复后跑 `-Quick`；
- 远程：`wait-ci.ps1` 轮询（每个 gh 调用 30s 硬超时、连续 3 次超时退出、总时长 `-TimeoutSeconds` 兜底），CI 绿才允许开 PR。

## 5. AI 审查 ↔ 修复循环（提交前）

- 对象：逻辑组 diff；执行者：Code Reviewer；
- 收敛检测：连续 4 轮 🔴/🟡 数量不下降或同类问题重复 → "收敛异常提醒"；轮次过多提醒：超过 5 轮；
- 审计记录：`notes/reviews/<branch>-r<N>.md`（每轮）；
- 职责边界：审查 agent 只发现问题不改码；实现 agent 只修自己的问题；主循环核实、调度、跑门禁。

## 6. AI PR 审查（开 PR 后，不批准合并）

- 审查对象：整个 PR diff + 与 spec/plan/tasks 一致性 + 宪法合规 + 门禁/CI 证据 + PR 说明完整性；
- 执行者：Code Reviewer；CI/流水线类 PR 可请 DevOps Automator 出具交叉意见（仅意见）；
- 结论：PASS / FAIL + findings（级别、file:line、修复方向）；
- 审计文件：`notes/reviews/<branch>-pr-review.md`；
- **PR Comment 回填**：`gh pr comment <pr> --body-file ...`，人工远程审查；**AI 绝不 Approve、绝不 merge**；
- FAIL → 同分支修复循环；"回退"默认 Fix-in-place，方向性错误才 Rebuild（需人工确认）。

## 7. 自动进入下一功能

- 触发条件：当前分支 AI PR 审查 PASS + CI 绿 + 审计记录完成（不要求已合并）；
- 下一个 = tasks.md 下一阶段/故事；单活动分支。

## 8. 配套改动

- SKILL.md：AI PR 审查阶段、链式分支队列与合并顺序、收敛检测 4 轮、停止点与汇报模板更新；
- `scripts/check-pr-order.ps1`（CI 顺序检查）、`scripts/pr-review-template.md`、`scripts/merge-rebase-next.ps1`；
- ci.yml：新增 `check-pr-order` job（仅 pull_request）；
- 执行说明：DevOps 任务交 DevOps Automator subagent。

## 9. 风险与兜底

| 风险 | 兜底 |
|---|---|
| 前序 PR 长期不合并、链变长 | 活动分支照常推进；人工按序批量合并；可配置暂停开关 |
| 合并后 rebase 冲突 | 自动化停下中文报告，人工介入 |
| AI 审查误判 | 审查记录留痕 + 可选第二意见；最终人工 Approve |
| spawn 投递失效 | 回退直接执行并在审计记录标注 |
| CI 顺序闸误伤 | 只读检查、明确提示，可人工放行 |

## 10. 决策记录

- 分支策略：链式单队列（推荐方案，已确认）；
- 前序 PR 未合并时允许下一分支继续开工，靠顺序门禁兜底（已确认）；
- DevOps 类任务的执行交给 DevOps Automator subagent，不并入自动化流程（已确认）；
- 收敛检测 4 轮、轮次过多提醒 5 轮（已确认）；
- PR 审查结论回填 GitHub PR Comment，人工远程审查（已确认）。
