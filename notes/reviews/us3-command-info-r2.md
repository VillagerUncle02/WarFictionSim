# US3 第一组第 1 轮修复 · 第 2 轮轻量复审记录

- 仓库：VillagerUncle02/WarFictionSim
- 分支：feature/us3-command-info
- 复审基线：8c093bf（US3 第一组基线）
- 复审对象（8 条提交）：
  - 3b9a7be R1 指挥树构建顺序无关
  - 7d47d4e S1 摘要按编制子树聚合
  - 3094415 S2 relay 保留并上卷 T033 F5 识别档位字段
  - 9268537 S3 IntelRecord F5 三字段空值省略（旧存档字节往返）
  - c5979c6 S4 通信范围公式口径统一
  - fcab2a3 S5 节点链路健康绑定通信保障部队
  - 40427f6 S6 command_org 校验分支负例测试
  - e8648fd 格式整理（clang-format）
- HEAD：e8648fd（file:line 均以此为准）
- 审查方式：只读静态复审，仅 git show/git diff/git log 读对象库；未修改任何文件，未运行测试

## 结论

**总评：PASS（置信度约 0.9）。** 七条修复均真实闭环，未发现本轮引入的 🔴 阻断项或 🟡 建议项；仅记录 3 条 💭 备注（见文末）。新增 11 个 TEST 用例与 S2 追加的 6 条断言均静态核实为"真实命中"（非恒真/恒假）。data-model §18 三处登记与代码口径一致。

测试执行证据（修复代理报告，本次未复跑）：Release/Debug CTest 412/412、dotnet 218/218；与静态核对一致（11 个新 TEST 用例与既有 401 项合计 412）。

## 逐条核实

### R1 指挥树构建顺序无关 —— PASS（0.95）

- 实现：`CommandTree::from_json` 改为两阶段——先剥离 parent_id 全量 `AddNode`，再按声明顺序统一 `SetParent` 接线（[command_node.h](/D:/Workbench/Agent/NewProject/native/sim/include/wfs/sim/model/command_node.h:302)）；`SetParent` 拦截未知父/自环/成环（[command_node.h](/D:/Workbench/Agent/NewProject/native/sim/include/wfs/sim/model/command_node.h:262)）。
- 正序：既有测试（`LoadsValidHierarchyAndMapsEchelons`、模型测试 `JsonRoundTripPreservesTree`）继续通过；反序：新测试 `LoadsChildBeforeParentInInsertionOrder`（[command_org_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/command_org_test.cpp:116)）以"子节点声明在前"重排后断言加载成功且插入顺序保持声明顺序。
- 非法树：`validate_command_org` 的父存在性/环检测基于 `node_ids` 集合与 `parent_links` 映射，天然顺序无关；`load_command_org` 先校验后构建（[command_org.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/command_org.cpp:302)），环在校验期产出结构化 `COMMAND_ORG_NODE_CYCLE` 并提前返回，不进入会抛异常的构建路径；新测试 `IllegalTreeReportsStructuredIssuesWithoutThrowing`（[command_org_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/command_org_test.cpp:134)）锁定。
- 存档路径：`CommandOrgState::from_json` 抽取 `node` 子对象后复用同一 `CommandTree::from_json`（[command_org.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/command_org.cpp:340)），与 `load_command_org` 同构；既有 `RoundTripsThroughJson` 与模型测试的重复 id/未知父 `EXPECT_THROW` 语义不变。

### S1 摘要子树聚合 —— PASS（0.95）

- 实现：`NodeSubtree` 按指挥树 `children_of` 做确定性 BFS（[summary.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/summary.cpp:61)）；损失、任务结果、执行中任务均改为 `subtree.contains(unit.node_id)` 判定（[summary.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/summary.cpp:217)）。支援需求仍按 `request.from_node == from_node` 计自身未决，与文档一致。
- 不泄露明细：`SummaryReport` 仅含聚合数值字段；`build_authorized_view_json` 的下级条目只放摘要、不放 units/x/soldiers，新测试 `BattalionViewContainsCompanySummaryWithoutUnitInternals`（[summary_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/summary_test.cpp:166)）双向锁定。
- 断言真实性：场景中 `plt-1-sq-1` 挂 `node-plt-1`（`node-co-1` 的下级，见 scn-intel-roles.json），`build_summary(state,"node-co-1",...)` 旧逻辑（仅直属）会得到 0 损失/0 结果；新测试期望 2/1/1/1，只有子树聚合才能通过，非恒真。

