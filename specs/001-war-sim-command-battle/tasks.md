---

description: "Task list template for feature implementation"
---

# Tasks: 战争幻想模拟器 —— 核心玩法、指挥体系与战斗系统

**Input**: Design documents from `/specs/001-war-sim-command-battle/`

**Prerequisites**: plan.md（技术栈/结构）、spec.md（6 个用户故事与优先级）、research.md（确定性/性能/AI/数值基线）、data-model.md（实体与状态机）、contracts/（命令 Schema、C ABI、存档格式）、quickstart.md（无头验证场景）

**Tests**: 项目宪法第 2 条「测试保障（不可妥协）」强制每个功能附带自动化测试、确定性算法必须有单元测试覆盖，因此各用户故事均包含测试任务；测试须先编写并通过"失败验证"（RED）后再实现，与实现任务同阶段交付。

**Organization**: 任务按用户故事分组，支持各故事独立实现与测试。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行（不同文件、无未完成依赖）
- **[Story]**: 所属用户故事（US1–US6）
- 描述必须包含精确文件路径

## Path Conventions

仓库根目录即 `WarFictionSim/` 项目根：`sim/`（C++20 确定性核心）、`core_c/`（C11 数学库）、`ui/`（C# WPF）、`tools/`（C# 工具）、`tests/`（sim_tests / cli_tests / ui_tests）、`data/`（JSON 数据）、`contracts/`（运行时契约）、`scripts/`、`docs/`。

---

## Phase 1: Setup（共享基础设施）

**Purpose**: 项目初始化与基础结构，支撑三语言可复现构建（宪法第 18 条）

- [x] T001 按 plan.md Project Structure 创建 WarFictionSim 项目骨架（sim/core_c/ui/tools/tests/data/contracts/scripts/docs 目录与根 CMakeLists.txt 占位）
- [x] T002 初始化 vcpkg 清单模式依赖锁定（vcpkg.json + baseline：nlohmann-json、GoogleTest、PCG32 头文件库等），在 vcpkg.json 中锁定全部版本
- [x] T003 配置 CMake 构建（根 CMakeLists.txt：sim/core_c 目标、MSVC C++20/C11 标志、`/fp:precise` 确定性浮点选项、CTest 集成、Release/Debug 配置）
- [x] T004 [P] 创建 C#/.NET 10 LTS 解决方案（ui/、tools/）与 xUnit 测试工程（tests/ui_tests/），配置 CommunityToolkit.Mvvm 与 System.Text.Json
- [x] T005 [P] 创建 GitHub Actions CI（.github/workflows/ci.yml：三语言构建、单元测试、格式检查、Windows 打包）
- [x] T006 [P] 配置格式化与静态检查（.clang-format、.editorconfig、dotnet format 配置，纳入 CI）
- [x] T007 建立 data/ 与 contracts/ 数据契约骨架（data/ 各子目录 schema_version 约定、contracts/schemas/ JSON Schema 目录与 README 说明）

---

## Phase 2: Foundational（阻塞性前置）

**Purpose**: 确定性核心基础设施 —— 任何用户故事开始前必须完成（宪法第 7/9/13/14/15/16 条）

**⚠️ CRITICAL**: Foundational 未完成前不得开始任何用户故事

