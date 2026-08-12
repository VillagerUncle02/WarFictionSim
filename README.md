# WarFictionSim 战争幻想模拟器

单机 Windows 平台实时战争模拟游戏：玩家作为一方指挥官，敌方由大语言模型（LLM）驱动的智能指挥部指挥作战。

## 核心设计

- **实时推进**：模拟实时推进（可暂停、加速），单位不等待 AI 即可按已有命令持续行动；AI 决策异步到达并通过校验后生效。
- **AI 指挥官**：按指挥层级（连排级/营级/旅级）组织 AI 指挥体系，每个指挥节点由 AI 指挥官负责，可配备参谋；玩家可扮演三个指挥层级之一，其余层级由 AI 接管；AI 只生成命令，不参与数值判定。
- **俯视角 2D 战场**：玩家在地图上以兵牌形式指挥单位，外观风格参考 Armored Brigade / Armored Brigade II；3D 交火视图为后续可选阶段。
- **确定性结算**：战斗胜负、伤亡、地形效果等全部由确定性算法和固定参数结算，保证可复现、可测试。
- **高度自定义**：部队（名称、规模、装备、火力等）、地图（地形、设施等）均可由玩家自定义。

## 技术栈（规划）

- 语言：C / C++（确定性模拟核心）、C#（界面与工具）
- 构建：C/C++ 使用 CMake + Ninja + clang-cl + vcpkg；C# 使用 .NET SDK（dotnet）
- 开发环境：VS Code + clangd + CodeLLDB + C# Dev Kit
- 版本管理：Git + GitHub
- CI/CD：GitHub Actions（构建、测试、Windows 打包）
- AI 接入：云端 API / 本地模型 / 无 AI 脚本模式

## 项目结构

```
app/          C# 侧：UI 与工具（WPF / 控制台，由 dotnet 构建）
  WarFictionSim.sln
  ui/         游戏界面（WPF）
  tools/      数据编辑与校验工具
  tests/      C# 单元测试
native/       C/C++ 侧：确定性模拟核心（由 CMake 构建）
  CMakeLists.txt / CMakePresets.json
  core_c/     C11 底层数学库
  sim/        C++20 确定性模拟核心（含 C ABI 桥接 sim_core.dll）
  tests/      C++ 单元测试与黄金测试（CTest / GoogleTest）
contracts/    跨语言契约：JSON Schema 与 C ABI 头（两侧唯一依赖）
data/         剧本与单位/地形数据
docs/         设计参考与归档
.agents/     Speckit 代理技能定义
.claude/     Claude 代理配置
.codex/      Codex 代理配置
.specify/    Spec-kit 工作流（模板、宪法、脚本、扩展）
specs/        功能规格（spec / plan / tasks）
```

## 本地构建

原生侧（C/C++）：

```powershell
# 先设置 vcpkg 根目录（本机示例）
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'

cd native
cmake --preset clang-cl-debug     # 或 clang-cl-release
cmake --build --preset clang-cl-debug
ctest --preset clang-cl-debug
```

C# 侧（UI / 工具）：

```powershell
dotnet build app/WarFictionSim.sln -c Debug
dotnet test  app/WarFictionSim.sln -c Debug --no-build
```

工具链细节与注意事项见 [docs/toolchain.md](docs/toolchain.md)。

## 开发工作流

遵循 spec-kit 工作流：spec → plan → tasks；所有变更必须通过 Pull Request 合并，并由代码审查者批准；治理规则见 `.specify/memory/constitution.md`。

## 当前状态

项目脚手架阶段：spec-kit 工作流已就绪，宪法已生效（v2.0.0）；C/C++ 与 C# 已按
`native/` + `app/` 分离，正在按任务合入基础功能。
