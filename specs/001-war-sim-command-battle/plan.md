# Implementation Plan: 战争幻想模拟器 —— 核心玩法、指挥体系与战斗系统

**Branch**: `001-war-sim-command-battle` | **Date**: 2026-08-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-war-sim-command-battle/spec.md`

## Summary

本特性交付 v1 单机 Windows 实时战术战争模拟器：v1 场景为营级对抗（双方各一个营级单位群，5×5 km 地图），同时支持连排级战斗；玩家可扮演连排级或营级指挥官，其余指挥层级由 AI 指挥体系接管；模拟实时推进且完全确定，采用多线程确定性分区并行；AI 在关键事件异步决策，战斗数值全部由确定性算法结算。

技术路线（详见 [research.md](research.md)）：

- **C++20 确定性模拟核心**：固定 tick（20 Hz）、统一 RNG（PCG32，显式种子与按分桶划分的子流）、多线程确定性分区并行（空间区域/实体分桶 + 固定边界同步 + 确定性调度与固定归约顺序，线程数不影响状态哈希）、事件/命令队列按确定性顺序处理，无头 CLI 可运行完整战斗。
- **C# WPF 表现层**：2D 兵牌俯视视图 + 命令/简报/编辑器面板，渲染循环独立于模拟 tick，通过 C ABI（P/Invoke）消费纯数据快照、注入命令。
- **AI 双后端**：云端模型（结构化命令输出，JSON Schema 校验）与无 AI 脚本模式（确定性脚本 AI 兜底）；AI 输出经校验后异步注入确定性命令队列，模拟不阻塞；本地模型后置。
- **数据驱动**：派系模板、剧本、单位/武器/地形全部 JSON 定义并在加载时校验；存档版本化（magic "WFS-SAVE" + format_version + header_json + state_blob + 迁移链）。
- **可复现构建**：CMake + vcpkg 清单锁版本，GitHub Actions 覆盖三语言构建、测试、格式检查与 Windows 打包。

## Technical Context

**Language/Version**: C++20（MSVC，确定性模拟核心）；C#/.NET 10 LTS（WPF 表现层与工具）；C11（仅底层确定性数学库 `core_c`）

**Primary Dependencies**:

- `sim`：nlohmann/json（数据加载）、PCG32（确定性 RNG 子流，头文件库）、GoogleTest（测试）
- `ui`：WPF、CommunityToolkit.Mvvm、System.Text.Json、xUnit（测试）
- `interop`：纯 C ABI + P/Invoke（不使用 C++/CLI）
- `ai`：自封装 OpenAI 兼容 REST 客户端（HttpClient）；命令契约采用 JSON Schema

**Storage**: 数据驱动 JSON（派系模板/剧本/单位/地形/工事）；存档为版本化快照（`WFS-SAVE` + `format_version` + `header_json` + `state_blob`），带迁移链；决策日志 JSONL（AI 输入/输出/状态快照，供可复现性与复盘）

**Testing**: C++ GoogleTest（单元 + 黄金确定性测试：同种子两次运行状态一致；1 线程 vs N 线程状态哈希一致）；C# xUnit（DTO/校验器/存档往返）；无头 CLI 集成测试（脚本驱动完整战斗，覆盖 13 种任务与胜负判定）

**Target Platform**: Windows 10/11 单机桌面

**Project Type**: 桌面游戏（确定性模拟引擎 + 2D 表现层 + 数据编辑器工具）

**Performance Goals**: v1 营级对抗场景（双方各一个营级单位群，5×5 km，实体量级 300–800）；模拟 tick 20 Hz、多线程（4–8 线程）下单 tick 墙钟 ≤ 5 ms；内存 < 512 MB；UI 渲染独立 ≥ 60 fps（低配允许渲染降级、下限 ≥30 fps，模拟 tick 恒 20 Hz，FR-025）；启动数据校验 ≤ 5 s

**Constraints**: 确定性（同输入同输出、统一 RNG 子流、禁现实时钟参与结算、线程数与调度方式不影响状态哈希）；AI 异步非阻塞、仅生成命令且须 schema 校验；非法/超时拒绝或降级、脚本兜底；模拟核心无头可测；数据驱动禁止硬编码；依赖锁定、构建可复现；存档带版本与迁移；多线程确定性分区并行（区域分桶 + 固定边界同步 + 固定归约顺序）

**Scale/Scope**: v1 = 营级对抗（5×5 km）+ 连排级战斗；1 个营级战斗剧本 + 1 个连排级新手教程场景；13 种任务类型全部进入 v1；战术分队、基础队形、烟幕、雷场、平民设施联动、三级超限分数进入 v1；旅级、两旅交战框架、本地模型、航空、无人机、反炮兵、投降、3D 视图、战役制后置

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| 宪法原则 | 规划落实 | 结果 |
|---|---|---|
| 7 确定性裁决 | 多线程确定性分区并行（区域分桶、固定边界同步、确定性调度与固定归约顺序，线程数不影响状态哈希）；统一 RNG 子流；任务完成判定由确定性代码 | 通过 |
| 8 LLM 指挥官为核心 | AI 异步决策注入命令队列，模拟不等待；决策频率上限按游戏时间 | 通过 |
| 9 AI 边界 | AI 仅生成命令、JSON Schema 校验、非法/超时拒绝或降级、脚本兜底、不参与任务判定 | 通过 |
| 10 可复现性 | AI 决策点记录状态快照 + 输入 + 输出（决策日志 JSONL） | 通过 |
| 11 AI 资产版本化 | 提示词/模型版本/采样参数入库版本管理 | 通过 |
| 12 数据驱动 | 一切可自定义内容以 JSON 数据定义，加载校验，非法数据报错不崩溃 | 通过 |
| 13 存档兼容 | 存档携带格式版本号，破坏性变更含迁移链 | 通过 |
| 14 语言边界与分层调用 | C++ 核心 / C# UI / C11 底层库；C ABI 纯数据边界，表现层不直改模拟状态 | 通过 |
| 15 无头可测 | `sim` 无头 CLI 可完整运行战斗；1 vs N 线程哈希一致性作为确定性门禁 | 通过 |
| 16 统一时间模型 | 离散 tick 与游戏时钟，暂停/加速仅发生在表现层 | 通过 |
| 17 错误处理 | 禁止静默吞错，错误记录或提示，损坏状态可定位 | 通过 |
| 18 依赖与可复现构建 | vcpkg 锁版本、CI 覆盖三语言构建/测试/格式/打包 | 通过 |
| 1–6、治理 | 沿用：PR + 审查、Conventional Commits、测试门禁、无敏感信息、文件级注释与文档 | 通过 |

无违规项，无需 Complexity Tracking。Phase 1 设计完成后复检结论见 [plan.md 末尾](#phase-1-复检)。

## Project Structure

### Documentation (this feature)

```text
specs/001-war-sim-command-battle/
├── plan.md              # 本文件（$speckit-plan 输出）
├── research.md          # Phase 0 输出（确定性并行/AI/规模研究结论）
├── data-model.md        # Phase 1 输出（实体模型与状态机）
├── quickstart.md        # Phase 1 输出（无头验证与运行指南）
├── contracts/           # Phase 1 输出（命令 Schema、C ABI、存档格式契约）
└── tasks.md             # Phase 2 输出（$speckit-tasks 生成，本命令不创建）
```

### Source Code (repository root)

```text
WarFictionSim/
├── CMakeLists.txt
├── .github/workflows/ci.yml
├── sim/                              # C++20 确定性模拟核心（无头、可测试）
│   ├── include/wfs/sim/              #   公开 API（场景、命令、快照、存档）
│   ├── src/                          #   系统实现（机动/战斗/情报/任务/指挥/后勤/并行）
│   └── headless/                     #   无头 CLI（sim_headless，T019）
├── core_c/                           # C11 底层确定性数学库（确定性浮点封装、弹道计算）
│   ├── include/wfs/core/
│   └── src/
├── ui/                               # C# WPF 表现层（2D 兵牌视图、命令/简报/编辑器面板）
│   └── src/
├── tools/                            # C# 工具（剧本校验、AI 命令模拟器、数据校验 CLI）
│   └── src/
├── tests/
│   ├── sim_tests/                    # C++ GoogleTest：单元 + 黄金确定性测试（含 1 vs N 线程）
│   ├── cli_tests/                    # 无头集成测试：脚本驱动完整战斗
│   ├── bench/                        # 性能预算基准（tick/内存，T093）
│   └── ui_tests/                     # C# xUnit：DTO/校验器/存档往返
├── data/                             # 数据驱动内容（JSON）
│   ├── factions/                     #   三大派系模板（资源池 + 指挥风格）
│   ├── scenarios/                    #   剧本（营级战斗 + 连排级新手教程）
│   ├── units/                        #   单位/武器/弹药/装备/工事
│   ├── terrain/                      #   地形/环境/功能设施
│   └── ai/                           #   AI 提示词与模型参数（版本化）
├── contracts/                        # 运行时契约（JSON Schema 等，镜像 specs 设计）
├── scripts/                          # 构建/验证/打包脚本
└── docs/                             # 架构文档与操作手册
```

**Structure Decision**: 采用"确定性核心 + 表现层 + 工具"三部分结构，直接落实宪法第 14/15 条：`sim/`（C++20）与 `core_c/`（C11）可独立无头构建与测试，`ui/`（C# WPF）仅通过 C ABI 消费快照与注入命令，`tools/` 复用同一核心。`data/` 与 `contracts/` 支撑数据驱动与契约可测（宪法第 12/2 条）。

## Complexity Tracking

本特性 Constitution Check 全部通过，无违规项，此表留空。

## Phase 1 复检

Phase 1 完成后复检：data-model 与 contracts 未放宽任何宪法约束——确定性由分区并行、固定边界同步、RNG 子流与命令/事件队列保证（线程数不影响状态哈希）；AI 边界由命令 Schema 与脚本兜底保证；数据驱动、存档版本化、无头可测、统一时间模型均在数据模型与契约中显式建模。复检结论：**通过，无违规**。