- [x] T008 [P] 实现 C11 确定性数学库 core_c（core_c/include/wfs/core/math.h、core_c/src/math.c：开方/取整/三角确定性封装，IEEE-754 double）
- [x] T009 [P] 实现统一确定性 RNG（sim/include/wfs/sim/rng.h、sim/src/rng.cpp：PCG32、显式种子、RNG 流仅由模拟核心消费）
- [x] T010 实现离散 tick 与游戏时钟模型（sim/include/wfs/sim/clock.h、sim/src/clock.cpp：默认 20 Hz、可配置（1–1,000,000 Hz）、游戏时间戳、禁止现实时钟参与结算）
- [x] T011 实现事件/命令确定性队列（sim/include/wfs/sim/queue.h、sim/src/queue.cpp：按（game_tick, 单调序列号）排序、玩家命令与 AI 决策同一队列）
- [x] T012 实现 C ABI 边界层（sim/src/c_api.cpp：wfs_sim_* 系列、不透明句柄、wfs_sim_result 错误码、快照缓冲生命周期、wfs_sim_version ABI 版本号，见 contracts/sim-c-api.md）
- [x] T013 实现场景/数据加载与校验框架（sim/src/loader.cpp：JSON Schema + 语义校验、非法数据报错而非崩溃、启动校验预算 ≤5s）
- [x] T014 实现命令 Schema 与双重校验管道（sim/src/command_validation.cpp：JSON Schema + 语义校验，含越权命令拒绝/未注册类型/条件可求值/弹药存在性，见 contracts/command-schema.md §2）
- [x] T015 [P] 实现事件日志基础设施（sim/src/event_log.cpp：分类/严重级、5000 条环形保留、关键事件优先、过滤与搜索接口）
- [x] T016 实现状态快照与状态哈希（sim/src/snapshot.cpp：wfs_sim_get_snapshot 只读 JSON 快照、wfs_sim_get_state_hash SHA-256）
- [x] T017 实现存档序列化框架（sim/src/save.cpp：magic "WFS-SAVE" + format_version + header_json + state_blob、migrate 迁移链骨架、wfs_sim_save/load_save，见 contracts/save-format.md）
- [x] T018 实现确定性分区并行框架（sim/src/parallel.cpp：按空间区域/实体分桶、固定边界同步、确定性调度与固定归约顺序、线程数不影响状态哈希）
- [ ] T019 实现无头 CLI（sim/headless/main.cpp：sim_headless run/inject/save 子命令、--scenario/--seed/--script/--out/--hash/--threads/--ai-backend，JSONL 事件输出）
- [ ] T020 实现 AI 后端抽象与决策注入通道（sim/include/wfs/sim/ai/ia_backend.h、sim/src/ai_inject.cpp：wfs_sim_inject_ai_decision 经校验入队、到达 tick+序列号记录，支持 CHK052 回放）
- [ ] T021 实现无 AI 脚本后端（sim/src/ai/script_backend.cpp：确定性脚本 AI，输出与 LLM 同构的命令 JSON，无 AI 模式完整驱动战斗）
- [ ] T022 搭建黄金确定性测试框架（tests/sim_tests/golden/：同输入两次运行哈希一致、--threads 1 vs 4 哈希一致、跨运行形态一致性）

**Checkpoint**: 基础就绪 —— 用户故事实现可在 Foundational 后并行开始

---

## Phase 3: User Story 1 - 连排级战斗指挥闭环（Priority: P1）🎯 MVP

**Goal**: 玩家扮演连排级指挥官直接指挥班一级单位，完整走通"命令下达 → 通讯延迟 → 单位执行 → 战斗结算 → 任务判定 → 事件上报"闭环，含迷雾/情报识别、暂停/加速（SC-003、FR-025/030/033/034/040/042/043/060/061/063/065）

**Independent Test**: 创建单场连排级遭遇战独立测试：命令链路事件序列（COMMAND_ISSUED → COMMAND_ACKNOWLEDGED → UNIT_MOVING → MISSION_COMPLETED）与通讯延迟时间戳、战斗结算黄金样例、固定种子两次运行哈希一致；无需营级/旅级功能即可演示

### Tests for User Story 1（宪法第 2 条强制，先写后实现）⚠️

> **NOTE**: 先编写测试并确认 FAIL，再开始实现

- [ ] T023 [P] [US1] 命令链路集成测试（tests/cli_tests/test_command_chain.jsonl + runner：下达→确认接受→移动→完成、连排 3–10s 延迟时间戳比对、撤回/批量部分接受，见 quickstart §3.2）
- [ ] T024 [P] [US1] 战斗结算黄金测试（tests/sim_tests/golden/combat_golden.cpp：命中/伤害/压制/失联固定种子逐字段一致、目标选择与自动选弹黄金样例，见 quickstart §3.3）

### Implementation for User Story 1

