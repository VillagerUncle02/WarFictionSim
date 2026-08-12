cmake_minimum_required(VERSION 3.30)

# WarFictionSim —— 原生侧（C/C++）构建入口
# native/ 只负责 C/C++：core_c（C11 底层数学库）与 sim（C++20 确定性模拟核心），
# 必须可独立无头构建（宪法第 14/15 条）。
# C# 侧（app/）由 app/WarFictionSim.sln + dotnet 构建，不参与本工程；
# 两侧唯一契约是 contracts/ 与 native 产出的 C ABI DLL（sim_core）。
#
# 工具链（详见 docs/toolchain.md）：CMake 3.31.x + Ninja + clang-cl + vcpkg。
# 一键配置：cmake --preset clang-cl-debug / clang-cl-release（需先设置 VCPKG_ROOT）。
project(WarFictionSim LANGUAGES C CXX)

# ---- 确定性浮点：固定 /fp:precise（research.md §1，宪法第 7 条） ----
# clang-cl 兼容 MSVC 命令行，同样进入 MSVC 分支。
if (MSVC)
    add_compile_options(/W4 /permissive- /fp:precise)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- LLDB 调试（CodeLLDB）----
# clang-cl 默认生成 CodeView(PDB)，LLDB 无法读取；Debug 配置额外生成 DWARF
# （/clang:-gdwarf），并统一用 lld-link 链接（避免 MSVC link.exe 处理 DWARF 段
# 时告警或失败）。
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<CONFIG:Debug>:/clang:-gdwarf>)
    add_link_options("-fuse-ld=lld")
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
