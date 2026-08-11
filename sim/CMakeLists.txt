# sim —— C++20 确定性模拟核心（宪法第 15 条：无头可测）
# 骨架阶段为 INTERFACE 目标；T008 起转换为 STATIC 并加入源码。
add_library(sim INTERFACE)
target_include_directories(sim INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(sim INTERFACE core_c)
