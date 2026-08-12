# sim_tests —— C++20 GoogleTest 单元测试工程
# 覆盖确定性数学库（core_c）与统一 RNG（sim）等确定性核心契约（宪法第 2/7 条）。
find_package(GTest CONFIG REQUIRED)

add_executable(sim_tests
    math_test.cpp
    rng_test.cpp
)

target_link_libraries(sim_tests PRIVATE GTest::gtest_main sim core_c)

if (MSVC)
    target_compile_options(sim_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
gtest_discover_tests(sim_tests)
