# sim_tests —— C++20 GoogleTest 单元测试工程
# 覆盖确定性数学库（core_c）与统一 RNG（sim）等确定性核心契约（宪法第 2/7 条）。
find_package(GTest CONFIG REQUIRED)

add_executable(sim_tests
    math_test.cpp
    rng_test.cpp
    clock_test.cpp
)

target_link_libraries(sim_tests PRIVATE GTest::gtest_main sim core_c)

# Windows 下 vcpkg 的 gtest 为动态库：把运行期 DLL 复制到测试可执行文件目录，
# 避免直接运行 sim_tests.exe 时"找不到 gtest.dll"（本地与 CI 均适用）。
if (WIN32)
    add_custom_command(TARGET sim_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sim_tests>
            $<TARGET_FILE_DIR:sim_tests>
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif()

if (MSVC)
    target_compile_options(sim_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
gtest_discover_tests(sim_tests)
