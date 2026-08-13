# T054（US2 集成指挥链闭环验证）审查记录 R1

- 审查对象：分支 `feature/us2-integration`（tip `a7d79e7`）
- 增量范围：`git diff feature/us2-support-ui...feature/us2-integration`（提交 `62642c1`/`724836b`/`8bc06a7`/`a7d79e7`，10 个文件 +561/−5）
- 审查方式：只读静态审查（仅 `git show`/`git diff`/`git log` 读对象库；未修改任何项目文件）
- 测试依据：实现代理报告 Release/Debug CTest 380/380、dotnet 218/218（本次未重新执行，静态核对与报告无矛盾）
- 结论：**PASS（置信度高）**

## 1. 派系选择通道（重点 1）——PASS

- 接线链路成立：`SupportConfig::FromScenario` 读取场景 `support.faction_id`（support.h）→ `initialize_support_state` 以 `<data_root>/factions/<faction_id>.json` 加载模板并做 JSON Schema + 语义 + data/units 交叉校验（support_runtime.cpp:240-246；faction.cpp）→ 取 `battalion` 池的 `support_score` 作为连排级有限分数总额、`entries` 作为可用力量（support_runtime.cpp:247-254）。
- 三派系场景已实测一致：去除 `id`/`name`/`faction_id` 后三个场景 JSON 完全相等（同脚本可跨派系复跑）。
- 非法 faction_id：运行时会产生结构化事件 `SUPPORT_CONFIG_INVALID faction=<id> reason=...`（support_runtime.cpp:241-244），UI 也映射为“支援配置无效”。但存在两个缺口，见 F2。
- schema/契约：faction 模板侧 schema 存在且校验通过（faction.schema.json）；场景侧 `support` 对象未进入 scenario.schema.json（`additionalProperties: true`），见 F2。

## 2. 差异指标断言真实性（重点 2）——PASS

逐一对照数据与代码，测试中的数字均非凑断言：

| 指标 | 测试断言 | 数据/代码出处 | 核对 |
| --- | --- | --- | --- |
| 审批层级基线 | 0/1/2 | faction-china/nato/russia `approval_level` | 一致 |
| 审批转发延迟 | 0/10/20 tick | `approval_level × approval_step_ticks`（默认 10；support.h、support_runtime.cpp:135-137） | 一致 |
| 评估→裁决间隔 | 20/30/40 | `evaluation_delay_ticks(20) + approval_delay`（support_runtime.cpp:137） | 一致 |
| 营级池分数总额 | 60/55/45 | 三模板 battalion `support_score` | 一致 |
| 迫击炮成本 | 30/30/40 | 三模板 battalion entries `squad-mortar-team.cost` | 一致 |
| 扣分合计/最终剩余 | 45/30/40；15/25/5 | 池总额 − 全部 SUPPORT_ASSIGNED 扣分合计（60−30−15、55−30、45−40） | 一致 |
| cmd-3 hmg | 中国可配属（cost=15）；北约/苏俄 SCOPE_VIOLATION | 仅中国 battalion 池含 `squad-hmg-team`；`adjudicate` 范围检查优先（attach.cpp） | 一致 |
| cmd-2 拒绝原因 | 中/北约 INSUFFICIENT_SCORE、苏俄 INSUFFICIENT_AVAILABLE | 中/北约 atgm 可用 3/2 ≥ 2 但成本 2×25=50 > 剩余 30；苏俄 atgm 数量 1 < 2；`adjudicate` 顺序为 范围→可用→分数 | 一致 |
| 三派系哈希两两不同 | 3 个唯一哈希 | 事件含 faction/approval_level/approval_delay 且池差异落入状态 | 成立 |
| 同派系 threads 1/2 一致 | 哈希 + 事件序列一致 | `serialize_state_json` 显式排除 threads；RNG 种子固定（state_serialization.h、headless_driver.cpp） | 成立 |

## 3. quickstart §3.6/§3.10 与实现一致（重点 3）——PASS（一处映射缺口见 F1）

- §3.6 两条命令（scn-support-platoon + test_support_chain.jsonl；scn-support-battalion + empty_script.jsonl）与 CLI 参数语义一致（main.cpp：`--script`/`--out`/`--hash`/`--threads`/`--ticks`；inject 会跳过 `#` 注释行），涉及文件均存在于分支。
- 预期结果准确：连排级 cmd-1 `score_cost=30 score_remaining=30`、cmd-2 `INSUFFICIENT_SCORE`、归建序列；营级 assign/reject（SCOPE_VIOLATION）/escalate（NO_SUPERIOR_ESCALATION）均与代码路径一致。
- §3.10 三命令与 run_faction_chain_tests.ps1 断言一致；全部数字与模板/代码一致。
- SC-004 映射明确（quickstart.md:55,61,69）；SC-009 未显式映射（见 F1）。

## 4. 新问题排查（重点 4）——PASS（两处注意点见 F3/F4）