- [ ] T025 [P] [US1] 创建指挥节点/编制/最小可指挥单位模型（sim/include/wfs/sim/model/command_node.h、organization.h、min_unit.h：CommandNode/Owner/指挥树/command_limit/coordination/experience、OrganizationUnit、MinCommandUnit、每节点单一 AI 归属（FR-048））
- [ ] T026 [P] [US1] 创建士兵/班组/载具/武器/弹药模型（sim/include/wfs/sim/model/combat.h：Soldier/Squad/Vehicle/Weapon/Ammo、四方向防护、模块状态、乘员/载员、重装备标志）
- [ ] T027 [P] [US1] 创建地形/设施/工事/环境模型（sim/include/wfs/sim/model/terrain.h：TerrainElement/Facility/Fortification/EnvironmentState、统一 passability、两栖规则、设施可见性规则与生命周期（部署/取消/重布置、侦察残留，FR-014））
- [ ] T028 [P] [US1] 创建任务模型与 13 种任务类型注册表（sim/src/mission_registry.cpp：Mission/MissionType 全部 13 种、ConditionExpr 确定性求值器、任务状态机数据表，见 data-model §11）
- [ ] T029 [US1] 实现命令下达与通讯延迟链路（sim/src/command_chain.cpp：下达→确认接受→执行、连排 3–10s 延迟、生效前撤回/修改、（优先级,序列号）裁决、批量部分接受）
- [ ] T030 [US1] 实现机动系统（sim/src/movement.cpp：路径移动、地形速度系数/通行限制、行军/战斗队形自动选择与切换耗时、烟幕区域遮蔽）
- [ ] T031 [US1] 实现战斗结算系统（sim/src/combat.cpp：动能/化学能穿深与伤害查表、过穿衰减、班组区域结算→个人防护衔接、自动目标选择/选弹与不匹配降级、压制量化、模块损伤、弃车与乘员/载员结算（FR-062））
- [ ] T032 [US1] 实现失联机制（sim/src/contact.cpp：失联概率统一 RNG、最后已知状态、恢复 60–180s、压制/失联/模块损伤复合状态取最严叠加）
- [ ] T033 [US1] 实现迷雾/情报识别系统（sim/src/intel.cpp：可视距离与观察能力、识别分档 T1–T3、记忆保留、最后动向、情报来源标注与过期）
- [ ] T034 [US1] 实现任务判定与事件上报（sim/src/mission_exec.cpp：确定性完成/失败判定、超时处置、失败后处置、持续任务循环、任务状态事件上报上级）
- [ ] T035 [US1] 实现侦察类任务判定机制（sim/src/recon_tasks.cpp：HIDDEN_RECON/INFILTRATE_RECON/OBSERVATION_POST/FIRE_RECON 判定机制实现，并按 CHK064 登记"实现阶段判定机制"待办）
- [ ] T036 [US1] 实现基础胜负判定（sim/src/outcome.cpp：关键目标/关键失败条件/时间上限、完成度加权、失败优先、部署超时兜底）
- [ ] T037 [P] [US1] 创建基础数据文件（data/units/、data/terrain/：班/武器/弹药/地形/工事/功能设施基线 JSON，带 schema_version，加载校验通过）
- [ ] T038 [P] [US1] 创建连排级新手教程场景（data/scenarios/scn-tutorial-platoon.json：小规模战斗、目标/失败条件/时间上限、教程独立存档标识）
- [ ] T039 [P] [US1] 实现主菜单与作战规模选择 UI（ui/src/MainMenu/：选择连排/营级与扮演节点、教程入口、新游戏/读档入口）
- [ ] T040 [P] [US1] 实现 2D 兵牌地图视图（ui/src/BattleMap/：平移/缩放、迷雾生效、兵牌渲染、识别档位信息与来源标注显示、最后已知状态）
- [ ] T041 [US1] 实现命令面板与三级校验提示（ui/src/CommandPanel/：点选/框选目标、类型/完成条件/优先级/时限、错误/警告/建议内嵌提示、全键盘可操作）
- [ ] T042 [US1] 实现暂停/加速与事件日志面板（ui/src/GameControls/ + EventLogPanel/：1x/2x/4x/8x 档位与暂停独立状态、日志回看/过滤/搜索、关键事件置顶）
- [ ] T043 [US1] 实现 UI↔核心互操作封装（ui/src/Interop/：P/Invoke 封装 wfs_sim_*、快照只读消费、命令注入为唯一写路径，见 contracts/sim-c-api.md）
- [ ] T044 [US1] 集成连排级闭环端到端验证（quickstart §3.1–3.3：同输入哈希一致、命令链路、战斗结算、无头 CLI 与 UI 状态哈希一致）

**Checkpoint**: 至此 User Story 1 可独立完整演示（MVP：基础战斗可玩、可暂停/加速、可存档读档基础路径）

---

## Phase 4: User Story 2 - 指挥链、支援请求与配属（Priority: P1）

**Goal**: 连排级有限分数支援与营级及以上完整配属链；战术分队编组与归建；派系资源池约束（SC-004、FR-005/008/009/010/016/046/050/073）

**Independent Test**: 一场包含支援请求的连排级任务独立测试"有限分数请求 → 编制资源池响应 → 扣分生效 → 任务结束归建"；营级"请求 → 上级评估 → 配属/拒绝/转请 → 归建"配属链单独测试

### Tests for User Story 2（宪法第 2 条强制，先写后实现）⚠️

- [ ] T045 [P] [US2] 支援请求链路集成测试（tests/cli_tests/test_support_chain.jsonl：连排级扣分明确生效、营级配属/拒绝/转请与归建事件序列，SC-004）
- [ ] T046 [P] [US2] 战术分队归建测试（tests/sim_tests/support_test.cpp：任务结束归建无单位丢失、损失/失联成员处置、拆分命令作用域）

