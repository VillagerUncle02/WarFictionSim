# tests/cli_tests —— T019 无头 CLI 集成测试（CTest 直接驱动 sim_headless）。
# 覆盖 run/inject/save 子命令、--scenario/--seed/--script/--out/--hash/--threads/
# --ai-backend 参数解析、JSONL 事件输出与错误路径（宪法 15：无头可测）。
add_test(
    NAME cli_tests
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_tests.ps1"
        -Exe "$<TARGET_FILE:sim_headless>"
        -Scenario "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-smoke-test.json"
        -Script "${CMAKE_CURRENT_SOURCE_DIR}/command_chain.jsonl"
)
set_tests_properties(cli_tests PROPERTIES TIMEOUT 120)

# T023 命令链路集成测试：下达→确认接受→移动→完成、连排 3–10s 通讯延迟、
# 生效前撤回/修改、（优先级,序列号）裁决与批量部分接受（quickstart §3.2）。
add_test(
    NAME command_chain_tests
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/run_command_chain_tests.ps1"
        -Exe "$<TARGET_FILE:sim_headless>"
        -Scenario "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-tutorial-platoon.json"
        -Script "${CMAKE_CURRENT_SOURCE_DIR}/test_command_chain.jsonl"
)
set_tests_properties(command_chain_tests PROPERTIES TIMEOUT 120)

# T045 支援请求链路集成测试：连排级有限分数（扣分/拒绝/归建）与营级配属链
# （配属/拒绝/转请/归建，SC-004），两种作战规模各用一个专用场景。
add_test(
    NAME support_chain_tests
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/run_support_chain_tests.ps1"
        -Exe "$<TARGET_FILE:sim_headless>"
        -ScenarioPlatoon "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-support-platoon.json"
        -ScenarioBattalion "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-support-battalion.json"
        -Script "${CMAKE_CURRENT_SOURCE_DIR}/test_support_chain.jsonl"
)
set_tests_properties(support_chain_tests PROPERTIES TIMEOUT 180)

# T054 派系差异观测集成测试：同一连排级请求脚本在三派系场景分别运行，
# 断言审批层级 0/1/2、转发延迟 0/10/20、有限分数总额与池种类差异在事件中
# 可观察且互不相同，同派系两次运行确定性一致（SC-004/FR-006）。
add_test(
    NAME faction_chain_tests
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/run_faction_chain_tests.ps1"
        -Exe "$<TARGET_FILE:sim_headless>"
        -ScenarioChina "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-support-faction-china.json"
        -ScenarioNato "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-support-faction-nato.json"
        -ScenarioRussia "${PROJECT_SOURCE_DIR}/../data/scenarios/scn-support-faction-russia.json"
        -Script "${CMAKE_CURRENT_SOURCE_DIR}/test_faction_chain.jsonl"
)
set_tests_properties(faction_chain_tests PROPERTIES TIMEOUT 180)