- 命令延迟与裁决顺序：最终剩余分数按“池总额 − 全部 ASSIGNED 扣分合计”断言，与裁决顺序无关（run_faction_chain_tests.ps1:263-272），设计稳健；各请求序列断言按 request id 隔离，稳健。
- 唯一时序耦合在归建序列断言（见 F3）：当前参数下余量充分且 seed 固定，测试确定不抖动。
- SUPPORT_EVALUATING 新增字段放在 `resolve_tick` 之前：既有 CLI 测试均用前缀正则（`^SUPPORT_EVALUATING request=...`），UI 用 `StartsWith` + 字段提取（SupportStatusMapper.cs:150），均不受影响；sim 单测使用结构体字段而非日志文本。UI 测试夹具字符串仍为旧格式，仅风格性不一致（F4）。

## 5. Findings

### 🟡 F1：quickstart 未显式映射 SC-009
- 位置：specs/001-war-sim-command-battle/quickstart.md:55（§3.6 头仅 SC-004）、:93（§3.10 头仅 FR-006），全文检索无 SC-009。
- 原因：T054 验收要求“SC-004/SC-009 映射明确”；SC-009（连排级与营级均能完成至少一场标准任务）实际上由 §3.6 的连排级 + 营级两个用例承载，但没有标注。
- 建议：在 §3.6 或 §3.10 补充一句“连排级与营级各完成一场标准任务（SC-009）”的显式映射。

### 🟡 F2：场景侧 support 契约未进 schema，非法 faction_id 无测试覆盖
- 位置：contracts/schemas/scenario.schema.json:225（`additionalProperties: true`，全文未定义 `support` 对象）；native/sim/src/support_runtime.cpp:232-233（faction_id 为空时静默返回，无事件）、:240-244（未知 id 仅记事件，CLI 仍以退出码 0 继续）。
- 原因：`support.faction_id` 依赖运行时加载路径兜底，Schema 层不拦截拼写错误；本增量（run_faction_chain_tests.ps1）只覆盖三个合法派系，`git grep` 确认没有任何测试断言 `SUPPORT_CONFIG_INVALID`。
- 影响：非本增量引入（属 T052/T053 既有契约缺口），但与 T054“schema/契约同步、非法 faction_id 结构化报错”的核对项直接相关。
- 建议：a) 在 scenario.schema.json 增加 `support` 对象定义（faction_id、scale、节点、延迟、scripted_requests）或独立契约文档；b) 补一条 CLI/单测断言非法 faction_id → `SUPPORT_CONFIG_INVALID` 且无 SUPPORT_* 请求事件；c) 空 faction_id 也考虑记录结构化事件，避免静默未配置。

### 🟡 F3：归建序列断言隐含时序假设
- 位置：native/tests/cli_tests/run_faction_chain_tests.ps1:274-280。
- 原因：断言要求 `SUPPORT_ASSIGNED req-cmd-1` 严格先于 `MISSION_COMPLETED`。系统不变量只是“已配属且关联命令完成后触发归建”；若通讯延迟/移动速度/容差配置变化（当前 60–200 tick 延迟 vs 最早约 343 tick 完成移动，余量约 100 tick；seed 固定），`MISSION_COMPLETED` 可能先于 `ASSIGNED`，断言会误报失败。
- 建议：拆成两条独立序列（`ASSIGNED → RETURNING → RETURNED`；`MISSION_COMPLETED → RETURNING`），不要求 ASSIGNED 与 MISSION_COMPLETED 的相对顺序。

### 💭 F4：UI 测试夹具仍是旧格式 SUPPORT_EVALUATING
- 位置：app/tests/ui_tests/WarFictionSim.Ui.Tests/SupportPanel/SupportPanelViewModelTests.cs:256,266,287；SupportStatusMapperTests.cs:34,48,134,138,189。
- 原因：夹具字符串缺少新增的 `faction`/`approval_level`/`approval_delay` 字段；功能无影响（前缀/字段提取解析，dotnet 218/218 通过），但与运行期格式漂移。
- 建议：同步夹具为新格式，或注明“最小兼容格式”以免后续按夹具写精确匹配。

### 💭 F5：faction.schema.json 的 support_score 非必填
- 位置：contracts/schemas/faction.schema.json（resource_pools 条目仅 required `support_kinds`/`entries`，`support_score` 可选，缺失默认 0）。
- 原因：属 T052 既有契约弱化，本增量未引入；T054 三模板均显式声明，无实际影响。可留待契约收紧时一并处理。

## 6. 正面评价

- 三派系场景仅 `id`/`name`/`faction_id` 不同，脚本完全复用，改动最小且可复跑。
- “池总额 − 扣分合计”式断言刻意规避裁决顺序，是稳健测试设计的好范例。
- SUPPORT_EVALUATING 新增可观察字段放在 `resolve_tick` 之前且全部为键值对，对既有前缀/字段解析零破坏。
- 确定性链路（哈希排除 threads、RNG 固定、脚本注释行处理）与 threads 1/2 一致性断言自洽。

## 7. 建议的后续动作

1. 合并前处理 F1（一行文档补充）与 F3（序列断言加固），成本低。
2. F2 可随 US3 契约工作一并补齐（schema + 非法路径测试），不阻塞本 PR 合并。
3. F4/F5 为非阻塞风格/契约事项，择机处理。