### Implementation for User Story 2

- [ ] T047 [P] [US2] 创建支援请求/配属模型与状态机（sim/src/support.cpp：SupportRequest 实体、SUBMITTED→EVALUATING→EXECUTING/REJECTED、归建状态，见 data-model §14）
- [ ] T048 [P] [US2] 创建战术分队模型（sim/src/tactical.cpp：TacticalElement、拆分/合并、火力组命令作用域、解除战术编成恢复行政编制）
- [ ] T049 [US2] 实现资源池与有限分数计算（sim/src/resource_pool.cpp：编制资源池确定性读取、可用力量计算（AI 不可影响输入）、连排级分数扣减、请求范围约束）
- [ ] T050 [US2] 实现配属链仲裁与归建（sim/src/attach.cpp：（优先级,到达序列号）仲裁、配属/拒绝/转请、归建/重新配属、途中补给/维修处理）
- [ ] T051 [US2] 实现四种交互类型约束（sim/src/interaction.cpp：TASK_DISPATCH/EXECUTION/SUMMARY_REPORT/SUPPORT_REQUEST 强制枚举、同级经上级转发通道，见 command-schema §5）
- [ ] T052 [P] [US2] 创建派系模板基础数据（data/factions/faction-china.json、faction-nato.json、faction-russia.json：各层级资源池 + 审批层级基线 0/1/2，schema 校验通过）
- [ ] T053 [US2] 实现支援请求 UI（ui/src/SupportPanel/：目标/需求类型/支援种类选择、分数显示与扣减反馈、请求状态与结果提示）
- [ ] T054 [US2] 集成指挥链闭环验证（quickstart §3.6/§3.10：连排级与营级请求用例、派系差异指标可观察、SC-004/SC-009）

**Checkpoint**: 至此 User Story 1 与 User Story 2 均可独立工作，支援/配属闭环完整

---

## Phase 5: User Story 3 - 营级指挥（Priority: P2）

**Goal**: 玩家扮演营级指挥官指挥排级单位；参谋 AI 态势汇总与方案建议；下级摘要上报与信息权限；层级化情报同步；通信状态；火力任务与后勤规划；平民/设施联动（FR-028/029/037/049/051/053/074/075/077，SC-005/012）

**Independent Test**: 营级任务独立测试：玩家指挥多个排级单位完成一场营级战斗，验证情报汇总、任务下发、配属与后勤请求链路、火力任务收益、通信中断/失联

### Tests for User Story 3（宪法第 2 条强制，先写后实现）⚠️

- [ ] T055 [P] [US3] 情报权限验收测试（tests/cli_tests/test_intel_roles.jsonl：上级仅见摘要、下级完整状态、摘要快照语义、来源标注，FR-051/CHK159/162/165）
- [ ] T056 [P] [US3] 后勤闭环测试（tests/cli_tests/test_logistics.jsonl：送达/未送达反馈、按实际送达量结算、车队遇袭、补员冷却限制，SC-012/CHK055/057）

### Implementation for User Story 3

- [ ] T057 [P] [US3] 实现营级指挥节点与编制校验（sim/src/command_org.cpp：BATTALION 层级、直属班协调能力基数、指挥树无环/层级链校验、非法编制报错）
- [ ] T058 [P] [US3] 实现层级化情报同步（sim/src/intel_sync.cpp：连排 5s/营 15s 同步间隔、三类信息共用间隔、直属可指挥单位实时同步、最后已知状态）
- [ ] T059 [P] [US3] 实现摘要上报与信息权限裁剪（sim/src/summary.cpp：下级摘要（任务状态/完成度/损失摘要/支援需求）、统一裁剪规则、生成时刻快照语义）
- [ ] T060 [US3] 实现通信状态模型（sim/src/comm.cpp：通信装备/保障部队/地形与民用设施修正、通信范围判定、中断与失联取更严、恢复独立）
- [ ] T061 [US3] 实现火力任务系统（sim/src/fire_mission.cpp：预标定/未标定反应时间、诸元计算、散布圈与校射收敛、观察引导、有效打击阈值、观察单位失联退回预标定）
- [ ] T062 [US3] 实现后勤规划/执行链路（sim/src/logistics.cpp：请求→调度→延迟效果→反馈闭环、分层运输、车辆/人力模式、途中遇袭、伤员后送与补员冷却）
- [ ] T063 [US3] 实现参谋 AI（sim/src/ai/staff.cpp：态势汇总与态势报告、≤3 个方案建议、冲突以指挥官决策为准、探讨界面不改变模拟状态、连排级规模不实例化参谋 AI 与下级 AI 的配置断言（FR-053））
- [ ] T064 [US3] 实现平民/居民与设施联动（sim/src/civilian.cpp：聚居点/设施级抽象、断电连锁传播与恢复、居民协作、人道主义告知疏散任务 CIVILIAN_WARNING 注册、负面评价统计）
- [ ] T065 [P] [US3] 创建营级对抗剧本数据（data/scenarios/scn-battalion-v1.json：双方各一个营级单位群、5×5 km 地图、目标/失败条件/时间上限、炮兵与后勤资源、允许中途切换选项）
- [ ] T066 [P] [US3] 实现营级指挥 UI（ui/src/BattalionView/：排级单位视图、摘要/参谋面板、简报面板（周期/紧急视觉区分）、派系风格差异呈现）
- [ ] T067 [US3] 集成营级闭环验证（quickstart §3.6–3.8/§3.11/§3.12：配属链、后勤、火力任务收益、通信中断/失联、信息权限、迷雾复现）

