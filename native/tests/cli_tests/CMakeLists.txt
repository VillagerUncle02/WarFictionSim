# tests/cli_tests —— T019 无头 CLI 集成测试（CTest 直接驱动 sim_headless）。
# 覆盖 run/inject/save 子命令、--scenario/--seed/--script/--out/--hash/--threads/
# --ai-backend 参数解析、JSONL 事件输出与错误路径（宪法 15：无头可测）。
add_test(
    NAME cli_tests
    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_tests.ps1"
        -Exe "$<TARGET_FILE:sim_headless>"
        -Scenario "${PROJECT_SOURCE_DIR}/data/scenarios/scn-smoke-test.json"
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
        -Scenario "${PROJECT_SOURCE_DIR}/data/scenarios/scn-tutorial-platoon.json"
        -Script "${CMAKE_CURRENT_SOURCE_DIR}/test_command_chain.jsonl"
)
set_tests_properties(command_chain_tests PROPERTIES TIMEOUT 120)
