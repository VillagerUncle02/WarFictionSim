# sim_tests —— C++20 GoogleTest 单元测试工程
# 覆盖 core_c（T008）与 sim 确定性核心 T009–T011（RNG/游戏时钟/事件命令队列）
# 的确定性契约（宪法第 2/7/16 条）。
find_package(GTest CONFIG REQUIRED)

add_executable(sim_tests
    math_test.cpp
    rng_test.cpp
    clock_test.cpp
    queue_test.cpp
)

target_link_libraries(sim_tests PRIVATE GTest::gtest_main sim core_c)

# Windows 下 vcpkg 的 gtest 为动态库时，把运行期 DLL 复制到测试可执行文件目录，
# 避免直接运行 sim_tests.exe 时"找不到 gtest.dll"（本地与 CI 均适用）。
# 静态 triplet 下 GTest::gtest 为 .lib，TARGET_RUNTIME_DLLS 展开为空会导致
# copy 命令参数不完整而报错，因此在 configure 时按 IMPORTED_LOCATION 扩展名判断。
if (WIN32)
    get_target_property(_gtest_location GTest::gtest IMPORTED_LOCATION_RELEASE)
    if (NOT _gtest_location OR _gtest_location MATCHES "-NOTFOUND$")
        get_target_property(_gtest_location GTest::gtest IMPORTED_LOCATION)
    endif()
    if (_gtest_location)
        string(TOLOWER "${_gtest_location}" _gtest_location_lower)
        if (_gtest_location_lower MATCHES "\\.dll$")
            add_custom_command(TARGET sim_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:sim_tests>
                    $<TARGET_FILE_DIR:sim_tests>
                COMMAND_EXPAND_LISTS
                VERBATIM)
        endif()
    endif()
    unset(_gtest_location)
    unset(_gtest_location_lower)
endif()

if (MSVC)
    target_compile_options(sim_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
if (WIN32)
    # 双保险：CTest 运行测试时把 gtest DLL 目录与测试输出目录前置到 PATH，
    # 即使 POST_BUILD 复制失效也不会出现"找不到 gtest.dll"弹窗。
    # 注意：CMake 4.x 的 GoogleTestAddTests.cmake 在 write_test_to_file 中以
    # 未加引号的 ${arg_TEST_PROPERTIES} 展开 PROPERTIES，含分号的属性值会被
    # 压平为多个属性名，因此不能直接经 gtest_discover_tests PROPERTIES 传递
    # 两条 PATH 修改。改为构建后生成追加 include（见 patch_test_env.cmake），
    # 基于 TEST_LIST 变量（sim_tests_TESTS）统一重设 ENVIRONMENT_MODIFICATION。
    gtest_discover_tests(sim_tests)
    set(_sim_tests_env_include "${CMAKE_CURRENT_BINARY_DIR}/sim_tests_env.cmake")
    add_custom_command(TARGET sim_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D "TEST_INCLUDE_OUT=${_sim_tests_env_include}"
            -D "TEST_LIST_NAME=sim_tests_TESTS"
            -D "GTEST_DIR=$<TARGET_FILE_DIR:GTest::gtest>"
            -D "EXE_DIR=$<TARGET_FILE_DIR:sim_tests>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/patch_test_env.cmake"
        VERBATIM)
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_sim_tests_env_include}")
else()
    gtest_discover_tests(sim_tests)
endif()
