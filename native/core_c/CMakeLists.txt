# core_c —— C11 底层确定性数学库（确定性浮点封装、弹道计算，宪法第 14 条）
# 骨架阶段为 INTERFACE 目标；T008 起转换为 STATIC 并加入源码。
add_library(core_c INTERFACE)
target_include_directories(core_c INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
