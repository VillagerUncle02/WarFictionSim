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
    if (_gtest_location MATCHES "\\.dll$")
        add_custom_command(TARGET sim_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:sim_tests>
                $<TARGET_FILE_DIR:sim_tests>
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
    unset(_gtest_location)
endif()

if (MSVC)
    target_compile_options(sim_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
if (WIN32)
    # 双保险：CTest 运行测试时把 gtest DLL 目录与测试输出目录前置到 PATH，
    # 即使 POST_BUILD 复制失效也不会出现"找不到 gtest.dll"弹窗。
    # ENVIRONMENT 属性是分号分隔的 VAR=value 列表，无法安全拼接目录；
    # 改用 CMake 3.22+ 的 ENVIRONMENT_MODIFICATION（path_list_prepend）。
    gtest_discover_tests(sim_tests PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:GTest::gtest>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:sim_tests>")
else()
    gtest_discover_tests(sim_tests)
endif()
