# sim —— C++20 确定性模拟核心（宪法第 15 条：无头可测）
# 骨架阶段为 INTERFACE 目标；T008 起转换为 STATIC 并加入源码。
add_library(sim INTERFACE)
target_include_directories(sim INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(sim INTERFACE core_c)

# ---- sim_core：C# 桥接 DLL（contracts/sim-c-api.md，宪法第 14 条） ----
# C ABI 边界源码（src/c_api.cpp）由后续 PR 提供；源码出现前不构建，避免
# main 骨架期配置失败。C# 侧通过 app/Directory.Build.targets 从
# native/build/clang-cl-<config>/ 复制本 DLL 到输出目录。
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/c_api.cpp")
    add_library(sim_core SHARED src/c_api.cpp)
    target_compile_definitions(sim_core PRIVATE WFS_SIM_CORE_EXPORTS)
    target_include_directories(sim_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(sim_core PRIVATE sim)
    # 过渡策略：c_api.h 尚未带导出宏时先导出 DLL 全部符号（C ABI 按名
    # P/Invoke）；后续可在 c_api.h 增加 WFS_SIM_API 后移除本属性。
    set_target_properties(sim_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if (MSVC)
        target_compile_options(sim_core PRIVATE /utf-8)
    endif()
    if (WIN32)
        # 运行期依赖 DLL（如 json-schema-validator）复制到 sim_core.dll 同目录，
        # 避免 C# 加载时找不到依赖。
        add_custom_command(TARGET sim_core POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:sim_core>
                $<TARGET_FILE_DIR:sim_core>
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
endif()
