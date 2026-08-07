# Quickstart: 战争幻想模拟器 —— 无头验证与运行指南（Phase 1 输出）

> 本指南验证 v1 核心（营级对抗 5×5 km + 连排级战斗）端到端可运行；实现细节见 `tasks.md`。章节编号与 tasks 引用一致。

## 1. 前置

- Windows 10/11；MSVC（C++20）、CMake ≥ 3.25、vcpkg（清单模式）、.NET 10 SDK
- 依赖锁定：`vcpkg.json` + baseline（nlohmann-json、GoogleTest、PCG32）

## 2. 构建

```bash
cmake -B build -S . -DVCPKG_MANIFEST_MODE=ON
cmake --build build --config Release
dotnet build ui/ -c Release
```

## 3. 无头验证（核心门禁）

### 3.1 黄金确定性（SC-001）

```bash
sim_headless run --scenario data/scenarios/scn-battalion-v1.json --seed 42 --out a.jsonl --hash
sim_headless run --scenario data/scenarios/scn-battalion-v1.json --seed 42 --out b.jsonl --hash
# 两次 --hash 输出必须一致
```

### 3.2 命令链路（FR-040/043/045/046）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/command_chain.txt
# 事件序列 COMMAND_ISSUED → COMMAND_ACKNOWLEDGED → UNIT_MOVING → MISSION_COMPLETED；
# 连排级 3–10s 通讯延迟时间戳比对；含撤回、批量部分接受
```

### 3.3 战斗结算黄金样例（FR-054–065）

由黄金测试套件覆盖：`tests/sim_tests/golden/combat_golden.cpp`（命中/伤害/压制/失联固定种子逐字段一致；自动目标选择与选弹黄金样例），无头命令见 §3.1。

### 3.4 AI 兜底与触发（FR-026/036/068/069，SC-002/007）

```bash
sim_headless run --scenario data/scenarios/scn-battalion-v1.json --seed 42 --ai-backend script --out full.jsonl
# 无 AI 整局运行且结果确定；非法/超时输出拒绝或降级；云端熔断/切回为手动验证（不进 CI）
```

### 3.5 失联与情报同步（FR-032/065）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/contact_loss.txt
# 失联→最后已知状态→恢复逐级延迟；恢复时间与压制恢复能力相关
```

### 3.6 支援与配属链（FR-008–010，SC-004）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/support_chain.txt
# 连排级有限分数请求→扣分生效→归建；营级配属/拒绝/转请→归建
```

### 3.7 后勤闭环（FR-073–075/078，SC-012）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/logistics.txt
# 弹药/油料/给养按缺额补充；伤员后送与补员冷却；送达/未送达反馈；战场抢修
```

### 3.8 火力任务（FR-060/074）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/fire_mission.txt
# 预标定/未标定反应时间、散布圈与校射收敛、观察引导收益、有效打击阈值
```

### 3.9 平民与设施联动（FR-024）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/civilian_facility.txt
# 断电按电网连接传播、居民协作、撤离车队、人道主义疏散告知任务
```

### 3.10 派系差异（FR-006）

由 `tests/cli_tests` 派系差异用例验证：中国合成营（内置坦克/炮兵）与苏俄营级（需向上申请）资源池差异在配属链测试中可观察（T045/T052）；各派系场景均确定性可复现。

### 3.11 信息权限（FR-051/053，SC-005）

```bash
sim_headless inject --scenario data/scenarios/scn-battalion-v1.json --seed 42 --script scripts/intel_roles.txt
# 上级仅见摘要、下级完整状态；来源标注（直属/同级经上级/层级）100% 可见
```

### 3.12 迷雾复现（FR-033–035，SC-006）

```bash
sim_headless run --scenario data/scenarios/scn-battalion-v1.json --seed 42 --out fog.jsonl
# 未目视敌单位不可见；丢失目视保留最后已知状态与最后动向
```

## 4. UI 运行

```bash
dotnet run --project ui/
```

启动后进入营级战斗剧本（玩家可选连排级或营级扮演）或连排级新手教程；命令面板内嵌提示与校验反馈（SC-011）。

## 5. 自定义与分数闭环（US4，SC-008）

```bash
dotnet run --project tools/src/DataValidator -- data/ --all
# 编辑器流程：自定义步兵班/载具 → 三级超限（L1≤110%/L2≤125%/L3>125%）→ 保存派系模板 → 投入战斗
```

## 6. 预期结果

- 所有无头命令以 0 退出码完成，事件 JSONL 输出完整；
- 黄金确定性、1 vs N 线程哈希一致（CI 门禁）、存档往返全部通过；
- 非法数据/非法命令被明确拒绝并报错（非崩溃）；
- UI 以 ≥60 fps 渲染（低配允许降级至 ≥30 fps），模拟 tick 不因 AI 决策而阻塞。

## 7. 参考

- 契约：`contracts/command-schema.md`、`contracts/sim-c-api.md`、`contracts/save-format.md`
- 数据模型：`data-model.md`
