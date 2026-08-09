# 归档：speckit-implement-loop v2（单体技能）

- **归档时间**：2026-08-10
- **归档来源**：`.agents/skills/speckit-implement-loop/SKILL.md`（v2 版本，2026-08 由 `feature/automation-v2` 提交保存）
- **取代者**：`speckit-implement-loop` 扩展（源码位于 `extensions/implement-loop/`，已注册 5 个命令：`run` / `gates` / `ci-wait` / `pr-review` / `next`）

## 用途

本文件是"自动化实现循环"的 v2 单体技能存档，仅作**历史参考**：当扩展版出现行为回归、需要对比旧实现、或排查分支策略/审查循环设计时使用。**不要**把它重新放回 `.agents/skills/` 作为活动技能。

## 相关参考材料（未移动，仍在原位）

- `docs/implement-loop-v2.md`：v2 方案设计文档（链式单队列、AI PR 审查、合并顺序门禁）
- `scripts/check-pr-order.ps1` / `scripts/merge-rebase-next.ps1`：v2 链式分支辅助脚本（`ci.yml` 仍引用项目版 check-pr-order）
- `notes/reviews/automation-v2-pr-review.md`：v2 的 AI PR 审查记录

## 与扩展版的差异（简要）

| 维度 | v2 单体技能 | 扩展版 |
|---|---|---|
| 形态 | 单个 SKILL.md，项目硬编码 | `extension.yml` + 5 命令 + 8 脚本 + 配置模板 |
| feature 目录 | 硬编码 `specs/001-war-sim-command-battle` | 自动读 `.specify/feature.json` / 配置 / 扫描回退 |
| 门禁/PR/CI 脚本 | 项目 `scripts/*.ps1` | 项目自定义优先，否则扩展自带通用脚本 |
| 语言/角色/分支 | 写死中文与固定角色名 | 全部可配置（`implement-loop-config.yml` + 环境变量） |
