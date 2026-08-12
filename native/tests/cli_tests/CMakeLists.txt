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
