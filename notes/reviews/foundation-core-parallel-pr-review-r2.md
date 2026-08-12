# AI PR 审查记录 — PR #108（feature/foundation-core-parallel，T015–T018）r2

- 分支：`feature/foundation-core-parallel`
- PR：https://github.com/VillagerUncle02/WarFictionSim/pull/108
- 本轮 head：`fdb8241`（build(native): keep sim_core C ABI DLL target after layout rebase）
- 审查时间：2026-08-13
- 审查方式：只读代码审查（git diff/show/log），未修改任何源码，未 push/merge/approve
- 结论：**PASS（置信度 0.90）**，1 条 🟡、6 条 💭，无 🔴

## 1. 审查范围

- 任务：T015 事件日志（环形 5000/分类/严重级/过滤/搜索）、T016 快照与 SHA-256、
  T017 存档序列化与迁移骨架、T018 确定性分区并行框架，及对应 tests；
  另含 T017 支撑的 `queue.h/.cpp` 的 `restore_next_seq` 与 `c_api` 中
  `get_state_hash/save/load_save` 从 NOT_IMPLEMENTED 落地。
- 提交：`feature/foundation-core-abi...feature/foundation-core-parallel`（三点 diff，
  merge-base = 8760c0b），共 26 个文件、+2699/-43。重点核对最新提交 fdb8241。
- 前序 PR #106/#107 内容（T008–T014、loader/schema、rng/ci 等）不在本轮 findings
  范围内，仅作路径与契约参照。

## 2. 对比上一轮（head 2396018 → fdb8241）

- 上轮为 PASS（第 2 轮）。本轮增量实质是一次 native/ 布局 rebase：
  `2396018..fdb8241` 的树对比显示全部 sim/tests 文件均为 R100 逐文件重命名
  （`sim/` → `native/sim/`、`tests/` → `native/tests/`），内容零改动；
  `tests/sim_tests/CMakeLists.txt` 仅 R099（新增 4 个测试源文件）。**无因 rebase
  丢失的测试目标**：math/rng/clock/queue/event_log/snapshot/save/parallel/loader/
  command_validation/c_api 12 个测试源文件全部在位并纳入 `add_executable`。
- 上轮修复点逐一确认仍有效（行号为 fdb8241）：
  - 🔴 blob_length 无符号回绕 → `save.cpp:330` 先减后比，恶意超大长度直接拒绝；
  - EventLog seq MAX 饱和不回绕 → `event_log.cpp:91-111` + 单测
    `ExplicitMaxSeqSaturatesWithoutWrapping`；
  - 非 Windows 保存回退 → `save.cpp:171-191`（rename 覆盖 + 显式注释降级窗口）；
  - 队列游标序列化/恢复 → `snapshot.cpp:100-104`、`queue.cpp:110-115`
    （restore_next_seq 拒绝回退）+ `save_test.cpp:384-418` 游标一致性测试。
- 本轮新情况：fdb8241 重新加回的 `sim_core` CMake 块是 8760c0b 的**未加固版本**
  （详见 Findings F1），未带入姊妹分支 #107 头（42c1700）对同一块的加固。

## 3. Findings 表

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| F1 | 🟡 | native/sim/CMakeLists.txt:46-53 | sim_core 的 POST_BUILD 无条件复制 `$<TARGET_RUNTIME_DLLS:sim_core>`：当 json-schema-validator 为静态库（如 x64-windows-static triplet）时该表达式展开为空，`cmake -E copy_if_different` 缺源参数导致构建失败。同一仓库 `native/tests/sim_tests/CMakeLists.txt:33-50` 已按 IMPORTED_LOCATION 扩展名守卫此失败模式，#107 分支（42c1700）也已对 sim_core 加固；fdb8241 恢复的是未加固旧块，属 rebase 未带入姊妹分支修复 | 带回 42c1700 的条件复制守卫（或等价实现）；与 #107 的合并冲突解决时保留该守卫 | 待修复 |
| N1 | 💭 | native/sim/CMakeLists.txt:42 | 导出正确性无自动化断言：WINDOWS_EXPORT_ALL_SYMBOLS 机制静态确认可行（从 c_api.cpp 的目标文件生成 .def，导出 extern "C" 的 wfs_sim_* 符号），但 CI 无 dumpbin/GetProcAddress 级校验，改导出设置会静默回归 | 增加 sim_core.dll 导出符号冒烟检查；或按注释计划引入 WFS_SIM_API 宏后移除全量导出 | 建议 |
| N2 | 💭 | native/sim/src/save.cpp:384-391 | 加载失败根因在 ABI 边界丢失：magic/版本/哈希/元数据错配全部折叠为 INVALID_DATA，异常文本被丢弃，无 last_error 出参或日志；§17"可定位"仅到错误码粒度（未静默吞错，但不可区分失败环节） | 增加错误详情出参（如 wfs_sim_last_error）或内部日志记录失败环节 | 建议 |
| N3 | 💭 | native/sim/src/event_log.cpp:94 | EventLog 游标==UINT64_MAX 即抛 overflow_error，即使 MAX 从未被占用；与 EventQueue（queue.cpp:41-46，仅当 MAX 已占用才抛、否则可分配 MAX 后饱和）语义不一致 | 对齐 EventQueue 语义，或在两处注释互引说明差异 | 建议 |
| N4 | 💭 | native/sim/src/save.cpp:123 | PathLock 键仅 `lexically_normal()`：Windows 大小写不敏感路径的不同拼写会取到不同锁（MoveFileExW 仍保证不半写，只影响并发写序），且锁表只增不删 | Windows 下键统一转小写或使用 canonical（文件存在时） | 建议 |
| N5 | 💭 | native/sim/src/save.cpp:333 | 存档尾部在 state_hash 之后的额外字节不被拒绝（宽松解析，利于前向扩展但会放过拼接/截断类畸形文件） | 如需严格校验增加文件长度一致性检查；否则显式注释宽松理由 | 建议 |
| N6 | 💭 | native/sim/include/wfs/sim/c_api.h:77 | load_save 要求句柄的 seed/scenario 与存档一致，但 C ABI 无"读取存档头元数据"接口，UI 主菜单读档需先拿 seed/scenario 再 create 的流程在契约中未闭环 | T089/UI 阶段增加 peek 接口，或评估哈希覆盖下从存档取 seed 的取舍 | 建议 |

