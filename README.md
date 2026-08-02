# WarFictionSim 战争幻想模拟器

单机 Windows 平台回合制战争幻想模拟器：玩家作为一方指挥官，敌方由大语言模型（LLM）驱动的智能指挥部（司令、参谋、前线指挥官）指挥作战。

## 核心设计

- **回合制**：游戏内回合时长由玩家配置（例如一分钟一回合），支持加速；每回合等待 AI 指挥官返回命令并通过校验后，再推进结算。
- **LLM 指挥官**：通过 agent / subagent 架构模拟敌方司令部（司令、参谋、前线指挥官）；AI 只生成命令，不参与数值判定。
- **确定性结算**：战斗胜负、伤亡、地形效果等全部由确定性算法和固定参数结算，保证可复现、可测试。
- **高度自定义**：部队（名称、规模、火力等）、地图（地形、设施等）均可由玩家自定义。

## 技术栈（规划）

- 语言：C / C++（确定性模拟核心）、C#（界面与工具）
- 版本管理：Git + GitHub
- CI/CD：GitHub Actions（构建、测试、Windows 打包）
- AI 接入：LLM API / 本地模型 / 脚本兜底（provider 抽象）

## 项目结构

```
.agents/     Speckit 代理技能定义
.claude/     Claude 代理配置
.codex/      Codex 代理配置
.specify/    Spec-kit 工作流（模板、宪法、脚本、扩展）
```

## 开发工作流

遵循 spec-kit 工作流：spec → plan → tasks；所有变更必须通过 Pull Request 合并，并由代码审查者批准；治理规则见 `.specify/memory/constitution.md`。

## 当前状态

项目脚手架阶段：spec-kit 工作流已就绪，尚未开始代码实现。