### S2 relay 上卷 F5 三字段 —— PASS（0.9）

- `register_intel` 更新分支同步写入 `observed_count/type_name/composition`（[intel.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel.cpp:280)）；`MergeChildIntel` 转发时随 tier 一并复制（[intel_sync.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel_sync.cpp:104)），sync/relay 每一跳保留。
- 断言真实性：修复前连/营记录三字段恒为 0/空，`HierarchySyncIntervalsAndSourceAnnotation` 追加的 `EXPECT_GT(company->observed_count,0)`、营级同理断言（[intel_sync_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/intel_sync_test.cpp:70)）修复前必失败。

### S3 F5 空值省略与旧存档字节往返 —— PASS（0.95）

- `to_json` 对三字段按"非缺省才输出"策略（[intel.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel.cpp:215)），与 `IntelSource.level` 同构；`from_json` 用 `.value(...)` 缺省回填（[intel.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel.cpp:240)），旧存档（无三键）可加载且回写字节不变。
- 新测试 `LegacyRecordSerializationOmitsDefaultF5Fields`（[intel_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/intel_test.cpp:168)）断言缺省省略 + 两次 `dump()` 逐字节相等 + 非缺省仍输出；全仓库仅此一处读写三字段，无其他消费方受影响。

### S4 通信范围公式口径 —— PASS（0.95）

- 代码实现本就含 `base_range_km * power_factor * power_scale`（[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:280) `effective_comm_range_km`）；本次修正的是 comm.h/comm.cpp 注释与 §18 中漏写 `base_range_km` 乘项的口径偏差，并新增公式级单测 `RangeFormulaIncludesBaseRangeMultiplier`（[comm_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/comm_test.cpp:100)）钉住 8×1×1=8、8×2×1=16 两个基准点。

### S5 节点链路绑定保障单位 —— PASS（0.9）

- 载体解析：`node_carrier_unit` 取该节点 `comm_role=support` 的首个单位，无则 `nullptr`（[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:172)）；`NodeEndpoint` 载体绑定健康、普通单位仅作位置代理（[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:145)）；`link_effective` 只对 `binds_health` 端点判 destroyed/out_of_contact（[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:343)），`step_comm` 同理（[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:417)）。
- `ChildNodeLost` 改用载体判定，无载体时按"全部直属单位失联/摧毁"判定（[intel_sync.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel_sync.cpp:61)），与 §18 登记一致。
- 断言真实性：场景单位顺序 `plt-1-sq-1`（普通）在前、`plt-1-sq-2`（被赋 support）在后；修复前节点端点绑定首个普通单位，摧毁 `plt-1-sq-1` 即断链，新测试在摧毁普通单位后断言链路仍生效、摧毁保障单位后 `ENDEPOINT_DISABLED`（[comm_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/comm_test.cpp:196)），修复前必失败。

### S6 校验负例 —— PASS（0.95）

- 四个新测试分别命中真实校验分支：`COMMAND_ORG_ORGANIZATION_MISMATCH`（互反不一致）、`COMMAND_ORG_ORGANIZATION_LINK_INVALID`（编制成环，SetParent 拦截）、`COMMAND_ORG_NODE_DUPLICATE_ID` + `COMMAND_ORG_ORGANIZATION_DUPLICATE_ID`、`COMMAND_ORG_NODE_INVALID`（squad 层级被 `IsCommandEchelon` 拒绝），与 `validate_command_org` 中的 issue 码一一对应（[command_org_test.cpp](/D:/Workbench/Agent/NewProject/native/tests/sim_tests/command_org_test.cpp:171) 起）。

### e8648fd 格式整理 —— PASS

- 纯 clang-format 空白/换行调整，无逻辑改动。

## 测试命中核对（新增 11 项）

