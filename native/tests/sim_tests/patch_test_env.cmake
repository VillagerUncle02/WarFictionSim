# tests/sim_tests/patch_test_env.cmake
#
# POST_BUILD 辅助脚本：为已发现的 GoogleTest 测试重设 ENVIRONMENT_MODIFICATION。
#
# 背景：CMake 4.x 的 GoogleTestAddTests.cmake 把 gtest_discover_tests PROPERTIES
# 以未加引号的 ${arg_TEST_PROPERTIES} 展开，含分号的属性值会被压平为多个属性名，
# 因此 ENVIRONMENT_MODIFICATION 的两条 path_list_prepend 无法直接传透。本脚本
# 在构建后把最终目录写入追加 include；该 include 由 CTest 按 TEST_INCLUDE_FILES
# 顺序在测试发现之后执行，基于文档化的 TEST_LIST 变量重设属性为单值双条目。

if(NOT DEFINED TEST_INCLUDE_OUT OR NOT DEFINED TEST_LIST_NAME OR NOT DEFINED GTEST_DIR OR NOT DEFINED EXE_DIR)
    message(FATAL_ERROR
        "patch_test_env.cmake requires TEST_INCLUDE_OUT/TEST_LIST_NAME/GTEST_DIR/EXE_DIR")
endif()

file(WRITE "${TEST_INCLUDE_OUT}"
    "if(DEFINED ${TEST_LIST_NAME} AND ${TEST_LIST_NAME})\n"
    "    set_tests_properties(\${${TEST_LIST_NAME}} PROPERTIES ENVIRONMENT_MODIFICATION\n"
    "        \"PATH=path_list_prepend:${GTEST_DIR};PATH=path_list_prepend:${EXE_DIR}\")\n"
    "endif()\n")
