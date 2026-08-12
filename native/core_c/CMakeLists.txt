# core_c —— C11 底层确定性数学库（确定性浮点封装、弹道计算，宪法第 14 条）
# T008：转换为 STATIC 并加入确定性数学封装源码。
add_library(core_c STATIC
    src/math.c
)

target_include_directories(core_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if (MSVC)
    target_compile_options(core_c PRIVATE /utf-8)
endif()
