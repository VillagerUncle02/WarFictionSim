# sim —— C++20 确定性模拟核心（宪法第 15 条：无头可测）
# T009：转换为 STATIC 并加入统一确定性 RNG（PCG32）源码；继续链接 core_c。
add_library(sim STATIC
    src/rng.cpp
    src/clock.cpp
    src/queue.cpp
)

target_include_directories(sim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sim PUBLIC core_c)

if (MSVC)
    target_compile_options(sim PRIVATE /utf-8)
endif()
