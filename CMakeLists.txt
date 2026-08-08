cmake_minimum_required(VERSION 3.30)

# WarFictionSim —— 三语言项目根构建配置
# sim/（C++20 确定性模拟核心）与 core_c/（C11 底层数学库）必须可独立无头构建
# （宪法第 14/15 条）；ui/、tools/（C# WPF）由各自工程构建，本文件不直接引用。
project(WarFictionSim LANGUAGES C CXX)

# ---- 确定性浮点：MSVC 固定 /fp:precise（research.md §1，宪法第 7 条） ----
if (MSVC)
    add_compile_options(/W4 /permissive- /fp:precise)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- 子目录目标由 T003 配置骨架，后续阶段逐步充实 ----
add_subdirectory(core_c)
add_subdirectory(sim)

# ---- 测试（CTest；GoogleTest 在对应阶段接入） ----
include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