## 4. 未验证猜测（不作为确定问题）

1. sim_core.dll 的实际导出符号未在本审查中动态验证：静态结论为"机制正确、符号应导出"，
  但 CI 只证明目标构建成功，未断言导出表内容（见 N1）。
2. "线程数不影响状态哈希"目前成立分两层：序列化显式排除 threads（snapshot.cpp:88-112）
  与框架级 1/4/8 线程一致性测试（parallel_test.cpp:77-96）；但 wfs_sim_step 尚未调用
  map_reduce，FR-025 的端到端保证要等结算系统接入后才能全链路验证——这是阶段性问题，
  不是缺陷。
3. #107 与 #108 并改 native/sim/CMakeLists.txt 的同一 sim_core 块；check-pr-order 以
  -IgnoreOrder 运行、不强制顺序。若 #108 先合并，F1 的旧块会覆盖 #107 的守卫——合并
  冲突解决时需人工确认保留 42c1700 版逻辑。
4. CI 证据（push run 31590587310 success、pull_request run 31590590665 success、
  mergeable=CLEAN）按用户给定采信，本审查未重复调 gh 验证。

## 5. 宪法合规要点

- §2 测试保障：T015–T018 各配自动化测试（event_log/snapshot/save/parallel 共 4 个新文件 +
  queue restore 测试），SHA-256 KAT（含百万 'a'）、损坏档/版本/哈希/元数据拒绝、并发写、
  异常传播均覆盖；CI 双 run 绿。
- §7 确定性：统一 RNG 状态序列化、队列按 (tick,seq) 固定序、map_reduce 按桶序升序固定
  归约、threads 不进哈希；1 vs N 线程一致性有测试。
- §13 存档兼容：magic "WFS-SAVE" + format_version(u32 LE) + header_json + state_blob +
  state_hash，与 contracts/save-format.md 一致；migrate 迁移链骨架，未来/不可迁移版本显式拒绝。
- §15 无头可测：sim 为纯静态库、GoogleTest 无 UI 依赖，native/ 可独立无头构建。
- §17 错误处理：错误码贯穿 C ABI，损坏数据显式 INVALID_DATA/IO_ERROR，无静默吞错
  （根因粒度见 N2）。
- §18 可复现构建：vcpkg builtin-baseline c4d9956 锁定 + CI 固定 windows-2022 镜像。
- §5 注释：全部新源文件均含文件级总览注释与"为什么"说明。

## 6. 整体结论

**PASS（置信度 0.90）**。无 🔴 阻断项；F1（🟡）为构建稳健性/姊妹分支一致性回退，
当前受支持 triplet（x64-windows）下 CI 绿、不影响合并，但建议在合并冲突解决时带回守卫。
其余 6 条 💭 为诊断粒度、导出断言与设计取舍类建议。

## 7. 收敛与轮次检测

- 本轮 = r2（rebase 后 head fdb8241 重审）。r1 遗留问题全部确认修复落地，未复发。
- 本轮唯一新问题是 rebase 引入的 sim_core 守卫回退（F1）；其余 6 条 💭 均为增量建议。
- 下一轮检查点：F1 是否带回静态 triplet 守卫；#107/#108 合并冲突解决是否保留 42c1700
  逻辑；可选增加 sim_core 导出符号冒烟测试（N1）。
- 防重复声明：前轮已修问题（blob_length 回绕、seq 饱和、非 Windows 回退、游标恢复）
  本轮只核对不重提；前轮 PASS 结论不推翻。