**Checkpoint**: 至此营级指挥完整可用，User Stories 1–3 各自独立可测

---

## Phase 6: User Story 4 - 自定义、派系模板与分数（Priority: P2）

**Goal**: 战前自定义步兵班与载具；分数上限与三级超限；派系模板扩展与可视化微调面板；数据校验 CLI 工具（SC-008、FR-007/011/013/019/020/021，CHK113/122）

**Independent Test**: 单独测试编辑器流程：自定义步兵班与载具 → 触发三级超限规则 → 保存自定义派系模板并投入一场战斗

### Implementation for User Story 4（编辑器流程含自动校验测试，宪法第 2 条）

- [ ] T068 [P] [US4] 实现分数配置模型与三级超限（sim/src/score.cpp：ScoreConfig、L1≤110%/L2≤125%/L3>125%、分数汇总到营级、剧本双方总分差 ≤10% 校验与拒绝加载）
- [ ] T069 [P] [US4] 实现数据校验 CLI 工具（tools/src/DataValidator/：校验剧本/单位/存档数据、自定义载具兼容约束（底盘武器匹配/附件槽位/乘员载员匹配）、错误报告不崩溃）
- [ ] T070 [P] [US4] 实现步兵班编辑器（ui/src/Editors/SquadEditor/：士兵/班组武器/防护装备配置、分数实时显示、错误/警告/建议内联反馈链）
- [ ] T071 [P] [US4] 实现载具编辑器（ui/src/Editors/VehicleEditor/：底盘/武器/附加装甲/光电设备/乘员防护与训练、兼容约束校验、Level 3 禁止保存并给出原因）
- [ ] T072 [P] [US4] 实现派系模板扩展与微调面板（ui/src/Editors/FactionEditor/：资源池与指挥风格参数编辑、进攻/防御/协调倾向可视化、模板 schema 校验）
- [ ] T073 [US4] 实现编辑器与模拟集成（sim/src/editor_integration.cpp、data/scenarios/：保存配置→校验→投入战斗、派系模板/自定义单位在场景中生效，含 Level 1/2/3 行为自动化测试 tests/sim_tests/score_test.cpp）
- [ ] T074 [US4] 集成验证自定义闭环（quickstart §5 SC-008 映射：三级超限行为、模板保存投入战斗、自定义载具约束用例）

**Checkpoint**: 自定义/分数闭环完整，SC-008 验收达成

---

## Phase 7: User Story 5 - 旅级指挥（Priority: P3）

**Goal**: 旅级（与团等价的"最高战术指挥层"）指挥营级单位；旅级摘要同步与配属/后勤复用；旅级 AI 指挥官仅经营级下达命令；旅级剧本按 plan/spec 登记后置（FR-001/003/004/053，CHK212/225）

**Independent Test**: 旅级链路可独立测试：玩家向多个营级单位下发任务，营级 AI 指挥官接收并拆解执行，处理旅级配属请求闭环；v1 以合成测试场景验证，旅级正式剧本后置登记

### Implementation for User Story 5

- [ ] T075 [US5] 实现旅级最高战术指挥层抽象（sim/src/command_org.cpp：level=BRIGADE 复用同一指挥链实现、无团/旅特判逻辑，满足 FR-004/CHK212/225 架构约束）
- [ ] T076 [P] [US5] 实现旅级摘要同步与配属/后勤复用通道（sim/src/summary.cpp、sim/src/logistics.cpp：旅级 60s 同步间隔、旅级配属/转请/后勤规划链路复用验证）
- [ ] T077 [US5] 实现旅级 AI 指挥官（sim/src/ai/：旅级节点 AI 决策、仅向营级下发任务不直接操控营以下单位、关键事件触发）
- [ ] T078 [P] [US5] 创建旅级合成测试场景并登记后置项（data/scenarios/scn-brigade-test.json + docs/ROADMAP.md：旅级正式剧本 v1 后置登记，验收以合成场景执行）
- [ ] T079 [US5] 集成验证旅级链路（tests/cli_tests/test_brigade.jsonl：旅级向营级下发任务→营级 AI 拆解执行→旅级配属请求闭环；或按后置登记验收）