| 提交 | 新增 TEST 用例 | 是否真实命中 |
| --- | --- | --- |
| 3b9a7be | LoadsChildBeforeParentInInsertionOrder、IllegalTreeReportsStructuredIssuesWithoutThrowing | 是（顺序无关与非法树提前返回路径） |
| 7d47d4e | BuildSummaryRollsUpSubtreeLossesAndOutcomes、BattalionViewContainsCompanySummaryWithoutUnitInternals | 是（连→营上卷与权限视图不泄露） |
| 9268537 | LegacyRecordSerializationOmitsDefaultF5Fields | 是（缺省省略 + 逐字节往返） |
| c5979c6 | RangeFormulaIncludesBaseRangeMultiplier | 是（公式基准钉住） |
| fcab2a3 | NodeLinkHealthBindsSupportUnitNotOrdinaryUnits | 是（普通单位与保障单位差异化） |
| 40427f6 | RejectsOrganizationParentSubordinateMismatch、RejectsOrganizationCycle、RejectsDuplicateNodeAndOrganizationIds、RejectsSquadCommandNode | 是（四个 issue 分支） |

合计 11 个新 TEST；另 S2（3094415）在既有 `HierarchySyncIntervalsAndSourceAnnotation` 追加 6 条 F5 字段断言（连级 3 + 营级 3），同样真实命中。412/412 与"新增 11 项"的算术自洽。

## data-model §18 三处登记核对

| 登记项 | 位置 | 与代码一致 |
| --- | --- | --- |
| FR-051 摘要子树聚合、支援需求取自身未决、字段不含单位明细 | data-model.md:136 | 一致（summary.cpp NodeSubtree / support_requests / SummaryReport 字段） |
| FR-077 范围公式（含 base_range_km 乘项）+ 节点载体语义（首个 support 单位、普通单位不承载健康、无保障部队按全部直属判定） | data-model.md:144 | 一致（comm.cpp effective_comm_range_km / node_carrier_unit / ChildNodeLost） |
| T033 F5 三字段观察时填充并随快照输出 | data-model.md:155 | 一致（intel.cpp 观察填充 + 条件序列化，非缺省必输出） |

## Findings（本轮无新 🔴 / 🟡，仅 💭）

- 💭 S5 语义延伸：摧毁保障载体同时会使该节点的全部 unit↔node 链路降级（所有单位链路的节点侧端点都绑定载体健康）。这是"载体=节点通信枢纽"语义的自然结果，且该爆炸半径与修复前"摧毁首单位断全部"一致（只是触发对象从普通单位换成载体）；但 §18 只字面写了"节点链路"，建议补一句明确 unit↔node 链路同样受载体影响，避免误解。涉及 [comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:145)、[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:343)、data-model.md:144。
- 💭 S2+S3 覆写风险：`register_intel` 在 `last_seen_tick >= target.last_seen_tick` 分支无条件以入站值覆写 F5 三字段；若未来出现不填充 F5 的生产者（或旧代码路径）产生更新，可能清空上级已识别的数量/类型/构成。当前所有观察路径（ObservePairWithIndex）都填充，实际风险低；建议后续改为"非空才覆写"或与 tier 升级联动。涉及 [intel.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel.cpp:280)。
- 💭 既有注释与实现表述不一致（非本轮引入）：`ChildNodeLost` 中"无实体单位视为未失联"的返回分支（`has_unit && !any_operational` 取 false）实际不可达——无单位子节点的 kNode 链路在 `link_effective` 因端点无单位即判 false，先于该分支返回 true。行为与 S5 修复前一致，仅建议修正注释或补齐该边界测试。涉及 [intel_sync.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/intel_sync.cpp:61)、[comm.cpp](/D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:343)。

## 复核限制

- 本次为静态只读复审，未复跑 CTest/dotnet；412/412 与 218/218 为修复代理报告的执行证据，静态核对（新增用例数、断言非恒真、分支可达）与之自洽。
- 第 1 轮审查记录（notes/reviews/us3-command-info-r1.md）为工作区未跟踪文件，按"只读对象库"约束未读取；R1/S1–S6 的原始要求以本次任务描述与各提交 message 为准。