**Checkpoint**: 旅级架构接入完成，复用验证通过；正式旅级剧本按后置登记推进

---

## Phase 8: User Story 6 - AI 指挥官体系与无 AI 兜底（Priority: P3）

**Goal**: 事件驱动 AI 决策、周期/紧急简报、异步校验注入、云端 + 脚本双后端、熔断降级与自动切回、决策日志（SC-002/007、FR-026/036/037/039/052/068/069，宪法第 9/10/11 条）

**Independent Test**: 独立测试 AI 触发与兜底：触发接敌等关键事件观察对应层级决策；注入非法/超时输出验证拒绝/降级；无 AI 脚本模式完整运行一局战斗且结果确定

### Implementation for User Story 6（云端/熔断含自动化测试，宪法第 2 条）

- [ ] T080 [P] [US6] 实现云端 AI 后端（ui/src/Ai/CloudBackend.cs：OpenAI 兼容 REST、HttpClient、response_format json_schema 结构化输出、现实超时 15s）
- [ ] T081 [P] [US6] 实现 AI 事件触发引擎（sim/src/ai/trigger.cpp：FR-036 触发清单、态势突变量化边界、每节点 60 游戏秒频率上限、事件风暴合并去重、决策按指挥链顺序排队、按规模 AI 节点构成（FR-048/053））
- [ ] T082 [P] [US6] 实现 AI 决策输入裁剪与摘要生成（sim/src/ai/input.cpp：变化 ≤50 条/历史 ≤20 条/总 token ≤4000、统一裁剪规则、简报结构按 command-schema §4）
- [ ] T083 [US6] 实现周期/紧急简报系统（sim/src/ai/briefing.cpp：周期简报按配置游戏时间间隔、紧急简报由上级要求或关键事件触发、视觉区分、进入可回看事件日志）
- [ ] T084 [US6] 实现熔断/降级/自动切回（ui/src/Ai/CircuitBreaker.cs：校验失败或超时重试 1 次、连续 3 次失败熔断至脚本、健康检查连续 1 次成功自动切回、切换仅在无飞行中请求时允许）
- [ ] T085 [US6] 实现 AI 决策日志（sim/src/ai/decision_log.cpp：JSONL 每行 decision_id/node_id/trigger/game_tick/state_hash/prompt_ref/response/validation_result、滚动保留 20 场、按 decision_id/node_id/game_tick 查询、不落密钥与完整提示词明文）
- [ ] T086 [P] [US6] 创建版本化 AI 提示词与模型参数资产（data/ai/prompts/*.md、data/ai/models.json：各节点角色提示词、采样参数、摘要规则登记，随仓库版本管理）
- [ ] T087 [US6] 实现 AI 后端切换 UI（ui/src/Settings/：后端选择（cloud/script）、飞行中请求等待状态可见、暂停窗口内切换/存档约束）
- [ ] T088 [US6] 集成验证 AI 兜底与触发（quickstart §3.4：非法命令拒绝并降级、无 AI 整局运行、云端重试/熔断/自动切回、决策日志可回放与确定性复现）

**Checkpoint**: AI 指挥官体系与兜底完整，SC-002/SC-007 验收达成

---

## Phase 9: Polish & Cross-Cutting Concerns（收尾与横切）

**Purpose**: 影响多个用户故事的完善项：存档槽管理、结算页、可访问性、上手性、性能、可复现构建与文档

- [ ] T089 实现存档槽管理（sim/src/save_slots.cpp + ui/src/SaveSlots/：槽位上限 20、教程与剧本存档隔离、自动轮换 5 个不覆盖手动档、并发串行化、异常退出恢复入口、损坏槽位隔离恢复）
- [ ] T090 [P] 实现结算页（ui/src/SummaryScreen/：胜负结果、人员/载具/模块/设施/任务五类损失统计、任务完成度百分比、返回主菜单、无重开/读档、战后日志回看入口）
- [ ] T091 [P] 实现可访问性（ui/src/A11y/：全键盘操作与 Tab 导航/焦点可见、高对比与色弱友好标识、可调字号，CHK035）
- [ ] T092 实现操作手册与面板提示同源（docs/manual.md + ui/src/CommandPanel/hints 共享数据源、版本同步、手册滞后标注"手册待更新"、新手教程 5 分钟 SC-011 验收与自动化度量）
- [ ] T093 [P] 性能预算验证（tests/bench/：tick ≤5ms、内存 <512MB、事件日志写入 ≤tick 预算 5%、决策日志查询 ≤100ms、数据校验 ≤5s、UI 低配 ≥30fps 渲染降级且 tick 恒 20Hz）
- [ ] T094 [P] 复现构建验收（.github/workflows/ci.yml、scripts/：同一提交产物 SHA-256 一致、产物不嵌入时间戳、依赖锁定校验，CHK147）
- [ ] T095 执行 quickstart.md 全量验收（SC-001~SC-012 逐条映射、无头 CLI 与 UI 冒烟、语言边界依赖方向 CI 检查（核心无 UI 引用、快照只读/命令注入唯一写路径））
- [ ] T096 [P] 文档、代码清理与安全加固（docs/ 架构文档、文件级总览注释、死代码清理、无敏感信息入库、决策日志敏感字段边界复核）

---

## 补充任务（规格分析修复：FR-002/047/066/067/076/078）

> 以下任务按所属用户故事补入对应阶段执行（T097–T098 → US2；T099–T101/T103 → US3；T102 → US1）。

- [ ] T097 [US2] 实现 AI 上级命令确认交互（sim/src/command_ack.cpp + ui/src/CommandPanel/：命令送达后暂停模拟、玩家确认接受或上报替代方案、附带加强/支援请求、上级未采纳按原命令执行，FR-047）
- [ ] T098 [US2] AI 上级命令确认链路测试（tests/cli_tests/test_superior_order_ack.jsonl：送达→暂停→接受/替代方案→执行，含附带请求与未采纳分支，FR-047）
- [ ] T099 [P] [US3] 实现雷场与工兵专业行动（sim/src/minefield.cpp：雷场/障碍统一通行数据模型、三种发现途径（观测布雷/触发标记疑似/工兵扫描）、"疑似雷场"禁止通行、工兵排雷/破障/紧急抢修，测试 tests/sim_tests/minefield_test.cpp，FR-076）
- [ ] T100 [P] [US3] 实现战场抢修（sim/src/repair.cpp：修复模块损伤与未摧毁载具击穿、无法恢复未受损状态、彻底修复需维修设施/车辆，测试 tests/sim_tests/repair_test.cpp，FR-078）
- [ ] T101 [US3] 实现油料与给养补给（扩展 sim/src/logistics.cpp：弹药/油料/给养三类按缺额计算补充量与耗时、油料耗尽无法机动、给养长期缺乏降低作战状态，并入 T056/T062 验收，FR-066）
- [ ] T102 [P] [US1] 实现构筑工事系统（sim/src/fortify.cpp：构筑时间、效果叠加、依托建筑构筑、专用工事"适用武器类别"，测试 tests/sim_tests/fortify_test.cpp，FR-067）
- [ ] T103 [P] [US3] 实现中途切换扮演节点机制（sim/src/node_switch.cpp：切换原子操作、原节点立即 AI 接管、新节点情报按 FR-028 同步与授权/任务摘要交接，测试 tests/cli_tests/test_node_switch.jsonl，FR-002）

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup（Phase 1）**: 无依赖，可立即开始
- **Foundational（Phase 2）**: 依赖 Setup 完成 —— **阻塞全部用户故事**
- **User Stories（Phase 3+）**: 全部依赖 Foundational 完成；故事间可并行（人员充足时）或按优先级顺序（P1 → P2 → P3）推进
- **Polish（Phase 9）**: 依赖所有纳入 v1 验收的用户故事完成

### User Story Dependencies

- **User Story 1（P1）**: Foundational 后可开始，无其他故事依赖 —— MVP
- **User Story 2（P1）**: Foundational 后可开始；复用 US1 命令链路与 Mission 模型，但可独立测试（支持请求脚本直接驱动）
- **User Story 3（P2）**: 依赖 US1 核心闭环与 US2 配属链，以及 Foundational 的 IAiBackend 抽象/脚本后端（参谋 AI 与下级 AI 指挥官运行于其上）
- **User Story 4（P2）**: 依赖 US1 单位/战斗模型；编辑器流程可独立测试
- **User Story 5（P3）**: 依赖 US3 营级机制（指挥链/摘要/配属/后勤复用）；旅级正式剧本后置
- **User Story 6（P3）**: 依赖 US1 命令校验链路与 Foundational 的注入通道/脚本后端；云端客户端独立，完整集成验证在 US3 之后

### Within Each User Story

- 测试（若包含）必须先行编写并确认 FAIL 后再实现（宪法第 2 条）
- 模型 → 服务 → 接口/UI → 集成
- 故事内核心实现先于集成验证
- 每个 Checkpoint 停下独立验证该故事

### Parallel Opportunities

- Setup：T004/T005/T006 可并行
- Foundational：T008/T009、T015 可并行（同阶段其他任务同时进行）
- US1：测试 T023/T024 并行；模型 T025–T028 并行；数据 T037/T038 与 UI T039/T040 并行
- US2：测试 T045/T046 并行；模型 T047/T048 并行
- US3：测试 T055/T056 并行；模型 T057–T059 并行；T065/T066 并行
- US4：T068–T072 编辑器与模型可并行
- US5：T076/T078 可并行
- US6：T080–T082、T086 可并行
- Polish：T090/T091/T093/T094/T096 可并行
- 补充任务：T099/T100/T102/T103 可并行（不同文件）；T101 与 T062 同改 sim/src/logistics.cpp 须串行；T097→T098 依序（实现→测试）
- 不同用户故事之间在 Foundational 后可并行（由不同团队成员承担）

---

## Parallel Example: User Story 1

```bash
# 先并行编写测试（确认 FAIL）：
Task: "命令链路集成测试 in tests/cli_tests/test_command_chain.jsonl"
Task: "战斗结算黄金测试 in tests/sim_tests/golden/combat_golden.cpp"

# 再并行创建模型：
Task: "创建指挥节点/编制/最小可指挥单位模型 in sim/include/wfs/sim/model/"
Task: "创建士兵/班组/载具/武器/弹药模型 in sim/include/wfs/sim/model/combat.h"
Task: "创建地形/设施/工事/环境模型 in sim/include/wfs/sim/model/terrain.h"
Task: "创建任务模型与任务类型注册表 in sim/src/mission_registry.cpp"
```

## Parallel Example: User Story 3

```bash
Task: "情报权限验收测试 in tests/cli_tests/test_intel_roles.jsonl"
Task: "后勤闭环测试 in tests/cli_tests/test_logistics.jsonl"

Task: "营级指挥节点与编制校验 in sim/src/command_org.cpp"
Task: "层级化情报同步 in sim/src/intel_sync.cpp"
Task: "摘要上报与信息权限裁剪 in sim/src/summary.cpp"
```

---

## Implementation Strategy

### MVP First（User Story 1 Only）

1. 完成 Phase 1: Setup
2. 完成 Phase 2: Foundational（CRITICAL —— 阻塞所有故事）
3. 完成 Phase 3: User Story 1（连排级战斗指挥闭环）
4. **STOP and VALIDATE**: 以 quickstart §3.1–3.3 独立验证 US1（确定性哈希、命令链路、战斗结算）
5. 交付可演示 MVP

### Incremental Delivery

1. Setup + Foundational → 确定性核心就绪
2. +User Story 1 → 独立测试 → 演示（MVP）
3. +User Story 2 → 指挥链/配属闭环 → 演示
4. +User Story 3 → 营级指挥 → 演示
5. +User Story 4 → 自定义/分数 → 演示
6. +User Story 5（架构接入 + 合成场景）→ 演示
7. +User Story 6 → AI 指挥官与兜底 → 演示
8. Polish → v1 发布（GitHub Release，带版本号）

### Parallel Team Strategy

1. 团队共同完成 Setup + Foundational
2. Foundational 完成后：
   - 开发者 A：User Story 1
   - 开发者 B：User Story 2
   - 开发者 C：User Story 3/4（依赖 US1 完成后接入）
   - 开发者 D：User Story 6 云端后端（独立文件）
3. 各故事独立完成并集成

---

## Notes

- [P] 任务 = 不同文件、无未完成依赖；同一文件的任务串行
- [Story] 标签映射 spec.md 用户故事，保证可追溯
- 每个用户故事必须独立可完成、可测试（测试来自 quickstart 对应场景与宪法第 2 条）
- 实现前确认测试 FAIL；实现后测试通过再进入下一任务
- 每任务或逻辑组完成后以 Conventional Commits 提交，PR 经审查合并（宪法第 3/4 条）
- 侦察类任务判定机制（CHK064）与旅级正式剧本（CHK 后置项）为显式登记待办，不阻塞 SC-009/SC-010 验收
- 跨语言边界只传递纯数据（C ABI/JSON）；UI 快照只读、命令注入唯一写路径（宪法第 14 条）
- 避免模糊任务、同文件冲突、破坏故事独立性的跨故事依赖
